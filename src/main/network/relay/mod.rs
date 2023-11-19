use std::net::Ipv4Addr;
use std::sync::Arc;
use std::sync::Weak;

use atomic_refcell::AtomicRefCell;
use shadow_shim_helper_rs::simulation_time::SimulationTime;

use crate::core::work::task::TaskRef;
use crate::core::worker::Worker;
use crate::cshadow as c;
use crate::host::host::Host;
use crate::network::packet::PacketStatus;
use crate::network::relay::token_bucket::TokenBucket;
use crate::network::PacketRc;
use crate::utility::ObjectCounter;

mod token_bucket;

/// A `Relay` forwards `PacketRc`s between `PacketDevice`s, optionally enforcing a
/// bandwidth limit on the rate at which we forward `PacketRc`s between devices.
/// The `Relay` is considered the "active" part of the `PacketRc` forwarding
/// process: it initiates `PacketRc` forwarding and internally schedules tasks to
/// ensure that `PacketRc`s are continually forwarded over time without exceeding
/// the configured `RateLimit`.
///
/// An `Ipv4Addr` associated with a source `PacketDevice` object is supplied
/// when creating a `Relay`. This `Ipv4Addr` is only meaningful to the extent
/// that the `Host` understands how to map this `Ipv4Addr` to the intended
/// `PacketDevice` when `Host::get_packet_device(Ipv4Addr)` is called. This
/// source `PacketDevice` supplies the `Relay` with a stream of `PacketRc`s
/// (through its implementation of `PacketDevice::pop()`) that the `Relay` will
/// forward to a destination.
///
/// `Relay::notify()` must be called whenever the source `PacketDevice` changes
/// state from empty to non-empty, to trigger an idle `Relay` to start
/// forwarding `PacketRc`s again.
///
/// For each `PacketRc` that needs to be forwarded, the `Relay` uses the
/// `PacketRc`'s destination `Ipv4Addr` to obtain the destination `PacketDevice`
/// from the `Host` by calling its `Host::get_packet_device(Ipv4Addr)` function.
/// The `PacketRc` is forwarded to the destination through the destination
/// `PacketDevice`'s implementation of `PacketDevice::push()`.
///
/// This design allows the `Host` to use `Host::get_packet_device` to define its
/// own routing table.
///
/// Note that `PacketRc`s forwarded between identical source and destination
/// `PacketDevices` are considered "local" to that device and exempt from any
/// configured `RateLimit`.
pub struct Relay {
    /// Allow for internal mutability. It as assumed that this will never be
    /// mutably borrowed outside of `Relay::forward_until_blocked()`.
    internal: AtomicRefCell<RelayInternal>,
}

struct RelayInternal {
    _counter: ObjectCounter,
    rate_limiter: Option<TokenBucket>,
    src_dev_address: Ipv4Addr,
    state: RelayState,
    next_packet: Option<PacketRc>,
}

/// Track's the `Relay`s state, which typically moves from Idle to Pending to
/// Forwarding, and then back to either Idle or Pending.
#[derive(PartialEq, Copy, Clone, Debug)]
enum RelayState {
    /// Relay is idle (is not currently forwarding packets) and has not
    /// scheduled a forwarding event.
    Idle,
    /// A forwarding event has been scheduled, and we are waiting for it to be
    /// executed before we start forwarding packets.
    Pending,
    /// We are currently running our packet forwarding loop.
    Forwarding,
}

/// Specifies a throughput limit the relay should enforce when forwarding packets.
pub enum RateLimit {
    BytesPerSecond(u64),
    Unlimited,
}

impl Relay {
    /// Creates a new `Relay` that will forward `PacketRc`s following the given
    /// `RateLimit` from the `PacketDevice` returned by the `Host` when passing
    /// the given `src_dev_address` to `Host::get_packet_device()`. The `Relay`
    /// internally schedules tasks as needed to ensure packets continue to be
    /// forwarded over time without exceeding the configured `RateLimit`.
    pub fn new(rate: RateLimit, src_dev_address: Ipv4Addr) -> Self {
        let rate_limiter = match rate {
            RateLimit::BytesPerSecond(bytes) => Some(create_token_bucket(bytes)),
            RateLimit::Unlimited => None,
        };

        Self {
            internal: AtomicRefCell::new(RelayInternal {
                _counter: ObjectCounter::new("Relay"),
                rate_limiter,
                src_dev_address,
                state: RelayState::Idle,
                next_packet: None,
            }),
        }
    }

    /// Notify the relay that its packet source now has packets available for
    /// relaying to the packet sink. This must be called when the source changes
    /// state from empty to non-empty to signal the relay to resume forwarding.
    pub fn notify(self: &Arc<Self>, host: &Host) {
        // The only time we hold a mutable borrow of our internals while
        // executing outside of this module is when we're running our forwarding
        // loop, and forwarding packets can certainly cause a call to
        // Relay::notify(). Thus, it's safe to assume that we are in the
        // Forwarding state if the borrow fails.
        let state = match self.internal.try_borrow() {
            Ok(internal) => internal.state,
            Err(_) => RelayState::Forwarding,
        };

        #[allow(dead_code)]
        match state {
            RelayState::Idle => {
                // Allow packets to accumulate and unwind the stack to forward
                // them.
                self.forward_later(SimulationTime::ZERO, host);
            }
            RelayState::Pending => {
                log::trace!("Relay forward task already scheduled; skipping forward request.");
            }
            RelayState::Forwarding => {
                log::trace!("Relay forward task currently running; skipping forward request.");
            }
        }
    }

    /// Schedule an event to trigger us to run the forwarding loop later, and
    /// changes our state to `RelayState::Pending`. This allows us to run the
    /// forwarding loop after unwinding the current stack, and allows socket
    /// data to accumulate so we can forward multiple packets at once.
    ///
    /// Must not be called if our state is already `RelayState::Pending`, to
    /// avoid scheduling multiple forwarding events simultaneously.
    fn forward_later(self: &Arc<Self>, delay: SimulationTime, host: &Host) {
        // We should not already be waiting for a scheduled forwarding task.
        {
            let mut internal = self.internal.borrow_mut();
            assert_ne!(internal.state, RelayState::Pending);
            internal.state = RelayState::Pending;
        }

        // Schedule a forwarding task using a weak reference to allow the relay
        // to be dropped before the forwarding task is executed.
        let weak_self = Arc::downgrade(self);
        let task = TaskRef::new(move |host| Self::run_forward_task(&weak_self, host));
        host.schedule_task_with_delay(task, delay);
        log::trace!(
            "Relay src={} scheduled event to start forwarding packets after {:?}",
            self.internal.borrow().src_dev_address,
            delay
        );
    }

    /// The initial entry point for the forwarding event executed by the scheduler.
    fn run_forward_task(weak_self: &Weak<Self>, host: &Host) {
        // Ignore the task if the relay was dropped while the task was pending.
        let Some(strong_self) = Weak::upgrade(weak_self) else {
            log::trace!("Relay no longer exists; skipping forward task.");
            return;
        };

        // Relay still exists, and task is no longer pending.
        strong_self.internal.borrow_mut().state = RelayState::Idle;

        // Run the main packet forwarding loop.
        strong_self.forward_now(host);
    }

    /// Runs the forward loop, and then schedules a task to run it again if needed.
    fn forward_now(self: &Arc<Self>, host: &Host) {
        if let Some(blocking_dur) = self.forward_until_blocked(host) {
            // Block until we have enough tokens to forward the next packet.
            // Our state will be changed to `RelayState::Pending`.
            self.forward_later(blocking_dur, host);
        }
    }

    /// Run our main packet forwarding loop that continues forwarding packets
    /// from the source device to the destination device until we run out of
    /// either tokens or packets.
    ///
    /// Causes our state to change to `RelayState::Forwarding` during execution
    /// of the loop, and then either `RelayState::Idle` if we run out of
    /// packets, or `RelayState::Pending` if we run out of tokens before all
    /// available packets are forwarded and we scheduled an event to resume
    /// forwarding later.
    ///
    /// The duration until we have enough tokens to forward the next packet is
    /// returned in case we run out of tokens in the forwarding loop.
    fn forward_until_blocked(self: &Arc<Self>, host: &Host) -> Option<SimulationTime> {
        // We don't enforce rate limits during bootstrapping.
        let is_bootstrapping = Worker::is_bootstrapping();

        // Get a mutable reference to internals, which we'll continuously hold
        // for the rest of this function (for the entire time that we remain in
        // the Forwarding state).
        let mut internal = self.internal.borrow_mut();
        internal.state = RelayState::Forwarding;

        // The source device supplies us with the stream of packets to forward.
        let src = host.get_packet_device(internal.src_dev_address);

        // Continue forwarding until we run out of either packets or tokens.
        loop {
            // Get next packet from our local cache, or from the source device.
            let Some(mut packet) = internal.next_packet.take().or_else(|| src.pop()) else {
                // Ran out of packets to forward.
                internal.state = RelayState::Idle;
                return None;
            };

            // The packet is local if the src and dst refer to the same device.
            // This can happen for the loopback device, and for the inet device
            // if both sockets use the public ip to communicate over localhost.
            let is_local = src.get_address() == *packet.dst_address().ip();

            // Check if we have enough tokens for forward the packet. Rate
            // limits do not apply during bootstrapping, or if the source and
            // destination are the same device.
            if !is_bootstrapping && !is_local {
                // Rate limit applies only if we have a token bucket.
                if let Some(tb) = internal.rate_limiter.as_mut() {
                    // Try to remove tokens for this packet.
                    if let Err(blocking_dur) = tb.comforming_remove(packet.total_size() as u64) {
                        // Too few tokens, need to block.
                        log::trace!(
                            "Relay src={} dst={} exceeded rate limit, need {} more tokens \
                            for packet of size {}, blocking for {:?}",
                            src.get_address(),
                            packet.dst_address().ip(),
                            packet
                                .total_size()
                                .saturating_sub(tb.comforming_remove(0).unwrap() as usize),
                            packet.total_size(),
                            blocking_dur
                        );

                        // Cache the packet until we can forward it later.
                        packet.add_status(PacketStatus::RelayCached);
                        assert!(internal.next_packet.is_none());
                        internal.next_packet = Some(packet);
                        internal.state = RelayState::Idle;

                        // Call Relay::forward_later() after dropping the mutable borrow.
                        return Some(blocking_dur);
                    }
                }
            }

            // Forward the packet to the destination device now.
            packet.add_status(PacketStatus::RelayForwarded);
            if is_local {
                // The source and destination are the same. Avoid a double
                // mutable borrow of the packet device.
                src.push(packet);
            } else {
                // The source and destination are different.
                let dst = host.get_packet_device(*packet.dst_address().ip());
                dst.push(packet);
            }
        }
    }
}

/// Configures a token bucket according the the given bytes_per_second rate
/// limit. We always refill at least 1 byte per millisecond.
fn create_token_bucket(bytes_per_second: u64) -> TokenBucket {
    let refill_interval = SimulationTime::from_millis(1);
    let refill_size = std::cmp::max(1, bytes_per_second / 1000);

    // Only the `capacity` of the bucket is increased by the burst allowance,
    // not the `refill_size`. Therefore, the long term rate limit enforced by
    // the token bucket (configured by `refill_size`) is not affected much.
    let capacity = refill_size + get_burst_allowance();

    TokenBucket::new(capacity, refill_size, refill_interval).unwrap()
}

/// Returns the "burst allowance" we use in our token buckets.
///
/// What the burst allowance ensures is that we don't lose tokens that are
/// unused because we don't fragment packets. If we set the capacity of the
/// bucket to exactly the refill size (i.e., without the `CONFIG_MTU` burst
/// allowance) and there are only 1499 tokens left in this sending round, a full
/// packet would not fit. The next time the bucket refills, it adds
/// `refill_size` tokens but in doing so 1499 tokens would fall over the top of
/// the bucket; these tokens would represent wasted bandwidth, and could
/// potentially accumulate in every refill interval leading to a significantly
/// lower achievable bandwidth.
///
/// A downside of the `CONFIG_MTU` burst allowance is that the sending rate
/// could possibly become "bursty" with a behavior such as:
/// - interval 1: send `refill_size` + `CONFIG_MTU` bytes, sending over the
///      allowance by 1500 bytes
/// - refill: `refill_size` token gets added to the bucket
/// - interval 2: send `refill_size` - `CONFIG_MTU` bytes, sending under the
///       allowance by 1500 bytes
/// - refill: `refill_size` token gets added to the bucket
/// - interval 3: send `refill_size` + `CONFIG_MTU` bytes, sending over the
///      allowance by 1500 bytes
/// - repeat
///
/// So it could become less smooth and more "bursty" even though the long term
/// average is maintained. But I don't think this would happen much in practice,
/// and we are batching sends for performance reasons.
fn get_burst_allowance() -> u64 {
    c::CONFIG_MTU.into()
}
