use std::collections::{LinkedList, VecDeque};
use std::io::Read;
use std::ops::DerefMut;
use std::sync::{Arc, Weak};

use atomic_refcell::AtomicRefCell;
use linux_api::errno::Errno;
use linux_api::ioctls::IoctlRequest;
use linux_api::socket::Shutdown;
use nix::sys::socket::MsgFlags;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::cshadow as c;
use crate::host::descriptor::listener::{StateEventSource, StateListenHandle, StateListenerFilter};
use crate::host::descriptor::shared_buf::{
    BufferHandle, BufferSignals, BufferState, ReaderHandle, SharedBuf, WriterHandle,
};
use crate::host::descriptor::socket::abstract_unix_ns::AbstractUnixNamespace;
use crate::host::descriptor::socket::{RecvmsgArgs, RecvmsgReturn, SendmsgArgs, Socket};
use crate::host::descriptor::{
    File, FileMode, FileSignals, FileState, FileStatus, OpenFile, SyscallResult,
};
use crate::host::memory_manager::MemoryManager;
use crate::host::network::namespace::NetworkNamespace;
use crate::host::syscall::io::{IoVec, IoVecReader, IoVecWriter};
use crate::host::syscall::types::SyscallError;
use crate::utility::callback_queue::CallbackQueue;
use crate::utility::sockaddr::{SockaddrStorage, SockaddrUnix};
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

    pub fn status(&self) -> FileStatus {
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
        self.common.supports_sa_restart()
    }

    pub fn set_has_open_file(&mut self, val: bool) {
        self.common.has_open_file = val;
    }

    pub fn getsockname(&self) -> Result<Option<SockaddrUnix<libc::sockaddr_un>>, Errno> {
        // return the bound address if set, otherwise return an empty unix sockaddr
        Ok(Some(
            self.protocol_state
                .bound_address()?
                .unwrap_or_else(SockaddrUnix::new_unnamed),
        ))
    }

    pub fn getpeername(&self) -> Result<Option<SockaddrUnix<libc::sockaddr_un>>, Errno> {
        // return the peer address if set, otherwise return an empty unix sockaddr
        Ok(Some(
            self.protocol_state
                .peer_address()?
                .unwrap_or_else(SockaddrUnix::new_unnamed),
        ))
    }

    pub fn address_family(&self) -> linux_api::socket::AddressFamily {
        linux_api::socket::AddressFamily::AF_UNIX
    }

    fn recv_buffer(&self) -> &Arc<AtomicRefCell<SharedBuf>> {
        &self.common.recv_buffer
    }

    fn inform_bytes_read(&mut self, num: u64, cb_queue: &mut CallbackQueue) {
        self.protocol_state
            .inform_bytes_read(&mut self.common, num, cb_queue);
    }

    pub fn close(&mut self, cb_queue: &mut CallbackQueue) -> Result<(), SyscallError> {
        self.protocol_state.close(&mut self.common, cb_queue)
    }

    fn refresh_file_state(&mut self, signals: FileSignals, cb_queue: &mut CallbackQueue) {
        self.protocol_state
            .refresh_file_state(&mut self.common, signals, cb_queue)
    }

    pub fn bind(
        socket: &Arc<AtomicRefCell<Self>>,
        addr: Option<&SockaddrStorage>,
        _net_ns: &NetworkNamespace,
        rng: impl rand::Rng,
    ) -> Result<(), SyscallError> {
        let socket_ref = &mut *socket.borrow_mut();
        socket_ref
            .protocol_state
            .bind(&mut socket_ref.common, socket, addr, rng)
    }

    pub fn readv(
        &mut self,
        _iovs: &[IoVec],
        _offset: Option<libc::off_t>,
        _flags: libc::c_int,
        _mem: &mut MemoryManager,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        // we could call UnixSocket::recvmsg() here, but for now we expect that there are no code
        // paths that would call UnixSocket::readv() since the readv() syscall handler should have
        // called UnixSocket::recvmsg() instead
        panic!("Called UnixSocket::readv() on a unix socket.");
    }

    pub fn writev(
        &mut self,
        _iovs: &[IoVec],
        _offset: Option<libc::off_t>,
        _flags: libc::c_int,
        _mem: &mut MemoryManager,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        // we could call UnixSocket::sendmsg() here, but for now we expect that there are no code
        // paths that would call UnixSocket::writev() since the writev() syscall handler should have
        // called UnixSocket::sendmsg() instead
        panic!("Called UnixSocket::writev() on a unix socket");
    }

    pub fn sendmsg(
        socket: &Arc<AtomicRefCell<Self>>,
        args: SendmsgArgs,
        mem: &mut MemoryManager,
        _net_ns: &NetworkNamespace,
        _rng: impl rand::Rng,
        cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        let socket_ref = &mut *socket.borrow_mut();
        socket_ref
            .protocol_state
            .sendmsg(&mut socket_ref.common, socket, args, mem, cb_queue)
    }

    pub fn recvmsg(
        socket: &Arc<AtomicRefCell<Self>>,
        args: RecvmsgArgs,
        mem: &mut MemoryManager,
        cb_queue: &mut CallbackQueue,
    ) -> Result<RecvmsgReturn, SyscallError> {
        let socket_ref = &mut *socket.borrow_mut();
        socket_ref
            .protocol_state
            .recvmsg(&mut socket_ref.common, socket, args, mem, cb_queue)
    }

    pub fn ioctl(
        &mut self,
        request: IoctlRequest,
        arg_ptr: ForeignPtr<()>,
        memory_manager: &mut MemoryManager,
    ) -> SyscallResult {
        self.protocol_state
            .ioctl(&mut self.common, request, arg_ptr, memory_manager)
    }

    pub fn stat(&self) -> Result<linux_api::stat::stat, SyscallError> {
        warn_once_then_debug!("We do not yet handle stat calls on unix sockets");
        Err(Errno::EINVAL.into())
    }

    pub fn listen(
        socket: &Arc<AtomicRefCell<Self>>,
        backlog: i32,
        _net_ns: &NetworkNamespace,
        _rng: impl rand::Rng,
        cb_queue: &mut CallbackQueue,
    ) -> Result<(), Errno> {
        let mut socket_ref = socket.borrow_mut();
        let socket_ref = socket_ref.deref_mut();
        socket_ref
            .protocol_state
            .listen(&mut socket_ref.common, backlog, cb_queue)
    }

    pub fn connect(
        socket: &Arc<AtomicRefCell<Self>>,
        addr: &SockaddrStorage,
        _net_ns: &NetworkNamespace,
        _rng: impl rand::Rng,
        cb_queue: &mut CallbackQueue,
    ) -> Result<(), SyscallError> {
        let socket_ref = &mut *socket.borrow_mut();
        socket_ref
            .protocol_state
            .connect(&mut socket_ref.common, socket, addr, cb_queue)
    }

    pub fn accept(
        &mut self,
        _net_ns: &NetworkNamespace,
        _rng: impl rand::Rng,
        cb_queue: &mut CallbackQueue,
    ) -> Result<OpenFile, SyscallError> {
        self.protocol_state.accept(&mut self.common, cb_queue)
    }

    pub fn shutdown(
        &mut self,
        _how: Shutdown,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<(), SyscallError> {
        log::warn!("shutdown() syscall not yet supported for unix sockets; Returning ENOSYS");
        Err(Errno::ENOSYS.into())
    }

    pub fn getsockopt(
        &mut self,
        _level: libc::c_int,
        _optname: libc::c_int,
        _optval_ptr: ForeignPtr<()>,
        _optlen: libc::socklen_t,
        _memory_manager: &mut MemoryManager,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<libc::socklen_t, SyscallError> {
        log::warn!("getsockopt() syscall not yet supported for unix sockets; Returning ENOSYS");
        Err(Errno::ENOSYS.into())
    }

    pub fn setsockopt(
        &mut self,
        _level: libc::c_int,
        _optname: libc::c_int,
        _optval_ptr: ForeignPtr<()>,
        _optlen: libc::socklen_t,
        _memory_manager: &MemoryManager,
    ) -> Result<(), SyscallError> {
        log::warn!("setsockopt() syscall not yet supported for unix sockets; Returning ENOSYS");
        Err(Errno::ENOSYS.into())
    }

    pub fn pair(
        status: FileStatus,
        socket_type: UnixSocketType,
        namespace: &Arc<AtomicRefCell<AbstractUnixNamespace>>,
        cb_queue: &mut CallbackQueue,
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
                    cb_queue,
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
                    cb_queue,
                )
                .unwrap();
        }

        (socket_1, socket_2)
    }

    pub fn add_listener(
        &mut self,
        monitoring_state: FileState,
        monitoring_signals: FileSignals,
        filter: StateListenerFilter,
        notify_fn: impl Fn(FileState, FileState, FileSignals, &mut CallbackQueue)
            + Send
            + Sync
            + 'static,
    ) -> StateListenHandle {
        self.common.event_source.add_listener(
            monitoring_state,
            monitoring_signals,
            filter,
            notify_fn,
        )
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
    bound_addr: Option<SockaddrUnix<libc::sockaddr_un>>,
}
struct ConnOrientedListening {
    bound_addr: SockaddrUnix<libc::sockaddr_un>,
    queue: VecDeque<Arc<AtomicRefCell<UnixSocket>>>,
    queue_limit: u32,
}
struct ConnOrientedConnected {
    bound_addr: Option<SockaddrUnix<libc::sockaddr_un>>,
    peer_addr: Option<SockaddrUnix<libc::sockaddr_un>>,
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
    bound_addr: Option<SockaddrUnix<libc::sockaddr_un>>,
    peer_addr: Option<SockaddrUnix<libc::sockaddr_un>>,
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
                let mut cb_queue = CallbackQueue::new();

                // dgram unix sockets are immediately able to receive data, so initialize the
                // receive buffer

                // increment the buffer's reader count
                let reader_handle = common.recv_buffer.borrow_mut().add_reader(&mut cb_queue);

                let weak = Weak::clone(socket);
                let recv_buffer_handle = common.recv_buffer.borrow_mut().add_listener(
                    BufferState::READABLE,
                    BufferSignals::BUFFER_GREW,
                    move |_, signals, cb_queue| {
                        if let Some(socket) = weak.upgrade() {
                            let signals = if signals.contains(BufferSignals::BUFFER_GREW) {
                                FileSignals::READ_BUFFER_GREW
                            } else {
                                FileSignals::empty()
                            };

                            socket.borrow_mut().refresh_file_state(signals, cb_queue);
                        }
                    },
                );

                // make sure no events were generated since if there were events to run, they would
                // probably not run correctly if the socket's Arc is not fully created yet (as in
                // the case of `Arc::new_cyclic`)
                assert!(cb_queue.is_empty());

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

    fn peer_address(&self) -> Result<Option<SockaddrUnix<libc::sockaddr_un>>, Errno> {
        match self {
            Self::ConnOrientedInitial(x) => x.as_ref().unwrap().peer_address(),
            Self::ConnOrientedListening(x) => x.as_ref().unwrap().peer_address(),
            Self::ConnOrientedConnected(x) => x.as_ref().unwrap().peer_address(),
            Self::ConnOrientedClosed(x) => x.as_ref().unwrap().peer_address(),
            Self::ConnLessInitial(x) => x.as_ref().unwrap().peer_address(),
            Self::ConnLessClosed(x) => x.as_ref().unwrap().peer_address(),
        }
    }

    fn bound_address(&self) -> Result<Option<SockaddrUnix<libc::sockaddr_un>>, Errno> {
        match self {
            Self::ConnOrientedInitial(x) => x.as_ref().unwrap().bound_address(),
            Self::ConnOrientedListening(x) => x.as_ref().unwrap().bound_address(),
            Self::ConnOrientedConnected(x) => x.as_ref().unwrap().bound_address(),
            Self::ConnOrientedClosed(x) => x.as_ref().unwrap().bound_address(),
            Self::ConnLessInitial(x) => x.as_ref().unwrap().bound_address(),
            Self::ConnLessClosed(x) => x.as_ref().unwrap().bound_address(),
        }
    }

    fn refresh_file_state(
        &self,
        common: &mut UnixSocketCommon,
        signals: FileSignals,
        cb_queue: &mut CallbackQueue,
    ) {
        match self {
            Self::ConnOrientedInitial(x) => x
                .as_ref()
                .unwrap()
                .refresh_file_state(common, signals, cb_queue),
            Self::ConnOrientedListening(x) => x
                .as_ref()
                .unwrap()
                .refresh_file_state(common, signals, cb_queue),
            Self::ConnOrientedConnected(x) => x
                .as_ref()
                .unwrap()
                .refresh_file_state(common, signals, cb_queue),
            Self::ConnOrientedClosed(x) => x
                .as_ref()
                .unwrap()
                .refresh_file_state(common, signals, cb_queue),
            Self::ConnLessInitial(x) => x
                .as_ref()
                .unwrap()
                .refresh_file_state(common, signals, cb_queue),
            Self::ConnLessClosed(x) => x
                .as_ref()
                .unwrap()
                .refresh_file_state(common, signals, cb_queue),
        }
    }

    fn close(
        &mut self,
        common: &mut UnixSocketCommon,
        cb_queue: &mut CallbackQueue,
    ) -> Result<(), SyscallError> {
        let (new_state, rv) = match self {
            Self::ConnOrientedInitial(x) => x.take().unwrap().close(common, cb_queue),
            Self::ConnOrientedListening(x) => x.take().unwrap().close(common, cb_queue),
            Self::ConnOrientedConnected(x) => x.take().unwrap().close(common, cb_queue),
            Self::ConnOrientedClosed(x) => x.take().unwrap().close(common, cb_queue),
            Self::ConnLessInitial(x) => x.take().unwrap().close(common, cb_queue),
            Self::ConnLessClosed(x) => x.take().unwrap().close(common, cb_queue),
        };

        *self = new_state;
        rv
    }

    fn bind(
        &mut self,
        common: &mut UnixSocketCommon,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        addr: Option<&SockaddrStorage>,
        rng: impl rand::Rng,
    ) -> Result<(), SyscallError> {
        match self {
            Self::ConnOrientedInitial(x) => x.as_mut().unwrap().bind(common, socket, addr, rng),
            Self::ConnOrientedListening(x) => x.as_mut().unwrap().bind(common, socket, addr, rng),
            Self::ConnOrientedConnected(x) => x.as_mut().unwrap().bind(common, socket, addr, rng),
            Self::ConnOrientedClosed(x) => x.as_mut().unwrap().bind(common, socket, addr, rng),
            Self::ConnLessInitial(x) => x.as_mut().unwrap().bind(common, socket, addr, rng),
            Self::ConnLessClosed(x) => x.as_mut().unwrap().bind(common, socket, addr, rng),
        }
    }

    fn sendmsg(
        &mut self,
        common: &mut UnixSocketCommon,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        args: SendmsgArgs,
        mem: &mut MemoryManager,
        cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        match self {
            Self::ConnOrientedInitial(x) => x
                .as_mut()
                .unwrap()
                .sendmsg(common, socket, args, mem, cb_queue),
            Self::ConnOrientedListening(x) => x
                .as_mut()
                .unwrap()
                .sendmsg(common, socket, args, mem, cb_queue),
            Self::ConnOrientedConnected(x) => x
                .as_mut()
                .unwrap()
                .sendmsg(common, socket, args, mem, cb_queue),
            Self::ConnOrientedClosed(x) => x
                .as_mut()
                .unwrap()
                .sendmsg(common, socket, args, mem, cb_queue),
            Self::ConnLessInitial(x) => x
                .as_mut()
                .unwrap()
                .sendmsg(common, socket, args, mem, cb_queue),
            Self::ConnLessClosed(x) => x
                .as_mut()
                .unwrap()
                .sendmsg(common, socket, args, mem, cb_queue),
        }
    }

    fn recvmsg(
        &mut self,
        common: &mut UnixSocketCommon,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        args: RecvmsgArgs,
        mem: &mut MemoryManager,
        cb_queue: &mut CallbackQueue,
    ) -> Result<RecvmsgReturn, SyscallError> {
        match self {
            Self::ConnOrientedInitial(x) => x
                .as_mut()
                .unwrap()
                .recvmsg(common, socket, args, mem, cb_queue),
            Self::ConnOrientedListening(x) => x
                .as_mut()
                .unwrap()
                .recvmsg(common, socket, args, mem, cb_queue),
            Self::ConnOrientedConnected(x) => x
                .as_mut()
                .unwrap()
                .recvmsg(common, socket, args, mem, cb_queue),
            Self::ConnOrientedClosed(x) => x
                .as_mut()
                .unwrap()
                .recvmsg(common, socket, args, mem, cb_queue),
            Self::ConnLessInitial(x) => x
                .as_mut()
                .unwrap()
                .recvmsg(common, socket, args, mem, cb_queue),
            Self::ConnLessClosed(x) => x
                .as_mut()
                .unwrap()
                .recvmsg(common, socket, args, mem, cb_queue),
        }
    }

    fn inform_bytes_read(
        &mut self,
        common: &mut UnixSocketCommon,
        num: u64,
        cb_queue: &mut CallbackQueue,
    ) {
        match self {
            Self::ConnOrientedInitial(x) => {
                x.as_mut().unwrap().inform_bytes_read(common, num, cb_queue)
            }
            Self::ConnOrientedListening(x) => {
                x.as_mut().unwrap().inform_bytes_read(common, num, cb_queue)
            }
            Self::ConnOrientedConnected(x) => {
                x.as_mut().unwrap().inform_bytes_read(common, num, cb_queue)
            }
            Self::ConnOrientedClosed(x) => {
                x.as_mut().unwrap().inform_bytes_read(common, num, cb_queue)
            }
            Self::ConnLessInitial(x) => {
                x.as_mut().unwrap().inform_bytes_read(common, num, cb_queue)
            }
            Self::ConnLessClosed(x) => x.as_mut().unwrap().inform_bytes_read(common, num, cb_queue),
        }
    }

    fn ioctl(
        &mut self,
        common: &mut UnixSocketCommon,
        request: IoctlRequest,
        arg_ptr: ForeignPtr<()>,
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
        cb_queue: &mut CallbackQueue,
    ) -> Result<(), Errno> {
        let (new_state, rv) = match self {
            Self::ConnOrientedInitial(x) => x.take().unwrap().listen(common, backlog, cb_queue),
            Self::ConnOrientedListening(x) => x.take().unwrap().listen(common, backlog, cb_queue),
            Self::ConnOrientedConnected(x) => x.take().unwrap().listen(common, backlog, cb_queue),
            Self::ConnOrientedClosed(x) => x.take().unwrap().listen(common, backlog, cb_queue),
            Self::ConnLessInitial(x) => x.take().unwrap().listen(common, backlog, cb_queue),
            Self::ConnLessClosed(x) => x.take().unwrap().listen(common, backlog, cb_queue),
        };

        *self = new_state;
        rv
    }

    fn connect(
        &mut self,
        common: &mut UnixSocketCommon,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        addr: &SockaddrStorage,
        cb_queue: &mut CallbackQueue,
    ) -> Result<(), SyscallError> {
        let (new_state, rv) = match self {
            Self::ConnOrientedInitial(x) => {
                x.take().unwrap().connect(common, socket, addr, cb_queue)
            }
            Self::ConnOrientedListening(x) => {
                x.take().unwrap().connect(common, socket, addr, cb_queue)
            }
            Self::ConnOrientedConnected(x) => {
                x.take().unwrap().connect(common, socket, addr, cb_queue)
            }
            Self::ConnOrientedClosed(x) => {
                x.take().unwrap().connect(common, socket, addr, cb_queue)
            }
            Self::ConnLessInitial(x) => x.take().unwrap().connect(common, socket, addr, cb_queue),
            Self::ConnLessClosed(x) => x.take().unwrap().connect(common, socket, addr, cb_queue),
        };

        *self = new_state;
        rv
    }

    fn connect_unnamed(
        &mut self,
        common: &mut UnixSocketCommon,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        peer: Arc<AtomicRefCell<UnixSocket>>,
        cb_queue: &mut CallbackQueue,
    ) -> Result<(), SyscallError> {
        let (new_state, rv) = match self {
            Self::ConnOrientedInitial(x) => x
                .take()
                .unwrap()
                .connect_unnamed(common, socket, peer, cb_queue),
            Self::ConnOrientedListening(x) => x
                .take()
                .unwrap()
                .connect_unnamed(common, socket, peer, cb_queue),
            Self::ConnOrientedConnected(x) => x
                .take()
                .unwrap()
                .connect_unnamed(common, socket, peer, cb_queue),
            Self::ConnOrientedClosed(x) => x
                .take()
                .unwrap()
                .connect_unnamed(common, socket, peer, cb_queue),
            Self::ConnLessInitial(x) => x
                .take()
                .unwrap()
                .connect_unnamed(common, socket, peer, cb_queue),
            Self::ConnLessClosed(x) => x
                .take()
                .unwrap()
                .connect_unnamed(common, socket, peer, cb_queue),
        };

        *self = new_state;
        rv
    }

    fn accept(
        &mut self,
        common: &mut UnixSocketCommon,
        cb_queue: &mut CallbackQueue,
    ) -> Result<OpenFile, SyscallError> {
        match self {
            Self::ConnOrientedInitial(x) => x.as_mut().unwrap().accept(common, cb_queue),
            Self::ConnOrientedListening(x) => x.as_mut().unwrap().accept(common, cb_queue),
            Self::ConnOrientedConnected(x) => x.as_mut().unwrap().accept(common, cb_queue),
            Self::ConnOrientedClosed(x) => x.as_mut().unwrap().accept(common, cb_queue),
            Self::ConnLessInitial(x) => x.as_mut().unwrap().accept(common, cb_queue),
            Self::ConnLessClosed(x) => x.as_mut().unwrap().accept(common, cb_queue),
        }
    }

    /// Called on the listening socket when there is an incoming connection.
    fn queue_incoming_conn(
        &mut self,
        common: &mut UnixSocketCommon,
        from_address: Option<SockaddrUnix<libc::sockaddr_un>>,
        peer: &Arc<AtomicRefCell<UnixSocket>>,
        child_send_buffer: &Arc<AtomicRefCell<SharedBuf>>,
        cb_queue: &mut CallbackQueue,
    ) -> Result<&Arc<AtomicRefCell<UnixSocket>>, IncomingConnError> {
        match self {
            Self::ConnOrientedInitial(x) => x.as_mut().unwrap().queue_incoming_conn(
                common,
                from_address,
                peer,
                child_send_buffer,
                cb_queue,
            ),
            Self::ConnOrientedListening(x) => x.as_mut().unwrap().queue_incoming_conn(
                common,
                from_address,
                peer,
                child_send_buffer,
                cb_queue,
            ),
            Self::ConnOrientedConnected(x) => x.as_mut().unwrap().queue_incoming_conn(
                common,
                from_address,
                peer,
                child_send_buffer,
                cb_queue,
            ),
            Self::ConnOrientedClosed(x) => x.as_mut().unwrap().queue_incoming_conn(
                common,
                from_address,
                peer,
                child_send_buffer,
                cb_queue,
            ),
            Self::ConnLessInitial(x) => x.as_mut().unwrap().queue_incoming_conn(
                common,
                from_address,
                peer,
                child_send_buffer,
                cb_queue,
            ),
            Self::ConnLessClosed(x) => x.as_mut().unwrap().queue_incoming_conn(
                common,
                from_address,
                peer,
                child_send_buffer,
                cb_queue,
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
    fn peer_address(&self) -> Result<Option<SockaddrUnix<libc::sockaddr_un>>, Errno>;
    fn bound_address(&self) -> Result<Option<SockaddrUnix<libc::sockaddr_un>>, Errno>;
    fn refresh_file_state(
        &self,
        common: &mut UnixSocketCommon,
        signals: FileSignals,
        cb_queue: &mut CallbackQueue,
    );

    fn close(
        self,
        _common: &mut UnixSocketCommon,
        _cb_queue: &mut CallbackQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        log::warn!("close() while in state {}", std::any::type_name::<Self>());
        (self.into(), Err(Errno::EOPNOTSUPP.into()))
    }

    fn bind(
        &mut self,
        _common: &mut UnixSocketCommon,
        _socket: &Arc<AtomicRefCell<UnixSocket>>,
        _addr: Option<&SockaddrStorage>,
        _rng: impl rand::Rng,
    ) -> Result<(), SyscallError> {
        log::warn!("bind() while in state {}", std::any::type_name::<Self>());
        Err(Errno::EOPNOTSUPP.into())
    }

    fn sendmsg(
        &mut self,
        _common: &mut UnixSocketCommon,
        _socket: &Arc<AtomicRefCell<UnixSocket>>,
        _args: SendmsgArgs,
        _mem: &mut MemoryManager,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        log::warn!("sendmsg() while in state {}", std::any::type_name::<Self>());
        Err(Errno::EOPNOTSUPP.into())
    }

    fn recvmsg(
        &mut self,
        _common: &mut UnixSocketCommon,
        _socket: &Arc<AtomicRefCell<UnixSocket>>,
        _args: RecvmsgArgs,
        _mem: &mut MemoryManager,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<RecvmsgReturn, SyscallError> {
        log::warn!("recvmsg() while in state {}", std::any::type_name::<Self>());
        Err(Errno::EOPNOTSUPP.into())
    }

    fn inform_bytes_read(
        &mut self,
        _common: &mut UnixSocketCommon,
        _num: u64,
        _cb_queue: &mut CallbackQueue,
    ) {
        panic!(
            "inform_bytes_read() while in state {}",
            std::any::type_name::<Self>()
        );
    }

    fn ioctl(
        &mut self,
        _common: &mut UnixSocketCommon,
        _request: IoctlRequest,
        _arg_ptr: ForeignPtr<()>,
        _memory_manager: &mut MemoryManager,
    ) -> SyscallResult {
        log::warn!("ioctl() while in state {}", std::any::type_name::<Self>());
        Err(Errno::EOPNOTSUPP.into())
    }

    fn listen(
        self,
        _common: &mut UnixSocketCommon,
        _backlog: i32,
        _cb_queue: &mut CallbackQueue,
    ) -> (ProtocolState, Result<(), Errno>) {
        log::warn!("listen() while in state {}", std::any::type_name::<Self>());
        (self.into(), Err(Errno::EOPNOTSUPP))
    }

    fn connect(
        self,
        _common: &mut UnixSocketCommon,
        _socket: &Arc<AtomicRefCell<UnixSocket>>,
        _addr: &SockaddrStorage,
        _cb_queue: &mut CallbackQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        log::warn!("connect() while in state {}", std::any::type_name::<Self>());
        (self.into(), Err(Errno::EOPNOTSUPP.into()))
    }

    fn connect_unnamed(
        self,
        _common: &mut UnixSocketCommon,
        _socket: &Arc<AtomicRefCell<UnixSocket>>,
        _peer: Arc<AtomicRefCell<UnixSocket>>,
        _cb_queue: &mut CallbackQueue,
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
        _cb_queue: &mut CallbackQueue,
    ) -> Result<OpenFile, SyscallError> {
        log::warn!("accept() while in state {}", std::any::type_name::<Self>());
        Err(Errno::EOPNOTSUPP.into())
    }

    fn queue_incoming_conn(
        &mut self,
        _common: &mut UnixSocketCommon,
        _from_address: Option<SockaddrUnix<libc::sockaddr_un>>,
        _peer: &Arc<AtomicRefCell<UnixSocket>>,
        _child_send_buffer: &Arc<AtomicRefCell<SharedBuf>>,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<&Arc<AtomicRefCell<UnixSocket>>, IncomingConnError> {
        log::warn!(
            "queue_incoming_conn() while in state {}",
            std::any::type_name::<Self>()
        );
        Err(IncomingConnError::NotSupported)
    }
}

impl Protocol for ConnOrientedInitial {
    fn peer_address(&self) -> Result<Option<SockaddrUnix<libc::sockaddr_un>>, Errno> {
        Err(Errno::ENOTCONN)
    }

    fn bound_address(&self) -> Result<Option<SockaddrUnix<libc::sockaddr_un>>, Errno> {
        Ok(self.bound_addr)
    }

    fn refresh_file_state(
        &self,
        common: &mut UnixSocketCommon,
        signals: FileSignals,
        cb_queue: &mut CallbackQueue,
    ) {
        assert!(!signals.contains(FileSignals::READ_BUFFER_GREW));
        common.update_state(
            /* mask= */ FileState::all(),
            FileState::ACTIVE,
            signals,
            cb_queue,
        );
    }

    fn close(
        self,
        common: &mut UnixSocketCommon,
        cb_queue: &mut CallbackQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        let new_state = ConnOrientedClosed {};
        new_state.refresh_file_state(common, FileSignals::empty(), cb_queue);
        (new_state.into(), common.close(cb_queue))
    }

    fn bind(
        &mut self,
        common: &mut UnixSocketCommon,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        addr: Option<&SockaddrStorage>,
        rng: impl rand::Rng,
    ) -> Result<(), SyscallError> {
        // if already bound
        if self.bound_addr.is_some() {
            return Err(Errno::EINVAL.into());
        }

        self.bound_addr = Some(common.bind(socket, addr, rng)?);
        Ok(())
    }

    fn sendmsg(
        &mut self,
        common: &mut UnixSocketCommon,
        _socket: &Arc<AtomicRefCell<UnixSocket>>,
        args: SendmsgArgs,
        _mem: &mut MemoryManager,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        match (common.socket_type, args.addr) {
            (UnixSocketType::Stream, Some(_)) => Err(Errno::EOPNOTSUPP.into()),
            (UnixSocketType::Stream, None) => Err(Errno::ENOTCONN.into()),
            (UnixSocketType::SeqPacket, _) => Err(Errno::ENOTCONN.into()),
            (UnixSocketType::Dgram, _) => panic!(
                "A dgram unix socket is in the connection-oriented {:?} state",
                std::any::type_name::<Self>()
            ),
        }
    }

    fn recvmsg(
        &mut self,
        common: &mut UnixSocketCommon,
        _socket: &Arc<AtomicRefCell<UnixSocket>>,
        _args: RecvmsgArgs,
        _mem: &mut MemoryManager,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<RecvmsgReturn, SyscallError> {
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
        request: IoctlRequest,
        arg_ptr: ForeignPtr<()>,
        memory_manager: &mut MemoryManager,
    ) -> SyscallResult {
        common.ioctl(request, arg_ptr, memory_manager)
    }

    fn listen(
        self,
        common: &mut UnixSocketCommon,
        backlog: i32,
        cb_queue: &mut CallbackQueue,
    ) -> (ProtocolState, Result<(), Errno>) {
        // it must have already been bound
        let bound_addr = match self.bound_addr {
            Some(x) => x,
            None => return (self.into(), Err(Errno::EINVAL)),
        };

        let new_state = ConnOrientedListening {
            bound_addr,
            queue: VecDeque::new(),
            queue_limit: backlog_to_queue_size(backlog),
        };

        // refresh the socket's file state
        new_state.refresh_file_state(common, FileSignals::empty(), cb_queue);

        (new_state.into(), Ok(()))
    }

    fn connect(
        self,
        common: &mut UnixSocketCommon,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        addr: &SockaddrStorage,
        cb_queue: &mut CallbackQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        let Some(addr) = addr.as_unix() else {
            return (self.into(), Err(Errno::EINVAL.into()));
        };

        // look up the server socket
        let server = match lookup_address(
            &common.namespace.borrow(),
            common.socket_type,
            &addr.as_ref(),
        ) {
            Ok(x) => x,
            Err(e) => return (self.into(), Err(e.into())),
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
            cb_queue,
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
                let err = SyscallError::new_blocked_on_file(
                    File::Socket(Socket::Unix(Arc::clone(&server))),
                    FileState::SOCKET_ALLOWING_CONNECT | FileState::CLOSED,
                    server_mut.supports_sa_restart(),
                );

                return (self.into(), Err(err));
            }
        };

        // our send buffer will be the peer's receive buffer
        let send_buffer = Arc::clone(peer.borrow().recv_buffer());

        let weak = Arc::downgrade(socket);
        let send_buffer_handle = send_buffer.borrow_mut().add_listener(
            BufferState::WRITABLE | BufferState::NO_READERS,
            BufferSignals::empty(),
            move |_, _, cb_queue| {
                if let Some(socket) = weak.upgrade() {
                    socket
                        .borrow_mut()
                        .refresh_file_state(FileSignals::empty(), cb_queue);
                }
            },
        );

        // increment the buffer's writer count
        let writer_handle = send_buffer.borrow_mut().add_writer(cb_queue);

        let weak = Arc::downgrade(socket);
        let recv_buffer_handle = common.recv_buffer.borrow_mut().add_listener(
            BufferState::READABLE | BufferState::NO_WRITERS,
            BufferSignals::BUFFER_GREW,
            move |_, signals, cb_queue| {
                if let Some(socket) = weak.upgrade() {
                    let signals = if signals.contains(BufferSignals::BUFFER_GREW) {
                        FileSignals::READ_BUFFER_GREW
                    } else {
                        FileSignals::empty()
                    };
                    socket.borrow_mut().refresh_file_state(signals, cb_queue);
                }
            },
        );

        // increment the buffer's reader count
        let reader_handle = common.recv_buffer.borrow_mut().add_reader(cb_queue);

        let new_state = ConnOrientedConnected {
            bound_addr: self.bound_addr,
            peer_addr: Some(addr.into_owned()),
            peer: Arc::clone(peer),
            reader_handle,
            writer_handle,
            _recv_buffer_handle: recv_buffer_handle,
            _send_buffer_handle: send_buffer_handle,
        };

        new_state.refresh_file_state(common, FileSignals::empty(), cb_queue);

        (new_state.into(), Ok(()))
    }

    fn connect_unnamed(
        self,
        common: &mut UnixSocketCommon,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        peer: Arc<AtomicRefCell<UnixSocket>>,
        cb_queue: &mut CallbackQueue,
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
                BufferSignals::empty(),
                move |_, _, cb_queue| {
                    if let Some(socket) = weak.upgrade() {
                        socket
                            .borrow_mut()
                            .refresh_file_state(FileSignals::empty(), cb_queue);
                    }
                },
            );

            // increment the buffer's writer count
            writer_handle = send_buffer.borrow_mut().add_writer(cb_queue);
        }

        let weak = Arc::downgrade(socket);
        let recv_buffer_handle = common.recv_buffer.borrow_mut().add_listener(
            BufferState::READABLE | BufferState::NO_WRITERS,
            BufferSignals::BUFFER_GREW,
            move |_, signals, cb_queue| {
                if let Some(socket) = weak.upgrade() {
                    let signals = if signals.contains(BufferSignals::BUFFER_GREW) {
                        FileSignals::READ_BUFFER_GREW
                    } else {
                        FileSignals::empty()
                    };
                    socket.borrow_mut().refresh_file_state(signals, cb_queue);
                }
            },
        );

        // increment the buffer's reader count
        let reader_handle = common.recv_buffer.borrow_mut().add_reader(cb_queue);

        let new_state = ConnOrientedConnected {
            bound_addr: None,
            peer_addr: None,
            peer,
            reader_handle,
            writer_handle,
            _recv_buffer_handle: recv_buffer_handle,
            _send_buffer_handle: send_buffer_handle,
        };

        new_state.refresh_file_state(common, FileSignals::empty(), cb_queue);

        (new_state.into(), Ok(()))
    }

    fn accept(
        &mut self,
        _common: &mut UnixSocketCommon,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<OpenFile, SyscallError> {
        log::warn!("accept() while in state {}", std::any::type_name::<Self>());
        Err(Errno::EINVAL.into())
    }
}

impl Protocol for ConnOrientedListening {
    fn peer_address(&self) -> Result<Option<SockaddrUnix<libc::sockaddr_un>>, Errno> {
        Err(Errno::ENOTCONN)
    }

    fn bound_address(&self) -> Result<Option<SockaddrUnix<libc::sockaddr_un>>, Errno> {
        Ok(Some(self.bound_addr))
    }

    fn refresh_file_state(
        &self,
        common: &mut UnixSocketCommon,
        signals: FileSignals,
        cb_queue: &mut CallbackQueue,
    ) {
        let mut new_state = FileState::ACTIVE;

        // socket is readable if the queue is not empty
        new_state.set(FileState::READABLE, !self.queue.is_empty());

        // socket allows connections if the queue is not full
        new_state.set(FileState::SOCKET_ALLOWING_CONNECT, !self.queue_is_full());

        // Note: This can cause a thundering-herd condition where multiple blocked connect() calls
        // are all notified at the same time, even if there isn't enough space to allow all of them.
        // In practice this should be uncommon so we don't worry about it, and avoids requiring that
        // the server keep a list of all connecting clients.

        common.update_state(
            /* mask= */ FileState::all(),
            new_state,
            signals,
            cb_queue,
        );
    }

    fn close(
        self,
        common: &mut UnixSocketCommon,
        cb_queue: &mut CallbackQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        for sock in self.queue {
            // close all queued sockets
            if let Err(e) = sock.borrow_mut().close(cb_queue) {
                log::warn!("Unexpected error while closing queued unix socket: {:?}", e);
            }
        }

        let new_state = ConnOrientedClosed {};
        new_state.refresh_file_state(common, FileSignals::empty(), cb_queue);
        (new_state.into(), common.close(cb_queue))
    }

    fn listen(
        mut self,
        common: &mut UnixSocketCommon,
        backlog: i32,
        cb_queue: &mut CallbackQueue,
    ) -> (ProtocolState, Result<(), Errno>) {
        self.queue_limit = backlog_to_queue_size(backlog);

        // refresh the socket's file state
        self.refresh_file_state(common, FileSignals::empty(), cb_queue);

        (self.into(), Ok(()))
    }

    fn connect(
        self,
        _common: &mut UnixSocketCommon,
        _socket: &Arc<AtomicRefCell<UnixSocket>>,
        _addr: &SockaddrStorage,
        _cb_queue: &mut CallbackQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        (self.into(), Err(Errno::EINVAL.into()))
    }

    fn accept(
        &mut self,
        common: &mut UnixSocketCommon,
        cb_queue: &mut CallbackQueue,
    ) -> Result<OpenFile, SyscallError> {
        let child_socket = match self.queue.pop_front() {
            Some(x) => x,
            None => return Err(Errno::EWOULDBLOCK.into()),
        };

        // refresh the socket's file state
        self.refresh_file_state(common, FileSignals::empty(), cb_queue);

        Ok(OpenFile::new(File::Socket(Socket::Unix(child_socket))))
    }

    fn queue_incoming_conn(
        &mut self,
        common: &mut UnixSocketCommon,
        from_address: Option<SockaddrUnix<libc::sockaddr_un>>,
        peer: &Arc<AtomicRefCell<UnixSocket>>,
        child_send_buffer: &Arc<AtomicRefCell<SharedBuf>>,
        cb_queue: &mut CallbackQueue,
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
            BufferSignals::empty(),
            move |_, _, cb_queue| {
                if let Some(socket) = weak.upgrade() {
                    socket
                        .borrow_mut()
                        .refresh_file_state(FileSignals::empty(), cb_queue);
                }
            },
        );

        // increment the buffer's writer count
        let writer_handle = child_send_buffer.borrow_mut().add_writer(cb_queue);

        let weak = Arc::downgrade(&child_socket);
        let recv_buffer_handle = child_recv_buffer.borrow_mut().add_listener(
            BufferState::READABLE | BufferState::NO_WRITERS,
            BufferSignals::BUFFER_GREW,
            move |_, signals, cb_queue| {
                if let Some(socket) = weak.upgrade() {
                    let signals = if signals.contains(BufferSignals::BUFFER_GREW) {
                        FileSignals::READ_BUFFER_GREW
                    } else {
                        FileSignals::empty()
                    };
                    socket.borrow_mut().refresh_file_state(signals, cb_queue);
                }
            },
        );

        // increment the buffer's reader count
        let reader_handle = child_recv_buffer.borrow_mut().add_reader(cb_queue);

        let new_child_state = ConnOrientedConnected {
            // use the parent's bind address
            bound_addr: Some(self.bound_addr),
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
        cb_queue.add(move |cb_queue| {
            if let Some(child_socket) = weak.upgrade() {
                child_socket
                    .borrow_mut()
                    .refresh_file_state(FileSignals::empty(), cb_queue);
            }
        });

        // add the child socket to the accept queue
        self.queue.push_back(child_socket);

        // refresh the server socket's file state
        self.refresh_file_state(common, FileSignals::empty(), cb_queue);

        // return a reference to the enqueued child socket
        Ok(self.queue.back().unwrap())
    }
}

impl Protocol for ConnOrientedConnected {
    fn peer_address(&self) -> Result<Option<SockaddrUnix<libc::sockaddr_un>>, Errno> {
        Ok(self.peer_addr)
    }

    fn bound_address(&self) -> Result<Option<SockaddrUnix<libc::sockaddr_un>>, Errno> {
        Ok(self.bound_addr)
    }

    fn refresh_file_state(
        &self,
        common: &mut UnixSocketCommon,
        signals: FileSignals,
        cb_queue: &mut CallbackQueue,
    ) {
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

        common.update_state(
            /* mask= */ FileState::all(),
            new_state,
            signals,
            cb_queue,
        );
    }

    fn close(
        self,
        common: &mut UnixSocketCommon,
        cb_queue: &mut CallbackQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        // inform the buffer that there is one fewer readers
        common
            .recv_buffer
            .borrow_mut()
            .remove_reader(self.reader_handle, cb_queue);

        // inform the buffer that there is one fewer writers
        self.peer
            .borrow()
            .recv_buffer()
            .borrow_mut()
            .remove_writer(self.writer_handle, cb_queue);

        let new_state = ConnOrientedClosed {};
        new_state.refresh_file_state(common, FileSignals::empty(), cb_queue);
        (new_state.into(), common.close(cb_queue))
    }

    fn sendmsg(
        &mut self,
        common: &mut UnixSocketCommon,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        args: SendmsgArgs,
        mem: &mut MemoryManager,
        cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        if !args.control_ptr.ptr().is_null() {
            log::debug!("Unix sockets don't yet support control data for sendmsg()");
            return Err(Errno::EINVAL.into());
        }

        let recv_socket = common.resolve_destination(Some(&self.peer), args.addr)?;
        let rv = common.sendmsg(socket, args.iovs, args.flags, &recv_socket, mem, cb_queue)?;

        self.refresh_file_state(common, FileSignals::empty(), cb_queue);

        Ok(rv.try_into().unwrap())
    }

    fn recvmsg(
        &mut self,
        common: &mut UnixSocketCommon,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        args: RecvmsgArgs,
        mem: &mut MemoryManager,
        cb_queue: &mut CallbackQueue,
    ) -> Result<RecvmsgReturn, SyscallError> {
        if !args.control_ptr.ptr().is_null() {
            log::debug!("Unix sockets don't yet support control data for recvmsg()");
            return Err(Errno::EINVAL.into());
        }

        let (rv, num_removed_from_buf, msg_flags) =
            common.recvmsg(socket, args.iovs, args.flags, mem, cb_queue)?;
        let num_removed_from_buf = u64::try_from(num_removed_from_buf).unwrap();

        if num_removed_from_buf > 0 {
            // defer informing the peer until we're done processing the current socket
            let peer = Arc::clone(&self.peer);
            cb_queue.add(move |cb_queue| {
                peer.borrow_mut()
                    .inform_bytes_read(num_removed_from_buf, cb_queue);
            });
        }

        self.refresh_file_state(common, FileSignals::empty(), cb_queue);

        Ok(RecvmsgReturn {
            return_val: rv.try_into().unwrap(),
            addr: self.peer_addr.map(Into::into),
            msg_flags,
            control_len: 0,
        })
    }

    fn inform_bytes_read(
        &mut self,
        common: &mut UnixSocketCommon,
        num: u64,
        cb_queue: &mut CallbackQueue,
    ) {
        common.sent_len = common.sent_len.checked_sub(num).unwrap();
        self.refresh_file_state(common, FileSignals::empty(), cb_queue);
    }

    fn ioctl(
        &mut self,
        common: &mut UnixSocketCommon,
        request: IoctlRequest,
        arg_ptr: ForeignPtr<()>,
        memory_manager: &mut MemoryManager,
    ) -> SyscallResult {
        common.ioctl(request, arg_ptr, memory_manager)
    }

    fn accept(
        &mut self,
        _common: &mut UnixSocketCommon,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<OpenFile, SyscallError> {
        log::warn!("accept() while in state {}", std::any::type_name::<Self>());
        Err(Errno::EINVAL.into())
    }
}

impl Protocol for ConnOrientedClosed {
    fn peer_address(&self) -> Result<Option<SockaddrUnix<libc::sockaddr_un>>, Errno> {
        Err(Errno::ENOTCONN)
    }

    fn bound_address(&self) -> Result<Option<SockaddrUnix<libc::sockaddr_un>>, Errno> {
        Err(Errno::EBADFD)
    }

    fn refresh_file_state(
        &self,
        common: &mut UnixSocketCommon,
        signals: FileSignals,
        cb_queue: &mut CallbackQueue,
    ) {
        assert!(!signals.contains(FileSignals::READ_BUFFER_GREW));
        common.update_state(
            /* mask= */ FileState::all(),
            FileState::CLOSED,
            signals,
            cb_queue,
        );
    }

    fn close(
        self,
        _common: &mut UnixSocketCommon,
        _cb_queue: &mut CallbackQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        // why are we trying to close an already closed file? we probably want a bt here...
        panic!("Trying to close an already closed socket");
    }

    fn inform_bytes_read(
        &mut self,
        _common: &mut UnixSocketCommon,
        _num: u64,
        _cb_queue: &mut CallbackQueue,
    ) {
        // do nothing since we're already closed
    }
}

impl Protocol for ConnLessInitial {
    fn peer_address(&self) -> Result<Option<SockaddrUnix<libc::sockaddr_un>>, Errno> {
        match self.peer {
            Some(_) => Ok(self.peer_addr),
            None => Err(Errno::ENOTCONN),
        }
    }

    fn bound_address(&self) -> Result<Option<SockaddrUnix<libc::sockaddr_un>>, Errno> {
        Ok(self.bound_addr)
    }

    fn refresh_file_state(
        &self,
        common: &mut UnixSocketCommon,
        signals: FileSignals,
        cb_queue: &mut CallbackQueue,
    ) {
        let mut new_state = FileState::ACTIVE;

        {
            let recv_buffer = common.recv_buffer.borrow();

            new_state.set(FileState::READABLE, recv_buffer.has_data());
            new_state.set(FileState::WRITABLE, common.sent_len < common.send_limit);
        }

        common.update_state(
            /* mask= */ FileState::all(),
            new_state,
            signals,
            cb_queue,
        );
    }

    fn close(
        self,
        common: &mut UnixSocketCommon,
        cb_queue: &mut CallbackQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        // inform the buffer that there is one fewer readers
        common
            .recv_buffer
            .borrow_mut()
            .remove_reader(self.reader_handle, cb_queue);

        for byte_data in self.recv_data.into_iter() {
            // defer informing the senders until we're done processing the current socket
            cb_queue.add(move |cb_queue| {
                byte_data
                    .from_socket
                    .borrow_mut()
                    .inform_bytes_read(byte_data.num_bytes, cb_queue);
            });
        }

        let new_state = ConnLessClosed {};
        new_state.refresh_file_state(common, FileSignals::empty(), cb_queue);
        (new_state.into(), common.close(cb_queue))
    }

    fn bind(
        &mut self,
        common: &mut UnixSocketCommon,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        addr: Option<&SockaddrStorage>,
        rng: impl rand::Rng,
    ) -> Result<(), SyscallError> {
        // if already bound
        if self.bound_addr.is_some() {
            return Err(Errno::EINVAL.into());
        }

        self.bound_addr = Some(common.bind(socket, addr, rng)?);
        Ok(())
    }

    fn sendmsg(
        &mut self,
        common: &mut UnixSocketCommon,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        args: SendmsgArgs,
        mem: &mut MemoryManager,
        cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        if !args.control_ptr.ptr().is_null() {
            log::debug!("Unix sockets don't yet support control data for sendmsg()");
            return Err(Errno::EINVAL.into());
        }

        let recv_socket = common.resolve_destination(self.peer.as_ref(), args.addr)?;
        let rv = common.sendmsg(socket, args.iovs, args.flags, &recv_socket, mem, cb_queue)?;

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

        self.refresh_file_state(common, FileSignals::empty(), cb_queue);

        Ok(rv.try_into().unwrap())
    }

    fn recvmsg(
        &mut self,
        common: &mut UnixSocketCommon,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        args: RecvmsgArgs,
        mem: &mut MemoryManager,
        cb_queue: &mut CallbackQueue,
    ) -> Result<RecvmsgReturn, SyscallError> {
        if !args.control_ptr.ptr().is_null() {
            log::debug!("Unix sockets don't yet support control data for recvmsg()");
            return Err(Errno::EINVAL.into());
        }

        let (rv, num_removed_from_buf, msg_flags) =
            common.recvmsg(socket, args.iovs, args.flags, mem, cb_queue)?;
        let num_removed_from_buf = u64::try_from(num_removed_from_buf).unwrap();

        let byte_data = self.recv_data.pop_front().unwrap();
        assert!(num_removed_from_buf == byte_data.num_bytes);

        // defer informing the sender until we're done processing the current socket
        cb_queue.add(move |cb_queue| {
            byte_data
                .from_socket
                .borrow_mut()
                .inform_bytes_read(byte_data.num_bytes, cb_queue);
        });

        self.refresh_file_state(common, FileSignals::empty(), cb_queue);

        Ok(RecvmsgReturn {
            return_val: rv.try_into().unwrap(),
            addr: byte_data.from_addr.map(Into::into),
            msg_flags,
            control_len: 0,
        })
    }

    fn inform_bytes_read(
        &mut self,
        common: &mut UnixSocketCommon,
        num: u64,
        cb_queue: &mut CallbackQueue,
    ) {
        common.sent_len = common.sent_len.checked_sub(num).unwrap();
        self.refresh_file_state(common, FileSignals::empty(), cb_queue);
    }

    fn ioctl(
        &mut self,
        common: &mut UnixSocketCommon,
        request: IoctlRequest,
        arg_ptr: ForeignPtr<()>,
        memory_manager: &mut MemoryManager,
    ) -> SyscallResult {
        common.ioctl(request, arg_ptr, memory_manager)
    }

    fn connect(
        self,
        common: &mut UnixSocketCommon,
        _socket: &Arc<AtomicRefCell<UnixSocket>>,
        addr: &SockaddrStorage,
        cb_queue: &mut CallbackQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        // TODO: support AF_UNSPEC to disassociate
        let Some(addr) = addr.as_unix() else {
            return (self.into(), Err(Errno::EINVAL.into()));
        };

        // find the socket bound at the address
        let peer = match lookup_address(&common.namespace.borrow(), common.socket_type, &addr) {
            Ok(x) => x,
            Err(e) => return (self.into(), Err(e.into())),
        };

        let new_state = Self {
            peer_addr: Some(addr.into_owned()),
            peer: Some(peer),
            ..self
        };

        new_state.refresh_file_state(common, FileSignals::empty(), cb_queue);

        (new_state.into(), Ok(()))
    }

    fn connect_unnamed(
        self,
        common: &mut UnixSocketCommon,
        _socket: &Arc<AtomicRefCell<UnixSocket>>,
        peer: Arc<AtomicRefCell<UnixSocket>>,
        cb_queue: &mut CallbackQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        assert!(self.peer_addr.is_none());
        assert!(self.bound_addr.is_none());

        let new_state = Self {
            bound_addr: None,
            peer_addr: None,
            peer: Some(peer),
            ..self
        };

        new_state.refresh_file_state(common, FileSignals::empty(), cb_queue);

        (new_state.into(), Ok(()))
    }
}

impl Protocol for ConnLessClosed {
    fn peer_address(&self) -> Result<Option<SockaddrUnix<libc::sockaddr_un>>, Errno> {
        Ok(None)
    }

    fn bound_address(&self) -> Result<Option<SockaddrUnix<libc::sockaddr_un>>, Errno> {
        Ok(None)
    }

    fn refresh_file_state(
        &self,
        common: &mut UnixSocketCommon,
        signals: FileSignals,
        cb_queue: &mut CallbackQueue,
    ) {
        assert!(!signals.contains(FileSignals::READ_BUFFER_GREW));
        common.update_state(
            /* mask= */ FileState::all(),
            FileState::CLOSED,
            signals,
            cb_queue,
        );
    }

    fn close(
        self,
        _common: &mut UnixSocketCommon,
        _cb_queue: &mut CallbackQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        // why are we trying to close an already closed file? we probably want a bt here...
        panic!("Trying to close an already closed socket");
    }

    fn inform_bytes_read(
        &mut self,
        _common: &mut UnixSocketCommon,
        _num: u64,
        _cb_queue: &mut CallbackQueue,
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
    pub fn supports_sa_restart(&self) -> bool {
        true
    }

    pub fn close(&mut self, cb_queue: &mut CallbackQueue) -> Result<(), SyscallError> {
        // check that the CLOSED flag was set by the protocol state
        if !self.state.contains(FileState::CLOSED) {
            // set the flag here since we missed doing it before
            // do this before the below panic, otherwise rust gives us warnings
            self.update_state(
                /* mask= */ FileState::all(),
                FileState::CLOSED,
                FileSignals::empty(),
                cb_queue,
            );

            // panic in debug builds since the backtrace will be helpful for debugging
            debug_panic!("When closing a unix socket, the CLOSED flag was not set");
        }

        Ok(())
    }

    pub fn bind(
        &mut self,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        addr: Option<&SockaddrStorage>,
        rng: impl rand::Rng,
    ) -> Result<SockaddrUnix<libc::sockaddr_un>, SyscallError> {
        // get the unix address
        let Some(addr) = addr.and_then(|x| x.as_unix()) else {
            log::warn!(
                "Attempted to bind unix socket to non-unix address {:?}",
                addr
            );
            return Err(Errno::EINVAL.into());
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
                Ok(()) => addr.into_owned(),
                // address is in use
                Err(_) => return Err(Errno::EADDRINUSE.into()),
            }
        } else if addr.is_unnamed() {
            // if given an "unnamed" address
            let namespace = Arc::clone(&self.namespace);
            match AbstractUnixNamespace::autobind(
                &namespace,
                self.socket_type,
                socket,
                &mut self.event_source,
                rng,
            ) {
                Ok(ref name) => SockaddrUnix::new_abstract(name).unwrap(),
                Err(_) => return Err(Errno::EADDRINUSE.into()),
            }
        } else {
            log::warn!("Only abstract names are currently supported for unix sockets");
            return Err(Errno::ENOTSUP.into());
        };

        Ok(bound_addr)
    }

    pub fn resolve_destination(
        &self,
        peer: Option<&Arc<AtomicRefCell<UnixSocket>>>,
        addr: Option<SockaddrStorage>,
    ) -> Result<Arc<AtomicRefCell<UnixSocket>>, SyscallError> {
        let addr = match addr {
            Some(ref addr) => Some(addr.as_unix().ok_or(Errno::EINVAL)?),
            None => None,
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
            Some(x) => Arc::clone(x),
            None => {
                // look up the socket from the address name
                let recv_socket =
                    lookup_address(&self.namespace.borrow(), self.socket_type, &addr.unwrap())?;
                // store an Arc of the recv buffer
                Arc::clone(&recv_socket)
            }
        };

        Ok(peer)
    }

    pub fn sendmsg(
        &mut self,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        iovs: &[IoVec],
        flags: libc::c_int,
        peer: &Arc<AtomicRefCell<UnixSocket>>,
        mem: &mut MemoryManager,
        cb_queue: &mut CallbackQueue,
    ) -> Result<usize, SyscallError> {
        // MSG_NOSIGNAL is currently a no-op, since we haven't implemented the behavior
        // it's meant to disable.
        // TODO: Once we've implemented generating a SIGPIPE when the peer on a
        // stream-oriented socket has closed the connection, MSG_NOSIGNAL should
        // disable it.
        // Ignore the MSG_TRUNC flag since it doesn't do anything when sending.
        let supported_flags = MsgFlags::MSG_DONTWAIT | MsgFlags::MSG_NOSIGNAL | MsgFlags::MSG_TRUNC;

        // if there's a flag we don't support, it's probably best to raise an error rather than do
        // the wrong thing
        let Some(mut flags) = MsgFlags::from_bits(flags) else {
            log::warn!("Unrecognized send flags: {:#b}", flags);
            return Err(Errno::EINVAL.into());
        };
        if flags.intersects(!supported_flags) {
            log::warn!("Unsupported send flags: {:?}", flags);
            return Err(Errno::EINVAL.into());
        }

        if self.status.contains(FileStatus::NONBLOCK) {
            flags.insert(MsgFlags::MSG_DONTWAIT);
        }

        // run in a closure so that an early return doesn't return from the syscall handler
        let result = (|| {
            let peer_ref = peer.borrow();
            let mut send_buffer = peer_ref.recv_buffer().borrow_mut();

            // if the buffer has no readers, the destination socket is closed
            if send_buffer.num_readers() == 0 {
                return Err(match self.socket_type {
                    // connection-oriented socket
                    UnixSocketType::Stream | UnixSocketType::SeqPacket => Errno::EPIPE,
                    // connectionless socket
                    UnixSocketType::Dgram => Errno::ECONNREFUSED,
                });
            }

            let len = iovs.iter().map(|x| x.len).sum::<libc::size_t>();

            // we keep track of the send buffer size manually, since the unix socket buffers all have
            // usize::MAX length
            let space_available = self
                .send_limit
                .saturating_sub(self.sent_len)
                .try_into()
                .unwrap();

            if space_available == 0 {
                return Err(Errno::EAGAIN);
            }

            let len = match self.socket_type {
                UnixSocketType::Stream => std::cmp::min(len, space_available),
                UnixSocketType::Dgram | UnixSocketType::SeqPacket => {
                    if len <= space_available {
                        len
                    } else if len <= self.send_limit.try_into().unwrap() {
                        // we can send this when the buffer has more space available
                        return Err(Errno::EAGAIN);
                    } else {
                        // we could never send this message
                        return Err(Errno::EMSGSIZE);
                    }
                }
            };

            let reader = IoVecReader::new(iovs, mem);
            let reader = reader.take(len.try_into().unwrap());

            let num_copied = match self.socket_type {
                UnixSocketType::Stream => {
                    if len == 0 {
                        0
                    } else {
                        send_buffer
                            .write_stream(reader, len, cb_queue)
                            .map_err(|e| Errno::try_from(e).unwrap())?
                    }
                }
                UnixSocketType::Dgram | UnixSocketType::SeqPacket => {
                    send_buffer
                        .write_packet(reader, len, cb_queue)
                        .map_err(|e| Errno::try_from(e).unwrap())?;
                    len
                }
            };

            // if we successfully sent bytes, update the sent count
            self.sent_len += u64::try_from(num_copied).unwrap();

            Ok(num_copied)
        })();

        // if the syscall would block and we don't have the MSG_DONTWAIT flag
        if result.as_ref().err() == Some(&Errno::EWOULDBLOCK)
            && !flags.contains(MsgFlags::MSG_DONTWAIT)
        {
            return Err(SyscallError::new_blocked_on_file(
                File::Socket(Socket::Unix(socket.clone())),
                FileState::WRITABLE,
                self.supports_sa_restart(),
            ));
        }

        Ok(result?)
    }

    pub fn recvmsg(
        &mut self,
        socket: &Arc<AtomicRefCell<UnixSocket>>,
        iovs: &[IoVec],
        flags: libc::c_int,
        mem: &mut MemoryManager,
        cb_queue: &mut CallbackQueue,
    ) -> Result<(usize, usize, libc::c_int), SyscallError> {
        let supported_flags = MsgFlags::MSG_DONTWAIT | MsgFlags::MSG_TRUNC;

        // if there's a flag we don't support, it's probably best to raise an error rather than do
        // the wrong thing
        let Some(mut flags) = MsgFlags::from_bits(flags) else {
            log::warn!("Unrecognized recv flags: {:#b}", flags);
            return Err(Errno::EINVAL.into());
        };
        if flags.intersects(!supported_flags) {
            log::warn!("Unsupported recv flags: {:?}", flags);
            return Err(Errno::EINVAL.into());
        }

        if self.status.contains(FileStatus::NONBLOCK) {
            flags.insert(MsgFlags::MSG_DONTWAIT);
        }

        // run in a closure so that an early return doesn't return from the syscall handler
        let result = (|| {
            let mut recv_buffer = self.recv_buffer.borrow_mut();

            // the read would block if all:
            //  1. the recv buffer has no data
            //  2. it's a connectionless socket OR the connection-oriented destination socket is not
            //     closed
            if !recv_buffer.has_data()
                && (self.socket_type == UnixSocketType::Dgram || recv_buffer.num_writers() > 0)
            {
                // return EWOULDBLOCK even if 'bytes' has length 0
                return Err(Errno::EWOULDBLOCK);
            }

            let writer = IoVecWriter::new(iovs, mem);

            let (num_copied, num_removed_from_buf) = recv_buffer
                .read(writer, cb_queue)
                .map_err(|e| Errno::try_from(e).unwrap())?;

            let mut msg_flags = 0;

            if flags.contains(MsgFlags::MSG_TRUNC)
                && [UnixSocketType::Dgram, UnixSocketType::SeqPacket].contains(&self.socket_type)
            {
                if num_copied < num_removed_from_buf {
                    msg_flags |= libc::MSG_TRUNC;
                }

                // we're a message-based socket and MSG_TRUNC is set, so return the total size of
                // the message, not the number of bytes we read
                Ok((num_removed_from_buf, num_removed_from_buf, msg_flags))
            } else {
                // We're a stream-based socket. Unlike TCP sockets, unix stream sockets ignore the
                // MSG_TRUNC flag.
                Ok((num_copied, num_removed_from_buf, msg_flags))
            }
        })();

        // if the syscall would block and we don't have the MSG_DONTWAIT flag
        if result.as_ref().err() == Some(&Errno::EWOULDBLOCK)
            && !flags.contains(MsgFlags::MSG_DONTWAIT)
        {
            return Err(SyscallError::new_blocked_on_file(
                File::Socket(Socket::Unix(socket.clone())),
                FileState::READABLE,
                self.supports_sa_restart(),
            ));
        }

        Ok(result?)
    }

    pub fn ioctl(
        &mut self,
        request: IoctlRequest,
        _arg_ptr: ForeignPtr<()>,
        _memory_manager: &mut MemoryManager,
    ) -> SyscallResult {
        log::warn!("We do not yet handle ioctl request {request:?} on unix sockets");
        Err(Errno::EINVAL.into())
    }

    fn update_state(
        &mut self,
        mask: FileState,
        state: FileState,
        signals: FileSignals,
        cb_queue: &mut CallbackQueue,
    ) {
        let old_state = self.state;

        // remove the masked flags, then copy the masked flags
        self.state.remove(mask);
        self.state.insert(state & mask);

        self.handle_state_change(old_state, signals, cb_queue);
    }

    fn handle_state_change(
        &mut self,
        old_state: FileState,
        signals: FileSignals,
        cb_queue: &mut CallbackQueue,
    ) {
        let states_changed = self.state ^ old_state;

        // if nothing changed
        if states_changed.is_empty() && signals.is_empty() {
            return;
        }

        self.event_source
            .notify_listeners(self.state, states_changed, signals, cb_queue);
    }
}

fn lookup_address(
    namespace: &AbstractUnixNamespace,
    socket_type: UnixSocketType,
    addr: &SockaddrUnix<&libc::sockaddr_un>,
) -> Result<Arc<AtomicRefCell<UnixSocket>>, linux_api::errno::Errno> {
    // if an abstract address
    if let Some(name) = addr.as_abstract() {
        // look up the socket from the address name
        namespace
            .lookup(socket_type, name)
            .ok_or(linux_api::errno::Errno::ECONNREFUSED)
    } else {
        warn_once_then_debug!("Unix sockets with pathname addresses are not yet supported");
        Err(linux_api::errno::Errno::ENOENT)
    }
}

fn backlog_to_queue_size(backlog: i32) -> u32 {
    // linux also makes this cast, so negative backlogs wrap around to large positive backlogs
    // https://elixir.free-electrons.com/linux/v5.11.22/source/net/unix/af_unix.c#L628
    let backlog = backlog as u32;

    // the linux '__sys_listen()' applies the somaxconn max to all protocols, including unix sockets
    let queue_limit = std::cmp::min(backlog, c::SHADOW_SOMAXCONN);

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

struct ByteData {
    from_socket: Arc<AtomicRefCell<UnixSocket>>,
    from_addr: Option<SockaddrUnix<libc::sockaddr_un>>,
    num_bytes: u64,
}
