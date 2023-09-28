//! Tests for the TCP state machine. All of the types in this module (ex: `Host`, `Event`, etc) are
//! only for testing, and are modelled after their respective Shadow counterparts.

// ignore dead code in tests
// TODO: ideally remove this
#![allow(dead_code)]

mod send_recv;
mod transitions;
mod window_scale;

pub mod util;

use std::cell::{Cell, Ref, RefCell};
use std::cmp::{Ordering, Reverse};
use std::collections::{BinaryHeap, VecDeque};
use std::io::{Read, Write};
use std::net::{Ipv4Addr, SocketAddrV4};
use std::rc::{Rc, Weak};

use crate::tests::util::time::{Duration, Instant};

#[allow(unused_imports)]
use crate::{
    AcceptError, CloseError, ConnectError, Dependencies, Ipv4Header, ListenError, Payload,
    PollState, PopPacketError, PushPacketError, RecvError, RstCloseError, SendError, Shutdown,
    TcpConfig, TcpFlags, TcpHeader, TcpState, TimerRegisteredBy,
};

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
struct Errno(u16);

impl Errno {
    const EINVAL: Self = Self(1);
    const EAGAIN: Self = Self(2);
    const EWOULDBLOCK: Self = Self::EAGAIN;
    const EALREADY: Self = Self(3);
    const EISCONN: Self = Self(4);
    const EPIPE: Self = Self(5);
    const ENOTCONN: Self = Self(5);
}

struct Event {
    time: Instant,
    id: u64,
    callback: Box<dyn FnOnce()>,
}

impl std::fmt::Debug for Event {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Event")
            .field("time", &self.time)
            .field("id", &self.id)
            .field("callback", &"<callback>")
            .finish()
    }
}

impl PartialOrd for Event {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        if std::ptr::eq(self, other) {
            return Some(Ordering::Equal);
        }

        let cmp = self
            .time
            .partial_cmp(&other.time)
            .unwrap()
            .then(self.id.partial_cmp(&other.id).unwrap());

        match cmp {
            // the time and id are the same, but the callbacks aren't comparable (you cannot compare
            // pointers to trait objects)
            Ordering::Equal => None,
            // the time or id was not the same
            x => Some(x),
        }
    }
}

impl Ord for Event {
    fn cmp(&self, other: &Self) -> Ordering {
        // panic if the events aren't comparable
        self.partial_cmp(other).unwrap()
    }
}

impl PartialEq for Event {
    fn eq(&self, other: &Self) -> bool {
        if std::ptr::eq(self, other) {
            return true;
        }

        // either the time or id must be different, if not we must panic since trait objects aren't
        // comparable (you cannot compare pointers to trait objects)
        assert!(self.time != other.time || self.id != other.id);
        false
    }
}

impl Eq for Event {}

#[derive(Debug)]
struct EventQueue {
    queue: BinaryHeap<Reverse<Event>>,
    counter: u64,
}

impl EventQueue {
    pub fn new() -> Self {
        Self {
            queue: BinaryHeap::new(),
            counter: 0,
        }
    }

    pub fn push(&mut self, time: Instant, callback: impl FnOnce() + 'static) {
        self.queue.push(Reverse(Event {
            time,
            id: self.counter,
            callback: Box::new(callback),
        }));

        self.counter += 1;
    }

    // Pop the next event if it is before or equal to `time`.
    pub fn pop_up_to(&mut self, time: Instant) -> Option<(Instant, Box<dyn FnOnce()>)> {
        if let Some(event) = self.queue.peek() {
            if event.0.time > time {
                return None;
            }
        } else {
            return None;
        }

        let event = self.queue.pop().unwrap().0;
        Some((event.time, event.callback))
    }

    pub fn len(&self) -> usize {
        self.queue.len()
    }
}

#[derive(Debug)]
struct Scheduler {
    /// Events queued at specific times.
    event_queue: Rc<RefCell<EventQueue>>,
    /// The current simulation time.
    current_time: Rc<Cell<Instant>>,
    /// A queue of all outgoing packets sent by sockets.
    outgoing_packet_queue: Rc<RefCell<VecDeque<(TcpHeader, Payload)>>>,
}

impl Scheduler {
    pub fn new() -> Self {
        Self {
            event_queue: Rc::new(RefCell::new(EventQueue::new())),
            current_time: Rc::new(Cell::new(Instant::EPOCH)),
            outgoing_packet_queue: Rc::new(RefCell::new(VecDeque::new())),
        }
    }

    pub fn advance_to(&self, time: Instant) {
        // make sure we don't move backwards in time
        assert!(time >= self.current_time.get());

        loop {
            let Some((time, callback)) = self.event_queue.borrow_mut().pop_up_to(time) else {
                break;
            };

            self.current_time.set(time);
            callback();
        }

        self.current_time.set(time);
    }

    pub fn advance(&self, duration: Duration) {
        self.advance_to(self.current_time.get() + duration);
    }

    pub fn pop_packet(&self) -> Option<(TcpHeader, Payload)> {
        self.outgoing_packet_queue.borrow_mut().pop_front()
    }

    pub fn event_queue_rc(&self) -> Rc<RefCell<EventQueue>> {
        Rc::clone(&self.event_queue)
    }

    pub fn current_time_rc(&self) -> Rc<Cell<Instant>> {
        Rc::clone(&self.current_time)
    }

    pub fn outgoing_packet_queue_rc(&self) -> Rc<RefCell<VecDeque<(TcpHeader, Payload)>>> {
        Rc::clone(&self.outgoing_packet_queue)
    }
}

bitflags::bitflags! {
    #[derive(Default, Copy, Clone, Debug)]
    pub struct FileState: u32 {
        /// Can be read, i.e. there is data waiting for user.
        const READABLE = 1 << 0;
        /// Can be written, i.e. there is available buffer space.
        const WRITABLE = 1 << 1;
        /// User already called close.
        const CLOSED = 1 << 2;
    }
}

#[derive(Debug)]
struct StateEventSource {}

impl StateEventSource {
    pub fn new() -> Self {
        Self {}
    }

    pub fn notify_listeners(
        &mut self,
        _state: FileState,
        _changed: FileState,
        //cb_queue: &mut CallbackQueue,
    ) {
        // here we would notify the listeners and add callbacks to `cb_queue`
    }
}

#[derive(Debug)]
struct TestEnvTimerState {
    /// The socket that the timer callback will run on.
    socket: Weak<RefCell<TcpSocket>>,
    /// Whether the timer callback should modify the state of this socket
    /// ([`TimerRegisteredBy::Parent`]), or one of its child sockets ([`TimerRegisteredBy::Child`]).
    registered_by: TimerRegisteredBy,
}

#[derive(Debug)]
struct TestEnvState {
    event_queue: Rc<RefCell<EventQueue>>,
    current_time: Rc<Cell<Instant>>,
    /// State shared between all timers registered from this `TestEnvState`. This is needed since we
    /// may need to update existing pending timers in `Self::update_timers`.
    timer_state: Rc<RefCell<TestEnvTimerState>>,
}

impl Dependencies for TestEnvState {
    type Instant = Instant;
    type Duration = Duration;

    // in shadow we would add a task to the host's event queue through the global worker, but for
    // testing we need an explicit event queue object that we can add events to
    fn register_timer(
        &self,
        time: Instant,
        f: impl FnOnce(&mut TcpState<Self>, TimerRegisteredBy) + Send + Sync + 'static,
    ) {
        // make sure the socket is kept alive in the closure while the timer is waiting to be run
        // (don't store a weak reference), otherwise the socket may have already been dropped and
        // the timer won't run
        let timer_state = self.timer_state.borrow();
        let socket = timer_state.socket.upgrade().unwrap();
        let registered_by = timer_state.registered_by;

        self.event_queue.borrow_mut().push(time, move || {
            socket.borrow_mut().with_tcp_state(|state| {
                f(state, registered_by);
            });
        })
    }

    // in shadow we would get the current time from the global worker
    fn current_time(&self) -> Instant {
        self.current_time.get()
    }

    fn fork(&self) -> Self {
        let timer_state = self.timer_state.borrow();
        let socket_weak = &timer_state.socket;

        // if a child is trying to fork(), something has gone wrong
        assert_eq!(timer_state.registered_by, TimerRegisteredBy::Parent);

        Self {
            event_queue: self.event_queue.clone(),
            current_time: self.current_time.clone(),
            timer_state: Rc::new(RefCell::new(TestEnvTimerState {
                socket: socket_weak.clone(),
                registered_by: TimerRegisteredBy::Child,
            })),
        }
    }
}

/// A TCP socket file object.
#[derive(Debug)]
struct TcpSocket {
    /// The TCP state. Don't mutate this directly, and instead use [`with_tcp_state`].
    tcp_state: TcpState<TestEnvState>,
    socket_weak: Weak<RefCell<Self>>,
    file_state: FileState,
    event_source: StateEventSource,
    /// A global queue of all outgoing packets shared with all sockets.
    outgoing_packet_queue: Rc<RefCell<VecDeque<(TcpHeader, Payload)>>>,
    // a handle for the association of this socket with the host's network interface
    association_handle: Option<AssociationHandle>,
    collect_packets: bool,
}

impl TcpSocket {
    pub fn new(scheduler: &Scheduler, config: TcpConfig) -> Rc<RefCell<Self>> {
        // passed to the state machine so that it can scheduler tasks in the future
        let event_queue_rc = scheduler.event_queue_rc();
        // passed to the state machine so that it can get the current time
        let current_time_rc = scheduler.current_time_rc();
        let outgoing_packet_queue_rc = scheduler.outgoing_packet_queue_rc();

        let rv = Rc::new_cyclic(|weak: &Weak<RefCell<Self>>| {
            let test_env_state = TestEnvState {
                event_queue: event_queue_rc,
                current_time: current_time_rc,
                timer_state: Rc::new(RefCell::new(TestEnvTimerState {
                    socket: weak.clone(),
                    registered_by: TimerRegisteredBy::Parent,
                })),
            };

            RefCell::new(Self {
                tcp_state: TcpState::new(test_env_state, config),
                socket_weak: weak.clone(),
                // the file state shouldn't matter here since we run `with_tcp_state` below to
                // update it
                file_state: FileState::empty(),
                event_source: StateEventSource::new(),
                outgoing_packet_queue: outgoing_packet_queue_rc,
                association_handle: None,
                collect_packets: true,
            })
        });

        // run a no-op function on the state, which will force the socket to update its file state
        // to match the tcp state
        rv.borrow_mut().with_tcp_state(|_state| ());

        rv
    }

    /// Get a reference to the TCP state. This should only be used for testing/verifying the socket
    /// state.
    fn tcp_state(&self) -> &TcpState<TestEnvState> {
        &self.tcp_state
    }

    /// Update the current TCP state.
    fn with_tcp_state<T>(
        &mut self,
        f: impl FnOnce(&mut TcpState<TestEnvState>) -> T,
        // this would also take a callback queue in shadow
    ) -> T {
        let rv = f(&mut self.tcp_state);

        // if the tcp state wants to send a packet and we should collect packets
        if self.collect_packets {
            while self.tcp_state.wants_to_send() {
                if let Some(_socket) = self.socket_weak.upgrade() {
                    // in shadow we would add a closure to the callback queue, which would call
                    // `host.notify_socket_has_packets()` with `socket`

                    // pop a packet from the socket
                    let rv = self.tcp_state.pop_packet();

                    let (header, packet) = match rv {
                        Ok(x) => x,
                        Err(PopPacketError::NoPacket) => {
                            // the packet said it wants to send, so why didn't it give us a packet?
                            eprintln!("No packet available when popping packet");
                            break;
                        }
                        Err(e) => {
                            // the packet said it wants to send, but returned an error when doing so
                            eprintln!("Unexpected error when popping packet: {e:?}");
                            break;
                        }
                    };

                    // push the packet to the global queue so that the current test can access it
                    self.outgoing_packet_queue
                        .borrow_mut()
                        .push_back((header, packet));
                }
            }
        }

        // we may have mutated the tcp state, so update the socket's file state and notify listeners
        self.mirror_tcp_state(self.tcp_state.poll());

        rv
    }

    /// Set to `true` if we should collect any packets the tcp state wants to send and give them to
    /// the scheduler. Can be useful to disable if you want to test packet coalescing.
    pub fn collect_packets(&mut self, collect_packets: bool) {
        self.collect_packets = collect_packets;

        // run a no-op function on the state, which will force the socket to update its file state
        // to match the tcp state, and will also collect any packets it wants to send (if enabled)
        self.with_tcp_state(|_state| {});
    }

    fn emit_file_state(&mut self, new_state: FileState) {
        let changed = self.file_state ^ new_state;
        if changed.is_empty() {
            return;
        }
        self.event_source.notify_listeners(new_state, changed);
        self.file_state = new_state;
    }

    fn mirror_tcp_state(&mut self, poll_state: PollState) {
        let mut file_state = FileState::empty();
        if poll_state.intersects(PollState::READABLE | PollState::RECV_CLOSED) {
            file_state.insert(FileState::READABLE);
        }
        if poll_state.intersects(PollState::WRITABLE) {
            file_state.insert(FileState::WRITABLE);
        }
        if poll_state.intersects(PollState::ERROR | PollState::CLOSED) {
            file_state.insert(FileState::READABLE | FileState::WRITABLE);
        }

        // if the TCP state machine is in the closed state
        if poll_state.intersects(PollState::CLOSED) {
            // drop the association handle so that we're removed from the network interface
            self.association_handle = None;
        }

        // if the socket file is already closed
        if self.file_state.contains(FileState::CLOSED) {
            // if we're already closed, stay closed
            file_state.insert(FileState::CLOSED);
        }

        self.emit_file_state(file_state);
    }

    pub fn push_in_packet(&mut self, header: &TcpHeader, payload: Payload) {
        self.with_tcp_state(|s| s.push_packet(header, payload))
            .unwrap();
    }

    pub fn close(&mut self) -> Result<(), Errno> {
        // we don't expect close() to ever have an error
        self.with_tcp_state(|state| state.close()).unwrap();

        // add the closed flag
        self.emit_file_state(self.file_state | FileState::CLOSED);

        Ok(())
    }

    /// This shutdown method isn't intended to exactly replicate Linux. Instead it behaves as
    /// documented on [`Shutdown`]. Tests should not be written in a way that assumes this behaves
    /// exactly as Linux does. In a real application (such as Shadow), the Linux compatability would
    /// be implemented here in the socket file's shutdown method.
    pub fn shutdown(&mut self, how: Shutdown) -> Result<(), Errno> {
        // we don't expect shutdown() to ever have an error
        self.with_tcp_state(|state| state.shutdown(how)).unwrap();

        Ok(())
    }

    pub fn bind(
        tcp: &Rc<RefCell<Self>>,
        local_addr: SocketAddrV4,
        host: &mut Host,
    ) -> Result<(), Errno> {
        let tcp_ref = &mut *tcp.borrow_mut();

        // return an error if we're already associated
        if tcp_ref.association_handle.is_some() {
            return Err(Errno::EINVAL);
        }

        if !local_addr.ip().is_loopback()
            && !local_addr.ip().is_unspecified()
            && local_addr.ip() != &host.ip_addr
        {
            return Err(Errno::EINVAL);
        }

        let peer_addr = SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, 0);
        let tcp = Rc::clone(tcp);
        let handle = host.associate_socket(tcp, local_addr, peer_addr)?;
        tcp_ref.association_handle = Some(handle);

        Ok(())
    }

    // listen takes a reference-counted version of `Self` so that it can associate the
    // reference-counted socket with the host's network interface
    pub fn listen(tcp: &Rc<RefCell<Self>>, host: &mut Host, backlog: u32) -> Result<(), Errno> {
        let tcp_ref = &mut *tcp.borrow_mut();

        let local_addr = tcp_ref.association_handle.as_ref().map(|x| x.local_addr());

        let rv = if local_addr.is_some() {
            // if already associated, do nothing
            let associate_fn = || Ok(None);
            tcp_ref.with_tcp_state(|state| state.listen(backlog, associate_fn))
        } else {
            // if not associated, associate and return the handle
            let associate_fn = || {
                let local_addr = SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, 0);
                let peer_addr = SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, 0);
                let tcp = Rc::clone(tcp);
                host.associate_socket(tcp, local_addr, peer_addr).map(Some)
            };
            tcp_ref.with_tcp_state(|state| state.listen(backlog, associate_fn))
        };

        let handle = match rv {
            Ok(x) => x,
            Err(ListenError::InvalidState) => return Err(Errno::EINVAL),
            Err(ListenError::FailedAssociation(e)) => return Err(e),
        };

        // the `associate_fn` may or may not have run, so `rv` may or may not be set
        if let Some(handle) = handle {
            tcp_ref.association_handle = Some(handle);
        }

        Ok(())
    }

    pub fn accept(&mut self, host: &mut Host) -> Result<Rc<RefCell<Self>>, Errno> {
        let rv = self.with_tcp_state(|state| state.accept());

        let accepted_state = match rv {
            Ok(x) => x,
            Err(AcceptError::InvalidState) => return Err(Errno::EINVAL),
            Err(AcceptError::NothingToAccept) => return Err(Errno::EAGAIN),
        };

        let local_addr = accepted_state.local_addr();
        let remote_addr = accepted_state.remote_addr();

        // convert the accepted tcp state to a full tcp socket
        let new_socket = Rc::new_cyclic(|weak: &Weak<RefCell<Self>>| {
            let accepted_state = accepted_state.finalize(|test_env_state| {
                // update the timer state for new and existing pending timers to use the new
                // accepted socket rather than the parent listening socket
                let timer_state = &mut *test_env_state.timer_state.borrow_mut();
                timer_state.socket = weak.clone();
                timer_state.registered_by = TimerRegisteredBy::Parent;
            });

            RefCell::new(Self {
                tcp_state: accepted_state,
                socket_weak: weak.clone(),
                // the file state shouldn't matter here since we run `with_tcp_state` below to
                // update it
                file_state: FileState::empty(),
                event_source: StateEventSource::new(),
                outgoing_packet_queue: self.outgoing_packet_queue.clone(),
                association_handle: None,
                collect_packets: true,
            })
        });

        // run a no-op function on the state, which will force the socket to update its file state
        // to match the tcp state
        new_socket.borrow_mut().with_tcp_state(|_state| ());

        // TODO: if the association fails, we lose the child socket

        // associate the socket
        let tcp = Rc::clone(&new_socket);
        let handle = host.associate_socket(tcp, local_addr, remote_addr)?;
        new_socket.borrow_mut().association_handle = Some(handle);

        Ok(new_socket)
    }

    pub fn connect(
        socket: &Rc<RefCell<Self>>,
        peer_addr: SocketAddrV4,
        host: &mut Host,
    ) -> Result<(), Errno> {
        let socket_ref = &mut *socket.borrow_mut();

        let local_addr = socket_ref
            .association_handle
            .as_ref()
            .map(|x| x.local_addr());

        let rv = if let Some(mut local_addr) = local_addr {
            if local_addr.ip().is_unspecified() {
                let route = host
                    .get_outgoing_route(*local_addr.ip(), *peer_addr.ip())
                    .unwrap();
                local_addr.set_ip(route);
            }

            // if already associated, use the current address
            let associate_fn = || Ok((local_addr, None));
            socket_ref.with_tcp_state(|state| state.connect(peer_addr, associate_fn))
        } else {
            // if not associated, associate and return the handle
            let associate_fn = || {
                let route = host
                    .get_outgoing_route(Ipv4Addr::UNSPECIFIED, *peer_addr.ip())
                    .unwrap();
                let local_addr = SocketAddrV4::new(route, 0);

                let socket = Rc::clone(socket);
                let handle = host.associate_socket(socket, local_addr, peer_addr);
                handle.map(|x| (x.local_addr(), Some(x)))
            };
            socket_ref.with_tcp_state(|state| state.connect(peer_addr, associate_fn))
        };

        let handle = match rv {
            Ok(x) => x,
            Err(ConnectError::InProgress) => return Err(Errno::EALREADY),
            Err(ConnectError::AlreadyConnected) => return Err(Errno::EISCONN),
            Err(ConnectError::IsListening) => return Err(Errno::EISCONN),
            Err(ConnectError::InvalidState) => return Err(Errno::EINVAL),
            Err(ConnectError::FailedAssociation(e)) => return Err(e),
        };

        // the `associate_fn` may or may not have run, so `handle` may or may not be set
        if let Some(handle) = handle {
            socket_ref.association_handle = Some(handle);
        }

        Ok(())
    }

    pub fn sendmsg(
        socket: &Rc<RefCell<Self>>,
        buffer: impl Read,
        len: usize,
    ) -> Result<usize, Errno> {
        let socket_ref = &mut *socket.borrow_mut();

        let rv = socket_ref.with_tcp_state(|state| state.send(buffer, len));

        match rv {
            Ok(n) => Ok(n),
            Err(SendError::Full) => Err(Errno::EWOULDBLOCK),
            Err(SendError::NotConnected) => Err(Errno::EPIPE),
            Err(SendError::StreamClosed) => Err(Errno::EPIPE),
            Err(SendError::InvalidState) => Err(Errno::EINVAL),
            Err(SendError::Io(_e)) => Err(Errno::EINVAL),
        }
    }

    pub fn recvmsg(
        socket: &Rc<RefCell<Self>>,
        buffer: impl Write,
        len: usize,
    ) -> Result<usize, Errno> {
        let socket_ref = &mut *socket.borrow_mut();

        let rv = socket_ref.with_tcp_state(|state| state.recv(buffer, len));

        match rv {
            Ok(n) => Ok(n),
            Err(RecvError::Empty) => Err(Errno::EWOULDBLOCK),
            Err(RecvError::NotConnected) => Err(Errno::ENOTCONN),
            Err(RecvError::StreamClosed) => Ok(0),
            Err(RecvError::InvalidState) => Err(Errno::EINVAL),
            // return EOF if it won't receive any more data
            Err(RecvError::Io(_e)) => Err(Errno::EINVAL),
        }
    }
}

#[derive(Debug)]
struct Host {
    ip_addr: Ipv4Addr,
}

impl Host {
    pub fn new() -> Self {
        Host {
            ip_addr: "1.2.3.4".parse().unwrap(),
        }
    }

    pub fn associate_socket(
        &mut self,
        _socket: Rc<RefCell<TcpSocket>>,
        mut local_addr: SocketAddrV4,
        remote_addr: SocketAddrV4,
    ) -> Result<AssociationHandle, Errno> {
        if local_addr.port() == 0 {
            // TODO
            local_addr.set_port(10);
        }
        Ok(AssociationHandle {
            local_addr,
            remote_addr,
        })
    }

    pub fn get_outgoing_route(&self, src: Ipv4Addr, dst: Ipv4Addr) -> Option<Ipv4Addr> {
        assert!(!dst.is_unspecified());

        if src.is_unspecified() {
            if dst.is_loopback() {
                return Some(Ipv4Addr::LOCALHOST);
            } else {
                return Some(self.ip_addr);
            }
        }

        if src.is_loopback() && dst.is_loopback() {
            return Some(src);
        }

        if !src.is_loopback() && !dst.is_loopback() {
            return Some(src);
        }

        None
    }
}

#[derive(Debug)]
struct AssociationHandle {
    local_addr: SocketAddrV4,
    remote_addr: SocketAddrV4,
}

impl AssociationHandle {
    pub fn local_addr(&self) -> SocketAddrV4 {
        self.local_addr
    }

    pub fn remote_addr(&self) -> SocketAddrV4 {
        self.remote_addr
    }
}

#[test]
fn test_timer() {
    let scheduler = Scheduler::new();
    let mut host = Host::new();

    /// Helper to get the state from a socket.
    fn s(tcp: &Rc<RefCell<TcpSocket>>) -> Ref<TcpState<TestEnvState>> {
        Ref::map(tcp.borrow(), |x| x.tcp_state())
    }

    let tcp = TcpSocket::new(&scheduler, TcpConfig::default());
    assert!(s(&tcp).as_init().is_some());

    TcpSocket::listen(&tcp, &mut host, 10).unwrap();

    // send the SYN
    let header = TcpHeader {
        ip: Ipv4Header {
            src: "5.6.7.8".parse().unwrap(),
            dst: host.ip_addr,
        },
        flags: TcpFlags::SYN,
        src_port: 10,
        dst_port: 20,
        seq: 0,
        ack: 0,
        window_size: 10000,
        selective_acks: None,
        window_scale: None,
        timestamp: None,
        timestamp_echo: None,
    };
    tcp.borrow_mut().push_in_packet(&header, Payload::default());
    assert_eq!(s(&tcp).as_listen().unwrap().children.len(), 1);

    // the new child state set a timer event at 60 seconds to close if still in the "syn-received"
    // state, and we can see that event in the event queue
    assert_eq!(scheduler.event_queue.borrow().len(), 1);

    // at 59 seconds, the child state still exists
    scheduler.advance(Duration::from_secs(59));
    assert_eq!(scheduler.event_queue.borrow().len(), 1);
    assert_eq!(s(&tcp).as_listen().unwrap().children.len(), 1);

    // at 61 seconds, the event has run and the child state has now closed
    scheduler.advance(Duration::from_secs(2));
    assert_eq!(scheduler.event_queue.borrow().len(), 0);
    assert_eq!(s(&tcp).as_listen().unwrap().children.len(), 0);
}

#[test]
fn verify_send_sync() {
    #[derive(Debug)]
    struct SyncSendState {}

    impl Dependencies for SyncSendState {
        type Instant = Instant;
        type Duration = Duration;

        fn register_timer(
            &self,
            _time: Instant,
            _f: impl FnOnce(&mut TcpState<Self>, TimerRegisteredBy) + 'static,
        ) {
            todo!()
        }

        fn current_time(&self) -> Instant {
            todo!()
        }

        fn fork(&self) -> Self {
            todo!()
        }
    }

    // the `TestEnvState` that we use in our tests use `Rc` which is not sync/send, so we use a fake
    // `SyncSendState` which is sync/send to make sure that the remainder of our TCP state is also
    // sync/send
    static_assertions::assert_impl_all!(TcpState<SyncSendState>: Send);
    static_assertions::assert_impl_all!(TcpState<SyncSendState>: Sync);
}

/// Returns an established socket that is bound to the host's IP at port 10 and connected to
/// 5.6.7.8:20.
fn establish_helper(scheduler: &Scheduler, host: &mut Host) -> Rc<RefCell<TcpSocket>> {
    /// Helper to get the state from a socket.
    fn s(tcp: &Rc<RefCell<TcpSocket>>) -> Ref<TcpState<TestEnvState>> {
        Ref::map(tcp.borrow(), |x| x.tcp_state())
    }

    let tcp = TcpSocket::new(scheduler, TcpConfig::default());
    assert!(s(&tcp).as_init().is_some());

    TcpSocket::bind(&tcp, SocketAddrV4::new(host.ip_addr, 10), host).unwrap();

    TcpSocket::connect(&tcp, "5.6.7.8:20".parse().unwrap(), host).unwrap();
    assert!(s(&tcp).as_syn_sent().is_some());

    // read the SYN
    let (response_header, _) = scheduler.pop_packet().unwrap();
    assert_eq!(response_header.flags, TcpFlags::SYN);

    // send the SYN+ACK
    let header = TcpHeader {
        ip: Ipv4Header {
            src: "5.6.7.8".parse().unwrap(),
            dst: host.ip_addr,
        },
        flags: TcpFlags::SYN | TcpFlags::ACK,
        src_port: 20,
        dst_port: 10,
        seq: 0,
        ack: 1,
        window_size: 10000,
        selective_acks: None,
        window_scale: None,
        timestamp: None,
        timestamp_echo: None,
    };
    tcp.borrow_mut().push_in_packet(&header, Payload::default());
    assert!(s(&tcp).as_established().is_some());

    // read the ACK
    let (response_header, _) = scheduler.pop_packet().unwrap();
    assert_eq!(response_header.flags, TcpFlags::ACK);

    tcp
}
