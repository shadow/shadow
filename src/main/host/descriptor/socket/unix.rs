use std::collections::{LinkedList, VecDeque};
use std::sync::{Arc, Weak};

use atomic_refcell::AtomicRefCell;
use nix::errno::Errno;

use crate::cshadow as c;
use crate::host::descriptor::shared_buf::{
    BufferHandle, BufferState, ReaderHandle, SharedBuf, WriterHandle,
};
use crate::host::descriptor::socket::abstract_unix_ns::AbstractUnixNamespace;
use crate::host::descriptor::socket::{empty_sockaddr, Socket};
use crate::host::descriptor::{
    File, FileMode, FileState, FileStatus, StateEventSource, StateListenerFilter, SyscallResult,
};
use crate::host::memory_manager::MemoryManager;
use crate::host::syscall::Trigger;
use crate::host::syscall_condition::SysCallCondition;
use crate::host::syscall_types::{Blocked, PluginPtr, SysCallReg, SyscallError};
use crate::utility::event_queue::{EventQueue, Handle};
use crate::utility::stream_len::StreamLen;
use crate::utility::HostTreePointer;

const UNIX_SOCKET_DEFAULT_BUFFER_SIZE: u64 = 212_992;

/// A unix socket. The `UnixSocket` is the public-facing API, which forwards API calls to the inner
/// state object.
pub struct UnixSocket {
    /// Data and functionality that is general for all states.
    common: UnixSocketCommon,
    /// State-specific data and functionality.
    protocol_state: ProtocolState,
}

impl UnixSocket {
    pub fn new(
        status: FileStatus,
        socket_type: UnixSocketType,
        namespace: &Arc<AtomicRefCell<AbstractUnixNamespace>>,
    ) -> Arc<AtomicRefCell<Self>> {
        Arc::new_cyclic(|weak| {
            // each socket tracks its own send limit, and we let the receiver have an unlimited recv
            // buffer size
            let recv_buffer = SharedBuf::new(usize::MAX);
            let recv_buffer = Arc::new(AtomicRefCell::new(recv_buffer));

            let mut common = UnixSocketCommon {
                recv_buffer,
                send_limit: UNIX_SOCKET_DEFAULT_BUFFER_SIZE,
                sent_len: 0,
                event_source: StateEventSource::new(),
                state: FileState::ACTIVE,
                status,
                socket_type,
                namespace: Arc::clone(namespace),
                has_open_file: false,
            };

            // may generate new events
            let protocol_state = ProtocolState::new(socket_type, &mut common, weak);

            AtomicRefCell::new(Self {
                common,
                protocol_state,
            })
        })
    }

    pub fn get_status(&self) -> FileStatus {
        self.common.status
    }

    pub fn set_status(&mut self, status: FileStatus) {
        self.common.status = status;
    }

    pub fn mode(&self) -> FileMode {
        FileMode::READ | FileMode::WRITE
    }

    pub fn has_open_file(&self) -> bool {
        self.common.has_open_file
    }

    pub fn supports_sa_restart(&self) -> bool {
        true
    }

    pub fn set_has_open_file(&mut self, val: bool) {
        self.common.has_open_file = val;
    }

    pub fn getsockname(&self) -> Result<Option<nix::sys::socket::UnixAddr>, SyscallError> {
        // return the bound address if set, otherwise return an empty unix sockaddr
        Ok(Some(
            self.protocol_state
                .bound_address()?
                .unwrap_or_else(|| empty_unix_sockaddr()),
        ))
    }

    pub fn getpeername(&self) -> Result<Option<nix::sys::socket::UnixAddr>, SyscallError> {
        // return the peer address if set, otherwise return an empty unix sockaddr
        Ok(Some(
            self.protocol_state
                .peer_address()?
                .unwrap_or_else(|| empty_unix_sockaddr()),
        ))
    }

    pub fn address_family(&self) -> nix::sys::socket::AddressFamily {
        nix::sys::socket::AddressFamily::Unix
    }

    fn recv_buffer(&self) -> &Arc<AtomicRefCell<SharedBuf>> {
        &self.common.recv_buffer
    }

    fn inform_bytes_read(&mut self, num: u64, event_queue: &mut EventQueue) {
        self.protocol_state
            .inform_bytes_read(&mut self.common, num, event_queue);
    }

    pub fn close(&mut self, event_queue: &mut EventQueue) -> Result<(), SyscallError> {
        self.protocol_state.close(&mut self.common, event_queue)
    }

    fn refresh_file_state(&mut self, event_queue: &mut EventQueue) {
        self.protocol_state
            .refresh_file_state(&mut self.common, event_queue)
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    pub fn bind(
        socket: &Arc<AtomicRefCell<Self>>,
        addr: Option<&nix::sys::socket::SockAddr>,
        rng: impl rand::Rng,
    ) -> SyscallResult {
        let socket_ref = &mut *socket.borrow_mut();
        socket_ref
            .protocol_state
            .bind(&mut socket_ref.common, socket, addr, rng)
    }

    pub fn read<W>(
        &mut self,
        mut _bytes: W,
        _offset: libc::off_t,
        _event_queue: &mut EventQueue,
    ) -> SyscallResult
    where
        W: std::io::Write + std::io::Seek,
    {
        // we could call UnixSocket::recvfrom() here, but for now we expect that there are no code
        // paths that would call UnixSocket::read() since the read() syscall handler should have
        // called UnixSocket::recvfrom() instead
        panic!("Called UnixSocket::read() on a unix socket.");
    }

    pub fn write<R>(
        &mut self,
        mut _bytes: R,
        _offset: libc::off_t,
        _event_queue: &mut EventQueue,
    ) -> SyscallResult
    where
        R: std::io::Read + std::io::Seek,
    {
        // we could call UnixSocket::sendto() here, but for now we expect that there are no code
        // paths that would call UnixSocket::write() since the write() syscall handler should have
        // called UnixSocket::sendto() instead
        panic!("Called UnixSocket::write() on a unix socket");
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    pub fn sendto<R>(
        &mut self,
        bytes: R,
        addr: Option<nix::sys::socket::SockAddr>,
        event_queue: &mut EventQueue,
    ) -> SyscallResult
    where
        R: std::io::Read + std::io::Seek,
    {
        self.protocol_state
            .sendto(&mut self.common, bytes, addr, event_queue)
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    pub fn recvfrom<W>(
        &mut self,
        bytes: W,
        event_queue: &mut EventQueue,
    ) -> Result<(SysCallReg, Option<nix::sys::socket::SockAddr>), SyscallError>
    where
        W: std::io::Write + std::io::Seek,
    {
        self.protocol_state
            .recvfrom(&mut self.common, bytes, event_queue)
    }

    pub fn ioctl(
        &mut self,
        request: u64,
        arg_ptr: PluginPtr,
        memory_manager: &mut MemoryManager,
    ) -> SyscallResult {
        self.protocol_state
            .ioctl(&mut self.common, request, arg_ptr, memory_manager)
    }

    pub fn listen(
        &mut self,
        backlog: i32,
        event_queue: &mut EventQueue,
    ) -> Result<(), SyscallError> {
        self.protocol_state
            .listen(&mut self.common, backlog, event_queue)
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    pub fn connect(
        socket: &Arc<AtomicRefCell<Self>>,
        addr: &nix::sys::socket::SockAddr,
        event_queue: &mut EventQueue,
    ) -> Result<(), SyscallError> {
        let socket_ref = &mut *socket.borrow_mut();
        socket_ref
            .protocol_state
            .connect(&mut socket_ref.common, socket, addr, event_queue)
    }

    pub fn accept(
        &mut self,
        event_queue: &mut EventQueue,
    ) -> Result<Arc<AtomicRefCell<UnixSocket>>, SyscallError> {
        self.protocol_state.accept(&mut self.common, event_queue)
    }

    pub fn pair(
        status: FileStatus,
        socket_type: UnixSocketType,
        namespace: &Arc<AtomicRefCell<AbstractUnixNamespace>>,
        event_queue: &mut EventQueue,
    ) -> (Arc<AtomicRefCell<Self>>, Arc<AtomicRefCell<Self>>) {
        let socket_1 = UnixSocket::new(status, socket_type, namespace);
        let socket_2 = UnixSocket::new(status, socket_type, namespace);

        {
            let socket_1_ref = &mut *socket_1.borrow_mut();
            socket_1_ref
                .protocol_state
                .connect_unnamed(
                    &mut socket_1_ref.common,
                    &socket_1,
                    Arc::clone(&socket_2),
                    event_queue,
                )
                .unwrap();
        }

        {
            let socket_2_ref = &mut *socket_2.borrow_mut();
            socket_2_ref
                .protocol_state
                .connect_unnamed(
                    &mut socket_2_ref.common,
                    &socket_2,
                    Arc::clone(&socket_1),
                    event_queue,
                )
                .unwrap();
        }

        (socket_1, socket_2)
    }

    pub fn add_listener(
        &mut self,
        monitoring: FileState,
        filter: StateListenerFilter,
        notify_fn: impl Fn(FileState, FileState, &mut EventQueue) + Send + Sync + 'static,
    ) -> Handle<(FileState, FileState)> {
        self.common
            .event_source
            .add_listener(monitoring, filter, notify_fn)
    }

    pub fn add_legacy_listener(&mut self, ptr: HostTreePointer<c::StatusListener>) {
        self.common.event_source.add_legacy_listener(ptr);
    }

    pub fn remove_legacy_listener(&mut self, ptr: *mut c::StatusListener) {
        self.common.event_source.remove_legacy_listener(ptr);
    }

    pub fn state(&self) -> FileState {
        self.common.state
    }
}

struct ConnOrientedInitial {
    bound_addr: Option<nix::sys::socket::UnixAddr>,
}
struct ConnOrientedListening {
    bound_addr: nix::sys::socket::UnixAddr,
    queue: VecDeque<Arc<AtomicRefCell<UnixSocket>>>,
    queue_limit: u32,
}
struct ConnOrientedConnected {
    bound_addr: Option<nix::sys::socket::UnixAddr>,
    peer_addr: Option<nix::sys::socket::UnixAddr>,
    peer: Arc<AtomicRefCell<UnixSocket>>,
    reader_handle: ReaderHandle,
    writer_handle: WriterHandle,
    // these handles are never accessed, but we store them because of their drop impls
    _recv_buffer_handle: BufferHandle,
    _send_buffer_handle: BufferHandle,
}
struct ConnOrientedClosed {}

struct ConnLessInitial {
    this_socket: Weak<AtomicRefCell<UnixSocket>>,
    bound_addr: Option<nix::sys::socket::UnixAddr>,
    peer_addr: Option<nix::sys::socket::UnixAddr>,
    peer: Option<Arc<AtomicRefCell<UnixSocket>>>,
    recv_data: LinkedList<ByteData>,
    reader_handle: ReaderHandle,
    // this handle is never accessed, but we store it because of its drop impl
    _recv_buffer_handle: BufferHandle,
}
struct ConnLessClosed {}

impl ConnOrientedListening {
    fn queue_is_full(&self) -> bool {
        self.queue.len() >= self.queue_limit.try_into().unwrap()
    }
}

/// The current protocol state of the unix socket. An `Option` is required for each variant so that
/// the inner state object can be removed, transformed into a new state, and then re-added as a
/// different variant.
enum ProtocolState {
    ConnOrientedInitial(Option<ConnOrientedInitial>),
    ConnOrientedListening(Option<ConnOrientedListening>),
    ConnOrientedConnected(Option<ConnOrientedConnected>),
    ConnOrientedClosed(Option<ConnOrientedClosed>),
    ConnLessInitial(Option<ConnLessInitial>),
    ConnLessClosed(Option<ConnLessClosed>),
}

/// Upcast from a type to an enum variant.
macro_rules! state_upcast {
    ($type:ty, $parent:ident::$variant:ident) => {
        impl From<$type> for $parent {
            fn from(x: $type) -> Self {
                Self::$variant(Some(x))
            }
        }
    };
}

// implement upcasting for all state types
state_upcast!(ConnOrientedInitial, ProtocolState::ConnOrientedInitial);
state_upcast!(ConnOrientedListening, ProtocolState::ConnOrientedListening);
state_upcast!(ConnOrientedConnected, ProtocolState::ConnOrientedConnected);
state_upcast!(ConnOrientedClosed, ProtocolState::ConnOrientedClosed);
state_upcast!(ConnLessInitial, ProtocolState::ConnLessInitial);
state_upcast!(ConnLessClosed, ProtocolState::ConnLessClosed);

impl ProtocolState {
    fn new(
        socket_type: UnixSocketType,
        common: &mut UnixSocketCommon,
        socket: &Weak<AtomicRefCell<UnixSocket>>,
    ) -> Self {
        match socket_type {
            UnixSocketType::Stream | UnixSocketType::SeqPacket => {
                Self::ConnOrientedInitial(Some(ConnOrientedInitial { bound_addr: None }))
            }
            UnixSocketType::Dgram => {
                // this is a new socket and there are no listeners, so safe to use a temporary event queue
                let mut event_queue = EventQueue::new();

                // dgram unix sockets are immediately able to receive data, so initialize the
                // receive buffer

                // increment the buffer's reader count
                let reader_handle = common.recv_buffer.borrow_mut().add_reader(&mut event_queue);

                let weak = Weak::clone(socket);
                let recv_buffer_handle = common.recv_buffer.borrow_mut().add_listener(
                    BufferState::READABLE,
                    move |_, event_queue| {
                        if let Some(socket) = weak.upgrade() {
                            socket.borrow_mut().refresh_file_state(event_queue);
                        }
                    },
                );

                // make sure no events were generated since if there were events to run, they would
                // probably not run correctly if the socket's Arc is not fully created yet (as in
                // the case of `Arc::new_cyclic`)
                assert!(event_queue.is_empty());

                Self::ConnLessInitial(Some(ConnLessInitial {
                    this_socket: Weak::clone(socket),
                    bound_addr: None,
                    peer_addr: None,
                    peer: None,
                    recv_data: LinkedList::new(),
                    reader_handle,
                    _recv_buffer_handle: recv_buffer_handle,
                }))
            }
        }
    }

    fn peer_address(&self) -> Result<Option<nix::sys::socket::UnixAddr>, SyscallError> {
        match self {
            Self::ConnOrientedInitial(x) => x.as_ref().unwrap().peer_address(),
            Self::ConnOrientedListening(x) => x.as_ref().unwrap().peer_address(),
            Self::ConnOrientedConnected(x) => x.as_ref().unwrap().peer_address(),
            Self::ConnOrientedClosed(x) => x.as_ref().unwrap().peer_address(),
            Self::ConnLessInitial(x) => x.as_ref().unwrap().peer_address(),
            Self::ConnLessClosed(x) => x.as_ref().unwrap().peer_address(),
        }
    }

    fn bound_address(&self) -> Result<Option<nix::sys::socket::UnixAddr>, SyscallError> {
        match self {
            Self::ConnOrientedInitial(x) => x.as_ref().unwrap().bound_address(),
            Self::ConnOrientedListening(x) => x.as_ref().unwrap().bound_address(),
            Self::ConnOrientedConnected(x) => x.as_ref().unwrap().bound_address(),
            Self::ConnOrientedClosed(x) => x.as_ref().unwrap().bound_address(),
            Self::ConnLessInitial(x) => x.as_ref().unwrap().bound_address(),
            Self::ConnLessClosed(x) => x.as_ref().unwrap().bound_address(),
        }
    }

    fn refresh_file_state(&self, common: &mut UnixSocketCommon, event_queue: &mut EventQueue) {
        match self {
            Self::ConnOrientedInitial(x) => {
                x.as_ref().unwrap().refresh_file_state(common, event_queue)
            }
            Self::ConnOrientedListening(x) => {
                x.as_ref().unwrap().refresh_file_state(common, event_queue)
            }
            Self::ConnOrientedConnected(x) => {
                x.as_ref().unwrap().refresh_file_state(common, event_queue)
            }
            Self::ConnOrientedClosed(x) => {
                x.as_ref().unwrap().refresh_file_state(common, event_queue)
            }
            Self::ConnLessInitial(x) => x.as_ref().unwrap().refresh_file_state(common, event_queue),
            Self::ConnLessClosed(x) => x.as_ref().unwrap().refresh_file_state(common, event_queue),
        }
    }

    fn close(
        &mut self,
        common: &mut UnixSocketCommon,
        event_queue: &mut EventQueue,
    ) -> Result<(), SyscallError> {
        let (new_state, rv) = match self {
            Self::ConnOrientedInitial(x) => x.take().unwrap().close(common, event_queue),
            Self::ConnOrientedListening(x) => x.take().unwrap().close(common, event_queue),
            Self::ConnOrientedConnected(x) => x.take().unwrap().close(common, event_queue),
            Self::ConnOrientedClosed(x) => x.take().unwrap().close(common, event_queue),
            Self::ConnLessInitial(x) => x.take().unwrap().close(common, event_queue),
            Self::ConnLessClosed(x) => x.take().unwrap().close(common, event_queue),
        };

        *self = new_state;
        rv
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn bind(
        &mut self,
        common: &mut UnixSocketCommon,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        addr: Option<&nix::sys::socket::SockAddr>,
        rng: impl rand::Rng,
    ) -> SyscallResult {
        match self {
            Self::ConnOrientedInitial(x) => x.as_mut().unwrap().bind(common, socket, addr, rng),
            Self::ConnOrientedListening(x) => x.as_mut().unwrap().bind(common, socket, addr, rng),
            Self::ConnOrientedConnected(x) => x.as_mut().unwrap().bind(common, socket, addr, rng),
            Self::ConnOrientedClosed(x) => x.as_mut().unwrap().bind(common, socket, addr, rng),
            Self::ConnLessInitial(x) => x.as_mut().unwrap().bind(common, socket, addr, rng),
            Self::ConnLessClosed(x) => x.as_mut().unwrap().bind(common, socket, addr, rng),
        }
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn sendto<R>(
        &mut self,
        common: &mut UnixSocketCommon,
        bytes: R,
        addr: Option<nix::sys::socket::SockAddr>,
        event_queue: &mut EventQueue,
    ) -> SyscallResult
    where
        R: std::io::Read + std::io::Seek,
    {
        match self {
            Self::ConnOrientedInitial(x) => {
                x.as_mut().unwrap().sendto(common, bytes, addr, event_queue)
            }
            Self::ConnOrientedListening(x) => {
                x.as_mut().unwrap().sendto(common, bytes, addr, event_queue)
            }
            Self::ConnOrientedConnected(x) => {
                x.as_mut().unwrap().sendto(common, bytes, addr, event_queue)
            }
            Self::ConnOrientedClosed(x) => {
                x.as_mut().unwrap().sendto(common, bytes, addr, event_queue)
            }
            Self::ConnLessInitial(x) => {
                x.as_mut().unwrap().sendto(common, bytes, addr, event_queue)
            }
            Self::ConnLessClosed(x) => x.as_mut().unwrap().sendto(common, bytes, addr, event_queue),
        }
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn recvfrom<W>(
        &mut self,
        common: &mut UnixSocketCommon,
        bytes: W,
        event_queue: &mut EventQueue,
    ) -> Result<(SysCallReg, Option<nix::sys::socket::SockAddr>), SyscallError>
    where
        W: std::io::Write + std::io::Seek,
    {
        match self {
            Self::ConnOrientedInitial(x) => {
                x.as_mut().unwrap().recvfrom(common, bytes, event_queue)
            }
            Self::ConnOrientedListening(x) => {
                x.as_mut().unwrap().recvfrom(common, bytes, event_queue)
            }
            Self::ConnOrientedConnected(x) => {
                x.as_mut().unwrap().recvfrom(common, bytes, event_queue)
            }
            Self::ConnOrientedClosed(x) => x.as_mut().unwrap().recvfrom(common, bytes, event_queue),
            Self::ConnLessInitial(x) => x.as_mut().unwrap().recvfrom(common, bytes, event_queue),
            Self::ConnLessClosed(x) => x.as_mut().unwrap().recvfrom(common, bytes, event_queue),
        }
    }

    fn inform_bytes_read(
        &mut self,
        common: &mut UnixSocketCommon,
        num: u64,
        event_queue: &mut EventQueue,
    ) {
        match self {
            Self::ConnOrientedInitial(x) => {
                x.as_mut()
                    .unwrap()
                    .inform_bytes_read(common, num, event_queue)
            }
            Self::ConnOrientedListening(x) => {
                x.as_mut()
                    .unwrap()
                    .inform_bytes_read(common, num, event_queue)
            }
            Self::ConnOrientedConnected(x) => {
                x.as_mut()
                    .unwrap()
                    .inform_bytes_read(common, num, event_queue)
            }
            Self::ConnOrientedClosed(x) => {
                x.as_mut()
                    .unwrap()
                    .inform_bytes_read(common, num, event_queue)
            }
            Self::ConnLessInitial(x) => {
                x.as_mut()
                    .unwrap()
                    .inform_bytes_read(common, num, event_queue)
            }
            Self::ConnLessClosed(x) => {
                x.as_mut()
                    .unwrap()
                    .inform_bytes_read(common, num, event_queue)
            }
        }
    }

    fn ioctl(
        &mut self,
        common: &mut UnixSocketCommon,
        request: u64,
        arg_ptr: PluginPtr,
        memory_manager: &mut MemoryManager,
    ) -> SyscallResult {
        match self {
            Self::ConnOrientedInitial(x) => {
                x.as_mut()
                    .unwrap()
                    .ioctl(common, request, arg_ptr, memory_manager)
            }
            Self::ConnOrientedListening(x) => {
                x.as_mut()
                    .unwrap()
                    .ioctl(common, request, arg_ptr, memory_manager)
            }
            Self::ConnOrientedConnected(x) => {
                x.as_mut()
                    .unwrap()
                    .ioctl(common, request, arg_ptr, memory_manager)
            }
            Self::ConnOrientedClosed(x) => {
                x.as_mut()
                    .unwrap()
                    .ioctl(common, request, arg_ptr, memory_manager)
            }
            Self::ConnLessInitial(x) => {
                x.as_mut()
                    .unwrap()
                    .ioctl(common, request, arg_ptr, memory_manager)
            }
            Self::ConnLessClosed(x) => {
                x.as_mut()
                    .unwrap()
                    .ioctl(common, request, arg_ptr, memory_manager)
            }
        }
    }

    fn listen(
        &mut self,
        common: &mut UnixSocketCommon,
        backlog: i32,
        event_queue: &mut EventQueue,
    ) -> Result<(), SyscallError> {
        let (new_state, rv) = match self {
            Self::ConnOrientedInitial(x) => x.take().unwrap().listen(common, backlog, event_queue),
            Self::ConnOrientedListening(x) => {
                x.take().unwrap().listen(common, backlog, event_queue)
            }
            Self::ConnOrientedConnected(x) => {
                x.take().unwrap().listen(common, backlog, event_queue)
            }
            Self::ConnOrientedClosed(x) => x.take().unwrap().listen(common, backlog, event_queue),
            Self::ConnLessInitial(x) => x.take().unwrap().listen(common, backlog, event_queue),
            Self::ConnLessClosed(x) => x.take().unwrap().listen(common, backlog, event_queue),
        };

        *self = new_state;
        rv
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn connect(
        &mut self,
        common: &mut UnixSocketCommon,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        addr: &nix::sys::socket::SockAddr,
        event_queue: &mut EventQueue,
    ) -> Result<(), SyscallError> {
        let (new_state, rv) = match self {
            Self::ConnOrientedInitial(x) => {
                x.take().unwrap().connect(common, socket, addr, event_queue)
            }
            Self::ConnOrientedListening(x) => {
                x.take().unwrap().connect(common, socket, addr, event_queue)
            }
            Self::ConnOrientedConnected(x) => {
                x.take().unwrap().connect(common, socket, addr, event_queue)
            }
            Self::ConnOrientedClosed(x) => {
                x.take().unwrap().connect(common, socket, addr, event_queue)
            }
            Self::ConnLessInitial(x) => {
                x.take().unwrap().connect(common, socket, addr, event_queue)
            }
            Self::ConnLessClosed(x) => x.take().unwrap().connect(common, socket, addr, event_queue),
        };

        *self = new_state;
        rv
    }

    fn connect_unnamed(
        &mut self,
        common: &mut UnixSocketCommon,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        peer: Arc<AtomicRefCell<UnixSocket>>,
        event_queue: &mut EventQueue,
    ) -> Result<(), SyscallError> {
        let (new_state, rv) = match self {
            Self::ConnOrientedInitial(x) => {
                x.take()
                    .unwrap()
                    .connect_unnamed(common, socket, peer, event_queue)
            }
            Self::ConnOrientedListening(x) => {
                x.take()
                    .unwrap()
                    .connect_unnamed(common, socket, peer, event_queue)
            }
            Self::ConnOrientedConnected(x) => {
                x.take()
                    .unwrap()
                    .connect_unnamed(common, socket, peer, event_queue)
            }
            Self::ConnOrientedClosed(x) => {
                x.take()
                    .unwrap()
                    .connect_unnamed(common, socket, peer, event_queue)
            }
            Self::ConnLessInitial(x) => {
                x.take()
                    .unwrap()
                    .connect_unnamed(common, socket, peer, event_queue)
            }
            Self::ConnLessClosed(x) => {
                x.take()
                    .unwrap()
                    .connect_unnamed(common, socket, peer, event_queue)
            }
        };

        *self = new_state;
        rv
    }

    fn accept(
        &mut self,
        common: &mut UnixSocketCommon,
        event_queue: &mut EventQueue,
    ) -> Result<Arc<AtomicRefCell<UnixSocket>>, SyscallError> {
        match self {
            Self::ConnOrientedInitial(x) => x.as_mut().unwrap().accept(common, event_queue),
            Self::ConnOrientedListening(x) => x.as_mut().unwrap().accept(common, event_queue),
            Self::ConnOrientedConnected(x) => x.as_mut().unwrap().accept(common, event_queue),
            Self::ConnOrientedClosed(x) => x.as_mut().unwrap().accept(common, event_queue),
            Self::ConnLessInitial(x) => x.as_mut().unwrap().accept(common, event_queue),
            Self::ConnLessClosed(x) => x.as_mut().unwrap().accept(common, event_queue),
        }
    }

    /// Called on the listening socket when there is an incoming connection.
    fn queue_incoming_conn(
        &mut self,
        common: &mut UnixSocketCommon,
        from_address: Option<nix::sys::socket::UnixAddr>,
        peer: &Arc<AtomicRefCell<UnixSocket>>,
        child_send_buffer: &Arc<AtomicRefCell<SharedBuf>>,
        event_queue: &mut EventQueue,
    ) -> Result<&Arc<AtomicRefCell<UnixSocket>>, IncomingConnError> {
        match self {
            Self::ConnOrientedInitial(x) => x.as_mut().unwrap().queue_incoming_conn(
                common,
                from_address,
                peer,
                child_send_buffer,
                event_queue,
            ),
            Self::ConnOrientedListening(x) => x.as_mut().unwrap().queue_incoming_conn(
                common,
                from_address,
                peer,
                child_send_buffer,
                event_queue,
            ),
            Self::ConnOrientedConnected(x) => x.as_mut().unwrap().queue_incoming_conn(
                common,
                from_address,
                peer,
                child_send_buffer,
                event_queue,
            ),
            Self::ConnOrientedClosed(x) => x.as_mut().unwrap().queue_incoming_conn(
                common,
                from_address,
                peer,
                child_send_buffer,
                event_queue,
            ),
            Self::ConnLessInitial(x) => x.as_mut().unwrap().queue_incoming_conn(
                common,
                from_address,
                peer,
                child_send_buffer,
                event_queue,
            ),
            Self::ConnLessClosed(x) => x.as_mut().unwrap().queue_incoming_conn(
                common,
                from_address,
                peer,
                child_send_buffer,
                event_queue,
            ),
        }
    }
}

/// Methods that a protocol state may wish to handle. Default implementations which return an error
/// status are provided for many methods. Each type that implements this trait can override any of
/// these default implementations.
trait Protocol
where
    Self: Sized + Into<ProtocolState>,
{
    fn peer_address(&self) -> Result<Option<nix::sys::socket::UnixAddr>, SyscallError>;
    fn bound_address(&self) -> Result<Option<nix::sys::socket::UnixAddr>, SyscallError>;
    fn refresh_file_state(&self, common: &mut UnixSocketCommon, event_queue: &mut EventQueue);

    fn close(
        self,
        _common: &mut UnixSocketCommon,
        _event_queue: &mut EventQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        log::warn!("close() while in state {}", std::any::type_name::<Self>());
        (self.into(), Err(Errno::EOPNOTSUPP.into()))
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn bind(
        &mut self,
        _common: &mut UnixSocketCommon,
        _socket: &Arc<AtomicRefCell<UnixSocket>>,
        _addr: Option<&nix::sys::socket::SockAddr>,
        _rng: impl rand::Rng,
    ) -> SyscallResult {
        log::warn!("bind() while in state {}", std::any::type_name::<Self>());
        Err(Errno::EOPNOTSUPP.into())
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn sendto<R>(
        &mut self,
        _common: &mut UnixSocketCommon,
        _bytes: R,
        _addr: Option<nix::sys::socket::SockAddr>,
        _event_queue: &mut EventQueue,
    ) -> SyscallResult
    where
        R: std::io::Read + std::io::Seek,
    {
        log::warn!("sendto() while in state {}", std::any::type_name::<Self>());
        Err(Errno::EOPNOTSUPP.into())
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn recvfrom<W>(
        &mut self,
        _common: &mut UnixSocketCommon,
        _bytes: W,
        _event_queue: &mut EventQueue,
    ) -> Result<(SysCallReg, Option<nix::sys::socket::SockAddr>), SyscallError>
    where
        W: std::io::Write + std::io::Seek,
    {
        log::warn!(
            "recvfrom() while in state {}",
            std::any::type_name::<Self>()
        );
        Err(Errno::EOPNOTSUPP.into())
    }

    fn inform_bytes_read(
        &mut self,
        _common: &mut UnixSocketCommon,
        _num: u64,
        _event_queue: &mut EventQueue,
    ) {
        panic!(
            "inform_bytes_read() while in state {}",
            std::any::type_name::<Self>()
        );
    }

    fn ioctl(
        &mut self,
        _common: &mut UnixSocketCommon,
        _request: u64,
        _arg_ptr: PluginPtr,
        _memory_manager: &mut MemoryManager,
    ) -> SyscallResult {
        log::warn!("ioctl() while in state {}", std::any::type_name::<Self>());
        Err(Errno::EOPNOTSUPP.into())
    }

    fn listen(
        self,
        _common: &mut UnixSocketCommon,
        _backlog: i32,
        _event_queue: &mut EventQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        log::warn!("listen() while in state {}", std::any::type_name::<Self>());
        (self.into(), Err(Errno::EOPNOTSUPP.into()))
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn connect(
        self,
        _common: &mut UnixSocketCommon,
        _socket: &Arc<AtomicRefCell<UnixSocket>>,
        _addr: &nix::sys::socket::SockAddr,
        _event_queue: &mut EventQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        log::warn!("connect() while in state {}", std::any::type_name::<Self>());
        (self.into(), Err(Errno::EOPNOTSUPP.into()))
    }

    fn connect_unnamed(
        self,
        _common: &mut UnixSocketCommon,
        _socket: &Arc<AtomicRefCell<UnixSocket>>,
        _peer: Arc<AtomicRefCell<UnixSocket>>,
        _event_queue: &mut EventQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        log::warn!(
            "connect_unnamed() while in state {}",
            std::any::type_name::<Self>()
        );
        (self.into(), Err(Errno::EOPNOTSUPP.into()))
    }

    fn accept(
        &mut self,
        _common: &mut UnixSocketCommon,
        _event_queue: &mut EventQueue,
    ) -> Result<Arc<AtomicRefCell<UnixSocket>>, SyscallError> {
        log::warn!("accept() while in state {}", std::any::type_name::<Self>());
        Err(Errno::EOPNOTSUPP.into())
    }

    fn queue_incoming_conn(
        &mut self,
        _common: &mut UnixSocketCommon,
        _from_address: Option<nix::sys::socket::UnixAddr>,
        _peer: &Arc<AtomicRefCell<UnixSocket>>,
        _child_send_buffer: &Arc<AtomicRefCell<SharedBuf>>,
        _event_queue: &mut EventQueue,
    ) -> Result<&Arc<AtomicRefCell<UnixSocket>>, IncomingConnError> {
        log::warn!(
            "queue_incoming_conn() while in state {}",
            std::any::type_name::<Self>()
        );
        Err(IncomingConnError::NotSupported)
    }
}

impl Protocol for ConnOrientedInitial {
    fn peer_address(&self) -> Result<Option<nix::sys::socket::UnixAddr>, SyscallError> {
        Err(Errno::ENOTCONN.into())
    }

    fn bound_address(&self) -> Result<Option<nix::sys::socket::UnixAddr>, SyscallError> {
        Ok(self.bound_addr)
    }

    fn refresh_file_state(&self, common: &mut UnixSocketCommon, event_queue: &mut EventQueue) {
        common.copy_state(
            /* mask= */ FileState::all(),
            FileState::ACTIVE,
            event_queue,
        );
    }

    fn close(
        self,
        common: &mut UnixSocketCommon,
        event_queue: &mut EventQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        let new_state = ConnOrientedClosed {};
        new_state.refresh_file_state(common, event_queue);
        (new_state.into(), common.close(event_queue))
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn bind(
        &mut self,
        common: &mut UnixSocketCommon,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        addr: Option<&nix::sys::socket::SockAddr>,
        rng: impl rand::Rng,
    ) -> SyscallResult {
        // if already bound
        if self.bound_addr.is_some() {
            return Err(Errno::EINVAL.into());
        }

        self.bound_addr = Some(common.bind(socket, addr, rng)?);
        Ok(0.into())
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn sendto<R>(
        &mut self,
        common: &mut UnixSocketCommon,
        _bytes: R,
        addr: Option<nix::sys::socket::SockAddr>,
        _event_queue: &mut EventQueue,
    ) -> SyscallResult
    where
        R: std::io::Read + std::io::Seek,
    {
        match (common.socket_type, addr) {
            (UnixSocketType::Stream, Some(_)) => Err(Errno::EOPNOTSUPP.into()),
            (UnixSocketType::Stream, None) => Err(Errno::ENOTCONN.into()),
            (UnixSocketType::SeqPacket, _) => Err(Errno::ENOTCONN.into()),
            (UnixSocketType::Dgram, _) => panic!(
                "A dgram unix socket is in the connection-oriented {:?} state",
                std::any::type_name::<Self>()
            ),
        }
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn recvfrom<W>(
        &mut self,
        common: &mut UnixSocketCommon,
        _bytes: W,
        _event_queue: &mut EventQueue,
    ) -> Result<(SysCallReg, Option<nix::sys::socket::SockAddr>), SyscallError>
    where
        W: std::io::Write + std::io::Seek,
    {
        match common.socket_type {
            UnixSocketType::Stream => Err(Errno::EINVAL.into()),
            UnixSocketType::SeqPacket => Err(Errno::ENOTCONN.into()),
            UnixSocketType::Dgram => panic!(
                "A dgram unix socket is in the connection-oriented {:?} state",
                std::any::type_name::<Self>()
            ),
        }
    }

    fn ioctl(
        &mut self,
        common: &mut UnixSocketCommon,
        request: u64,
        arg_ptr: PluginPtr,
        memory_manager: &mut MemoryManager,
    ) -> SyscallResult {
        common.ioctl(request, arg_ptr, memory_manager)
    }

    fn listen(
        self,
        common: &mut UnixSocketCommon,
        backlog: i32,
        event_queue: &mut EventQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        // it must have already been bound
        let bound_addr = match self.bound_addr {
            Some(x) => x,
            None => return (self.into(), Err(Errno::EINVAL.into())),
        };

        let new_state = ConnOrientedListening {
            bound_addr,
            queue: VecDeque::new(),
            queue_limit: backlog_to_queue_size(backlog),
        };

        // refresh the socket's file state
        new_state.refresh_file_state(common, event_queue);

        (new_state.into(), Ok(()))
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn connect(
        self,
        common: &mut UnixSocketCommon,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        addr: &nix::sys::socket::SockAddr,
        event_queue: &mut EventQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        let addr = match addr {
            nix::sys::socket::SockAddr::Unix(x) => x,
            _ => return (self.into(), Err(Errno::EINVAL.into())),
        };

        // look up the server socket
        let server = match lookup_address(&common.namespace.borrow(), common.socket_type, addr) {
            Some(x) => x,
            None => return (self.into(), Err(Errno::ECONNREFUSED.into())),
        };

        // need to tell the server to queue a new child socket, and then link the current socket
        // with the new child socket

        // inform the server socket of the incoming connection and get the server socket's new child
        // socket
        let server_mut = &mut *server.borrow_mut();
        let peer = match server_mut.protocol_state.queue_incoming_conn(
            &mut server_mut.common,
            self.bound_addr,
            socket,
            &common.recv_buffer,
            event_queue,
        ) {
            Ok(peer) => peer,
            Err(IncomingConnError::NotSupported) => {
                return (self.into(), Err(Errno::ECONNREFUSED.into()))
            }
            Err(IncomingConnError::QueueFull) => {
                if common.status.contains(FileStatus::NONBLOCK) {
                    return (self.into(), Err(Errno::EWOULDBLOCK.into()));
                }

                // block until the server has room for new connections, or is closed
                let trigger = Trigger::from_file(
                    File::Socket(Socket::Unix(Arc::clone(&server))),
                    FileState::SOCKET_ALLOWING_CONNECT | FileState::CLOSED,
                );
                let blocked = Blocked {
                    condition: SysCallCondition::new(trigger),
                    restartable: server_mut.supports_sa_restart(),
                };

                return (self.into(), Err(SyscallError::Blocked(blocked)));
            }
        };

        // our send buffer will be the peer's receive buffer
        let send_buffer = Arc::clone(peer.borrow().recv_buffer());

        let weak = Arc::downgrade(socket);
        let send_buffer_handle = send_buffer.borrow_mut().add_listener(
            BufferState::WRITABLE | BufferState::NO_READERS,
            move |_, event_queue| {
                if let Some(socket) = weak.upgrade() {
                    socket.borrow_mut().refresh_file_state(event_queue);
                }
            },
        );

        // increment the buffer's writer count
        let writer_handle = send_buffer.borrow_mut().add_writer(event_queue);

        let weak = Arc::downgrade(socket);
        let recv_buffer_handle = common.recv_buffer.borrow_mut().add_listener(
            BufferState::READABLE | BufferState::NO_WRITERS,
            move |_, event_queue| {
                if let Some(socket) = weak.upgrade() {
                    socket.borrow_mut().refresh_file_state(event_queue);
                }
            },
        );

        // increment the buffer's reader count
        let reader_handle = common.recv_buffer.borrow_mut().add_reader(event_queue);

        let new_state = ConnOrientedConnected {
            bound_addr: self.bound_addr,
            peer_addr: Some(addr.clone()),
            peer: Arc::clone(peer),
            reader_handle,
            writer_handle,
            _recv_buffer_handle: recv_buffer_handle,
            _send_buffer_handle: send_buffer_handle,
        };

        new_state.refresh_file_state(common, event_queue);

        (new_state.into(), Ok(()))
    }

    fn connect_unnamed(
        self,
        common: &mut UnixSocketCommon,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        peer: Arc<AtomicRefCell<UnixSocket>>,
        event_queue: &mut EventQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        assert!(self.bound_addr.is_none());

        let send_buffer_handle;
        let writer_handle;

        {
            let peer_ref = peer.borrow();
            let send_buffer = peer_ref.recv_buffer();

            let weak = Arc::downgrade(socket);
            send_buffer_handle = send_buffer.borrow_mut().add_listener(
                BufferState::WRITABLE | BufferState::NO_READERS,
                move |_, event_queue| {
                    if let Some(socket) = weak.upgrade() {
                        socket.borrow_mut().refresh_file_state(event_queue);
                    }
                },
            );

            // increment the buffer's writer count
            writer_handle = send_buffer.borrow_mut().add_writer(event_queue);
        }

        let weak = Arc::downgrade(socket);
        let recv_buffer_handle = common.recv_buffer.borrow_mut().add_listener(
            BufferState::READABLE | BufferState::NO_WRITERS,
            move |_, event_queue| {
                if let Some(socket) = weak.upgrade() {
                    socket.borrow_mut().refresh_file_state(event_queue);
                }
            },
        );

        // increment the buffer's reader count
        let reader_handle = common.recv_buffer.borrow_mut().add_reader(event_queue);

        let new_state = ConnOrientedConnected {
            bound_addr: None,
            peer_addr: None,
            peer,
            reader_handle,
            writer_handle,
            _recv_buffer_handle: recv_buffer_handle,
            _send_buffer_handle: send_buffer_handle,
        };

        new_state.refresh_file_state(common, event_queue);

        (new_state.into(), Ok(()))
    }

    fn accept(
        &mut self,
        _common: &mut UnixSocketCommon,
        _event_queue: &mut EventQueue,
    ) -> Result<Arc<AtomicRefCell<UnixSocket>>, SyscallError> {
        log::warn!("accept() while in state {}", std::any::type_name::<Self>());
        Err(Errno::EINVAL.into())
    }
}

impl Protocol for ConnOrientedListening {
    fn peer_address(&self) -> Result<Option<nix::sys::socket::UnixAddr>, SyscallError> {
        Err(Errno::ENOTCONN.into())
    }

    fn bound_address(&self) -> Result<Option<nix::sys::socket::UnixAddr>, SyscallError> {
        Ok(Some(self.bound_addr))
    }

    fn refresh_file_state(&self, common: &mut UnixSocketCommon, event_queue: &mut EventQueue) {
        let mut new_state = FileState::ACTIVE;

        // socket is readable if the queue is not empty
        new_state.set(FileState::READABLE, self.queue.len() > 0);

        // socket allows connections if the queue is not full
        new_state.set(FileState::SOCKET_ALLOWING_CONNECT, !self.queue_is_full());

        // Note: This can cause a thundering-herd condition where multiple blocked connect() calls
        // are all notified at the same time, even if there isn't enough space to allow all of them.
        // In practice this should be uncommon so we don't worry about it, and avoids requiring that
        // the server keep a list of all connecting clients.

        common.copy_state(/* mask= */ FileState::all(), new_state, event_queue);
    }

    fn close(
        self,
        common: &mut UnixSocketCommon,
        event_queue: &mut EventQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        for sock in self.queue {
            // close all queued sockets
            if let Err(e) = sock.borrow_mut().close(event_queue) {
                log::warn!("Unexpected error while closing queued unix socket: {:?}", e);
            }
        }

        let new_state = ConnOrientedClosed {};
        new_state.refresh_file_state(common, event_queue);
        (new_state.into(), common.close(event_queue))
    }

    fn listen(
        mut self,
        common: &mut UnixSocketCommon,
        backlog: i32,
        event_queue: &mut EventQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        self.queue_limit = backlog_to_queue_size(backlog);

        // refresh the socket's file state
        self.refresh_file_state(common, event_queue);

        (self.into(), Ok(()))
    }

    fn accept(
        &mut self,
        common: &mut UnixSocketCommon,
        event_queue: &mut EventQueue,
    ) -> Result<Arc<AtomicRefCell<UnixSocket>>, SyscallError> {
        let child_socket = match self.queue.pop_front() {
            Some(x) => x,
            None => return Err(Errno::EWOULDBLOCK.into()),
        };

        // refresh the socket's file state
        self.refresh_file_state(common, event_queue);

        Ok(child_socket)
    }

    fn queue_incoming_conn(
        &mut self,
        common: &mut UnixSocketCommon,
        from_address: Option<nix::sys::socket::UnixAddr>,
        peer: &Arc<AtomicRefCell<UnixSocket>>,
        child_send_buffer: &Arc<AtomicRefCell<SharedBuf>>,
        event_queue: &mut EventQueue,
    ) -> Result<&Arc<AtomicRefCell<UnixSocket>>, IncomingConnError> {
        if self.queue.len() >= self.queue_limit.try_into().unwrap() {
            assert!(!common.state.contains(FileState::SOCKET_ALLOWING_CONNECT));
            return Err(IncomingConnError::QueueFull);
        }

        assert!(common.state.contains(FileState::SOCKET_ALLOWING_CONNECT));

        let child_socket = UnixSocket::new(
            // copy the parent's status
            common.status,
            common.socket_type,
            &common.namespace,
        );

        let child_recv_buffer = Arc::clone(&child_socket.borrow_mut().common.recv_buffer);

        let weak = Arc::downgrade(&child_socket);
        let send_buffer_handle = child_send_buffer.borrow_mut().add_listener(
            BufferState::WRITABLE | BufferState::NO_READERS,
            move |_, event_queue| {
                if let Some(socket) = weak.upgrade() {
                    socket.borrow_mut().refresh_file_state(event_queue);
                }
            },
        );

        // increment the buffer's writer count
        let writer_handle = child_send_buffer.borrow_mut().add_writer(event_queue);

        let weak = Arc::downgrade(&child_socket);
        let recv_buffer_handle = child_recv_buffer.borrow_mut().add_listener(
            BufferState::READABLE | BufferState::NO_WRITERS,
            move |_, event_queue| {
                if let Some(socket) = weak.upgrade() {
                    socket.borrow_mut().refresh_file_state(event_queue);
                }
            },
        );

        // increment the buffer's reader count
        let reader_handle = child_recv_buffer.borrow_mut().add_reader(event_queue);

        let new_child_state = ConnOrientedConnected {
            // use the parent's bind address
            bound_addr: Some(self.bound_addr.clone()),
            peer_addr: from_address,
            peer: Arc::clone(peer),
            reader_handle,
            writer_handle,
            _recv_buffer_handle: recv_buffer_handle,
            _send_buffer_handle: send_buffer_handle,
        };

        // update the child socket's state
        child_socket.borrow_mut().protocol_state = new_child_state.into();

        // defer refreshing the child socket's file-state until later
        let weak = Arc::downgrade(&child_socket);
        event_queue.add(move |event_queue| {
            if let Some(child_socket) = weak.upgrade() {
                child_socket.borrow_mut().refresh_file_state(event_queue);
            }
        });

        // add the child socket to the accept queue
        self.queue.push_back(child_socket);

        // refresh the server socket's file state
        self.refresh_file_state(common, event_queue);

        // return a reference to the enqueued child socket
        Ok(self.queue.back().unwrap())
    }
}

impl Protocol for ConnOrientedConnected {
    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn peer_address(&self) -> Result<Option<nix::sys::socket::UnixAddr>, SyscallError> {
        Ok(self.peer_addr)
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn bound_address(&self) -> Result<Option<nix::sys::socket::UnixAddr>, SyscallError> {
        Ok(self.bound_addr)
    }

    fn refresh_file_state(&self, common: &mut UnixSocketCommon, event_queue: &mut EventQueue) {
        let mut new_state = FileState::ACTIVE;

        {
            let recv_buffer = common.recv_buffer.borrow();
            let peer = self.peer.borrow();
            let send_buffer = peer.recv_buffer().borrow();

            new_state.set(
                FileState::READABLE,
                recv_buffer.has_data() || recv_buffer.num_writers() == 0,
            );
            new_state.set(
                FileState::WRITABLE,
                common.sent_len < common.send_limit || send_buffer.num_readers() == 0,
            );
        }

        common.copy_state(/* mask= */ FileState::all(), new_state, event_queue);
    }

    fn close(
        self,
        common: &mut UnixSocketCommon,
        event_queue: &mut EventQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        // inform the buffer that there is one fewer readers
        common
            .recv_buffer
            .borrow_mut()
            .remove_reader(self.reader_handle, event_queue);

        // inform the buffer that there is one fewer writers
        self.peer
            .borrow()
            .recv_buffer()
            .borrow_mut()
            .remove_writer(self.writer_handle, event_queue);

        let new_state = ConnOrientedClosed {};
        new_state.refresh_file_state(common, event_queue);
        (new_state.into(), common.close(event_queue))
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn sendto<R>(
        &mut self,
        common: &mut UnixSocketCommon,
        bytes: R,
        addr: Option<nix::sys::socket::SockAddr>,
        event_queue: &mut EventQueue,
    ) -> SyscallResult
    where
        R: std::io::Read + std::io::Seek,
    {
        let recv_socket = common.resolve_destination(Some(&self.peer), addr)?;
        let rv = common.sendto(bytes, &recv_socket, event_queue)?;

        self.refresh_file_state(common, event_queue);

        Ok(rv.into())
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn recvfrom<W>(
        &mut self,
        common: &mut UnixSocketCommon,
        bytes: W,
        event_queue: &mut EventQueue,
    ) -> Result<(SysCallReg, Option<nix::sys::socket::SockAddr>), SyscallError>
    where
        W: std::io::Write + std::io::Seek,
    {
        let (num_copied, num_removed_from_buf) = common.recvfrom(bytes, event_queue)?;
        let num_removed_from_buf = u64::try_from(num_removed_from_buf).unwrap();

        if num_removed_from_buf > 0 {
            // defer informing the peer until we're done processing the current socket
            let peer = Arc::clone(&self.peer);
            event_queue.add(move |event_queue| {
                peer.borrow_mut()
                    .inform_bytes_read(num_removed_from_buf, event_queue);
            });
        }

        self.refresh_file_state(common, event_queue);

        Ok((
            num_copied.into(),
            self.peer_addr.map(nix::sys::socket::SockAddr::Unix),
        ))
    }

    fn inform_bytes_read(
        &mut self,
        common: &mut UnixSocketCommon,
        num: u64,
        event_queue: &mut EventQueue,
    ) {
        common.sent_len = common.sent_len.checked_sub(num).unwrap();
        self.refresh_file_state(common, event_queue);
    }

    fn ioctl(
        &mut self,
        common: &mut UnixSocketCommon,
        request: u64,
        arg_ptr: PluginPtr,
        memory_manager: &mut MemoryManager,
    ) -> SyscallResult {
        common.ioctl(request, arg_ptr, memory_manager)
    }

    fn accept(
        &mut self,
        _common: &mut UnixSocketCommon,
        _event_queue: &mut EventQueue,
    ) -> Result<Arc<AtomicRefCell<UnixSocket>>, SyscallError> {
        log::warn!("accept() while in state {}", std::any::type_name::<Self>());
        Err(Errno::EINVAL.into())
    }
}

impl Protocol for ConnOrientedClosed {
    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn peer_address(&self) -> Result<Option<nix::sys::socket::UnixAddr>, SyscallError> {
        Err(Errno::ENOTCONN.into())
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn bound_address(&self) -> Result<Option<nix::sys::socket::UnixAddr>, SyscallError> {
        Err(Errno::EBADFD.into())
    }

    fn refresh_file_state(&self, common: &mut UnixSocketCommon, event_queue: &mut EventQueue) {
        common.copy_state(
            /* mask= */ FileState::all(),
            FileState::CLOSED,
            event_queue,
        );
    }

    fn close(
        self,
        _common: &mut UnixSocketCommon,
        _event_queue: &mut EventQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        // why are we trying to close an already closed file? we probably want a bt here...
        panic!("Trying to close an already closed socket");
    }

    fn inform_bytes_read(
        &mut self,
        _common: &mut UnixSocketCommon,
        _num: u64,
        _event_queue: &mut EventQueue,
    ) {
        // do nothing since we're already closed
    }
}

impl Protocol for ConnLessInitial {
    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn peer_address(&self) -> Result<Option<nix::sys::socket::UnixAddr>, SyscallError> {
        match self.peer {
            Some(_) => Ok(self.peer_addr),
            None => Err(Errno::ENOTCONN.into()),
        }
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn bound_address(&self) -> Result<Option<nix::sys::socket::UnixAddr>, SyscallError> {
        Ok(self.bound_addr)
    }

    fn refresh_file_state(&self, common: &mut UnixSocketCommon, event_queue: &mut EventQueue) {
        let mut new_state = FileState::ACTIVE;

        {
            let recv_buffer = common.recv_buffer.borrow();

            new_state.set(FileState::READABLE, recv_buffer.has_data());
            new_state.set(FileState::WRITABLE, common.sent_len < common.send_limit);
        }

        common.copy_state(/* mask= */ FileState::all(), new_state, event_queue);
    }

    fn close(
        self,
        common: &mut UnixSocketCommon,
        event_queue: &mut EventQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        // inform the buffer that there is one fewer readers
        common
            .recv_buffer
            .borrow_mut()
            .remove_reader(self.reader_handle, event_queue);

        for byte_data in self.recv_data.into_iter() {
            // defer informing the senders until we're done processing the current socket
            event_queue.add(move |event_queue| {
                byte_data
                    .from_socket
                    .borrow_mut()
                    .inform_bytes_read(byte_data.num_bytes, event_queue);
            });
        }

        let new_state = ConnLessClosed {};
        new_state.refresh_file_state(common, event_queue);
        (new_state.into(), common.close(event_queue))
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn bind(
        &mut self,
        common: &mut UnixSocketCommon,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        addr: Option<&nix::sys::socket::SockAddr>,
        rng: impl rand::Rng,
    ) -> SyscallResult {
        // if already bound
        if self.bound_addr.is_some() {
            return Err(Errno::EINVAL.into());
        }

        self.bound_addr = Some(common.bind(socket, addr, rng)?);
        Ok(0.into())
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn sendto<R>(
        &mut self,
        common: &mut UnixSocketCommon,
        bytes: R,
        addr: Option<nix::sys::socket::SockAddr>,
        event_queue: &mut EventQueue,
    ) -> SyscallResult
    where
        R: std::io::Read + std::io::Seek,
    {
        let recv_socket = common.resolve_destination(self.peer.as_ref(), addr)?;
        let rv = common.sendto(bytes, &recv_socket, event_queue)?;

        let byte_data = ByteData {
            from_socket: self.this_socket.upgrade().unwrap(),
            from_addr: self.bound_addr,
            num_bytes: rv.try_into().unwrap(),
        };

        match &mut recv_socket.borrow_mut().protocol_state {
            ProtocolState::ConnLessInitial(state) => {
                state.as_mut().unwrap().recv_data.push_back(byte_data);
            }
            _ => panic!(
                "Sending bytes to a socket in state {}",
                std::any::type_name::<Self>()
            ),
        }

        self.refresh_file_state(common, event_queue);

        Ok(rv.into())
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn recvfrom<W>(
        &mut self,
        common: &mut UnixSocketCommon,
        bytes: W,
        event_queue: &mut EventQueue,
    ) -> Result<(SysCallReg, Option<nix::sys::socket::SockAddr>), SyscallError>
    where
        W: std::io::Write + std::io::Seek,
    {
        let (num_copied, num_removed_from_buf) = common.recvfrom(bytes, event_queue)?;
        let num_removed_from_buf = u64::try_from(num_removed_from_buf).unwrap();

        let byte_data = self.recv_data.pop_front().unwrap();
        assert!(num_removed_from_buf == byte_data.num_bytes);

        // defer informing the sender until we're done processing the current socket
        event_queue.add(move |event_queue| {
            byte_data
                .from_socket
                .borrow_mut()
                .inform_bytes_read(byte_data.num_bytes, event_queue);
        });

        self.refresh_file_state(common, event_queue);

        Ok((
            num_copied.into(),
            byte_data.from_addr.map(nix::sys::socket::SockAddr::Unix),
        ))
    }

    fn inform_bytes_read(
        &mut self,
        common: &mut UnixSocketCommon,
        num: u64,
        event_queue: &mut EventQueue,
    ) {
        common.sent_len = common.sent_len.checked_sub(num).unwrap();
        self.refresh_file_state(common, event_queue);
    }

    fn ioctl(
        &mut self,
        common: &mut UnixSocketCommon,
        request: u64,
        arg_ptr: PluginPtr,
        memory_manager: &mut MemoryManager,
    ) -> SyscallResult {
        common.ioctl(request, arg_ptr, memory_manager)
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn connect(
        self,
        common: &mut UnixSocketCommon,
        _socket: &Arc<AtomicRefCell<UnixSocket>>,
        addr: &nix::sys::socket::SockAddr,
        event_queue: &mut EventQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        // TODO: support AF_UNSPEC to disassociate
        let addr = match addr {
            nix::sys::socket::SockAddr::Unix(x) => x,
            _ => return (self.into(), Err(Errno::EINVAL.into())),
        };

        // find the socket bound at the address
        let peer = match lookup_address(&common.namespace.borrow(), common.socket_type, addr) {
            Some(x) => x,
            None => return (self.into(), Err(Errno::ECONNREFUSED.into())),
        };

        let new_state = Self {
            peer_addr: Some(addr.clone()),
            peer: Some(peer),
            ..self
        };

        new_state.refresh_file_state(common, event_queue);

        (new_state.into(), Ok(()))
    }

    fn connect_unnamed(
        self,
        common: &mut UnixSocketCommon,
        _socket: &Arc<AtomicRefCell<UnixSocket>>,
        peer: Arc<AtomicRefCell<UnixSocket>>,
        event_queue: &mut EventQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        assert!(self.peer_addr.is_none());
        assert!(self.bound_addr.is_none());

        let new_state = Self {
            bound_addr: None,
            peer_addr: None,
            peer: Some(peer),
            ..self
        };

        new_state.refresh_file_state(common, event_queue);

        (new_state.into(), Ok(()))
    }
}

impl Protocol for ConnLessClosed {
    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn peer_address(&self) -> Result<Option<nix::sys::socket::UnixAddr>, SyscallError> {
        Ok(None)
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn bound_address(&self) -> Result<Option<nix::sys::socket::UnixAddr>, SyscallError> {
        Ok(None)
    }

    fn refresh_file_state(&self, common: &mut UnixSocketCommon, event_queue: &mut EventQueue) {
        common.copy_state(
            /* mask= */ FileState::all(),
            FileState::CLOSED,
            event_queue,
        );
    }

    fn close(
        self,
        _common: &mut UnixSocketCommon,
        _event_queue: &mut EventQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        // why are we trying to close an already closed file? we probably want a bt here...
        panic!("Trying to close an already closed socket");
    }

    fn inform_bytes_read(
        &mut self,
        _common: &mut UnixSocketCommon,
        _num: u64,
        _event_queue: &mut EventQueue,
    ) {
        // do nothing since we're already closed
    }
}

/// Common data and functionality that is useful for all states.
struct UnixSocketCommon {
    recv_buffer: Arc<AtomicRefCell<SharedBuf>>,
    /// The max number of "in flight" bytes (sent but not yet read from the receiving socket).
    send_limit: u64,
    /// The number of "in flight" bytes.
    sent_len: u64,
    event_source: StateEventSource,
    state: FileState,
    status: FileStatus,
    socket_type: UnixSocketType,
    namespace: Arc<AtomicRefCell<AbstractUnixNamespace>>,
    // should only be used by `OpenFile` to make sure there is only ever one `OpenFile` instance for
    // this file
    has_open_file: bool,
}

impl UnixSocketCommon {
    pub fn close(&mut self, event_queue: &mut EventQueue) -> Result<(), SyscallError> {
        if self.state.contains(FileState::CLOSED) {
            log::warn!("Attempting to close an already-closed unix socket");
        }

        // set the closed flag and remove any other flags
        self.copy_state(
            /* mask= */ FileState::all(),
            FileState::CLOSED,
            event_queue,
        );

        Ok(())
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    pub fn bind(
        &mut self,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        addr: Option<&nix::sys::socket::SockAddr>,
        rng: impl rand::Rng,
    ) -> Result<nix::sys::socket::UnixAddr, SyscallError> {
        // get the unix address
        let addr = match addr {
            Some(nix::sys::socket::SockAddr::Unix(x)) => x,
            _ => {
                log::warn!(
                    "Attempted to bind unix socket to non-unix address {:?}",
                    addr
                );
                return Err(Errno::EINVAL.into());
            }
        };

        // bind the socket
        let bound_addr = if let Some(name) = addr.as_abstract() {
            // if given an abstract socket address
            let namespace = Arc::clone(&self.namespace);
            match AbstractUnixNamespace::bind(
                &namespace,
                self.socket_type,
                name.to_vec(),
                socket,
                &mut self.event_source,
            ) {
                Ok(()) => *addr,
                // address is in use
                Err(_) => return Err(Errno::EADDRINUSE.into()),
            }
        } else if addr.path_len() == 0 {
            // if given an "unnamed" address
            let namespace = Arc::clone(&self.namespace);
            match AbstractUnixNamespace::autobind(
                &namespace,
                self.socket_type,
                socket,
                &mut self.event_source,
                rng,
            ) {
                Ok(ref name) => nix::sys::socket::UnixAddr::new_abstract(name).unwrap(),
                Err(_) => return Err(Errno::EADDRINUSE.into()),
            }
        } else {
            log::warn!("Only abstract names are currently supported for unix sockets");
            return Err(Errno::ENOTSUP.into());
        };

        Ok(bound_addr)
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    pub fn resolve_destination(
        &self,
        peer: Option<&Arc<AtomicRefCell<UnixSocket>>>,
        addr: Option<nix::sys::socket::SockAddr>,
    ) -> Result<Arc<AtomicRefCell<UnixSocket>>, SyscallError> {
        let addr = match addr {
            Some(nix::sys::socket::SockAddr::Unix(x)) => Some(x),
            None => None,
            _ => return Err(Errno::EINVAL.into()),
        };

        // returns either the send buffer, or None if we should look up the send buffer from the
        // socket address
        let peer = match (peer, addr) {
            // already connected but a destination address was given
            (Some(peer), Some(_addr)) => match self.socket_type {
                UnixSocketType::Stream => return Err(Errno::EISCONN.into()),
                // linux seems to ignore the destination address for connected seq packet sockets
                UnixSocketType::SeqPacket => Some(peer),
                UnixSocketType::Dgram => None,
            },
            // already connected and no destination address was given
            (Some(peer), None) => Some(peer),
            // not connected but a destination address was given
            (None, Some(_addr)) => match self.socket_type {
                UnixSocketType::Stream => return Err(Errno::EOPNOTSUPP.into()),
                UnixSocketType::SeqPacket => return Err(Errno::ENOTCONN.into()),
                UnixSocketType::Dgram => None,
            },
            // not connected and no destination address given
            (None, None) => return Err(Errno::ENOTCONN.into()),
        };

        // either use the existing send buffer, or look up the send buffer from the address
        let peer = match peer {
            Some(ref x) => Arc::clone(x),
            None => {
                // look up the socket from the address name
                match lookup_address(&self.namespace.borrow(), self.socket_type, &addr.unwrap()) {
                    // socket was found with the given name
                    Some(ref recv_socket) => {
                        // store an Arc of the recv buffer
                        Arc::clone(recv_socket)
                    }
                    // no socket has the given name
                    None => return Err(Errno::ECONNREFUSED.into()),
                }
            }
        };

        return Ok(peer);
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    pub fn sendto<R>(
        &mut self,
        mut bytes: R,
        peer: &Arc<AtomicRefCell<UnixSocket>>,
        event_queue: &mut EventQueue,
    ) -> Result<usize, SyscallError>
    where
        R: std::io::Read + std::io::Seek,
    {
        let peer_ref = peer.borrow();
        let mut send_buffer = peer_ref.recv_buffer().borrow_mut();

        // if the buffer has no readers, the destination socket is closed
        if send_buffer.num_readers() == 0 {
            return Err(match self.socket_type {
                // connection-oriented socket
                UnixSocketType::Stream | UnixSocketType::SeqPacket => nix::errno::Errno::EPIPE,
                // connectionless socket
                UnixSocketType::Dgram => nix::errno::Errno::ECONNREFUSED,
            }
            .into());
        }

        let len = bytes.stream_len_bp()? as usize;

        // we keep track of the send buffer size manually, since the unix socket buffers all have
        // usize::MAX length
        let space_available = self
            .send_limit
            .saturating_sub(self.sent_len)
            .try_into()
            .unwrap();

        if space_available == 0 {
            return Err(Errno::EAGAIN.into());
        }

        let len = match self.socket_type {
            UnixSocketType::Stream => std::cmp::min(len, space_available),
            UnixSocketType::Dgram | UnixSocketType::SeqPacket => {
                if len <= space_available {
                    len
                } else if len <= self.send_limit.try_into().unwrap() {
                    // we can send this when the buffer has more space available
                    return Err(Errno::EAGAIN.into());
                } else {
                    // we could never send this message
                    return Err(Errno::EMSGSIZE.into());
                }
            }
        };

        let bytes = bytes.take(len.try_into().unwrap());

        let num_copied = match self.socket_type {
            UnixSocketType::Stream => {
                if len == 0 {
                    0
                } else {
                    send_buffer.write_stream(bytes, len, event_queue)?
                }
            }
            UnixSocketType::Dgram | UnixSocketType::SeqPacket => {
                send_buffer.write_packet(bytes, len, event_queue)?;
                len.try_into().unwrap()
            }
        };

        // if we successfully sent bytes, update the sent count
        self.sent_len += u64::try_from(num_copied).unwrap();

        Ok(num_copied)
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    pub fn recvfrom<W>(
        &mut self,
        mut bytes: W,
        event_queue: &mut EventQueue,
    ) -> Result<(usize, usize), SyscallError>
    where
        W: std::io::Write + std::io::Seek,
    {
        let mut recv_buffer = self.recv_buffer.borrow_mut();

        // the read would block if all:
        //  1. the recv buffer has no data
        //  2. it's a connectionless socket OR the connection-oriented destination socket is not
        //     closed
        if !recv_buffer.has_data()
            && (self.socket_type == UnixSocketType::Dgram || recv_buffer.num_writers() > 0)
        {
            // return EWOULDBLOCK even if 'bytes' has length 0
            return Err(Errno::EWOULDBLOCK.into());
        }

        let (num_copied, num_removed_from_buf) = recv_buffer.read(&mut bytes, event_queue)?;

        Ok((num_copied, num_removed_from_buf))
    }

    pub fn ioctl(
        &mut self,
        request: u64,
        _arg_ptr: PluginPtr,
        _memory_manager: &mut MemoryManager,
    ) -> SyscallResult {
        log::warn!(
            "We do not yet handle ioctl request {} on unix sockets",
            request
        );
        Err(Errno::EINVAL.into())
    }

    fn copy_state(&mut self, mask: FileState, state: FileState, event_queue: &mut EventQueue) {
        let old_state = self.state;

        // remove the masked flags, then copy the masked flags
        self.state.remove(mask);
        self.state.insert(state & mask);

        self.handle_state_change(old_state, event_queue);
    }

    fn handle_state_change(&mut self, old_state: FileState, event_queue: &mut EventQueue) {
        let states_changed = self.state ^ old_state;

        // if nothing changed
        if states_changed.is_empty() {
            return;
        }

        self.event_source
            .notify_listeners(self.state, states_changed, event_queue);
    }
}

fn lookup_address(
    namespace: &AbstractUnixNamespace,
    socket_type: UnixSocketType,
    addr: &nix::sys::socket::UnixAddr,
) -> Option<Arc<AtomicRefCell<UnixSocket>>> {
    // if an abstract address
    if let Some(name) = addr.as_abstract() {
        // look up the socket from the address name
        namespace.lookup(socket_type, name)
    } else {
        log::warn!("Unix sockets with pathname addresses are not yet supported");
        None
    }
}

fn backlog_to_queue_size(backlog: i32) -> u32 {
    // linux also makes this cast, so negative backlogs wrap around to large positive backlogs
    // https://elixir.free-electrons.com/linux/v5.11.22/source/net/unix/af_unix.c#L628
    let backlog = backlog as u32;

    // the linux '__sys_listen()' applies the somaxconn max to all protocols, including unix sockets
    let queue_limit = std::cmp::min(backlog, c::SHADOW_SOMAXCONN.try_into().unwrap());

    // linux uses a limit of one greater than the provided backlog (ex: a backlog value of 0 allows
    // for one incoming connection at a time)
    queue_limit.saturating_add(1)
}

fn empty_unix_sockaddr() -> nix::sys::socket::UnixAddr {
    match empty_sockaddr(nix::sys::socket::AddressFamily::Unix) {
        // https://github.com/shadow/shadow/issues/2093
        #[allow(deprecated)]
        nix::sys::socket::SockAddr::Unix(x) => x,
        x => panic!("Unexpected socket address type: {:?}", x),
    }
}

// WARNING: don't add new enum variants without updating 'AbstractUnixNamespace::new()'
#[derive(Copy, Clone, Debug, Hash, PartialEq, Eq)]
pub enum UnixSocketType {
    Stream,
    Dgram,
    SeqPacket,
}

impl TryFrom<libc::c_int> for UnixSocketType {
    type Error = UnixSocketTypeConversionError;
    fn try_from(val: libc::c_int) -> Result<Self, Self::Error> {
        match val {
            libc::SOCK_STREAM => Ok(Self::Stream),
            libc::SOCK_DGRAM => Ok(Self::Dgram),
            libc::SOCK_SEQPACKET => Ok(Self::SeqPacket),
            x => Err(UnixSocketTypeConversionError(x)),
        }
    }
}

#[derive(Copy, Clone, Debug)]
pub struct UnixSocketTypeConversionError(libc::c_int);

impl std::error::Error for UnixSocketTypeConversionError {}

impl std::fmt::Display for UnixSocketTypeConversionError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(
            f,
            "Invalid socket type {}; unix sockets only support SOCK_STREAM, SOCK_DGRAM, and SOCK_SEQPACKET",
            self.0
        )
    }
}

#[derive(Copy, Clone, Debug)]
enum IncomingConnError {
    QueueFull,
    NotSupported,
}

struct ByteData {
    from_socket: Arc<AtomicRefCell<UnixSocket>>,
    from_addr: Option<nix::sys::socket::UnixAddr>,
    num_bytes: u64,
}
