use std::io::Read;
use std::sync::{Arc, Weak};

use atomic_refcell::AtomicRefCell;
use linux_api::errno::Errno;
use linux_api::ioctls::IoctlRequest;
use nix::sys::socket::{MsgFlags, Shutdown};
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::cshadow as c;
use crate::host::descriptor::listener::{StateEventSource, StateListenHandle, StateListenerFilter};
use crate::host::descriptor::shared_buf::SharedBuf;
use crate::host::descriptor::socket::{SendmsgArgs, Socket};
use crate::host::descriptor::{File, FileMode, FileSignals, FileState, FileStatus, SyscallResult};
use crate::host::memory_manager::MemoryManager;
use crate::host::network::namespace::NetworkNamespace;
use crate::host::syscall::io::{IoVec, IoVecReader};
use crate::host::syscall::types::SyscallError;
use crate::utility::callback_queue::CallbackQueue;
use crate::utility::sockaddr::SockaddrStorage;
use crate::utility::HostTreePointer;

// this constant is copied from UNIX_SOCKET_DEFAULT_BUFFER_SIZE
const NETLINK_SOCKET_DEFAULT_BUFFER_SIZE: u64 = 212_992;

pub struct NetlinkSocket {
    /// Data and functionality that is general for all states.
    common: NetlinkSocketCommon,
    /// State-specific data and functionality.
    protocol_state: ProtocolState,
}

impl NetlinkSocket {
    pub fn new(
        status: FileStatus,
        _socket_type: NetlinkSocketType,
        _family: NetlinkFamily,
    ) -> Arc<AtomicRefCell<Self>> {
        Arc::new_cyclic(|weak| {
            // each socket tracks its own send limit
            let buffer = SharedBuf::new(usize::MAX);
            let buffer = Arc::new(AtomicRefCell::new(buffer));

            let mut common = NetlinkSocketCommon {
                buffer,
                send_limit: NETLINK_SOCKET_DEFAULT_BUFFER_SIZE,
                sent_len: 0,
                event_source: StateEventSource::new(),
                state: FileState::ACTIVE,
                status,
                has_open_file: false,
            };
            let protocol_state = ProtocolState::new(&mut common, weak);
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
        unimplemented!()
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

    pub fn address_family(&self) -> nix::sys::socket::AddressFamily {
        unimplemented!()
    }

    pub fn close(&mut self, cb_queue: &mut CallbackQueue) -> Result<(), SyscallError> {
        unimplemented!()
    }

    pub fn shutdown(
        &mut self,
        _how: Shutdown,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<(), SyscallError> {
        // We follow the same approach as UnixSocket
        log::warn!("shutdown() syscall not yet supported for netlink sockets; Returning ENOSYS");
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
        // We follow the same approach as UnixSocket
        log::warn!("getsockopt() syscall not yet supported for netlink sockets; Returning ENOSYS");
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
        // We follow the same approach as UnixSocket
        log::warn!("setsockopt() syscall not yet supported for netlink sockets; Returning ENOSYS");
        Err(Errno::ENOSYS.into())
    }

    pub fn bind(
        socket: &Arc<AtomicRefCell<Self>>,
        addr: Option<&SockaddrStorage>,
        _net_ns: &NetworkNamespace,
        rng: impl rand::Rng,
    ) -> SyscallResult {
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
        // we could call NetlinkSocket::recvmsg() here, but for now we expect that there are no code
        // paths that would call NetlinkSocket::readv() since the readv() syscall handler should have
        // called NetlinkSocket::recvmsg() instead
        panic!("Called NetlinkSocket::readv() on a netlink socket.");
    }

    pub fn writev(
        &mut self,
        _iovs: &[IoVec],
        _offset: Option<libc::off_t>,
        _flags: libc::c_int,
        _mem: &mut MemoryManager,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        // we could call NetlinkSocket::sendmsg() here, but for now we expect that there are no code
        // paths that would call NetlinkSocket::writev() since the writev() syscall handler should have
        // called NetlinkSocket::sendmsg() instead
        panic!("Called NetlinkSocket::writev() on a netlink socket");
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

    pub fn ioctl(
        &mut self,
        request: IoctlRequest,
        _arg_ptr: ForeignPtr<()>,
        _memory_manager: &mut MemoryManager,
    ) -> SyscallResult {
        // We follow the same approach as UnixSocket
        log::warn!("We do not yet handle ioctl request {request:?} on unix sockets");
        Err(Errno::EINVAL.into())
    }

    pub fn add_listener(
        &mut self,
        monitoring: FileState,
        monitoring_signals: FileSignals,
        filter: StateListenerFilter,
        notify_fn: impl Fn(FileState, FileState, FileSignals, &mut CallbackQueue)
            + Send
            + Sync
            + 'static,
    ) -> StateListenHandle {
        self.common
            .event_source
            .add_listener(monitoring, monitoring_signals, filter, notify_fn)
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

struct InitialState {
    // Indicate that if the socket is already bound or not. We don't keep the bound address so that
    // we won't need to fill it.
    is_bound: bool,
}
struct ClosedState {}
/// The current protocol state of the netlink socket. An `Option` is required for each variant so that
/// the inner state object can be removed, transformed into a new state, and then re-added as a
/// different variant.
enum ProtocolState {
    Initial(Option<InitialState>),
    Closed(Option<ClosedState>),
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
state_upcast!(InitialState, ProtocolState::Initial);
state_upcast!(ClosedState, ProtocolState::Closed);

impl ProtocolState {
    fn new(common: &mut NetlinkSocketCommon, socket: &Weak<AtomicRefCell<NetlinkSocket>>) -> Self {
        ProtocolState::Initial(Some(InitialState { is_bound: false }))
    }

    fn bind(
        &mut self,
        common: &mut NetlinkSocketCommon,
        socket: &Arc<AtomicRefCell<NetlinkSocket>>,
        addr: Option<&SockaddrStorage>,
        rng: impl rand::Rng,
    ) -> SyscallResult {
        match self {
            Self::Initial(x) => x.as_mut().unwrap().bind(common, socket, addr, rng),
            Self::Closed(x) => x.as_mut().unwrap().bind(common, socket, addr, rng),
        }
    }

    fn sendmsg(
        &mut self,
        common: &mut NetlinkSocketCommon,
        socket: &Arc<AtomicRefCell<NetlinkSocket>>,
        args: SendmsgArgs,
        mem: &mut MemoryManager,
        cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        match self {
            Self::Initial(x) => x
                .as_mut()
                .unwrap()
                .sendmsg(common, socket, args, mem, cb_queue),
            Self::Closed(x) => x
                .as_mut()
                .unwrap()
                .sendmsg(common, socket, args, mem, cb_queue),
        }
    }
}

impl InitialState {
    fn refresh_file_state(&self, common: &mut NetlinkSocketCommon, cb_queue: &mut CallbackQueue) {
        let mut new_state = FileState::ACTIVE;

        {
            let buffer = common.buffer.borrow();

            new_state.set(FileState::READABLE, buffer.has_data());
            new_state.set(FileState::WRITABLE, common.sent_len < common.send_limit);
        }

        common.copy_state(/* mask= */ FileState::all(), new_state, cb_queue);
    }

    fn bind(
        &mut self,
        _common: &mut NetlinkSocketCommon,
        _socket: &Arc<AtomicRefCell<NetlinkSocket>>,
        addr: Option<&SockaddrStorage>,
        _rng: impl rand::Rng,
    ) -> SyscallResult {
        // if already bound
        if self.is_bound {
            return Err(Errno::EINVAL.into());
        }

        // get the netlink address
        let Some(addr) = addr.and_then(|x| x.as_netlink()) else {
            log::warn!(
                "Attempted to bind netlink socket to non-netlink address {:?}",
                addr
            );
            return Err(Errno::EINVAL.into());
        };
        // remember that the socket is bound
        self.is_bound = true;

        // According to netlink(7), if the pid is zero, the kernel takes care of assigning it, but
        // we will leave it untouched at the moment. We can implement the assignment later when we
        // want to support it.

        // According to netlink(7), if the groups is non-zero, it means that the socket wants to
        // listen to some groups. Since we don't support broadcasting to groups yet, we will emit
        // the error here.
        if addr.groups() != 0 {
            log::warn!(
                "Attempted to bind netlink socket to an address with non-zero groups {}",
                addr.groups()
            );
            return Err(Errno::EINVAL.into());
        }

        Ok(0.into())
    }

    fn sendmsg(
        &mut self,
        common: &mut NetlinkSocketCommon,
        socket: &Arc<AtomicRefCell<NetlinkSocket>>,
        args: SendmsgArgs,
        mem: &mut MemoryManager,
        cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        if !args.control_ptr.ptr().is_null() {
            log::debug!("Netlink sockets don't yet support control data for sendmsg()");
            return Err(Errno::EINVAL.into());
        }

        let Some(addr) = args.addr else {
            log::warn!("Attempted to send in netlink socket without destination address");
            return Err(Errno::EINVAL.into());
        };
        let Some(addr) = addr.as_netlink() else {
            log::warn!("Attempted to send to non-netlink address {:?}", args.addr);
            return Err(Errno::EINVAL.into());
        };
        // Sending to non-kernel address is not supported
        if addr.pid() != 0 {
            log::warn!("Attempted to send to non-kernel netlink address {:?}", addr);
            return Err(Errno::EINVAL.into());
        }
        // Sending to groups is not supported
        if addr.groups() != 0 {
            log::warn!("Attempted to send to netlink groups {:?}", addr);
            return Err(Errno::EINVAL.into());
        }

        let rv = common.sendmsg(socket, args.iovs, args.flags, mem, cb_queue)?;

        self.refresh_file_state(common, cb_queue);

        Ok(rv.try_into().unwrap())
    }
}

impl ClosedState {
    fn bind(
        &mut self,
        _common: &mut NetlinkSocketCommon,
        _socket: &Arc<AtomicRefCell<NetlinkSocket>>,
        _addr: Option<&SockaddrStorage>,
        _rng: impl rand::Rng,
    ) -> SyscallResult {
        // We follow the same approach as UnixSocket
        log::warn!("bind() while in state {}", std::any::type_name::<Self>());
        Err(Errno::EOPNOTSUPP.into())
    }

    fn sendmsg(
        &mut self,
        _common: &mut NetlinkSocketCommon,
        _socket: &Arc<AtomicRefCell<NetlinkSocket>>,
        _args: SendmsgArgs,
        _mem: &mut MemoryManager,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        // We follow the same approach as UnixSocket
        log::warn!("sendmsg() while in state {}", std::any::type_name::<Self>());
        Err(Errno::EOPNOTSUPP.into())
    }
}

/// Common data and functionality that is useful for all states.
struct NetlinkSocketCommon {
    buffer: Arc<AtomicRefCell<SharedBuf>>,
    /// The max number of "in flight" bytes (sent but not yet read from the receiving socket).
    send_limit: u64,
    /// The number of "in flight" bytes.
    sent_len: u64,
    event_source: StateEventSource,
    state: FileState,
    status: FileStatus,
    // should only be used by `OpenFile` to make sure there is only ever one `OpenFile` instance for
    // this file
    has_open_file: bool,
}

impl NetlinkSocketCommon {
    pub fn supports_sa_restart(&self) -> bool {
        true
    }

    pub fn sendmsg(
        &mut self,
        socket: &Arc<AtomicRefCell<NetlinkSocket>>,
        iovs: &[IoVec],
        flags: libc::c_int,
        mem: &mut MemoryManager,
        cb_queue: &mut CallbackQueue,
    ) -> Result<usize, SyscallError> {
        // MSG_NOSIGNAL is a no-op, since netlink sockets are not stream-oriented.
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
            let len = iovs.iter().map(|x| x.len).sum::<libc::size_t>();

            // we keep track of the send buffer size manually, since the netlink socket buffers all
            // have usize::MAX length
            let space_available = self
                .send_limit
                .saturating_sub(self.sent_len)
                .try_into()
                .unwrap();

            if space_available == 0 {
                return Err(Errno::EAGAIN);
            }

            if len > space_available {
                if len <= self.send_limit.try_into().unwrap() {
                    // we can send this when the buffer has more space available
                    return Err(Errno::EAGAIN);
                } else {
                    // we could never send this message
                    return Err(Errno::EMSGSIZE);
                }
            }

            let reader = IoVecReader::new(iovs, mem);
            let reader = reader.take(len.try_into().unwrap());

            // send the packet directly to the buffer of the socket so that it will be
            // processed when the socket is read.
            self.buffer
                .borrow_mut()
                .write_packet(reader, len, cb_queue)
                .map_err(|e| Errno::try_from(e).unwrap())?;

            // if we successfully sent bytes, update the sent count
            self.sent_len += u64::try_from(len).unwrap();
            Ok(len)
        })();

        // if the syscall would block and we don't have the MSG_DONTWAIT flag
        if result.as_ref().err() == Some(&Errno::EWOULDBLOCK)
            && !flags.contains(MsgFlags::MSG_DONTWAIT)
        {
            return Err(SyscallError::new_blocked_on_file(
                File::Socket(Socket::Netlink(socket.clone())),
                FileState::WRITABLE,
                self.supports_sa_restart(),
            ));
        }

        Ok(result?)
    }

    fn copy_state(&mut self, mask: FileState, state: FileState, cb_queue: &mut CallbackQueue) {
        let old_state = self.state;

        // remove the masked flags, then copy the masked flags
        self.state.remove(mask);
        self.state.insert(state & mask);

        self.handle_state_change(old_state, cb_queue);
    }

    fn handle_state_change(&mut self, old_state: FileState, cb_queue: &mut CallbackQueue) {
        let states_changed = self.state ^ old_state;

        // if nothing changed
        if states_changed.is_empty() {
            return;
        }

        self.event_source.notify_listeners(
            self.state,
            states_changed,
            FileSignals::empty(),
            cb_queue,
        );
    }
}

#[derive(Copy, Clone, Debug, Hash, PartialEq, Eq)]
pub enum NetlinkSocketType {
    Dgram,
    Raw,
}

impl TryFrom<libc::c_int> for NetlinkSocketType {
    type Error = NetlinkSocketTypeConversionError;
    fn try_from(val: libc::c_int) -> Result<Self, Self::Error> {
        match val {
            libc::SOCK_DGRAM => Ok(Self::Dgram),
            libc::SOCK_RAW => Ok(Self::Raw),
            x => Err(NetlinkSocketTypeConversionError(x)),
        }
    }
}

#[derive(Copy, Clone, Debug)]
pub struct NetlinkSocketTypeConversionError(libc::c_int);

impl std::error::Error for NetlinkSocketTypeConversionError {}

impl std::fmt::Display for NetlinkSocketTypeConversionError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(
            f,
            "Invalid socket type {}; netlink sockets only support SOCK_DGRAM and SOCK_RAW",
            self.0
        )
    }
}

#[derive(Copy, Clone, Debug, Hash, PartialEq, Eq)]
pub enum NetlinkFamily {
    Route,
}

impl TryFrom<libc::c_int> for NetlinkFamily {
    type Error = NetlinkFamilyConversionError;
    fn try_from(val: libc::c_int) -> Result<Self, Self::Error> {
        match val {
            libc::NETLINK_ROUTE => Ok(Self::Route),
            x => Err(NetlinkFamilyConversionError(x)),
        }
    }
}

#[derive(Copy, Clone, Debug)]
pub struct NetlinkFamilyConversionError(libc::c_int);

impl std::error::Error for NetlinkFamilyConversionError {}

impl std::fmt::Display for NetlinkFamilyConversionError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(
            f,
            "Invalid netlink family {}; netlink families only support NETLINK_ROUTE",
            self.0
        )
    }
}
