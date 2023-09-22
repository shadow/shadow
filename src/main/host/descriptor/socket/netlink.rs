use std::io::{Cursor, ErrorKind, Read, Write};
use std::net::Ipv4Addr;
use std::sync::{Arc, Weak};

use atomic_refcell::AtomicRefCell;
use linux_api::errno::Errno;
use linux_api::ioctls::IoctlRequest;
use neli::consts::nl::{NlmF, NlmFFlags, Nlmsg};
use neli::consts::rtnl::{
    Arphrd, Ifa, IfaF, IfaFFlags, Iff, IffFlags, Ifla, RtAddrFamily, RtScope, Rtm,
};
use neli::nl::{NlPayload, Nlmsghdr};
use neli::rtnl::{Ifaddrmsg, Ifinfomsg, Rtattr};
use neli::types::{Buffer, RtBuffer};
use neli::{FromBytes, ToBytes};
use nix::sys::socket::{MsgFlags, NetlinkAddr, Shutdown};
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::core::worker::Worker;
use crate::cshadow as c;
use crate::host::descriptor::listener::{StateEventSource, StateListenHandle, StateListenerFilter};
use crate::host::descriptor::shared_buf::{
    BufferHandle, BufferSignals, BufferState, ReaderHandle, SharedBuf,
};
use crate::host::descriptor::socket::{RecvmsgArgs, RecvmsgReturn, SendmsgArgs, Socket};
use crate::host::descriptor::{
    File, FileMode, FileSignals, FileState, FileStatus, OpenFile, SyscallResult,
};
use crate::host::memory_manager::MemoryManager;
use crate::host::network::namespace::NetworkNamespace;
use crate::host::syscall::io::{IoVec, IoVecReader, IoVecWriter};
use crate::host::syscall::types::SyscallError;
use crate::utility::callback_queue::CallbackQueue;
use crate::utility::sockaddr::SockaddrStorage;
use crate::utility::HostTreePointer;

// this constant is copied from UNIX_SOCKET_DEFAULT_BUFFER_SIZE
const NETLINK_SOCKET_DEFAULT_BUFFER_SIZE: u64 = 212_992;

// See linux/rtnetlink.h
const RTM_GETLINK: u16 = 18;
const RTM_GETADDR: u16 = 22;

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

            // Get the IP address of the host
            let default_ip = Worker::with_active_host(|host| host.default_ip()).unwrap();
            // All the interface configurations are the same as in the getifaddrs function handler
            let interfaces = vec![
                Interface {
                    address: Ipv4Addr::LOCALHOST,
                    label: String::from("lo"),
                    prefix_len: 8,
                    if_type: Arphrd::Loopback,
                    mtu: c::CONFIG_MTU,
                    scope: RtScope::Host,
                    index: 1,
                },
                Interface {
                    address: default_ip,
                    label: String::from("eth0"),
                    prefix_len: 24,
                    if_type: Arphrd::Ether,
                    mtu: c::CONFIG_MTU,
                    scope: RtScope::Universe,
                    index: 2,
                },
            ];

            let mut common = NetlinkSocketCommon {
                buffer,
                send_limit: NETLINK_SOCKET_DEFAULT_BUFFER_SIZE,
                sent_len: 0,
                event_source: StateEventSource::new(),
                state: FileState::ACTIVE,
                status,
                has_open_file: false,
                interfaces,
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

    pub fn getsockname(&self) -> Result<Option<nix::sys::socket::NetlinkAddr>, SyscallError> {
        log::warn!("getsockname() syscall not yet supported for netlink sockets; Returning ENOSYS");
        Err(Errno::ENOSYS.into())
    }

    pub fn getpeername(&self) -> Result<Option<nix::sys::socket::NetlinkAddr>, SyscallError> {
        log::warn!("getpeername() syscall not yet supported for netlink sockets; Returning ENOSYS");
        Err(Errno::ENOSYS.into())
    }

    pub fn address_family(&self) -> nix::sys::socket::AddressFamily {
        nix::sys::socket::AddressFamily::Netlink
    }

    pub fn close(&mut self, cb_queue: &mut CallbackQueue) -> Result<(), SyscallError> {
        self.protocol_state.close(&mut self.common, cb_queue)
    }

    fn refresh_file_state(&mut self, cb_queue: &mut CallbackQueue) {
        self.protocol_state
            .refresh_file_state(&mut self.common, cb_queue)
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

    pub fn listen(
        _socket: &Arc<AtomicRefCell<Self>>,
        _backlog: i32,
        _net_ns: &NetworkNamespace,
        _rng: impl rand::Rng,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<(), SyscallError> {
        // We follow the same approach as UnixSocket
        log::warn!("We do not yet handle listen request on netlink sockets");
        Err(Errno::EINVAL.into())
    }

    pub fn connect(
        _socket: &Arc<AtomicRefCell<Self>>,
        _addr: &SockaddrStorage,
        _net_ns: &NetworkNamespace,
        _rng: impl rand::Rng,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<(), SyscallError> {
        // We follow the same approach as UnixSocket
        log::warn!("We do not yet handle connect request on netlink sockets");
        Err(Errno::EINVAL.into())
    }

    pub fn accept(
        &mut self,
        _net_ns: &NetworkNamespace,
        _rng: impl rand::Rng,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<OpenFile, SyscallError> {
        // We follow the same approach as UnixSocket
        log::warn!("We do not yet handle accept request on netlink sockets");
        Err(Errno::EINVAL.into())
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
    reader_handle: ReaderHandle,
    // this handle is never accessed, but we store it because of its drop impl
    _buffer_handle: BufferHandle,
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
        // this is a new socket and there are no listeners, so safe to use a temporary event queue
        let mut cb_queue = CallbackQueue::new();

        // increment the buffer's reader count
        let reader_handle = common.buffer.borrow_mut().add_reader(&mut cb_queue);

        let weak = Weak::clone(socket);
        let buffer_handle = common.buffer.borrow_mut().add_listener(
            BufferState::READABLE,
            BufferSignals::BUFFER_GREW,
            move |_, _, cb_queue| {
                if let Some(socket) = weak.upgrade() {
                    socket.borrow_mut().refresh_file_state(cb_queue);
                }
            },
        );

        ProtocolState::Initial(Some(InitialState {
            is_bound: false,
            reader_handle,
            _buffer_handle: buffer_handle,
        }))
    }

    fn refresh_file_state(&self, common: &mut NetlinkSocketCommon, cb_queue: &mut CallbackQueue) {
        match self {
            Self::Initial(x) => x.as_ref().unwrap().refresh_file_state(common, cb_queue),
            Self::Closed(x) => x.as_ref().unwrap().refresh_file_state(common, cb_queue),
        }
    }

    fn close(
        &mut self,
        common: &mut NetlinkSocketCommon,
        cb_queue: &mut CallbackQueue,
    ) -> Result<(), SyscallError> {
        let (new_state, rv) = match self {
            Self::Initial(x) => x.take().unwrap().close(common, cb_queue),
            Self::Closed(x) => x.take().unwrap().close(common, cb_queue),
        };

        *self = new_state;
        rv
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

    fn recvmsg(
        &mut self,
        common: &mut NetlinkSocketCommon,
        socket: &Arc<AtomicRefCell<NetlinkSocket>>,
        args: RecvmsgArgs,
        mem: &mut MemoryManager,
        cb_queue: &mut CallbackQueue,
    ) -> Result<RecvmsgReturn, SyscallError> {
        match self {
            Self::Initial(x) => x
                .as_mut()
                .unwrap()
                .recvmsg(common, socket, args, mem, cb_queue),
            Self::Closed(x) => x
                .as_mut()
                .unwrap()
                .recvmsg(common, socket, args, mem, cb_queue),
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

    fn close(
        self,
        common: &mut NetlinkSocketCommon,
        cb_queue: &mut CallbackQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        // inform the buffer that there is one fewer readers
        common
            .buffer
            .borrow_mut()
            .remove_reader(self.reader_handle, cb_queue);

        let new_state = ClosedState {};
        new_state.refresh_file_state(common, cb_queue);
        (new_state.into(), Ok(()))
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

        // It's okay to not have a destination address
        if let Some(addr) = args.addr {
            // Parse the address
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
        }

        let rv = common.sendmsg(socket, args.iovs, args.flags, mem, cb_queue)?;

        self.refresh_file_state(common, cb_queue);

        Ok(rv.try_into().unwrap())
    }

    fn recvmsg(
        &mut self,
        common: &mut NetlinkSocketCommon,
        socket: &Arc<AtomicRefCell<NetlinkSocket>>,
        args: RecvmsgArgs,
        mem: &mut MemoryManager,
        cb_queue: &mut CallbackQueue,
    ) -> Result<RecvmsgReturn, SyscallError> {
        if !args.control_ptr.ptr().is_null() {
            log::debug!("Netlink sockets don't yet support control data for recvmsg()");
            return Err(Errno::EINVAL.into());
        }
        let Some(flags) = MsgFlags::from_bits(args.flags) else {
            log::warn!("Unrecognized recv flags: {:#b}", args.flags);
            return Err(Errno::EINVAL.into());
        };

        let mut packet_buffer = Vec::new();
        let (_rv, _num_removed_from_buf) =
            common.recvmsg(socket, &mut packet_buffer, flags, mem, cb_queue)?;
        self.refresh_file_state(common, cb_queue);

        let mut writer = IoVecWriter::new(args.iovs, mem);

        // We set the source address as the netlink address of the kernel
        let src_addr = SockaddrStorage::from_netlink(&NetlinkAddr::new(0, 0));

        let buffer = (|| {
            // TODO: Replace 16 with the size of nlmsghdr
            if packet_buffer.len() < 16 {
                log::warn!("The processed packet is too short");
                return self.handle_error(&packet_buffer[..]);
            }
            // TODO: Replace 4..6 with `memoffset::span_of!` of the `nlmsg_type` field
            let nlmsg_type = u16::from_le_bytes((&packet_buffer[4..6]).try_into().unwrap());

            match nlmsg_type {
                RTM_GETLINK => self.handle_ifinfomsg(common, &packet_buffer[..]),
                RTM_GETADDR => self.handle_ifaddrmsg(common, &packet_buffer[..]),
                _ => {
                    log::warn!(
                        "Found unsupported nlmsg_type: {nlmsg_type} (only RTM_GETLINK
                        and RTM_GETADDR are supported)"
                    );
                    self.handle_error(&packet_buffer[..])
                }
            }
        })();

        // Try to write as much as we can. If the buffer is too small, just discard the rest
        let mut total_copied = 0;
        let mut buf = buffer.as_slice();
        while !buf.is_empty() {
            match writer.write(buf) {
                Ok(0) => break,
                Ok(n) => {
                    buf = &buf[n..];
                    total_copied += n;
                }
                Err(ref e) if e.kind() == ErrorKind::Interrupted => continue,
                Err(e) => return Err(e.into()),
            }
        }

        let return_val = if flags.contains(MsgFlags::MSG_TRUNC) {
            buffer.len()
        } else {
            total_copied
        };

        Ok(RecvmsgReturn {
            return_val: return_val.try_into().unwrap(),
            addr: Some(src_addr),
            msg_flags: 0,
            control_len: 0,
        })
    }

    fn handle_error(&self, bytes: &[u8]) -> Vec<u8> {
        // If we can't get the pid, set it to zero
        // TODO: Replace 8..12 with `memoffset::span_of!` of the `nlmsg_seq` field
        let nlmsg_seq = match bytes.get(8..12) {
            Some(x) => u32::from_le_bytes(x.try_into().unwrap()),
            None => 0,
        };

        // Generate a dummy error with the same sequence number as the request
        let msg = {
            let len = None;
            let nl_type = Nlmsg::Error;
            let flags = NlmFFlags::empty();
            let pid = None;
            let payload = NlPayload::<Nlmsg, ()>::Empty;
            Nlmsghdr::new(len, nl_type, flags, Some(nlmsg_seq), pid, payload)
        };

        let mut buffer = Cursor::new(Vec::new());
        msg.to_bytes(&mut buffer).unwrap();
        buffer.into_inner()
    }

    fn handle_ifaddrmsg(&self, common: &mut NetlinkSocketCommon, bytes: &[u8]) -> Vec<u8> {
        let Ok(nlmsg) = Nlmsghdr::<Rtm, Ifaddrmsg>::from_bytes(&mut Cursor::new(bytes)) else {
            log::warn!("Failed to deserialize the message");
            return self.handle_error(bytes);
        };

        let Ok(ifaddrmsg) = nlmsg.get_payload() else {
            log::warn!("Failed to find the payload");
            return self.handle_error(bytes);
        };

        // The only supported interface address family is AF_INET
        if ifaddrmsg.ifa_family != RtAddrFamily::Unspecified
            && ifaddrmsg.ifa_family != RtAddrFamily::Inet
        {
            log::warn!("Unsupported ifa_family (only AF_UNSPEC and AF_INET are supported)");
            return self.handle_error(bytes);
        }

        // The rest of the fields are unsupported. We limit only the interest to the zero values
        if ifaddrmsg.ifa_prefixlen != 0
            || ifaddrmsg.ifa_flags != IfaFFlags::empty()
            || ifaddrmsg.ifa_index != 0
            || ifaddrmsg.ifa_scope != libc::c_uchar::from(RtScope::Universe)
        {
            log::warn!(
                "Unsupported ifa_prefixlen, ifa_flags, ifa_scope, or ifa_index (they have to be 0)"
            );
            return self.handle_error(bytes);
        }

        let mut buffer = Cursor::new(Vec::new());
        // Send the interface addresses
        for interface in &common.interfaces {
            let address = interface.address.octets();
            let broadcast = Ipv4Addr::from(
                0xffff_ffff_u32
                    .checked_shr(u32::from(interface.prefix_len))
                    .unwrap_or(0)
                    | u32::from(interface.address),
            )
            .octets();
            let mut label = Vec::from(interface.label.as_bytes());
            label.push(0); // Null-terminate

            // List of attribtes sent with the response for the current interface
            let attrs = [
                // I don't know the difference between IFA_ADDRESS and IFA_LOCAL. However, Linux
                // provides the same address for both attributes, so I do the same.
                // Run `strace ip addr` to see.
                Rtattr::new(None, Ifa::Address, Buffer::from(&address[..])).unwrap(),
                Rtattr::new(None, Ifa::Local, Buffer::from(&address[..])).unwrap(),
                Rtattr::new(None, Ifa::Broadcast, Buffer::from(&broadcast[..])).unwrap(),
                Rtattr::new(None, Ifa::Label, Buffer::from(label)).unwrap(),
            ];
            let ifaddrmsg = Ifaddrmsg {
                ifa_family: RtAddrFamily::Inet,
                ifa_prefixlen: interface.prefix_len,
                // IFA_F_PERMANENT is used to indicate that the address is permanent
                ifa_flags: IfaFFlags::new(&[IfaF::Permanent]),
                ifa_scope: libc::c_uchar::from(interface.scope),
                ifa_index: interface.index,
                rtattrs: RtBuffer::from_iter(attrs),
            };
            let nlmsg = {
                let len = None;
                let nl_type = Rtm::Newaddr;
                // The NLM_F_MULTI flag is used to indicate that we will send multiple messages
                let flags = NlmFFlags::new(&[NlmF::Multi]);
                // Use the same sequence number as the request
                let seq = Some(nlmsg.nl_seq);
                let pid = None;
                let payload = NlPayload::Payload(ifaddrmsg);
                Nlmsghdr::new(len, nl_type, flags, seq, pid, payload)
            };
            nlmsg.to_bytes(&mut buffer).unwrap();
        }
        // After sending the messages with the NLM_F_MULTI flag set, we need to send the NLMSG_DONE message
        let done_msg = {
            let len = None;
            let nl_type = Nlmsg::Done;
            let flags = NlmFFlags::new(&[NlmF::Multi]);
            // Use the same sequence number as the request
            let seq = Some(nlmsg.nl_seq);
            let pid = None;
            // Linux also emits 4 bytes of zeroes after the header. See `strace ip addr`
            let payload: NlPayload<Nlmsg, u32> = NlPayload::Payload(0);
            Nlmsghdr::new(len, nl_type, flags, seq, pid, payload)
        };
        done_msg.to_bytes(&mut buffer).unwrap();

        buffer.into_inner()
    }

    fn handle_ifinfomsg(&self, common: &mut NetlinkSocketCommon, bytes: &[u8]) -> Vec<u8> {
        let Ok(nlmsg) = Nlmsghdr::<Rtm, Ifinfomsg>::from_bytes(&mut Cursor::new(bytes)) else {
            log::warn!("Failed to deserialize the message");
            return self.handle_error(bytes);
        };

        let Ok(ifinfomsg) = nlmsg.get_payload() else {
            log::warn!("Failed to find the payload");
            return self.handle_error(bytes);
        };

        // The only supported interface address family is AF_INET
        if ifinfomsg.ifi_family != RtAddrFamily::Unspecified
            && ifinfomsg.ifi_family != RtAddrFamily::Inet
        {
            log::warn!("Unsupported ifi_family (only AF_UNSPEC and AF_INET are supported)");
            return self.handle_error(bytes);
        }

        // The rest of the fields are unsupported. We limit only the interest to the zero values
        if ifinfomsg.ifi_type != 0.into()
            || ifinfomsg.ifi_index != 0
            || ifinfomsg.ifi_flags != IffFlags::empty()
        {
            log::warn!("Unsupported ifi_type, ifi_index, or ifi_flags (they have to be 0)");
            return self.handle_error(bytes);
        }

        // We don't check for ifi_change because we found that `ip addr` sets it to zero even if
        // rtnetlink(7) recommends to set it to all 1s

        let mut buffer = Cursor::new(Vec::new());
        // Send the interface addresses
        for interface in &common.interfaces {
            let mut label = Vec::from(interface.label.as_bytes());
            label.push(0); // Null-terminate

            // List of attribtes sent with the response for the current interface
            let attrs = [
                Rtattr::new(None, Ifla::Ifname, Buffer::from(label)).unwrap(),
                // Not sure about the value of this one, but I always see 1000 from `ip addr`. If
                // we don't specify this, `ip addr` will create an AF_INET socket and do ioctl. See
                // https://git.kernel.org/pub/scm/network/iproute2/iproute2.git/tree/ip/ipaddress.c#n168
                Rtattr::new(None, Ifla::Txqlen, Buffer::from(&1000u32.to_le_bytes()[..])).unwrap(),
                Rtattr::new(
                    None,
                    Ifla::Mtu,
                    Buffer::from(&interface.mtu.to_le_bytes()[..]),
                )
                .unwrap(),
                // TODO: Add the MAC address through IFLA_ADDRESS and IFLA_BROADCAST
            ];
            let flags = if interface.if_type == Arphrd::Loopback {
                IffFlags::new(&[Iff::Up, Iff::Loopback, Iff::Running])
            } else {
                // Not sure about the IFF_MULTICAST, but it's also the one I got from `strace ip addr`
                IffFlags::new(&[Iff::Up, Iff::Broadcast, Iff::Running, Iff::Multicast])
            };
            let ifinfomsg = Ifinfomsg::new(
                RtAddrFamily::Inet,
                interface.if_type,
                interface.index,
                flags,
                IffFlags::from_bitmask(0xffffffff), // rtnetlink(7) recommends to set it to all 1s
                RtBuffer::from_iter(attrs),
            );
            let nlmsg = {
                let len = None;
                let nl_type = Rtm::Newlink;
                // The NLM_F_MULTI flag is used to indicate that we will send multiple messages
                let flags = NlmFFlags::new(&[NlmF::Multi]);
                // Use the same sequence number as the request
                let seq = Some(nlmsg.nl_seq);
                let pid = None;
                let payload = NlPayload::Payload(ifinfomsg);
                Nlmsghdr::new(len, nl_type, flags, seq, pid, payload)
            };
            nlmsg.to_bytes(&mut buffer).unwrap();
        }
        // After sending the messages with the NLM_F_MULTI flag set, we need to send the NLMSG_DONE message
        let done_msg = {
            let len = None;
            let nl_type = Nlmsg::Done;
            let flags = NlmFFlags::new(&[NlmF::Multi]);
            // Use the same sequence number as the request
            let seq = Some(nlmsg.nl_seq);
            let pid = None;
            // Linux also emits 4 bytes of zeroes after the header. See `strace ip addr`
            let payload: NlPayload<Nlmsg, u32> = NlPayload::Payload(0);
            Nlmsghdr::new(len, nl_type, flags, seq, pid, payload)
        };
        done_msg.to_bytes(&mut buffer).unwrap();

        buffer.into_inner()
    }
}

impl ClosedState {
    fn refresh_file_state(&self, common: &mut NetlinkSocketCommon, cb_queue: &mut CallbackQueue) {
        common.copy_state(
            /* mask= */ FileState::all(),
            FileState::CLOSED,
            cb_queue,
        );
    }

    fn close(
        self,
        _common: &mut NetlinkSocketCommon,
        _cb_queue: &mut CallbackQueue,
    ) -> (ProtocolState, Result<(), SyscallError>) {
        // why are we trying to close an already closed file? we probably want a bt here...
        panic!("Trying to close an already closed socket");
    }

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

    fn recvmsg(
        &mut self,
        _common: &mut NetlinkSocketCommon,
        _socket: &Arc<AtomicRefCell<NetlinkSocket>>,
        _args: RecvmsgArgs,
        _mem: &mut MemoryManager,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<RecvmsgReturn, SyscallError> {
        // We follow the same approach as UnixSocket
        log::warn!("recvmsg() while in state {}", std::any::type_name::<Self>());
        Err(Errno::EOPNOTSUPP.into())
    }
}

// The struct used to describe the network interface
struct Interface {
    address: Ipv4Addr,
    label: String,
    prefix_len: u8,
    if_type: Arphrd,
    mtu: u32,
    scope: RtScope,
    index: libc::c_int,
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
    /// Interfaces
    interfaces: Vec<Interface>,
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

    pub fn recvmsg<W: Write>(
        &mut self,
        socket: &Arc<AtomicRefCell<NetlinkSocket>>,
        dst: W,
        mut flags: MsgFlags,
        _mem: &mut MemoryManager,
        cb_queue: &mut CallbackQueue,
    ) -> Result<(usize, usize), SyscallError> {
        let supported_flags = MsgFlags::MSG_DONTWAIT | MsgFlags::MSG_PEEK | MsgFlags::MSG_TRUNC;

        // if there's a flag we don't support, it's probably best to raise an error rather than do
        // the wrong thing
        if flags.intersects(!supported_flags) {
            log::warn!("Unsupported recv flags: {:?}", flags);
            return Err(Errno::EINVAL.into());
        }

        if self.status.contains(FileStatus::NONBLOCK) {
            flags.insert(MsgFlags::MSG_DONTWAIT);
        }

        // run in a closure so that an early return doesn't return from the syscall handler
        let result = (|| {
            let mut buffer = self.buffer.borrow_mut();

            // the read would block if the buffer has no data
            if !buffer.has_data() {
                return Err(Errno::EWOULDBLOCK);
            }

            let (num_copied, num_removed_from_buf) = if flags.contains(MsgFlags::MSG_PEEK) {
                buffer.peek(dst).map_err(|e| Errno::try_from(e).unwrap())?
            } else {
                buffer
                    .read(dst, cb_queue)
                    .map_err(|e| Errno::try_from(e).unwrap())?
            };

            if flags.contains(MsgFlags::MSG_TRUNC) {
                // return the total size of the message, not the number of bytes we read
                Ok((num_removed_from_buf, num_removed_from_buf))
            } else {
                Ok((num_copied, num_removed_from_buf))
            }
        })();

        // if the syscall would block and we don't have the MSG_DONTWAIT flag
        if result.as_ref().err() == Some(&Errno::EWOULDBLOCK)
            && !flags.contains(MsgFlags::MSG_DONTWAIT)
        {
            return Err(SyscallError::new_blocked_on_file(
                File::Socket(Socket::Netlink(socket.clone())),
                FileState::READABLE,
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
