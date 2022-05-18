use std::collections::VecDeque;
use std::sync::Arc;

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

const UNIX_SOCKET_DEFAULT_BUFFER_SIZE: usize = 212_992;

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
        mode: FileMode,
        status: FileStatus,
        socket_type: UnixSocketType,
        namespace: &Arc<AtomicRefCell<AbstractUnixNamespace>>,
    ) -> Arc<AtomicRefCell<Self>> {
        // must be able to both read and write to the socket
        assert!(mode.contains(FileMode::READ) && mode.contains(FileMode::WRITE));

        // initialize the socket's receive buffer
        let recv_buffer = SharedBuf::new(UNIX_SOCKET_DEFAULT_BUFFER_SIZE);
        let recv_buffer = Arc::new(AtomicRefCell::new(recv_buffer));

        // increment the buffer's reader count; we'll return the handle back to the buffer when
        // we're no longer acting as a reader (when we're closed)
        // this is a new buffer and there are no listeners, so safe to use a temporary event queue
        let reader_handle = EventQueue::queue_and_run(|event_queue| {
            recv_buffer.borrow_mut().add_reader(event_queue)
        });

        let socket = Self {
            common: UnixSocketCommon {
                recv_buffer,
                event_source: StateEventSource::new(),
                state: FileState::ACTIVE,
                mode,
                status,
                socket_type,
                namespace: Arc::clone(namespace),
                recv_buffer_handle: None,
                reader_handle: Some(reader_handle),
                has_open_file: false,
            },
            protocol_state: ProtocolState::new(socket_type),
        };

        let socket = Arc::new(AtomicRefCell::new(socket));
        let mut socket_ref = socket.borrow_mut();

        // update the socket's state when the buffer's state changes
        let weak = Arc::downgrade(&socket);
        let recv_handle = socket_ref.common.recv_buffer.borrow_mut().add_listener(
            BufferState::READABLE | BufferState::NO_WRITERS,
            move |state, event_queue| {
                // if the file hasn't been dropped
                if let Some(socket) = weak.upgrade() {
                    let mut socket = socket.borrow_mut();

                    // if the socket is already closed, do nothing
                    if socket.common.state.contains(FileState::CLOSED) {
                        return;
                    }

                    // the socket is readable iff the buffer is readable or has no writers
                    socket.common.copy_state(
                        /* mask */ FileState::READABLE,
                        state
                            .intersects(BufferState::READABLE | BufferState::NO_WRITERS)
                            .then(|| FileState::READABLE)
                            .unwrap_or_default(),
                        event_queue,
                    );
                }
            },
        );

        socket_ref.common.recv_buffer_handle = Some(recv_handle);

        std::mem::drop(socket_ref);

        socket
    }

    pub fn get_status(&self) -> FileStatus {
        self.common.status
    }

    pub fn set_status(&mut self, status: FileStatus) {
        self.common.status = status;
    }

    pub fn mode(&self) -> FileMode {
        self.common.mode
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

    pub fn get_bound_address(&self) -> Option<nix::sys::socket::UnixAddr> {
        self.protocol_state.bound_address()
    }

    pub fn get_peer_address(&self) -> Option<nix::sys::socket::UnixAddr> {
        self.protocol_state.peer_address()
    }

    pub fn address_family(&self) -> nix::sys::socket::AddressFamily {
        nix::sys::socket::AddressFamily::Unix
    }

    pub fn recv_buffer(&self) -> &Arc<AtomicRefCell<SharedBuf>> {
        &self.common.recv_buffer
    }

    pub fn close(&mut self, event_queue: &mut EventQueue) -> Result<(), SyscallError> {
        self.protocol_state.close(&mut self.common, event_queue)
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
        mode: FileMode,
        status: FileStatus,
        socket_type: UnixSocketType,
        namespace: &Arc<AtomicRefCell<AbstractUnixNamespace>>,
        event_queue: &mut EventQueue,
    ) -> (Arc<AtomicRefCell<Self>>, Arc<AtomicRefCell<Self>>) {
        let socket_1 = UnixSocket::new(mode, status, socket_type, namespace);
        let socket_2 = UnixSocket::new(mode, status, socket_type, namespace);

        {
            let socket_1_ref = &mut *socket_1.borrow_mut();
            socket_1_ref
                .protocol_state
                .connect_unnamed(
                    &mut socket_1_ref.common,
                    &socket_1,
                    Arc::clone(socket_2.borrow().recv_buffer()),
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
                    Arc::clone(socket_1.borrow().recv_buffer()),
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
    peer_addr: nix::sys::socket::UnixAddr,
    send_buffer: Arc<AtomicRefCell<SharedBuf>>,
    writer_handle: WriterHandle,
    // this handle is never accessed, but we store it because of its drop impl
    _send_buffer_handle: BufferHandle,
}
struct ConnOrientedClosed {}

struct ConnLessInitial {
    bound_addr: Option<nix::sys::socket::UnixAddr>,
    peer_addr: Option<nix::sys::socket::UnixAddr>,
    send_buffer: Option<Arc<AtomicRefCell<SharedBuf>>>,
    writer_handle: Option<WriterHandle>,
    // this handle is never accessed, but we store it because of its drop impl
    _send_buffer_handle: Option<BufferHandle>,
}
struct ConnLessClosed {}

impl ConnOrientedListening {
    fn queue_is_full(&self) -> bool {
        self.queue.len() >= self.queue_limit.try_into().unwrap()
    }

    /// Determines the file state depending on the queue size and limit. Returns a file state mask,
    /// and the masked flags that should be updated.
    fn calculate_file_state(&self) -> (FileState, FileState) {
        let state_mask =
            FileState::READABLE | FileState::WRITABLE | FileState::SOCKET_ALLOWING_CONNECT;

        let mut new_state = FileState::empty();

        // socket is readable if the queue is not empty
        new_state.set(FileState::READABLE, self.queue.len() > 0);

        // socket allows connections if the queue is not full
        new_state.set(FileState::SOCKET_ALLOWING_CONNECT, !self.queue_is_full());

        // Note: This can cause a thundering-herd condition where multiple blocked connect() calls
        // are all notified at the same time, even if there isn't enough space to allow all of them.
        // In practice this should be uncommon so we don't worry about it, and avoids requiring that
        // the server keep a list of all connecting clients.

        (state_mask, new_state)
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
    fn new(socket_type: UnixSocketType) -> Self {
        match socket_type {
            UnixSocketType::Stream | UnixSocketType::SeqPacket => {
                Self::ConnOrientedInitial(Some(ConnOrientedInitial { bound_addr: None }))
            }
            UnixSocketType::Dgram => Self::ConnLessInitial(Some(ConnLessInitial {
                bound_addr: None,
                peer_addr: None,
                send_buffer: None,
                writer_handle: None,
                _send_buffer_handle: None,
            })),
        }
    }

    fn peer_address(&self) -> Option<nix::sys::socket::UnixAddr> {
        match self {
            Self::ConnOrientedInitial(x) => x.as_ref().unwrap().peer_address(),
            Self::ConnOrientedListening(x) => x.as_ref().unwrap().peer_address(),
            Self::ConnOrientedConnected(x) => x.as_ref().unwrap().peer_address(),
            Self::ConnOrientedClosed(x) => x.as_ref().unwrap().peer_address(),
            Self::ConnLessInitial(x) => x.as_ref().unwrap().peer_address(),
            Self::ConnLessClosed(x) => x.as_ref().unwrap().peer_address(),
        }
    }

    fn bound_address(&self) -> Option<nix::sys::socket::UnixAddr> {
        match self {
            Self::ConnOrientedInitial(x) => x.as_ref().unwrap().bound_address(),
            Self::ConnOrientedListening(x) => x.as_ref().unwrap().bound_address(),
            Self::ConnOrientedConnected(x) => x.as_ref().unwrap().bound_address(),
            Self::ConnOrientedClosed(x) => x.as_ref().unwrap().bound_address(),
            Self::ConnLessInitial(x) => x.as_ref().unwrap().bound_address(),
            Self::ConnLessClosed(x) => x.as_ref().unwrap().bound_address(),
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
        send_buffer: Arc<AtomicRefCell<SharedBuf>>,
        event_queue: &mut EventQueue,
    ) -> Result<(), SyscallError> {
        let (new_state, rv) = match self {
            Self::ConnOrientedInitial(x) => {
                x.take()
                    .unwrap()
                    .connect_unnamed(common, socket, send_buffer, event_queue)
            }
            Self::ConnOrientedListening(x) => {
                x.take()
                    .unwrap()
                    .connect_unnamed(common, socket, send_buffer, event_queue)
            }
            Self::ConnOrientedConnected(x) => {
                x.take()
                    .unwrap()
                    .connect_unnamed(common, socket, send_buffer, event_queue)
            }
            Self::ConnOrientedClosed(x) => {
                x.take()
                    .unwrap()
                    .connect_unnamed(common, socket, send_buffer, event_queue)
            }
            Self::ConnLessInitial(x) => {
                x.take()
                    .unwrap()
                    .connect_unnamed(common, socket, send_buffer, event_queue)
            }
            Self::ConnLessClosed(x) => {
                x.take()
                    .unwrap()
                    .connect_unnamed(common, socket, send_buffer, event_queue)
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
        child_send_buffer: &Arc<AtomicRefCell<SharedBuf>>,
        event_queue: &mut EventQueue,
    ) -> Result<&Arc<AtomicRefCell<UnixSocket>>, IncomingConnError> {
        match self {
            Self::ConnOrientedInitial(x) => x.as_mut().unwrap().queue_incoming_conn(
                common,
                from_address,
                child_send_buffer,
                event_queue,
            ),
            Self::ConnOrientedListening(x) => x.as_mut().unwrap().queue_incoming_conn(
                common,
                from_address,
                child_send_buffer,
                event_queue,
            ),
            Self::ConnOrientedConnected(x) => x.as_mut().unwrap().queue_incoming_conn(
                common,
                from_address,
                child_send_buffer,
                event_queue,
            ),
            Self::ConnOrientedClosed(x) => x.as_mut().unwrap().queue_incoming_conn(
                common,
                from_address,
                child_send_buffer,
                event_queue,
            ),
            Self::ConnLessInitial(x) => x.as_mut().unwrap().queue_incoming_conn(
                common,
                from_address,
                child_send_buffer,
                event_queue,
            ),
            Self::ConnLessClosed(x) => x.as_mut().unwrap().queue_incoming_conn(
                common,
                from_address,
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
    fn peer_address(&self) -> Option<nix::sys::socket::UnixAddr>;
    fn bound_address(&self) -> Option<nix::sys::socket::UnixAddr>;

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
        _send_buffer: Arc<AtomicRefCell<SharedBuf>>,
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
    fn peer_address(&self) -> Option<nix::sys::socket::UnixAddr> {
        None
    }

    fn bound_address(&self) -> Option<nix::sys::socket::UnixAddr> {
        self.bound_addr
    }

    fn close(
        self,
        common: &mut UnixSocketCommon,
        event_queue: &mut EventQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        let new_state = ConnOrientedClosed {};
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
        let (file_state_mask, file_state) = new_state.calculate_file_state();
        common.copy_state(file_state_mask, file_state, event_queue);

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
        let send_buffer_handle = common.connect_buffer(socket, &send_buffer, event_queue);

        // increment the buffer's writer count
        let writer_handle = send_buffer.borrow_mut().add_writer(event_queue);

        let new_state = ConnOrientedConnected {
            bound_addr: self.bound_addr,
            peer_addr: addr.clone(),
            send_buffer,
            _send_buffer_handle: send_buffer_handle,
            writer_handle,
        };

        (new_state.into(), Ok(()))
    }

    fn connect_unnamed(
        self,
        common: &mut UnixSocketCommon,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        send_buffer: Arc<AtomicRefCell<SharedBuf>>,
        event_queue: &mut EventQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        assert!(self.bound_addr.is_none());

        let unnamed_sock_addr = empty_unix_sockaddr();
        let send_buffer_handle = common.connect_buffer(socket, &send_buffer, event_queue);

        // increment the buffer's writer count
        let writer_handle = send_buffer.borrow_mut().add_writer(event_queue);

        let new_state = ConnOrientedConnected {
            // bind the socket to an unnamed address so that we don't accidentally bind it later
            bound_addr: Some(unnamed_sock_addr),
            peer_addr: unnamed_sock_addr,
            send_buffer,
            _send_buffer_handle: send_buffer_handle,
            writer_handle,
        };

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
    fn peer_address(&self) -> Option<nix::sys::socket::UnixAddr> {
        None
    }

    fn bound_address(&self) -> Option<nix::sys::socket::UnixAddr> {
        Some(self.bound_addr)
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
        let (file_state_mask, file_state) = self.calculate_file_state();
        common.copy_state(file_state_mask, file_state, event_queue);

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
        let (file_state_mask, file_state) = self.calculate_file_state();
        common.copy_state(file_state_mask, file_state, event_queue);

        Ok(child_socket)
    }

    fn queue_incoming_conn(
        &mut self,
        common: &mut UnixSocketCommon,
        from_address: Option<nix::sys::socket::UnixAddr>,
        child_send_buffer: &Arc<AtomicRefCell<SharedBuf>>,
        event_queue: &mut EventQueue,
    ) -> Result<&Arc<AtomicRefCell<UnixSocket>>, IncomingConnError> {
        if self.queue.len() >= self.queue_limit.try_into().unwrap() {
            assert!(!common.state.contains(FileState::SOCKET_ALLOWING_CONNECT));
            return Err(IncomingConnError::QueueFull);
        }

        assert!(common.state.contains(FileState::SOCKET_ALLOWING_CONNECT));

        let child_send_buffer = Arc::clone(child_send_buffer);

        let child_socket = UnixSocket::new(
            FileMode::READ | FileMode::WRITE,
            // copy the parent's status
            common.status,
            common.socket_type,
            &common.namespace,
        );

        let send_buffer_handle = child_socket.borrow_mut().common.connect_buffer(
            &child_socket,
            &child_send_buffer,
            event_queue,
        );

        // increment the buffer's writer count
        let writer_handle = child_send_buffer.borrow_mut().add_writer(event_queue);

        let new_child_state = ConnOrientedConnected {
            // use the parent's bind address
            bound_addr: Some(self.bound_addr.clone()),
            peer_addr: from_address.unwrap_or_else(|| empty_unix_sockaddr()),
            send_buffer: child_send_buffer,
            _send_buffer_handle: send_buffer_handle,
            writer_handle,
        };

        // update the child socket's state
        child_socket.borrow_mut().protocol_state = new_child_state.into();

        // add the child socket to the accept queue
        self.queue.push_back(child_socket);

        // refresh the server socket's file state
        let (file_state_mask, file_state) = self.calculate_file_state();
        common.copy_state(file_state_mask, file_state, event_queue);

        // return a reference to the enqueued child socket
        Ok(self.queue.back().unwrap())
    }
}

impl Protocol for ConnOrientedConnected {
    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn peer_address(&self) -> Option<nix::sys::socket::UnixAddr> {
        Some(self.peer_addr)
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn bound_address(&self) -> Option<nix::sys::socket::UnixAddr> {
        self.bound_addr
    }

    fn close(
        self,
        common: &mut UnixSocketCommon,
        event_queue: &mut EventQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        // inform the buffer that there is one fewer writers
        self.send_buffer
            .borrow_mut()
            .remove_writer(self.writer_handle, event_queue);

        let new_state = ConnOrientedClosed {};
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
        common.sendto(bytes, Some(&self.send_buffer), addr, event_queue)
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
        common.recvfrom(bytes, event_queue)
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
    fn peer_address(&self) -> Option<nix::sys::socket::UnixAddr> {
        None
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn bound_address(&self) -> Option<nix::sys::socket::UnixAddr> {
        None
    }

    fn close(
        self,
        _common: &mut UnixSocketCommon,
        _event_queue: &mut EventQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        // why are we trying to close an already closed file? we probably want a bt here...
        panic!("Trying to close an already closed socket");
    }
}

impl Protocol for ConnLessInitial {
    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn peer_address(&self) -> Option<nix::sys::socket::UnixAddr> {
        self.peer_addr
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn bound_address(&self) -> Option<nix::sys::socket::UnixAddr> {
        self.bound_addr
    }

    fn close(
        self,
        common: &mut UnixSocketCommon,
        event_queue: &mut EventQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        // inform the buffer that there is one fewer writers
        if let (Some(send_buffer), Some(writer_handle)) =
            (self.send_buffer.as_ref(), self.writer_handle)
        {
            send_buffer
                .borrow_mut()
                .remove_writer(writer_handle, event_queue);
        }

        let new_state = ConnLessClosed {};
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
        common.sendto(bytes, self.send_buffer.as_ref(), addr, event_queue)
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
        common.recvfrom(bytes, event_queue)
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
        socket: &Arc<AtomicRefCell<UnixSocket>>,
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

        // inform any existing buffer that there is one fewer writers
        if let (Some(old_send_buffer), Some(writer_handle)) =
            (self.send_buffer.as_ref(), self.writer_handle)
        {
            old_send_buffer
                .borrow_mut()
                .remove_writer(writer_handle, event_queue);
        }

        // get the new send buffer
        let new_send_buffer = Arc::clone(peer.borrow().recv_buffer());

        let send_buffer_handle = common.connect_buffer(socket, &new_send_buffer, event_queue);

        // increment the buffer's writer count
        let writer_handle = new_send_buffer.borrow_mut().add_writer(event_queue);

        let new_state = Self {
            peer_addr: Some(addr.clone()),
            send_buffer: Some(new_send_buffer),
            writer_handle: Some(writer_handle),
            _send_buffer_handle: Some(send_buffer_handle),
            ..self
        };

        (new_state.into(), Ok(()))
    }

    fn connect_unnamed(
        self,
        common: &mut UnixSocketCommon,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        send_buffer: Arc<AtomicRefCell<SharedBuf>>,
        event_queue: &mut EventQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        // bind the socket to an unnamed address so that it can't be bound later
        let unnamed_sock_addr = empty_unix_sockaddr();

        assert!(self.peer_addr.is_none());
        assert!(self.bound_addr.is_none());

        let send_buffer_handle = common.connect_buffer(socket, &send_buffer, event_queue);

        // increment the buffer's writer count
        let writer_handle = send_buffer.borrow_mut().add_writer(event_queue);

        let new_state = Self {
            bound_addr: Some(unnamed_sock_addr),
            peer_addr: Some(unnamed_sock_addr),
            send_buffer: Some(send_buffer),
            writer_handle: Some(writer_handle),
            _send_buffer_handle: Some(send_buffer_handle),
            ..self
        };

        (new_state.into(), Ok(()))
    }
}

impl Protocol for ConnLessClosed {
    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn peer_address(&self) -> Option<nix::sys::socket::UnixAddr> {
        None
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    fn bound_address(&self) -> Option<nix::sys::socket::UnixAddr> {
        None
    }

    fn close(
        self,
        _common: &mut UnixSocketCommon,
        _event_queue: &mut EventQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        // why are we trying to close an already closed file? we probably want a bt here...
        panic!("Trying to close an already closed socket");
    }
}

/// Common data and functionality that is useful for all states.
struct UnixSocketCommon {
    recv_buffer: Arc<AtomicRefCell<SharedBuf>>,
    event_source: StateEventSource,
    state: FileState,
    mode: FileMode,
    status: FileStatus,
    socket_type: UnixSocketType,
    namespace: Arc<AtomicRefCell<AbstractUnixNamespace>>,
    // listener handle for recv buffer events
    recv_buffer_handle: Option<BufferHandle>,
    // a handle that signifies we're acting as a reader for the recv buffer
    reader_handle: Option<ReaderHandle>,
    // should only be used by `OpenFile` to make sure there is only ever one `OpenFile` instance for
    // this file
    has_open_file: bool,
}

impl UnixSocketCommon {
    pub fn close(&mut self, event_queue: &mut EventQueue) -> Result<(), SyscallError> {
        if self.state.contains(FileState::CLOSED) {
            log::warn!("Attempting to close an already-closed unix socket");
        }

        // drop the event listener handle so that we stop receiving new events
        self.recv_buffer_handle.take().map(|h| h.stop_listening());

        // inform the buffer that there is one fewer readers
        if let Some(reader_handle) = self.reader_handle.take() {
            // return the handle back to the buffer
            self.recv_buffer
                .borrow_mut()
                .remove_reader(reader_handle, event_queue);
        }

        // set the closed flag and remove the active, readable, and writable flags
        self.copy_state(
            FileState::CLOSED
                | FileState::ACTIVE
                | FileState::READABLE
                | FileState::WRITABLE
                | FileState::SOCKET_ALLOWING_CONNECT,
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
    pub fn sendto<R>(
        &mut self,
        mut bytes: R,
        send_buffer: Option<&Arc<AtomicRefCell<SharedBuf>>>,
        addr: Option<nix::sys::socket::SockAddr>,
        event_queue: &mut EventQueue,
    ) -> SyscallResult
    where
        R: std::io::Read + std::io::Seek,
    {
        // if the file is not open for writing, return EBADF
        if !self.mode.contains(FileMode::WRITE) {
            return Err(nix::errno::Errno::EBADF.into());
        }

        let addr = match addr {
            Some(nix::sys::socket::SockAddr::Unix(x)) => Some(x),
            None => None,
            _ => return Err(Errno::EINVAL.into()),
        };

        // returns either the send buffer, or None if we should look up the send buffer from the
        // socket address
        let send_buffer = match (send_buffer, addr) {
            // already connected but a destination address was given
            (Some(send_buffer), Some(_addr)) => match self.socket_type {
                UnixSocketType::Stream => return Err(Errno::EISCONN.into()),
                // linux seems to ignore the destination address for connected seq packet sockets
                UnixSocketType::SeqPacket => Some(send_buffer),
                UnixSocketType::Dgram => None,
            },
            // already connected and no destination address was given
            (Some(send_buffer), None) => Some(send_buffer),
            // not connected but a destination address was given
            (None, Some(_addr)) => match self.socket_type {
                UnixSocketType::Stream => return Err(Errno::EOPNOTSUPP.into()),
                UnixSocketType::SeqPacket => return Err(Errno::ENOTCONN.into()),
                UnixSocketType::Dgram => None,
            },
            // not connected and no destination address given
            (None, None) => return Err(Errno::ENOTCONN.into()),
        };

        // a variable for storing an Arc of the recv buffer if needed
        let mut _buf_arc_storage;

        // either use the existing send buffer, or look up the send buffer from the address
        let send_buffer = match send_buffer {
            Some(x) => x,
            None => {
                // if an abstract address
                if let Some(name) = addr.unwrap().as_abstract() {
                    // look up the socket from the address name
                    match self.namespace.borrow().lookup(self.socket_type, name) {
                        // socket was found with the given name
                        Some(recv_socket) => {
                            // store an Arc of the recv buffer
                            _buf_arc_storage = Arc::clone(recv_socket.borrow().recv_buffer());
                            &_buf_arc_storage
                        }
                        // no socket has the given name
                        None => return Err(Errno::ECONNREFUSED.into()),
                    }
                } else {
                    log::warn!(
                        "Sending to pathname addresses from unix sockets is not yet supported"
                    );
                    return Err(Errno::ECONNREFUSED.into());
                }
            }
        };

        let mut send_buffer = send_buffer.borrow_mut();

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

        match self.socket_type {
            UnixSocketType::Stream => send_buffer.write_stream(bytes.by_ref(), len, event_queue),
            UnixSocketType::Dgram | UnixSocketType::SeqPacket => {
                send_buffer.write_packet(bytes.by_ref(), len, event_queue)?;
                Ok(len.into())
            }
        }
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    pub fn recvfrom<W>(
        &mut self,
        mut bytes: W,
        event_queue: &mut EventQueue,
    ) -> Result<(SysCallReg, Option<nix::sys::socket::SockAddr>), SyscallError>
    where
        W: std::io::Write + std::io::Seek,
    {
        // if the file is not open for reading, return EBADF
        if !self.mode.contains(FileMode::READ) {
            return Err(nix::errno::Errno::EBADF.into());
        }

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

        let num_read = recv_buffer.read(&mut bytes, event_queue)?;

        // TODO: support returning the source address
        Ok((num_read.into(), None))
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

    pub fn connect_buffer(
        &mut self,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        send_buffer: &Arc<AtomicRefCell<SharedBuf>>,
        event_queue: &mut EventQueue,
    ) -> BufferHandle {
        let mut send_buffer_ref = send_buffer.borrow_mut();

        // the socket is writable iff the buffer is writable or has no readers
        self.copy_state(
            /* mask */ FileState::WRITABLE,
            send_buffer_ref
                .state()
                .intersects(BufferState::WRITABLE | BufferState::NO_READERS)
                .then(|| FileState::WRITABLE)
                .unwrap_or_default(),
            event_queue,
        );

        // update the socket's state when the buffer's state changes
        let weak = Arc::downgrade(&socket);
        send_buffer_ref.add_listener(
            BufferState::WRITABLE | BufferState::NO_READERS,
            move |state, event_queue| {
                // if the file hasn't been dropped
                if let Some(socket) = weak.upgrade() {
                    let mut socket = socket.borrow_mut();

                    // if the socket is already closed, do nothing
                    if socket.common.state.contains(FileState::CLOSED) {
                        return;
                    }

                    // the socket is writable iff the buffer is writable or has no readers
                    socket.common.copy_state(
                        /* mask */ FileState::WRITABLE,
                        state
                            .intersects(BufferState::WRITABLE | BufferState::NO_READERS)
                            .then(|| FileState::WRITABLE)
                            .unwrap_or_default(),
                        event_queue,
                    );
                }
            },
        )
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

fn empty_unix_sockaddr() -> nix::sys::socket::UnixAddr {
    match empty_sockaddr(nix::sys::socket::AddressFamily::Unix) {
        // https://github.com/shadow/shadow/issues/2093
        #[allow(deprecated)]
        nix::sys::socket::SockAddr::Unix(x) => x,
        x => panic!("Unexpected socket address type: {:?}", x),
    }
}
