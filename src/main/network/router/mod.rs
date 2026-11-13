//! A [`Router`] routes [`PacketRc`]s between [`PacketDevice`]s and the
//! simulated network, enforcing configured send/receive rate limits on the
//! interfaces and the upstream/downstream links to a modeled network provider.
//!
//! The [`Router`] is responsible for actually moving packets around between
//! sockets and the simulated network. It initiates packet forwarding and
//! internally schedules tasks to ensure that packets are continually forwarded
//! over time without exceeding the configured [`RateLimit`]s.
//!
//! The [`Router`] implemented here is doing the job of multiple real world
//! physical components, rather than being a one-to-one mapping to a known
//! network component. It is managing sending from a physical network interface
//! card at a certain speed, and also it is managing the first-hop router device
//! that the network interface card is attached to, shaping traffic, and using a
//! codel queue to aggregate all its ports up through a single upstream link.
//!
//! [`PacketDevice`]s are registered using [`Router::attach_packet_device`],
//! assigning an [`Ipv4Addr`] to it. The address provided by the caller will
//! later be passed to [`RouterContext::get_packet_device_fn`] to obtain the
//! underlying object that implements the [`PacketDevice`] trait. This packet
//! device supplies the router with a stream of packets (through its
//! implementation of [`PacketDevice::pop`]) that will be forwarded to the
//! network, and it acts as a sink for incoming packets with a destination
//! address matching the packet device's address.
//!
//! [`Ipv4Addr::LOCALHOST`] has special meaning, and it is expected that a
//! packet device will be registered at this address. The router uses the device
//! as the loopback device, applying its rate limits for local packets being
//! sent from any device.
//!
//! The [`Router`] operates its own event management queues and its scheduler
//! must be run at appropriate times to faciliate the routing process.
//! [`Router::run`] must be called with event [`RouterEvent::DeviceInterrupt`]
//! when a packet device changes state from empty to non-empty, to alert an idle
//! router to start forwarding packets from that device. [`Router::run`] must be
//! called with event [`RouterEvent::PacketArrival`] when a packet arrives from
//! the simulated network. Finally, if [`Router::run`] returns an
//! [`EmulatedTime`], then [`Router::run`] must be called with event
//! [`RouterEvent::Wakeup`] at the specified time. All other routing tasks are
//! handled internally.
//!
//! The [`Router`] is designed to avoid directly relying on Shadow runtime
//! components and to be easy to test with unit tests. This is enabled through
//! the [`RouterContext`] object, which defines the minimal external API the
//! router needs to function. This context must be provided when calling
//! [`Router::run`].
//!
//! A variety of queues are used to model rate limits, as shown below. Each tx
//! queue and rx queue is a lossy rate-limited queue that can drop packets if it
//! becomes bloated (e.g., if we are allowed to push() to a queue at a faster
//! rate than we are allowed to pop() from it based on the configured rate
//! limits). Note, all pop() and push() actions are carried out by the router,
//! not the individual components listed here.
//!
//!   +--------------+               +--------------+
//!   | PacketDevice |               | PacketDevice |
//!   +--------------+               +--------------+
//!          |                              |
//!  Event::DeviceInterrupt        Event::DeviceInterrupt
//!          |                              |
//!          V push()                       V push()
//!     +-----------+                  +-----------+
//!     | Interface |                  | Interface |
//!     | tx queue  |                  | tx queue  |
//!     +-----------+                  +-----------+
//!          |                              |
//!  pop() @ dev tx rate limit    pop() @ dev tx rate limit
//!          |                              |
//!          +--------------+---------------+
//!                         |
//!                         V push()
//!               +-------------------+
//!               |   Global router   |
//!               |     tx queue      |
//!               +-------------------+
//!                         |
//!             pop() @ upstream tx rate limit
//!                         |
//!                         V Context::send_packet_fn()
//!               +--------------------+
//!               | packets to network |
//!               +--------------------+
//!                        |||
//!              core link ||| models latency, packet loss
//!                        ||| unlimited bandwidth
//!                        VVV
//!               +----------------------+
//!               | packets from network |
//!               +----------------------+
//!                         |
//!              RouterEvent::PacketArrival
//!                         |
//!                         V push()
//!               +-------------------+
//!               |   Global router   |
//!               |     rx queue      |
//!               +-------------------+
//!                         |
//!             pop() @ downstream rx rate limit
//!                         |
//!          +--------------+---------------+
//!          |                              |
//!          V push()                       V push()
//!     +-----------+                  +-----------+
//!     | Interface |                  | Interface |
//!     | rx queue  |                  | rx queue  |
//!     +-----------+                  +-----------+
//!          |                              |
//!  pop() @ dev rx rate limit    pop() @ dev rx rate limit
//!          |                              |
//!          V push()                       V push()
//!   +--------------+               +--------------+
//!   | PacketDevice |               | PacketDevice |
//!   +--------------+               +--------------+
//!
//! Note: the cross-device loopback process is not shown above.
//!

use std::cell::Ref;
use std::collections::{BTreeSet, HashMap};
use std::net::Ipv4Addr;

use atomic_refcell::AtomicRefCell;
use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::simulation_time::SimulationTime;

use crate::cshadow;
use crate::network::packet::PacketRc;
use crate::network::{PacketDevice, RateLimit};
use crate::utility::ObjectCounter;

use self::codel_queue::CoDelQueue;
use self::task::{TaskKind, TaskQueue};
use self::token_bucket::TokenBucket;

mod codel_queue;
mod task;
mod token_bucket;

/// An alias for the router context, to clearly separate it from other contexts
/// in the simulator. Code external to the router should probably prefer this
/// [`RouterContext`] to the [`Context`] syntax for clarity.
pub type RouterContext<'a, T> = Context<'a, T>;

/// Generalizes the runtime state needed to run the router.
//
// This both improves the clarity of the code, and enables unit testing. Note:
// we name this as Context here, instead of RouterContext, to keep the router's
// internal implementation more concise.
#[derive(Clone, Copy, Debug)]
pub struct Context<'a, T> {
    /// The current value of the runtime clock.
    pub now: EmulatedTime,
    /// If the runtime is in bootstrapping mode, where rate limits are ignored.
    pub is_bootstrapping: bool,
    /// The function the router will call when it needs to send a packet over
    /// the simulated network.
    pub send_packet_fn: fn(&'a T, PacketRc),
    /// The function the router will call to obtain the underlying packet device
    /// for the address configured in [`Router::attach_packet_device`]. The
    /// underlying device is a src/stream for outgoing packets and a dst/sink
    /// for incoming packets.
    pub get_packet_device_fn: fn(&'a T, Ipv4Addr) -> Option<Ref<'_, dyn PacketDevice>>,
    /// The first argument that will be supplied when calling the
    /// [`Context::send_packet_fn`] and [`Context::get_packet_device_fn`]
    /// callback functions.
    pub fn_arg: &'a T,
}

/// An alias for a router event, to clearly separate it from other events in the
/// simulator. Code external to the router should probably prefer this
/// [`RouterEvent`] to the [`Event`] syntax for clarity.
pub type RouterEvent = Event;

/// The events handled by the [`Router`].
//
// Although there is some overlap, this is designed to be distinct from the
// router's [`Task`] object: a task in an internal implementation detail and we
// don't expose all task kinds outside of this module.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Event {
    /// A packet has arrived to the host from the simulated network.
    PacketArrival(PacketRc),
    /// A packet device has new data that needs to be routed.
    DeviceInterrupt(Ipv4Addr),
    /// The router's requested wakeup time has been reached.
    Wakeup,
}

// In our internal implementation, every event is a task that needs to be run.
impl From<Event> for TaskKind {
    fn from(event: Event) -> Self {
        match event {
            Event::PacketArrival(packet) => TaskKind::Deliver(packet),
            Event::DeviceInterrupt(addr) => TaskKind::TxDevice(addr),
            Event::Wakeup => TaskKind::ProcessQueue,
        }
    }
}

/// Tracks the current state of the router to help prevent recursive run loops.
#[derive(Clone, Debug, Eq, PartialEq)]
enum State {
    /// The router is currently not executing its runtime logic; it is waiting
    /// to be notified that a [`Event`] has occured (via a call to
    /// [`Router::run`]).
    Idle,
    /// The router is executing its runtime logic. Any [`Event`]s that occur
    /// while in this state held in this deferred queue (to be handled when
    /// running is complete).
    Running(Vec<Event>),
}

/// A [`Router`] routes [`PacketRc`]s between [`PacketDevice`]s and the
/// simulated network, enforcing configured send/receive rate limits on the
/// interfaces and the upstream/downstream links to a modeled network provider.
///
/// For more information, see the module docs for [`crate::network::router`].
#[derive(Debug)]
pub struct Router {
    /// The router's current run state.
    state: AtomicRefCell<State>,
    /// The inner collection of queues and interfaces needed to route packets
    /// while conforming to rate limits.
    inner: AtomicRefCell<Inner>,
    /// The collection of tasks the router needs to perform to function.
    tasks: AtomicRefCell<TaskQueue>,
    /// The times we have requested an [`Event::Wakeup`] (i.e., the times we
    /// have returned from [`Router::run`]) that have not yet occurred.
    wakeup_times: AtomicRefCell<BTreeSet<EmulatedTime>>,
    /// Counts allocations/deallocations. Declared at the end so a drop is not
    /// counted until the other struct elements above are dropped.
    _counter: ObjectCounter,
}

#[derive(Debug)]
struct Inner {
    /// Our global transmit queue. Packets from this queue are routed to the network.
    txq: RateLimitedQueue,
    /// Our global receive queue. Packets from the network are routed to this queue.
    rxq: RateLimitedQueue,
    /// The attached packet devices, each of which is wrapped with our Interface
    /// type to manage its tx and rx queues.
    interfaces: HashMap<Ipv4Addr, Interface>,
}

impl Inner {
    fn new(tx_rate: RateLimit, rx_rate: RateLimit) -> Self {
        Self {
            txq: RateLimitedQueue::new(tx_rate),
            rxq: RateLimitedQueue::new(rx_rate),
            interfaces: HashMap::new(),
        }
    }
}

impl Router {
    /// Creates a new [`Router`] instance with the given rate limits set on its
    /// global transmit and receive queues.
    pub fn new(tx_rate: RateLimit, rx_rate: RateLimit) -> Self {
        Self {
            state: AtomicRefCell::new(State::Idle),
            inner: AtomicRefCell::new(Inner::new(tx_rate, rx_rate)),
            tasks: AtomicRefCell::new(TaskQueue::new()),
            wakeup_times: AtomicRefCell::new(BTreeSet::new()),
            _counter: ObjectCounter::new("Router"),
        }
    }

    /// Add a new device at the given address, and assign the given rate limits
    /// to the associated transmit and receive queues.
    ///
    /// It is assumed that the router will be able to obtain a reference to the
    /// underlying device implementing the [`PacketDevice`] trait when it calls
    /// [`RouterContext::get_packet_device_fn`] and passes the address provided here. It
    /// is up to the caller to set the correct router context and addressing.
    pub fn attach_packet_device(&mut self, addr: Ipv4Addr, tx_rate: RateLimit, rx_rate: RateLimit) {
        let device = Interface::new(addr, tx_rate, rx_rate);
        self.inner.borrow_mut().interfaces.insert(addr, device);
    }

    /// Run the router with the given context to handle the provided [`RouterEvent`].
    ///
    /// If this function returns [`Some`], the caller must ensure that
    /// [`Router::run`] is called again with the [`RouterEvent::Wakeup`] event
    /// at the contained [`EmulatedTime`].
    pub fn run<T>(&self, ctx: &Context<T>, event: RouterEvent) -> Option<EmulatedTime> {
        log::trace!("Router::run({event:?})");

        // If we are already running, defer this event to run in our
        // pre-existing processing loop to prevent double borrows.
        if let State::Running(deferred) = &mut *self.state.borrow_mut() {
            deferred.push(event);
            return None;
        }

        // We were idle and now we are running.
        *self.state.borrow_mut() = State::Running(Vec::new());

        if event == Event::Wakeup {
            self.wakeup_times.borrow_mut().remove(&ctx.now);
            self.process_task_queue(ctx);
        } else {
            self.schedule(ctx, TaskKind::from(event));
        };

        // Change state back from running to idle.
        let prev_state = {
            let state = &mut *self.state.borrow_mut();
            std::mem::replace(state, State::Idle)
        };
        let State::Running(deferred) = prev_state else {
            panic!("Expected router in State::Running");
        };

        // Schedule any events we deferred while we were running.
        for event in deferred {
            self.schedule(ctx, TaskKind::from(event));
        }

        // Decide when to run the router next.
        let next = self.tasks.borrow().peek().map(|task| task.when);
        next.and_then(|when| {
            if !self.wakeup_times.borrow().contains(&when) {
                self.wakeup_times.borrow_mut().insert(when);
                Some(when)
            } else {
                None
            }
        })
    }

    /// Schedules a task in the routers scheduling queue for future processing.
    fn schedule<T>(&self, ctx: &Context<T>, kind: TaskKind) {
        // Compute when we are allowed to run the task based on rate limits.
        let when = {
            let inner = self.inner.borrow();
            match &kind {
                TaskKind::Deliver(_) => ctx.now,
                TaskKind::TxGlobal => inner.txq.ready_at(ctx),
                TaskKind::RxGlobal => inner.rxq.ready_at(ctx),
                TaskKind::TxDevice(addr) => match inner.interfaces.get(addr) {
                    Some(iface) => iface.txq.ready_at(ctx),
                    None => return Router::warn_schedule_failed(&kind, "interface not found"),
                },
                TaskKind::RxDevice(addr) => match inner.interfaces.get(addr) {
                    Some(iface) => iface.rxq.ready_at(ctx),
                    None => return Router::warn_schedule_failed(&kind, "interface not found"),
                },
                TaskKind::ProcessQueue => ctx.now,
            }
        };

        // Schedule our task, ensuring forward progression of time.
        self.tasks.borrow_mut().schedule(kind, when.max(ctx.now));
    }

    /// Process all scheduled tasks that have been schedule to run at or before
    /// the current time provided in the context.
    fn process_task_queue<T>(&self, ctx: &Context<T>) {
        // Process all scheduled non-future tasks.
        while self
            .tasks
            .borrow()
            .peek()
            .is_some_and(|x| x.when <= ctx.now)
        {
            // Process the next operation without holding the borrow.
            let task = self.tasks.borrow_mut().pop().unwrap();
            self.process(ctx, task.kind);

            // Schedule any new tasks that were generated.
            for kind in self.collect_interrupts() {
                self.schedule(ctx, kind);
            }
        }
    }

    /// Process an individual task.
    fn process<T>(&self, ctx: &Context<T>, kind: TaskKind) {
        match kind {
            TaskKind::Deliver(packet) => self.process_deliver(ctx, packet),
            TaskKind::TxGlobal => self.process_tx_global(ctx),
            TaskKind::RxGlobal => self.process_rx_global(ctx),
            TaskKind::TxDevice(addr) => self.process_tx_device(ctx, addr),
            TaskKind::RxDevice(addr) => self.process_rx_device(ctx, addr),
            TaskKind::ProcessQueue => {} // Already in progress.
        }
    }

    /// A packet arriving from the network gets pushed to our global rx queue.
    fn process_deliver<T>(&self, ctx: &Context<T>, packet: PacketRc) {
        self.inner.borrow_mut().rxq.push(ctx, packet);
    }

    /// Use [`Context::send_packet_fn`] to send all packets ready to be sent.
    fn process_tx_global<T>(&self, ctx: &Context<T>) {
        while let Some(packet) = self.inner.borrow_mut().txq.pop(ctx) {
            (ctx.send_packet_fn)(ctx.fn_arg, packet);
        }
    }

    /// Route packets from the global receive queue to the corresponding device
    /// receive queue.
    fn process_rx_global<T>(&self, ctx: &Context<T>) {
        let mut inner = self.inner.borrow_mut();

        while let Some(packet) = inner.rxq.pop(ctx) {
            let dst_addr = *packet.dst_ipv4_address().ip();
            if let Some(iface) = inner.interfaces.get_mut(&dst_addr) {
                iface.rxq.push(ctx, packet);
            } else {
                Router::warn_process_failed(
                    &TaskKind::RxGlobal,
                    &format!("device not found, packet dst {dst_addr}"),
                );
            }
        }
    }

    /// Route packets from the underlying packet device to the correct transmit
    /// queue for that device, and then up to the global transmit queue as rate
    /// limits allow. Loopback packets are routed over to the loopback receive
    /// queue.
    fn process_tx_device<T>(&self, ctx: &Context<T>, addr: Ipv4Addr) {
        let mut inner = self.inner.borrow_mut();

        // Temporarily remove the device for processing.
        // TODO: add a drop guard to add it back later, so we don't return
        // somewhere and forget?
        let Some(mut iface) = inner.interfaces.remove(&addr) else {
            Router::warn_process_failed(&TaskKind::TxDevice(addr), "interface not found");
            return;
        };

        loop {
            let pkt = iface.txq.pop(ctx).or_else(|| {
                if iface.txq.blocked_until.is_some() {
                    // We reached our rate limit, try again later.
                    //
                    // TODO: since we return here, we won't be able to redirect
                    // loopback packets from this packet device (see below) while we
                    // are out of tokens. Fixing this may require a deeper redesign
                    // of shadow's network interface and socket layers.
                    return None; // returns from closure
                }

                // Not rate limited yet, check underlying packet device until we get
                // a packet.
                while let Some(pkt) = iface.pop(ctx) {
                    // We got a packet, use the correct rate limit.
                    //
                    // NOTE: Unbound UDP sockets may send both local and nonlocal
                    // packets from the default interface, and may send loopback
                    // packets, so we must redirect to the right rate limiter.
                    let src_addr = *pkt.src_ipv4_address().ip();
                    let dst_addr = *pkt.dst_ipv4_address().ip();
                    let is_local = src_addr == dst_addr || src_addr.is_loopback();

                    if is_local && !addr.is_loopback() {
                        if let Some(lo) = inner.interfaces.get_mut(&Ipv4Addr::LOCALHOST) {
                            // Redirect over loopback, applying its configured rate
                            // limits instead of this device's rate limits.
                            lo.txq.push(ctx, pkt);
                        } else {
                            Router::warn_routing_failed(&dst_addr, "lo tx interface not found");
                        }
                        continue;
                    } else {
                        // Push then pop from this device's tx queue so we only
                        // return a packet if it conforms to the device rate limit.
                        iface.txq.push(ctx, pkt);
                        return iface.txq.pop(ctx);
                    }
                }

                // The underlying device is empty for now.
                None
            });

            let Some(pkt) = pkt else {
                break;
            };

            // Got a conforming packet in the txq. Route based on the dst
            // address. From above, packets with default ip may show up on
            // loopback for rate limiting, which we now route over to its rxq.
            let src_addr = *pkt.src_ipv4_address().ip();
            let dst_addr = *pkt.dst_ipv4_address().ip();
            let is_local = src_addr == dst_addr || dst_addr.is_loopback();

            if is_local {
                // Route over loopback to its rxq.
                if addr.is_loopback() {
                    iface.rxq.push(ctx, pkt);
                } else if let Some(lo) = inner.interfaces.get_mut(&Ipv4Addr::LOCALHOST) {
                    lo.rxq.push(ctx, pkt);
                } else {
                    Router::warn_routing_failed(&dst_addr, "lo rx interface not found");
                }
            } else {
                // Route upstream for network transmission.
                inner.txq.push(ctx, pkt);
            }
        }

        // Restore the device in our collection.
        inner.interfaces.insert(addr, iface);
    }

    /// Route packets from the device receive queue to the actual underlying
    /// packet device.
    fn process_rx_device<T>(&self, ctx: &Context<T>, addr: Ipv4Addr) {
        let mut inner = self.inner.borrow_mut();

        // Temporarily remove the device for processing.
        // TODO: add a drop guard to add it back later, so we don't return
        // somewhere and forget?
        let Some(mut iface) = inner.interfaces.remove(&addr) else {
            Router::warn_process_failed(&TaskKind::RxDevice(addr), "device not found");
            return;
        };

        while let Some(pkt) = iface.rxq.pop(ctx) {
            let dst_addr = *pkt.dst_ipv4_address().ip();

            // We may have packets in the loopback queue that have src and dst
            // ip addresses of the default interface.
            if addr == dst_addr {
                iface.push(ctx, pkt);
            } else if let Some(other) = inner.interfaces.get_mut(&dst_addr) {
                other.push(ctx, pkt);
            } else {
                Router::warn_routing_failed(&dst_addr, "rx device not found");
            }
        }

        // Restore the device in our collection.
        inner.interfaces.insert(addr, iface);
    }

    /// Checks all interrupts that have occurred on our queues since the last
    /// time we checked, and collects the corresponding router tasks we need to
    /// execute to handle the interrupts.
    fn collect_interrupts(&self) -> impl IntoIterator<Item = TaskKind> {
        let mut kinds = Vec::new();
        let mut inner = self.inner.borrow_mut();

        if inner.txq.take_irq() {
            kinds.push(TaskKind::TxGlobal);
        }

        if inner.rxq.take_irq() {
            kinds.push(TaskKind::RxGlobal);
        }

        for (addr, iface) in inner.interfaces.iter_mut() {
            if iface.txq.take_irq() {
                kinds.push(TaskKind::TxDevice(*addr));
            }

            if iface.rxq.take_irq() {
                kinds.push(TaskKind::RxDevice(*addr));
            }
        }

        kinds
    }

    fn warn_schedule_failed(kind: &TaskKind, reason: &str) {
        log::warn!("Router cannot schedule task {kind:?}: {reason}");
    }

    fn warn_process_failed(kind: &TaskKind, reason: &str) {
        log::warn!("Router cannot process task {kind:?}: {reason}");
    }

    fn warn_routing_failed(addr: &Ipv4Addr, reason: &str) {
        log::warn!("Router cannot route packet on {addr:?}: {reason}");
    }
}

/// An object to help us pop and push to an underlying packet device while
/// conforming to configured rate limits.
#[derive(Debug)]
struct Interface {
    /// The address associated with the underlying packet device.
    addr: Ipv4Addr,
    /// The transmit queue, used to model the underlying packet devices send
    /// speed.
    txq: RateLimitedQueue,
    /// The receive queue, used to model the underlying packet devices receive
    /// speed.
    rxq: RateLimitedQueue,
}

impl Interface {
    /// Create a new interface with rate-limited transmit and receive queues.
    ///
    /// The provided address will be used with [`Context::get_packet_device_fn`]
    /// to obtain the underlying packet device when we need to push/pop to/from
    /// it.
    fn new(addr: Ipv4Addr, tx_rate: RateLimit, rx_rate: RateLimit) -> Self {
        Self {
            addr,
            txq: RateLimitedQueue::new(tx_rate),
            rxq: RateLimitedQueue::new(rx_rate),
        }
    }

    /// Pops the next packet directly from the underlying packet device.
    fn pop<T>(&mut self, ctx: &Context<T>) -> Option<PacketRc> {
        (ctx.get_packet_device_fn)(ctx.fn_arg, self.addr).and_then(|dev| dev.pop())
    }

    /// Pushes the packet directly to the underlying packet device.
    fn push<T>(&mut self, ctx: &Context<T>, packet: PacketRc) {
        if let Some(dev) = (ctx.get_packet_device_fn)(ctx.fn_arg, self.addr) {
            dev.push(packet)
        }
    }
}

/// A rate-limited queue that can be popped only if it conforms to the
/// configured rate limit. The queue is idle until it detects an action that
/// requires it to be popped again, at which point it will exhibit an interrupt
/// request.
///
/// There are two actions that cause interrupt requests (and that will need us
/// to schedule a task on the router queue):
/// 1. The queue changed from empty to nonempty.
/// 2. The queue changed from ready to rate limited.
///
/// [`RateLimitedQueue::take_irq`] should be polled to detect the requests and
/// schedule the appropriate router task.
#[derive(Debug)]
struct RateLimitedQueue {
    /// The underlying queue storing packets.
    packets: CoDelQueue,
    /// The object we use to enforce a rate limit; None means no limit.
    limiter: Option<TokenBucket>,
    /// If we reached our rate limit, we become unblocked at this time.
    blocked_until: Option<EmulatedTime>,
    /// Tracks the queue's interrupt request state.
    irq: bool,
}

impl RateLimitedQueue {
    /// Creates a new packet queue that can be popped according the the given
    /// rate limit.
    fn new(limit: RateLimit) -> Self {
        Self {
            packets: CoDelQueue::default(),
            limiter: limit.into(),
            blocked_until: None,
            irq: false,
        }
    }

    /// Pushes a packet to the underling queue, tracking if the router needs to
    /// take action on the queue after the push.
    fn push<T>(&mut self, ctx: &Context<T>, packet: PacketRc) {
        if self.packets.is_empty() {
            self.irq = true;
        }
        self.packets.push(packet, ctx.now);
    }

    /// Pops a packet from the underling queue according to the configured rate
    /// limit, tracking if the router needs to take action on the queue after
    /// the pop.
    ///
    /// Returns [`None`] if the queue is empty or has become rate limited.
    fn pop<T>(&mut self, ctx: &Context<T>) -> Option<PacketRc> {
        // Check that we do not exceed rate limit.
        if !ctx.is_bootstrapping
            && let Some(tb) = &mut self.limiter
            && let Some(packet) = self.packets.peek()
        {
            let tokens_needed = u64::try_from(packet.len()).unwrap_or(u64::MAX);
            match tb.comforming_remove(tokens_needed, &ctx.now) {
                Ok(_) => self.blocked_until = None,
                Err(duration) => {
                    if self.blocked_until.is_none() {
                        self.irq = true;
                    }
                    self.blocked_until = Some(ctx.now.saturating_add(duration));
                    return None;
                }
            }
        }

        let result = self.packets.pop(ctx.now);
        // If we had a pending irq but now emptied the queue, cancel it.
        if self.irq && self.packets.is_empty() {
            self.irq = false;
        }
        result
    }

    /// Returns true if we have an outstanding interrupt request, and clears our
    /// internal flag for tracking interrupt requests.
    fn take_irq(&mut self) -> bool {
        std::mem::replace(&mut self.irq, false)
    }

    /// Returns the time when we will not be blocked due to rate limiting (i.e.,
    /// when [`RateLimitedQueue::pop`] will not return [`None`] because of a
    /// rate limit block).
    fn ready_at<T>(&self, ctx: &Context<T>) -> EmulatedTime {
        self.blocked_until.unwrap_or(ctx.now)
    }
}

impl From<RateLimit> for Option<TokenBucket> {
    fn from(limit: RateLimit) -> Self {
        match limit {
            RateLimit::BytesPerSecond(bytes) => Some(create_token_bucket(bytes)),
            RateLimit::Unlimited => None,
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
    use std::cell::RefCell;
    use std::collections::VecDeque;
    use std::net::{Ipv4Addr, SocketAddrV4};

    // The time to start the router events. This needs to stay in sync with the
    // time in `TokenBucket::new()`.
    const START_TIME: EmulatedTime = EmulatedTime::SIMULATION_START;

    // For test convenience.
    const TEST_ADDR: Ipv4Addr = Ipv4Addr::new(10, 0, 0, 1);

    // Length of packet to use in tests.
    const PACKET_LEN: usize = 1_460;

    // Ensures CoDel queue won't drop packets during test. Equates to 500 bytes
    // per millisecond refill and ~2,000 byte capacity, so the bucket will have
    // to block after the first packet of length PACKET_LEN.
    const BPS_LIMIT_LOW: u64 = 500_000;

    // Ensures CoDel queue won't drop packets during test. Equates to 1,000
    // bytes per millisecond refill and ~2,500 byte capacity, so the bucket will
    // have to block after the first packet of length PACKET_LEN.
    const BPS_LIMIT_HIGH: u64 = 1_000_000;

    struct MockHost {
        router: Router,
        interfaces: HashMap<Ipv4Addr, RefCell<MockInterface>>,
        net_rx: RefCell<VecDeque<PacketRc>>,
    }

    impl MockHost {
        fn new(tx_rate: RateLimit, rx_rate: RateLimit) -> Self {
            Self {
                router: Router::new(tx_rate, rx_rate),
                interfaces: HashMap::new(),
                net_rx: RefCell::new(VecDeque::new()),
            }
        }

        fn add_device(&mut self, addr: Ipv4Addr, tx: RateLimit, rx: RateLimit) {
            let dev = RefCell::new(MockInterface::new(addr));
            self.interfaces.insert(addr, dev);
            self.router.attach_packet_device(addr, tx, rx);
        }

        fn ctx<'a>(&'a self, now: EmulatedTime, boot: bool) -> RouterContext<'a, Self> {
            RouterContext {
                now,
                is_bootstrapping: boot,
                send_packet_fn: |env, pkt| env.net_rx.borrow_mut().push_back(pkt),
                get_packet_device_fn: |env, addr| {
                    env.interfaces
                        .get(&addr)
                        .map(|d| d.borrow() as Ref<'_, dyn PacketDevice>)
                },
                fn_arg: self,
            }
        }

        fn enqueue(&mut self, pkt: PacketRc) {
            self.interfaces
                .get(pkt.src_ipv4_address().ip())
                .unwrap()
                .borrow_mut()
                .enqueue(pkt)
        }

        /// Run the router repeatedly until it returns `None`, i.e. becomes idle.
        /// Returns the final `EmulatedTime` that the router processed.
        fn run_router_to_idle(&self, mut now: EmulatedTime) -> EmulatedTime {
            loop {
                let ctx = self.ctx(now, false);
                let next = self.router.run(&ctx, Event::Wakeup);
                match next {
                    Some(t) => now = t, // advance simulated time
                    None => break now,
                }
            }
        }

        fn route_outbound_packet(&mut self, start: EmulatedTime) -> (PacketRc, EmulatedTime) {
            let (mut pkts, end) = self.route_outbound_packets(1, start);
            (pkts.pop().unwrap(), end)
        }

        fn route_outbound_packets(
            &mut self,
            n: usize,
            start: EmulatedTime,
        ) -> (Vec<PacketRc>, EmulatedTime) {
            let mut packets = Vec::new();

            // Packet starts in a device (socket) queue.
            for _ in 0..n {
                let pkt = create_packet(TEST_ADDR, Ipv4Addr::UNSPECIFIED, PACKET_LEN);
                self.enqueue(pkt.clone());
                packets.push(pkt);
            }

            // Deliver the device interrupt.
            let ctx = self.ctx(start, false);
            let res = self.router.run(&ctx, Event::DeviceInterrupt(TEST_ADDR));

            // Run the router until it's idle.
            let end = if let Some(timeout) = res {
                self.run_router_to_idle(timeout)
            } else {
                start
            };
            (packets, end)
        }

        fn route_inbound_packet(&mut self, start: EmulatedTime) -> (PacketRc, EmulatedTime) {
            let (mut pkts, end) = self.route_inbound_packets(1, start);
            (pkts.pop().unwrap(), end)
        }

        fn route_inbound_packets(
            &mut self,
            n: usize,
            start: EmulatedTime,
        ) -> (Vec<PacketRc>, EmulatedTime) {
            let mut packets = Vec::new();

            // A packet starts in the network side.
            for _ in 0..n {
                let pkt = create_packet(Ipv4Addr::UNSPECIFIED, TEST_ADDR, PACKET_LEN);
                packets.push(pkt);
            }

            // Deliver the packet arrival events.
            let ctx = self.ctx(start, false);

            // Keep track of timeouts so we wake it up when requested.
            let mut timeouts = VecDeque::new();

            for pkt in packets.iter() {
                let res = self.router.run(&ctx, Event::PacketArrival(pkt.clone()));
                if let Some(timeout) = res {
                    timeouts.push_back(timeout);
                }
            }

            // Advance simulated time.
            let mut now = start;

            while let Some(next) = timeouts.pop_front() {
                now = now.max(next);
                let ctx = self.ctx(now, false);
                if let Some(timeout) = self.router.run(&ctx, Event::Wakeup) {
                    timeouts.push_back(timeout);
                }
            }

            (packets, now)
        }
    }

    struct MockInterface {
        addr: Ipv4Addr,
        // Queue of packets the device will return on `pop`.
        tx: RefCell<VecDeque<PacketRc>>,
        // Captured packets that were `push`ed by the router.
        rx: RefCell<VecDeque<PacketRc>>,
    }

    impl MockInterface {
        fn new(addr: Ipv4Addr) -> Self {
            Self {
                addr,
                tx: RefCell::new(VecDeque::new()),
                rx: RefCell::new(VecDeque::new()),
            }
        }

        fn enqueue(&self, pkt: PacketRc) {
            self.tx.borrow_mut().push_back(pkt);
        }
    }

    impl PacketDevice for MockInterface {
        fn get_address(&self) -> Ipv4Addr {
            self.addr
        }

        fn pop(&self) -> Option<PacketRc> {
            self.tx.borrow_mut().pop_front()
        }

        fn push(&self, packet: PacketRc) {
            self.rx.borrow_mut().push_back(packet);
        }
    }

    fn create_packet(src: Ipv4Addr, dst: Ipv4Addr, len: usize) -> PacketRc {
        // The Router does not inspect port numbers so set to 0.
        let src = SocketAddrV4::new(src, 0);
        let dst = SocketAddrV4::new(dst, 0);
        PacketRc::new_ipv4_udp_mock_with(src, dst, len)
    }

    #[test]
    fn outbound_packet_is_pushed_to_network() {
        let mut host = MockHost::new(RateLimit::Unlimited, RateLimit::Unlimited);
        host.add_device(TEST_ADDR, RateLimit::Unlimited, RateLimit::Unlimited);

        let (pkt, end) = host.route_outbound_packet(START_TIME);

        // The packet should have been sent to the network.
        assert_eq!(host.net_rx.borrow().len(), 1);
        assert!(host.net_rx.borrow()[0] == pkt);

        // No time has passed.
        assert_eq!(end, START_TIME);
    }

    #[test]
    fn inbound_packet_is_pushed_to_device() {
        let mut host = MockHost::new(RateLimit::Unlimited, RateLimit::Unlimited);
        host.add_device(TEST_ADDR, RateLimit::Unlimited, RateLimit::Unlimited);

        let (pkt, end) = host.route_inbound_packet(START_TIME);

        // The packet should have been received by the device.
        let dev = host.interfaces.get(&TEST_ADDR).unwrap().borrow();
        let queue = dev.rx.borrow();
        assert_eq!(queue.len(), 1);
        assert!(queue[0] == pkt);

        // No time has passed.
        assert_eq!(end, START_TIME);
    }

    fn assert_net_rx_after_delay(
        host: &MockHost,
        pkts: Vec<PacketRc>,
        start: EmulatedTime,
        end: EmulatedTime,
    ) {
        // The packet should have been sent to the network.
        let queue = host.net_rx.borrow();
        assert_eq!(queue.len(), pkts.len());
        for i in 0..pkts.len() {
            assert!(queue[i] == pkts[i]);
        }

        // Needed some time to acquire enough tokens to send.
        assert!(end > start);
    }

    #[test]
    fn outbound_isp_rate_limited() {
        // Use rate limit so the packet will be throttled.
        let mut host = MockHost::new(
            RateLimit::BytesPerSecond(BPS_LIMIT_LOW),
            RateLimit::Unlimited,
        );
        host.add_device(TEST_ADDR, RateLimit::Unlimited, RateLimit::Unlimited);

        let (pkts, end) = host.route_outbound_packets(2, START_TIME);

        assert_net_rx_after_delay(&host, pkts, START_TIME, end);
    }

    #[test]
    fn outbound_device_rate_limited() {
        // Use rate limit so the packet will be throttled.
        let mut host = MockHost::new(RateLimit::Unlimited, RateLimit::Unlimited);
        host.add_device(
            TEST_ADDR,
            RateLimit::BytesPerSecond(BPS_LIMIT_LOW),
            RateLimit::Unlimited,
        );

        let (pkts, end) = host.route_outbound_packets(2, START_TIME);

        assert_net_rx_after_delay(&host, pkts, START_TIME, end);
    }

    #[test]
    fn outbound_isp_and_device_rate_limited() {
        // Use rate limit so the packet will be throttled.
        let mut host = MockHost::new(
            RateLimit::BytesPerSecond(BPS_LIMIT_LOW),
            RateLimit::Unlimited,
        );
        // The device can send before the isp so it hits both limits.
        host.add_device(
            TEST_ADDR,
            RateLimit::BytesPerSecond(BPS_LIMIT_HIGH),
            RateLimit::Unlimited,
        );

        let (pkts, end) = host.route_outbound_packets(2, START_TIME);

        assert_net_rx_after_delay(&host, pkts, START_TIME, end);
    }

    fn assert_dev_rx_after_delay(
        host: &MockHost,
        pkts: Vec<PacketRc>,
        start: EmulatedTime,
        end: EmulatedTime,
    ) {
        // The packet should have been received by the device.
        let dev = host.interfaces.get(&TEST_ADDR).unwrap().borrow();
        let queue = dev.rx.borrow();
        assert_eq!(queue.len(), pkts.len());
        for i in 0..pkts.len() {
            assert!(queue[i] == pkts[i]);
        }

        // Needed some time to acquire enough tokens to send.
        assert!(end > start);
    }

    #[test]
    fn inbound_isp_rate_limited() {
        // Use rate limit so the packet will be throttled.
        let mut host = MockHost::new(
            RateLimit::Unlimited,
            RateLimit::BytesPerSecond(BPS_LIMIT_LOW),
        );
        host.add_device(TEST_ADDR, RateLimit::Unlimited, RateLimit::Unlimited);

        let (pkts, end) = host.route_inbound_packets(2, START_TIME);

        assert_dev_rx_after_delay(&host, pkts, START_TIME, end);
    }

    #[test]
    fn inbound_device_rate_limited() {
        // Use rate limit so the packet will be throttled.
        let mut host = MockHost::new(RateLimit::Unlimited, RateLimit::Unlimited);
        host.add_device(
            TEST_ADDR,
            RateLimit::Unlimited,
            RateLimit::BytesPerSecond(BPS_LIMIT_LOW),
        );

        let (pkts, end) = host.route_inbound_packets(2, START_TIME);

        assert_dev_rx_after_delay(&host, pkts, START_TIME, end);
    }

    #[test]
    fn inbound_isp_and_device_rate_limited() {
        // Use rate limit so the packet will be throttled.
        // The isp can send before the device so it hits both limits.
        let mut host = MockHost::new(
            RateLimit::Unlimited,
            RateLimit::BytesPerSecond(BPS_LIMIT_HIGH),
        );
        host.add_device(
            TEST_ADDR,
            RateLimit::Unlimited,
            RateLimit::BytesPerSecond(BPS_LIMIT_LOW),
        );

        let (pkts, end) = host.route_inbound_packets(2, START_TIME);

        assert_dev_rx_after_delay(&host, pkts, START_TIME, end);
    }
}
