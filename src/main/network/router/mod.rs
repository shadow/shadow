use std::cell::RefCell;
use std::net::Ipv4Addr;
use std::sync::Arc;

use self::codel_queue::CoDelQueue;
use crate::core::worker::Worker;
use crate::cshadow;
use crate::host::host::Host;
use crate::network::packet::PacketRc;
use crate::network::router::relay::Relay;
use crate::network::router::token_bucket::TokenBucket;
use crate::network::{PacketDevice, RateLimit};
use crate::utility::{Magic, ObjectCounter};

mod codel_queue;
mod relay;
mod token_bucket;

use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::simulation_time::SimulationTime;

/// A router assists with moving packets between hosts across the simulated
/// network.
pub struct Router {
    // Forwards packets out from our internet interface to the router.
    relay_inet_out: Arc<Relay>,
    // Forwards packets from the router in to our internet interface.
    relay_inet_in: Arc<Relay>,
    // Forwards packets from the localhost interface back to itself.
    relay_loopback: Arc<Relay>,
}

impl Router {
    /// Create a new router at the default address that will assist with moving
    /// packets between interfaces and across the network.
    pub fn new(addr: Ipv4Addr, limit_up: RateLimit, limit_down: RateLimit) -> Router {
        Router {
            relay_inet_out: Arc::new(Relay::new(limit_up, addr)),
            relay_inet_in: Arc::new(Relay::new(limit_down, Ipv4Addr::UNSPECIFIED)),
            relay_loopback: Arc::new(Relay::new(RateLimit::Unlimited, Ipv4Addr::LOCALHOST)),
        }
    }

    pub fn notify_loopback(&self, host: &Host) {
        self.relay_loopback.notify(host)
    }

    pub fn notify_default_out(&self, host: &Host) {
        self.relay_inet_out.notify(host)
    }

    pub fn notify_default_in(&self, host: &Host) {
        self.relay_inet_in.notify(host);
    }
}

// Offers a device for handling packets coming in from the internet. Note: this
// is a temporary workaround, and will be removed when the router redesign is
// complete.
pub struct RouterDevice {
    magic: Magic<Self>,
    _counter: ObjectCounter,
    address: Ipv4Addr,
    /// Packets inbound to the host from the simulated network.
    inbound_packets: RefCell<CoDelQueue>,
}

impl RouterDevice {
    /// Create a new router for a host that will help route packets between it
    /// and other hosts. The `address` must uniquely identify this router to the
    /// host that owns it.
    pub fn new(address: Ipv4Addr) -> RouterDevice {
        RouterDevice {
            magic: Magic::new(),
            address,
            _counter: ObjectCounter::new("Router"),
            inbound_packets: RefCell::new(CoDelQueue::new()),
        }
    }

    fn push_inner(&self, packet: PacketRc, now: EmulatedTime) {
        self.magic.debug_check();
        self.inbound_packets.borrow_mut().push(packet, now);
    }

    fn pop_inner(&self, now: EmulatedTime) -> Option<PacketRc> {
        self.magic.debug_check();
        self.inbound_packets.borrow_mut().pop(now)
    }

    /// Routes the packet from the source host through the virtual internet to
    /// the destination host.
    fn route_outgoing_packet(&self, packet: PacketRc) {
        // TODO: move Worker::send_packet to here?
        Worker::with_active_host(|src_host| Worker::send_packet(src_host, packet)).unwrap();
    }

    /// Routes the packet from the virtual internet into our CoDel queue, which
    /// can then be received by the destiantion host by calling pop().
    pub fn route_incoming_packet(&self, packet: PacketRc) {
        self.push_inner(packet, Worker::current_time().unwrap())
    }
}

impl PacketDevice for RouterDevice {
    fn get_address(&self) -> Ipv4Addr {
        self.address
    }

    fn pop(&self) -> Option<PacketRc> {
        // When the host calls pop, we provide the next packet from the CoDel queue.
        self.pop_inner(Worker::current_time().unwrap())
    }

    fn push(&self, packet: PacketRc) {
        // When the host calls push, we send to the virtual internet.
        self.route_outgoing_packet(packet);
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
///   allowance by 1500 bytes
/// - refill: `refill_size` token gets added to the bucket
/// - interval 2: send `refill_size` - `CONFIG_MTU` bytes, sending under the
///   allowance by 1500 bytes
/// - refill: `refill_size` token gets added to the bucket
/// - interval 3: send `refill_size` + `CONFIG_MTU` bytes, sending over the
///   allowance by 1500 bytes
/// - repeat
///
/// So it could become less smooth and more "bursty" even though the long term
/// average is maintained. But I don't think this would happen much in practice,
/// and we are batching sends for performance reasons.
fn get_burst_allowance() -> u64 {
    cshadow::CONFIG_MTU.into()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::network::tests::mock_time_millis;

    #[test]
    fn empty() {
        let now = mock_time_millis(1000);
        let router = RouterDevice::new(Ipv4Addr::UNSPECIFIED);
        assert!(router.inbound_packets.borrow().peek().is_none());
        assert!(router.pop_inner(now).is_none());
    }

    #[test]
    // Ignore in miri for use of c::packet* functions.
    #[cfg_attr(miri, ignore)]
    fn push_pop_simple() {
        let now = mock_time_millis(1000);
        let router = RouterDevice::new(Ipv4Addr::UNSPECIFIED);

        const N: usize = 10;

        for _ in 1..=N {
            router.push_inner(PacketRc::new_ipv4_udp_mock(), now);
            assert!(router.inbound_packets.borrow().peek().is_some());
        }
        for _ in 1..=N {
            assert!(router.inbound_packets.borrow().peek().is_some());
            assert!(router.pop_inner(now).is_some());
        }

        assert!(router.inbound_packets.borrow().peek().is_none());
        assert!(router.pop_inner(now).is_none());
    }
}
