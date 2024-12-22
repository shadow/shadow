//! A TCP implementation with a somewhat-BSD-like socket API. It is written as a
//! ["sans-I/O"][sans-io] library meaning it doesn't do any networking I/O itself, it just accepts
//! bytes and provides bytes. A [dependencies](Dependencies) object must be be provided to support
//! setting timers and getting the current time. The TCP state object should probably be used with a
//! reference-counting wrapper so that a reference to the state object can be stored in the timer
//! callbacks.
//!
//! [sans-io]: https://sans-io.readthedocs.io
//!
//! ```
//! use std::cell::RefCell;
//! use std::rc::{Rc, Weak};
//!
//! #[derive(Debug)]
//! struct TcpDependencies {
//!     // a reference to the tcp state
//!     state: Weak<RefCell<tcp::TcpState<Self>>>,
//! }
//!
//! impl tcp::Dependencies for TcpDependencies {
//!     type Instant = std::time::Instant;
//!     type Duration = std::time::Duration;
//!
//!     fn register_timer(
//!         &self,
//!         time: Self::Instant,
//!         f: impl FnOnce(&mut tcp::TcpState<Self>, tcp::TimerRegisteredBy) + Send + Sync + 'static,
//!     ) {
//!         let tcp_state = self.state.upgrade().unwrap();
//!
//!         // TODO: To register timers, you would likely want to involve an async
//!         // runtime. A simple example would create a new task for each timer. The
//!         // task would sleep for some duration and then run the callback.
//!     }
//!
//!     fn current_time(&self) -> Self::Instant {
//!         std::time::Instant::now()
//!     }
//!
//!     fn fork(&self) -> Self {
//!         // TODO: the implementation here would depend on the implementation
//!         // of `register_timer`
//!         todo!();
//!     }
//! }
//!
//! // create the TCP state object
//! let tcp_state = Rc::new_cyclic(|weak| {
//!     let dependencies = TcpDependencies {
//!         state: weak.clone(),
//!     };
//!     RefCell::new(tcp::TcpState::new(dependencies, tcp::TcpConfig::default()))
//! });
//!
//! let mut tcp_state = tcp_state.borrow_mut();
//!
//! // connect to port 80
//! let dst_addr = "127.0.0.1:80".parse().unwrap();
//! tcp_state.connect(dst_addr, || {
//!     // here we would ask the network interface for an unused port (implicit bind),
//!     // or where we would use the port assigned to a raw IP socket
//!     let bind_addr = "127.0.0.1:2532".parse().unwrap();
//!     Ok::<_, ()>((bind_addr, ()))
//! }).unwrap();
//!
//! // get the SYN packet from the connect
//! let (header, _payload) = tcp_state.pop_packet().unwrap();
//! assert!(header.flags.contains(tcp::TcpFlags::SYN));
//! assert_eq!(header.dst(), dst_addr);
//! ```

// There are three related state types in this crate:
//
// - `TcpState` — This is the public-facing type for the TCP state. Its methods take shared or
//   mutable references. It contains a non-public `TcpStateEnum`.
// - `TcpStateEnum` — An enum of all individual TCP state types (ex: `ListeningState`,
//   `EstablishedState`). It implements the `TcpStateTrait` trait, so its methods usually take owned
//   objects and return owned objects.
// - `TcpStateTrait` — A trait implemented by each individual TCP state type, as well as the
//   `TcpStateEnum` enum that encapsulates all individual states. Its methods usually take owned
//   state objects and return owned `TcpStateEnum` objects.

#![forbid(unsafe_code)]

use std::fmt::Debug;
use std::io::{Read, Write};
use std::net::{Ipv4Addr, SocketAddrV4};

use bytes::{Bytes, BytesMut};

pub mod util;

mod buffer;
mod connection;
mod seq;
mod states;
mod window_scaling;

#[cfg(test)]
mod tests;

use crate::states::{
    CloseWaitState, ClosedState, ClosingState, EstablishedState, FinWaitOneState, FinWaitTwoState,
    InitState, LastAckState, ListenState, RstState, SynReceivedState, SynSentState, TimeWaitState,
};
use crate::util::SmallArrayBackedSlice;

/// A collection of methods that allow the TCP state to interact with the external system.
pub trait Dependencies: Debug + Sized {
    type Instant: crate::util::time::Instant<Duration = Self::Duration>;
    type Duration: crate::util::time::Duration;

    /// Register a timer. The callback will be run on the parent [state](TcpState). The callback can
    /// use the [`TimerRegisteredBy`] argument to know whether the timer was registered by the
    /// parent state or one of its child states.
    ///
    /// If a child state has not yet been accept()ed, it will be owned by a parent state. When a
    /// child state registers a timer, the timer's callback will run on the parent state and the
    /// callback will be given the `TimerRegisteredBy::Child` argument so that the callback can
    /// delegate accordingly.
    fn register_timer(
        &self,
        time: Self::Instant,
        f: impl FnOnce(&mut TcpState<Self>, TimerRegisteredBy) + Send + Sync + 'static,
    );

    /// Get the current time.
    fn current_time(&self) -> Self::Instant;

    /// Create a new `Dependencies` for use by a child state. When a timer is registered by the
    /// child state using this new object, the timer's callback will be run on the parent's state
    /// with the `TimerRegisteredBy::Child` argument so that the parent knows to run the callback on
    /// one of its child states.
    ///
    /// When a child state has been accept()ed, it will no longer be owned by the parent state and
    /// the parent state has no way to access this child state. The child state's `Dependencies`
    /// should be updated during the [`finalize`](AcceptedTcpState::finalize) call (on the
    /// [`AcceptedTcpState`] returned from [`accept`](TcpState::accept)) to run callbacks directly
    /// on this state instead, and the callbacks should be given `TimerRegisteredBy::Parent` (the
    /// child state has effectively become a parent state). This `Dependencies` object should also
    /// make sure that all existing timers from before the state was accept()ed are also updated to
    /// run callbacks directly on the state.
    fn fork(&self) -> Self;
}

/// Specifies whether the callback is meant to run on the parent state or a child state.
///
/// For example if a child state registers a timer, a value of `TimerRegisteredBy::Child` will be
/// given to the callback so that it knows to apply the callback to a child state, not the parent
/// state.
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum TimerRegisteredBy {
    Parent,
    Child,
}

#[enum_dispatch::enum_dispatch]
trait TcpStateTrait<X>: Debug + Sized
where
    X: Dependencies,
    TcpStateEnum<X>: From<Self>,
{
    /// Start closing this socket. It may or may not close immediately depending on what state the
    /// socket is currently in.
    fn close(self) -> (TcpStateEnum<X>, Result<(), CloseError>) {
        (self.into(), Err(CloseError::InvalidState))
    }

    /// Start closing this socket by sending an RST packet. It may or may not close immediately
    /// depending on what state the socket is currently in.
    ///
    /// TODO:
    /// RFC 9293: "The side of a connection issuing a reset should enter the TIME-WAIT state, [...]"
    fn rst_close(self) -> (TcpStateEnum<X>, Result<(), RstCloseError>) {
        (self.into(), Err(RstCloseError::InvalidState))
    }

    fn shutdown(self, _how: Shutdown) -> (TcpStateEnum<X>, Result<(), ShutdownError>) {
        (self.into(), Err(ShutdownError::InvalidState))
    }

    fn listen<T, E>(
        self,
        _backlog: u32,
        _associate_fn: impl FnOnce() -> Result<T, E>,
    ) -> (TcpStateEnum<X>, Result<T, ListenError<E>>) {
        (self.into(), Err(ListenError::InvalidState))
    }

    fn connect<T, E>(
        self,
        _addr: SocketAddrV4,
        _associate_fn: impl FnOnce() -> Result<(SocketAddrV4, T), E>,
    ) -> (TcpStateEnum<X>, Result<T, ConnectError<E>>) {
        (self.into(), Err(ConnectError::InvalidState))
    }

    /// Accept a new child state from the pending connection queue. The TCP state for the child
    /// socket is returned. The [`AcceptedTcpState::finalize`] method must be called immediately on
    /// the returned child state before any code calls into the parent state again, otherwise the
    /// child may miss some timer events.
    fn accept(self) -> (TcpStateEnum<X>, Result<AcceptedTcpState<X>, AcceptError>) {
        (self.into(), Err(AcceptError::InvalidState))
    }

    fn send(self, _reader: impl Read, _len: usize) -> (TcpStateEnum<X>, Result<usize, SendError>) {
        (self.into(), Err(SendError::InvalidState))
    }

    fn recv(self, _writer: impl Write, _len: usize) -> (TcpStateEnum<X>, Result<usize, RecvError>) {
        (self.into(), Err(RecvError::InvalidState))
    }

    /// Returns the number of bytes added to the TCP state's receive buffer. This may be
    /// smaller (ex: duplicate packet) or larger (ex: there is a non-empty reassembly queue)
    /// than the packet payload length.
    fn push_packet(
        self,
        _header: &TcpHeader,
        _payload: Payload,
    ) -> (TcpStateEnum<X>, Result<u32, PushPacketError>) {
        (self.into(), Err(PushPacketError::InvalidState))
    }

    fn pop_packet(
        self,
    ) -> (
        TcpStateEnum<X>,
        Result<(TcpHeader, Payload), PopPacketError>,
    ) {
        (self.into(), Err(PopPacketError::InvalidState))
    }

    fn clear_error(&mut self) -> Option<TcpError>;

    fn poll(&self) -> PollState;

    fn wants_to_send(&self) -> bool;

    fn local_remote_addrs(&self) -> Option<(SocketAddrV4, SocketAddrV4)>;
}

#[derive(Debug)]
pub struct TcpState<X: Dependencies>(Option<TcpStateEnum<X>>);

// this exposes many of the methods from `TcpStateTrait`, but not necessarily all of them (for
// example we don't expose `rst_close()`).
impl<X: Dependencies> TcpState<X> {
    pub fn new(deps: X, config: TcpConfig) -> Self {
        let new_state = InitState::new(deps, config);
        Self(Some(new_state.into()))
    }

    #[inline]
    fn with_state<T>(&mut self, f: impl FnOnce(TcpStateEnum<X>) -> (TcpStateEnum<X>, T)) -> T {
        // get the current state, pass it to `f`, and then put it back (`f` may actually replace the
        // state with an entirely different state object)
        let state = self.0.take().unwrap();
        let (state, rv) = f(state);
        self.0 = Some(state);

        rv
    }

    #[inline]
    pub fn close(&mut self) -> Result<(), CloseError> {
        self.with_state(|state| state.close())
    }

    #[inline]
    pub fn shutdown(&mut self, how: Shutdown) -> Result<(), ShutdownError> {
        self.with_state(|state| state.shutdown(how))
    }

    #[inline]
    pub fn listen<T, E>(
        &mut self,
        backlog: u32,
        associate_fn: impl FnOnce() -> Result<T, E>,
    ) -> Result<T, ListenError<E>> {
        self.with_state(|state| state.listen(backlog, associate_fn))
    }

    #[inline]
    pub fn connect<T, E>(
        &mut self,
        addr: SocketAddrV4,
        associate_fn: impl FnOnce() -> Result<(SocketAddrV4, T), E>,
    ) -> Result<T, ConnectError<E>> {
        self.with_state(|state| state.connect(addr, associate_fn))
    }

    #[inline]
    pub fn accept(&mut self) -> Result<AcceptedTcpState<X>, AcceptError> {
        self.with_state(|state| state.accept())
    }

    #[inline]
    pub fn send(&mut self, reader: impl Read, len: usize) -> Result<usize, SendError> {
        self.with_state(|state| state.send(reader, len))
    }

    #[inline]
    pub fn recv(&mut self, writer: impl Write, len: usize) -> Result<usize, RecvError> {
        self.with_state(|state| state.recv(writer, len))
    }

    #[inline]
    pub fn push_packet(
        &mut self,
        header: &TcpHeader,
        payload: Payload,
    ) -> Result<u32, PushPacketError> {
        self.with_state(|state| state.push_packet(header, payload))
    }

    #[inline]
    pub fn pop_packet(&mut self) -> Result<(TcpHeader, Payload), PopPacketError> {
        self.with_state(|state| state.pop_packet())
    }

    #[inline]
    pub fn clear_error(&mut self) -> Option<TcpError> {
        self.0.as_mut().unwrap().clear_error()
    }

    #[inline]
    pub fn poll(&self) -> PollState {
        self.0.as_ref().unwrap().poll()
    }

    #[inline]
    pub fn wants_to_send(&self) -> bool {
        self.0.as_ref().unwrap().wants_to_send()
    }

    #[inline]
    pub fn local_remote_addrs(&self) -> Option<(SocketAddrV4, SocketAddrV4)> {
        self.0.as_ref().unwrap().local_remote_addrs()
    }
}

/// A macro that forwards an argument-less method to the inner type.
///
/// ```ignore
/// // forward!(as_init, Option<&InitState<X>>);
/// #[inline]
/// pub fn as_init(&self) -> Option<&InitState<X>> {
///     self.0.as_ref().unwrap().as_init()
/// }
/// ```
#[cfg(test)]
macro_rules! forward {
    ($fn_name:ident, $($return_type:tt)*) => {
        #[inline]
        pub fn $fn_name(&self) -> $($return_type)* {
            self.0.as_ref().unwrap().$fn_name()
        }
    };
}

#[cfg(test)]
impl<X: Dependencies> TcpState<X> {
    forward!(as_init, Option<&InitState<X>>);
    forward!(as_listen, Option<&ListenState<X>>);
    forward!(as_syn_sent, Option<&SynSentState<X>>);
    forward!(as_syn_received, Option<&SynReceivedState<X>>);
    forward!(as_established, Option<&EstablishedState<X>>);
    forward!(as_fin_wait_one, Option<&FinWaitOneState<X>>);
    forward!(as_fin_wait_two, Option<&FinWaitTwoState<X>>);
    forward!(as_closing, Option<&ClosingState<X>>);
    forward!(as_time_wait, Option<&TimeWaitState<X>>);
    forward!(as_close_wait, Option<&CloseWaitState<X>>);
    forward!(as_last_ack, Option<&LastAckState<X>>);
    forward!(as_rst, Option<&RstState<X>>);
    forward!(as_closed, Option<&ClosedState<X>>);
}

#[enum_dispatch::enum_dispatch(TcpStateTrait<X>)]
#[derive(Debug)]
enum TcpStateEnum<X: Dependencies> {
    Init(InitState<X>),
    Listen(ListenState<X>),
    SynSent(SynSentState<X>),
    SynReceived(SynReceivedState<X>),
    Established(EstablishedState<X>),
    FinWaitOne(FinWaitOneState<X>),
    FinWaitTwo(FinWaitTwoState<X>),
    Closing(ClosingState<X>),
    TimeWait(TimeWaitState<X>),
    CloseWait(CloseWaitState<X>),
    LastAck(LastAckState<X>),
    Rst(RstState<X>),
    Closed(ClosedState<X>),
}

/// A macro that creates a method which casts to an inner variant.
///
/// ```ignore
/// // as_impl!(as_init, Init, InitState);
/// #[inline]
/// pub fn as_init(&self) -> Option<&InitState<X>> {
///     match self {
///         Self::Init(x) => Some(x),
///         _ => None,
///     }
/// }
/// ```
#[cfg(test)]
macro_rules! as_impl {
    ($fn_name:ident, $variant:ident, $return_type:ident) => {
        #[inline]
        pub fn $fn_name(&self) -> Option<&$return_type<X>> {
            match self {
                Self::$variant(x) => Some(x),
                _ => None,
            }
        }
    };
}

/// Casts to concrete types. This should only be called from unit tests to verify state.
#[cfg(test)]
impl<X: Dependencies> TcpStateEnum<X> {
    as_impl!(as_init, Init, InitState);
    as_impl!(as_listen, Listen, ListenState);
    as_impl!(as_syn_sent, SynSent, SynSentState);
    as_impl!(as_syn_received, SynReceived, SynReceivedState);
    as_impl!(as_established, Established, EstablishedState);
    as_impl!(as_fin_wait_one, FinWaitOne, FinWaitOneState);
    as_impl!(as_fin_wait_two, FinWaitTwo, FinWaitTwoState);
    as_impl!(as_closing, Closing, ClosingState);
    as_impl!(as_time_wait, TimeWait, TimeWaitState);
    as_impl!(as_close_wait, CloseWait, CloseWaitState);
    as_impl!(as_last_ack, LastAck, LastAckState);
    as_impl!(as_rst, Rst, RstState);
    as_impl!(as_closed, Closed, ClosedState);
}

/// An accept()ed TCP state. This is used to ensure that the caller uses
/// [`finalize`](Self::finalize) to update the state's `Dependencies` since the state is no longer
/// owned by the listening socket.
// we use a wrapper struct around an enum so that public code can't access the inner state object
pub struct AcceptedTcpState<X: Dependencies>(AcceptedTcpStateInner<X>);

/// An "established" or "close-wait" TCP state can be accept()ed, so we need to be able to store
/// either state.
enum AcceptedTcpStateInner<X: Dependencies> {
    Established(EstablishedState<X>),
    CloseWait(CloseWaitState<X>),
}

impl<X: Dependencies> AcceptedTcpState<X> {
    /// This allows the caller to update the state's `Dependencies`.
    ///
    /// This must be called immediately after [`TcpState::accept`], otherwise the accept()ed socket
    /// may miss some of its timer events.
    pub fn finalize(mut self, f: impl FnOnce(&mut X)) -> TcpState<X> {
        let deps = match &mut self.0 {
            AcceptedTcpStateInner::Established(state) => &mut state.common.deps,
            AcceptedTcpStateInner::CloseWait(state) => &mut state.common.deps,
        };

        // allow the caller to update `deps` for this state since this state is changing owners
        f(deps);

        TcpState(Some(self.0.into()))
    }

    pub fn local_addr(&self) -> SocketAddrV4 {
        match &self.0 {
            AcceptedTcpStateInner::Established(state) => state.connection.local_addr,
            AcceptedTcpStateInner::CloseWait(state) => state.connection.local_addr,
        }
    }

    pub fn remote_addr(&self) -> SocketAddrV4 {
        match &self.0 {
            AcceptedTcpStateInner::Established(state) => state.connection.remote_addr,
            AcceptedTcpStateInner::CloseWait(state) => state.connection.remote_addr,
        }
    }
}

impl<X: Dependencies> TryFrom<TcpStateEnum<X>> for AcceptedTcpState<X> {
    type Error = TcpStateEnum<X>;

    fn try_from(state: TcpStateEnum<X>) -> Result<Self, Self::Error> {
        match state {
            TcpStateEnum::Established(state) => Ok(Self(AcceptedTcpStateInner::Established(state))),
            TcpStateEnum::CloseWait(state) => Ok(Self(AcceptedTcpStateInner::CloseWait(state))),
            // return the state back to the caller
            state => Err(state),
        }
    }
}

impl<X: Dependencies> From<AcceptedTcpStateInner<X>> for TcpStateEnum<X> {
    fn from(inner: AcceptedTcpStateInner<X>) -> Self {
        match inner {
            AcceptedTcpStateInner::Established(state) => state.into(),
            AcceptedTcpStateInner::CloseWait(state) => state.into(),
        }
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum Shutdown {
    Read,
    Write,
    Both,
}

#[derive(Debug)]
pub enum TcpError {
    ResetSent,
    ResetReceived,
    /// The connection was closed while it was connecting, and no RST was sent or received.
    ClosedWhileConnecting,
    TimedOut,
}

// errors for operations on `TcpStateTrait` objects

#[derive(Debug)]
pub enum CloseError {
    InvalidState,
}

#[derive(Debug)]
enum RstCloseError {
    InvalidState,
}

#[derive(Debug)]
pub enum ListenError<E> {
    InvalidState,
    FailedAssociation(E),
}

#[derive(Debug)]
pub enum ConnectError<E> {
    InvalidState,
    /// A previous connection attempt is in progress.
    InProgress,
    /// A connection has previously been attempted and was either successful or unsuccessful (it may
    /// or may not have reached the "established" state). The connection may be established, timed
    /// out, closing, half-closed, closed, etc. This does not include connection attempts that are
    /// in progress ("syn-sent" or "syn-received" states).
    AlreadyConnected,
    /// Is already listening for new connections.
    IsListening,
    FailedAssociation(E),
}

#[derive(Debug)]
pub enum AcceptError {
    InvalidState,
    NothingToAccept,
}

#[derive(Debug)]
pub enum ShutdownError {
    NotConnected,
    InvalidState,
}

#[derive(Debug)]
pub enum SendError {
    InvalidState,
    Full,
    NotConnected,
    StreamClosed,
    Io(std::io::Error),
}

#[derive(Debug)]
pub enum RecvError {
    InvalidState,
    Empty,
    NotConnected,
    /// The peer has sent a FIN, so no more data will be received.
    StreamClosed,
    Io(std::io::Error),
}

#[derive(Debug)]
pub enum PushPacketError {
    InvalidState,
}

#[derive(Debug)]
pub enum PopPacketError {
    InvalidState,
    NoPacket,
}

// segment/packet headers

bitflags::bitflags! {
    #[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
    pub struct PollState: u32 {
        /// Data can be read.
        const READABLE = 1 << 0;
        /// Data can be written.
        const WRITABLE = 1 << 1;
        /// There is a pending error that can be read using [`TcpState::clear_error`].
        const ERROR = 1 << 2;
        /// The connection has been closed for receiving. This is not mutually exclusive with
        /// `READABLE` (even if it's closed for receiving, there may still be buffered data to
        /// read). Some possible causes are:
        /// - Received a FIN packet.
        /// - Sent or received a RST packet.
        /// - TCP was closed.
        const RECV_CLOSED = 1 << 3;
        /// The connection has been closed for sending. This should be mutually exclusive with
        /// `WRITABLE` (there would be no point in writing data if it's closed for sending). Some
        /// possible causes are:
        /// - Sent a FIN packet.
        /// - Sent or received a RST packet.
        /// - TCP was `shutdown()` for writing.
        /// - TCP was closed.
        const SEND_CLOSED = 1 << 4;
        /// Is listening for new connections.
        const LISTENING = 1 << 5;
        /// A listening socket has a new incoming connection that can be accepted.
        const READY_TO_ACCEPT = 1 << 6;
        /// Connection is in the process of opening. More specifically this means that it is in
        /// either the "syn-sent" or "syn-received" states.
        const CONNECTING = 1 << 7;
        /// A connection has previously been attempted and was either successful or unsuccessful (it
        /// may or may not have reached the "established" state). The connection may be established,
        /// timed out, closing, half-closed, closed, etc. This does not include connection attempts
        /// that are in progress ("syn-sent" or "syn-received" states).
        const CONNECTED = 1 << 8;
        /// TCP is fully closed (in the "closed" state). This may not be set immediately after a
        /// `close()` call, for example if `close()` was called while in the "established" state,
        /// and now is in the "fin-wait-1" state. This does not include the initial state (we don't
        /// consider a new TCP to be "closed").
        const CLOSED = 1 << 9;
    }
}

#[derive(Copy, Clone, Debug)]
#[non_exhaustive]
pub struct TcpConfig {
    pub(crate) window_scaling_enabled: bool,
}

impl TcpConfig {
    pub fn window_scaling(&mut self, enable: bool) {
        self.window_scaling_enabled = enable;
    }
}

impl Default for TcpConfig {
    fn default() -> Self {
        Self {
            window_scaling_enabled: true,
        }
    }
}

bitflags::bitflags! {
    #[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
    pub struct TcpFlags: u8 {
        const FIN = 1 << 0;
        const SYN = 1 << 1;
        const RST = 1 << 2;
        const PSH = 1 << 3;
        const ACK = 1 << 4;
        const URG = 1 << 5;
        const ECE = 1 << 6;
        const CWR = 1 << 7;
    }
}

#[derive(Copy, Clone, Debug)]
pub struct TcpHeader {
    pub ip: Ipv4Header,
    pub flags: TcpFlags,
    pub src_port: u16,
    pub dst_port: u16,
    pub seq: u32,
    pub ack: u32,
    pub window_size: u16,
    pub selective_acks: Option<SmallArrayBackedSlice<4, (u32, u32)>>,
    pub window_scale: Option<u8>,
    pub timestamp: Option<u32>,
    pub timestamp_echo: Option<u32>,
}

impl TcpHeader {
    pub fn src(&self) -> SocketAddrV4 {
        SocketAddrV4::new(self.ip.src, self.src_port)
    }

    pub fn dst(&self) -> SocketAddrV4 {
        SocketAddrV4::new(self.ip.dst, self.dst_port)
    }
}

#[derive(Copy, Clone, Debug)]
pub struct Ipv4Header {
    pub src: Ipv4Addr,
    pub dst: Ipv4Addr,
}

/// A packet payload containing a list of [byte](Bytes) chunks.
///
/// The sum of the lengths of each chunk must be at most [`u32::MAX`], otherwise operations on the
/// payload or other code using the payload may panic.
// TODO: Intuitively this seems like a good place to use a `SmallVec` to optimize the common case
// where there are a small number of chunks per packet. But I'm leaving this until we can test `Vec`
// vs `SmallVec` in a benchmark to see if there's any performance improvement in practice.
#[derive(Clone, Debug, Default)]
pub struct Payload(pub Vec<Bytes>);

// We don't implement `PartialEq` or `Eq` since it's not clear what equality means. Are payloads
// equal if they just contain the same bytes, or are they equal only if the chunks are exactly the
// same? For example is the payload `["hello", "world"]` the same as `["helloworld"]`?
static_assertions::assert_not_impl_any!(Payload: PartialEq, Eq);

impl Payload {
    /// Returns the number of bytes in the payload.
    pub fn len(&self) -> u32 {
        self.0
            .iter()
            // `fold` rather than `sum` so that we always panic on overflow
            .fold(0usize, |acc, x| acc.checked_add(x.len()).unwrap())
            .try_into()
            .unwrap()
    }

    /// Returns true if the payload has no data (no byte chunks or only empty byte chunks).
    pub fn is_empty(&self) -> bool {
        // should be faster than checking `self.len() == 0`
        self.0.iter().all(|x| x.len() == 0)
    }

    /// Concatenate the byte chunks into a single byte chunk. Unless the payload is empty or has a
    /// single chunk, this will allocate a large buffer and copy all of the individual chunks to
    /// this new buffer.
    pub fn concat(&self) -> Bytes {
        let num_bytes = self.len() as usize;
        let num_chunks = self.0.len();

        if num_bytes == 0 {
            return Bytes::new();
        }

        if num_chunks == 1 {
            // there's only one chunk, so just return a reference to the chunk
            return self.0[0].clone();
        }

        let mut bytes = BytesMut::with_capacity(num_bytes);

        for chunk in &self.0 {
            bytes.extend_from_slice(chunk);
        }

        debug_assert_eq!(bytes.len(), bytes.capacity());

        bytes.freeze()
    }
}

impl From<Bytes> for Payload {
    fn from(bytes: Bytes) -> Self {
        Self(vec![bytes])
    }
}

impl From<BytesMut> for Payload {
    fn from(bytes: BytesMut) -> Self {
        bytes.freeze().into()
    }
}
