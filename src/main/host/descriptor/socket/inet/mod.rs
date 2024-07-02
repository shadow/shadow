use std::net::{Ipv4Addr, SocketAddrV4};
use std::sync::{Arc, Weak};

use atomic_refcell::AtomicRefCell;
use linux_api::errno::Errno;
use linux_api::ioctls::IoctlRequest;
use linux_api::socket::Shutdown;
use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::cshadow as c;
use crate::host::descriptor::listener::{StateListenHandle, StateListenerFilter};
use crate::host::descriptor::socket::{RecvmsgArgs, RecvmsgReturn, SendmsgArgs};
use crate::host::descriptor::{
    FileMode, FileSignals, FileState, FileStatus, OpenFile, SyscallResult,
};
use crate::host::memory_manager::MemoryManager;
use crate::host::network::interface::FifoPacketPriority;
use crate::host::network::namespace::{AssociationHandle, NetworkNamespace};
use crate::host::syscall::io::IoVec;
use crate::host::syscall::types::SyscallError;
use crate::network::packet::PacketRc;
use crate::utility::callback_queue::CallbackQueue;
use crate::utility::sockaddr::SockaddrStorage;
use crate::utility::HostTreePointer;

use self::legacy_tcp::LegacyTcpSocket;
use self::tcp::TcpSocket;
use self::udp::UdpSocket;

pub mod legacy_tcp;
pub mod tcp;
pub mod udp;

#[derive(Clone)]
pub enum InetSocket {
    LegacyTcp(Arc<AtomicRefCell<LegacyTcpSocket>>),
    Tcp(Arc<AtomicRefCell<TcpSocket>>),
    Udp(Arc<AtomicRefCell<UdpSocket>>),
}

impl InetSocket {
    pub fn borrow(&self) -> InetSocketRef {
        match self {
            Self::LegacyTcp(ref f) => InetSocketRef::LegacyTcp(f.borrow()),
            Self::Tcp(ref f) => InetSocketRef::Tcp(f.borrow()),
            Self::Udp(ref f) => InetSocketRef::Udp(f.borrow()),
        }
    }

    pub fn try_borrow(&self) -> Result<InetSocketRef, atomic_refcell::BorrowError> {
        Ok(match self {
            Self::LegacyTcp(ref f) => InetSocketRef::LegacyTcp(f.try_borrow()?),
            Self::Tcp(ref f) => InetSocketRef::Tcp(f.try_borrow()?),
            Self::Udp(ref f) => InetSocketRef::Udp(f.try_borrow()?),
        })
    }

    pub fn borrow_mut(&self) -> InetSocketRefMut {
        match self {
            Self::LegacyTcp(ref f) => InetSocketRefMut::LegacyTcp(f.borrow_mut()),
            Self::Tcp(ref f) => InetSocketRefMut::Tcp(f.borrow_mut()),
            Self::Udp(ref f) => InetSocketRefMut::Udp(f.borrow_mut()),
        }
    }

    pub fn try_borrow_mut(&self) -> Result<InetSocketRefMut, atomic_refcell::BorrowMutError> {
        Ok(match self {
            Self::LegacyTcp(ref f) => InetSocketRefMut::LegacyTcp(f.try_borrow_mut()?),
            Self::Tcp(ref f) => InetSocketRefMut::Tcp(f.try_borrow_mut()?),
            Self::Udp(ref f) => InetSocketRefMut::Udp(f.try_borrow_mut()?),
        })
    }

    pub fn downgrade(&self) -> InetSocketWeak {
        match self {
            Self::LegacyTcp(x) => InetSocketWeak::LegacyTcp(Arc::downgrade(x)),
            Self::Tcp(x) => InetSocketWeak::Tcp(Arc::downgrade(x)),
            Self::Udp(x) => InetSocketWeak::Udp(Arc::downgrade(x)),
        }
    }

    /// Useful for getting a unique integer handle for a socket, or when we need to compare a C
    /// `LegacySocket` to a rust `InetSocket` (which may internally point to the same
    /// `LegacySocket`).
    pub fn canonical_handle(&self) -> usize {
        match self {
            // usually we'd use `Arc::as_ptr()`, but we want to use the handle for the C `TCP`
            // object for consistency with the handle for the `LegacySocket`
            Self::LegacyTcp(f) => f.borrow().canonical_handle(),
            Self::Tcp(f) => Arc::as_ptr(f) as usize,
            Self::Udp(f) => Arc::as_ptr(f) as usize,
        }
    }

    pub fn bind(
        &self,
        addr: Option<&SockaddrStorage>,
        net_ns: &NetworkNamespace,
        rng: impl rand::Rng,
    ) -> SyscallResult {
        match self {
            Self::LegacyTcp(socket) => LegacyTcpSocket::bind(socket, addr, net_ns, rng),
            Self::Tcp(socket) => TcpSocket::bind(socket, addr, net_ns, rng),
            Self::Udp(socket) => UdpSocket::bind(socket, addr, net_ns, rng),
        }
    }

    pub fn listen(
        &self,
        backlog: i32,
        net_ns: &NetworkNamespace,
        rng: impl rand::Rng,
        cb_queue: &mut CallbackQueue,
    ) -> Result<(), SyscallError> {
        match self {
            Self::LegacyTcp(socket) => {
                LegacyTcpSocket::listen(socket, backlog, net_ns, rng, cb_queue)
            }
            Self::Tcp(socket) => TcpSocket::listen(socket, backlog, net_ns, rng, cb_queue),
            Self::Udp(socket) => UdpSocket::listen(socket, backlog, net_ns, rng, cb_queue),
        }
    }

    pub fn connect(
        &self,
        addr: &SockaddrStorage,
        net_ns: &NetworkNamespace,
        rng: impl rand::Rng,
        cb_queue: &mut CallbackQueue,
    ) -> Result<(), SyscallError> {
        match self {
            Self::LegacyTcp(socket) => {
                LegacyTcpSocket::connect(socket, addr, net_ns, rng, cb_queue)
            }
            Self::Tcp(socket) => TcpSocket::connect(socket, addr, net_ns, rng, cb_queue),
            Self::Udp(socket) => UdpSocket::connect(socket, addr, net_ns, rng, cb_queue),
        }
    }

    pub fn sendmsg(
        &self,
        args: SendmsgArgs,
        memory_manager: &mut MemoryManager,
        net_ns: &NetworkNamespace,
        rng: impl rand::Rng,
        cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        match self {
            Self::LegacyTcp(socket) => {
                LegacyTcpSocket::sendmsg(socket, args, memory_manager, net_ns, rng, cb_queue)
            }
            Self::Tcp(socket) => {
                TcpSocket::sendmsg(socket, args, memory_manager, net_ns, rng, cb_queue)
            }
            Self::Udp(socket) => {
                UdpSocket::sendmsg(socket, args, memory_manager, net_ns, rng, cb_queue)
            }
        }
    }

    pub fn recvmsg(
        &self,
        args: RecvmsgArgs,
        memory_manager: &mut MemoryManager,
        cb_queue: &mut CallbackQueue,
    ) -> Result<RecvmsgReturn, SyscallError> {
        match self {
            Self::LegacyTcp(socket) => {
                LegacyTcpSocket::recvmsg(socket, args, memory_manager, cb_queue)
            }
            Self::Tcp(socket) => TcpSocket::recvmsg(socket, args, memory_manager, cb_queue),
            Self::Udp(socket) => UdpSocket::recvmsg(socket, args, memory_manager, cb_queue),
        }
    }
}

impl std::fmt::Debug for InetSocket {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::LegacyTcp(_) => write!(f, "LegacyTcp")?,
            Self::Tcp(_) => write!(f, "Tcp")?,
            Self::Udp(_) => write!(f, "Udp")?,
        }

        if let Ok(file) = self.try_borrow() {
            write!(
                f,
                "(state: {:?}, status: {:?})",
                file.state(),
                file.status()
            )
        } else {
            write!(f, "(already borrowed)")
        }
    }
}

impl PartialEq for InetSocket {
    /// Equal only if they are the same type and point to the same object. Two different socket
    /// objects with the exact same state are not considered equal.
    // Normally rust types implement `Eq` and `Hash` based on their internal state. So two different
    // objects with the same state will be equal and produce the same hash. We don't want that
    // behaviour in Shadow, where two different socket objects should always be considered unique.
    // I'm not sure if we should implement `Eq` and `Hash` on `InetSocket` directly, or if we should
    // create a wrapper type around `InetSocket` that implements our non-standard `Eq` and `Hash`
    // behaviour. For now I'm just implementing them directly on `InetSocket`.
    fn eq(&self, other: &Self) -> bool {
        // compare addresses first to shortcut more-expensive check
        if std::ptr::eq(self, other) {
            return true;
        }

        match (self, other) {
            (Self::LegacyTcp(self_), Self::LegacyTcp(other)) => Arc::ptr_eq(self_, other),
            (Self::Tcp(self_), Self::Tcp(other)) => Arc::ptr_eq(self_, other),
            (Self::Udp(self_), Self::Udp(other)) => Arc::ptr_eq(self_, other),
            _ => false,
        }
    }
}

impl Eq for InetSocket {}

impl std::hash::Hash for InetSocket {
    /// Returns a hash for the socket based on its address, and not the socket's state. Two
    /// different sockets with the same state will return different hashes, and the same socket will
    /// return the same hash even after being mutated.
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        // To match the `Eq` behaviour of `InetSocket`, the hashes of two sockets *must* be equal if
        // the types are the same and the Arc's pointers are equal, and the hashes *should not* be
        // equal if the two types are not the same or the Arc's pointers are not equal. We do return
        // the same hash if the two types are different but the pointers are equal, but this is
        // allowed by the `Hash` trait (it's just a hash collision) and we should never run into
        // this without a variant containing a zero-sized type, which wouldn't make sense in the
        // context of Shadow's sockets anyways.
        match self {
            Self::LegacyTcp(x) => Arc::as_ptr(x).cast::<libc::c_void>(),
            Self::Tcp(x) => Arc::as_ptr(x).cast(),
            Self::Udp(x) => Arc::as_ptr(x).cast(),
        }
        .hash(state);
    }
}

pub enum InetSocketRef<'a> {
    LegacyTcp(atomic_refcell::AtomicRef<'a, LegacyTcpSocket>),
    Tcp(atomic_refcell::AtomicRef<'a, TcpSocket>),
    Udp(atomic_refcell::AtomicRef<'a, UdpSocket>),
}

pub enum InetSocketRefMut<'a> {
    LegacyTcp(atomic_refcell::AtomicRefMut<'a, LegacyTcpSocket>),
    Tcp(atomic_refcell::AtomicRefMut<'a, TcpSocket>),
    Udp(atomic_refcell::AtomicRefMut<'a, UdpSocket>),
}

// file functions
impl InetSocketRef<'_> {
    enum_passthrough!(self, (), LegacyTcp, Tcp, Udp;
        pub fn state(&self) -> FileState
    );
    enum_passthrough!(self, (), LegacyTcp, Tcp, Udp;
        pub fn mode(&self) -> FileMode
    );
    enum_passthrough!(self, (), LegacyTcp, Tcp, Udp;
        pub fn status(&self) -> FileStatus
    );
    enum_passthrough!(self, (), LegacyTcp, Tcp, Udp;
        pub fn stat(&self) -> Result<linux_api::stat::stat, SyscallError>
    );
    enum_passthrough!(self, (), LegacyTcp, Tcp, Udp;
        pub fn has_open_file(&self) -> bool
    );
    enum_passthrough!(self, (), LegacyTcp, Tcp, Udp;
        pub fn supports_sa_restart(&self) -> bool
    );
}

// socket-specific functions
impl InetSocketRef<'_> {
    pub fn getpeername(&self) -> Result<Option<SockaddrStorage>, SyscallError> {
        match self {
            Self::LegacyTcp(socket) => socket.getpeername().map(|opt| opt.map(Into::into)),
            Self::Tcp(socket) => socket.getpeername().map(|opt| opt.map(Into::into)),
            Self::Udp(socket) => socket.getpeername().map(|opt| opt.map(Into::into)),
        }
    }

    pub fn getsockname(&self) -> Result<Option<SockaddrStorage>, SyscallError> {
        match self {
            Self::LegacyTcp(socket) => socket.getsockname().map(|opt| opt.map(Into::into)),
            Self::Tcp(socket) => socket.getsockname().map(|opt| opt.map(Into::into)),
            Self::Udp(socket) => socket.getsockname().map(|opt| opt.map(Into::into)),
        }
    }

    enum_passthrough!(self, (), LegacyTcp, Tcp, Udp;
        pub fn address_family(&self) -> linux_api::socket::AddressFamily
    );
}

// inet socket-specific functions
impl InetSocketRef<'_> {
    enum_passthrough!(self, (), LegacyTcp, Tcp, Udp;
        pub fn peek_next_packet_priority(&self) -> Option<FifoPacketPriority>
    );
    enum_passthrough!(self, (), LegacyTcp, Tcp, Udp;
        pub fn has_data_to_send(&self) -> bool
    );
}

// file functions
impl InetSocketRefMut<'_> {
    enum_passthrough!(self, (), LegacyTcp, Tcp, Udp;
        pub fn state(&self) -> FileState
    );
    enum_passthrough!(self, (), LegacyTcp, Tcp, Udp;
        pub fn mode(&self) -> FileMode
    );
    enum_passthrough!(self, (), LegacyTcp, Tcp, Udp;
        pub fn status(&self) -> FileStatus
    );
    enum_passthrough!(self, (), LegacyTcp, Tcp, Udp;
        pub fn stat(&self) -> Result<linux_api::stat::stat, SyscallError>
    );
    enum_passthrough!(self, (), LegacyTcp, Tcp, Udp;
        pub fn has_open_file(&self) -> bool
    );
    enum_passthrough!(self, (val), LegacyTcp, Tcp, Udp;
        pub fn set_has_open_file(&mut self, val: bool)
    );
    enum_passthrough!(self, (), LegacyTcp, Tcp, Udp;
        pub fn supports_sa_restart(&self) -> bool
    );
    enum_passthrough!(self, (cb_queue), LegacyTcp, Tcp, Udp;
        pub fn close(&mut self, cb_queue: &mut CallbackQueue) -> Result<(), SyscallError>
    );
    enum_passthrough!(self, (status), LegacyTcp, Tcp, Udp;
        pub fn set_status(&mut self, status: FileStatus)
    );
    enum_passthrough!(self, (request, arg_ptr, memory_manager), LegacyTcp, Tcp, Udp;
        pub fn ioctl(&mut self, request: IoctlRequest, arg_ptr: ForeignPtr<()>, memory_manager: &mut MemoryManager) -> SyscallResult
    );
    enum_passthrough!(self, (monitoring_state, monitoring_signals, filter, notify_fn), LegacyTcp, Tcp, Udp;
        pub fn add_listener(
            &mut self,
            monitoring_state: FileState,
            monitoring_signals: FileSignals,
            filter: StateListenerFilter,
            notify_fn: impl Fn(FileState, FileState, FileSignals, &mut CallbackQueue) + Send + Sync + 'static,
        ) -> StateListenHandle
    );
    enum_passthrough!(self, (ptr), LegacyTcp, Tcp, Udp;
        pub fn add_legacy_listener(&mut self, ptr: HostTreePointer<c::StatusListener>)
    );
    enum_passthrough!(self, (ptr), LegacyTcp, Tcp, Udp;
        pub fn remove_legacy_listener(&mut self, ptr: *mut c::StatusListener)
    );
    enum_passthrough!(self, (iovs, offset, flags, mem, cb_queue), LegacyTcp, Tcp, Udp;
        pub fn readv(&mut self, iovs: &[IoVec], offset: Option<libc::off_t>, flags: libc::c_int,
                     mem: &mut MemoryManager, cb_queue: &mut CallbackQueue) -> Result<libc::ssize_t, SyscallError>
    );
    enum_passthrough!(self, (iovs, offset, flags, mem, cb_queue), LegacyTcp, Tcp, Udp;
        pub fn writev(&mut self, iovs: &[IoVec], offset: Option<libc::off_t>, flags: libc::c_int,
                      mem: &mut MemoryManager, cb_queue: &mut CallbackQueue) -> Result<libc::ssize_t, SyscallError>
    );
}

// socket-specific functions
impl InetSocketRefMut<'_> {
    pub fn getpeername(&self) -> Result<Option<SockaddrStorage>, SyscallError> {
        match self {
            Self::LegacyTcp(socket) => socket.getpeername().map(|opt| opt.map(Into::into)),
            Self::Tcp(socket) => socket.getpeername().map(|opt| opt.map(Into::into)),
            Self::Udp(socket) => socket.getpeername().map(|opt| opt.map(Into::into)),
        }
    }

    pub fn getsockname(&self) -> Result<Option<SockaddrStorage>, SyscallError> {
        match self {
            Self::LegacyTcp(socket) => socket.getsockname().map(|opt| opt.map(Into::into)),
            Self::Tcp(socket) => socket.getsockname().map(|opt| opt.map(Into::into)),
            Self::Udp(socket) => socket.getsockname().map(|opt| opt.map(Into::into)),
        }
    }

    enum_passthrough!(self, (), LegacyTcp, Tcp, Udp;
        pub fn address_family(&self) -> linux_api::socket::AddressFamily
    );

    enum_passthrough!(self, (level, optname, optval_ptr, optlen, memory_manager, cb_queue), LegacyTcp, Tcp, Udp;
        pub fn getsockopt(&mut self, level: libc::c_int, optname: libc::c_int, optval_ptr: ForeignPtr<()>,
                          optlen: libc::socklen_t, memory_manager: &mut MemoryManager, cb_queue: &mut CallbackQueue)
        -> Result<libc::socklen_t, SyscallError>
    );

    enum_passthrough!(self, (level, optname, optval_ptr, optlen, memory_manager), LegacyTcp, Tcp, Udp;
        pub fn setsockopt(&mut self, level: libc::c_int, optname: libc::c_int, optval_ptr: ForeignPtr<()>,
                          optlen: libc::socklen_t, memory_manager: &MemoryManager)
        -> Result<(), SyscallError>
    );

    pub fn accept(
        &mut self,
        net_ns: &NetworkNamespace,
        rng: impl rand::Rng,
        cb_queue: &mut CallbackQueue,
    ) -> Result<OpenFile, SyscallError> {
        match self {
            Self::LegacyTcp(socket) => socket.accept(net_ns, rng, cb_queue),
            Self::Tcp(socket) => socket.accept(net_ns, rng, cb_queue),
            Self::Udp(socket) => socket.accept(net_ns, rng, cb_queue),
        }
    }

    enum_passthrough!(self, (how, cb_queue), LegacyTcp, Tcp, Udp;
        pub fn shutdown(&mut self, how: Shutdown, cb_queue: &mut CallbackQueue) -> Result<(), SyscallError>
    );
}

// inet socket-specific functions
impl InetSocketRefMut<'_> {
    enum_passthrough!(self, (packet, cb_queue, recv_time), LegacyTcp, Tcp, Udp;
        pub fn push_in_packet(&mut self, packet: PacketRc, cb_queue: &mut CallbackQueue, recv_time: EmulatedTime)
    );
    enum_passthrough!(self, (cb_queue), LegacyTcp, Tcp, Udp;
        pub fn pull_out_packet(&mut self, cb_queue: &mut CallbackQueue) -> Option<PacketRc>
    );
    enum_passthrough!(self, (), LegacyTcp, Tcp, Udp;
        pub fn peek_next_packet_priority(&self) -> Option<FifoPacketPriority>
    );
    enum_passthrough!(self, (), LegacyTcp, Tcp, Udp;
        pub fn has_data_to_send(&self) -> bool
    );
}

impl std::fmt::Debug for InetSocketRef<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::LegacyTcp(_) => write!(f, "LegacyTcp")?,
            Self::Tcp(_) => write!(f, "Tcp")?,
            Self::Udp(_) => write!(f, "Udp")?,
        }

        write!(
            f,
            "(state: {:?}, status: {:?})",
            self.state(),
            self.status()
        )
    }
}

impl std::fmt::Debug for InetSocketRefMut<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::LegacyTcp(_) => write!(f, "LegacyTcp")?,
            Self::Tcp(_) => write!(f, "Tcp")?,
            Self::Udp(_) => write!(f, "Udp")?,
        }

        write!(
            f,
            "(state: {:?}, status: {:?})",
            self.state(),
            self.status()
        )
    }
}

#[derive(Clone)]
pub enum InetSocketWeak {
    LegacyTcp(Weak<AtomicRefCell<LegacyTcpSocket>>),
    Tcp(Weak<AtomicRefCell<TcpSocket>>),
    Udp(Weak<AtomicRefCell<UdpSocket>>),
}

impl InetSocketWeak {
    pub fn upgrade(&self) -> Option<InetSocket> {
        match self {
            Self::LegacyTcp(x) => x.upgrade().map(InetSocket::LegacyTcp),
            Self::Tcp(x) => x.upgrade().map(InetSocket::Tcp),
            Self::Udp(x) => x.upgrade().map(InetSocket::Udp),
        }
    }
}

/// Associate the socket with a network interface. If the local address is unspecified, the socket
/// will be associated with every available interface. If the local address has a port of 0, a
/// non-zero port will be chosen. The final local address will be returned. If the peer address is
/// unspecified and has a port of 0, the socket will receive packets from every peer address. The
/// socket will be automatically disassociated when the returned [`AssociationHandle`] is dropped.
/// If `check_generic_peer` is true, the association will also fail if there is already a socket
/// associated with the local address `local_addr` and peer address 0.0.0.0:0.
fn associate_socket(
    socket: InetSocket,
    local_addr: SocketAddrV4,
    peer_addr: SocketAddrV4,
    check_generic_peer: bool,
    net_ns: &NetworkNamespace,
    rng: impl rand::Rng,
) -> Result<(SocketAddrV4, AssociationHandle), SyscallError> {
    log::trace!("Trying to associate socket with addresses (local={local_addr}, peer={peer_addr})");

    if !local_addr.ip().is_unspecified() && net_ns.interface_borrow(*local_addr.ip()).is_none() {
        log::debug!(
            "No network interface exists for the provided local address {}",
            local_addr.ip(),
        );
        return Err(Errno::EINVAL.into());
    };

    let protocol = match socket {
        InetSocket::LegacyTcp(_) => c::_ProtocolType_PTCP,
        InetSocket::Tcp(_) => c::_ProtocolType_PTCP,
        InetSocket::Udp(_) => c::_ProtocolType_PUDP,
    };

    // get a free ephemeral port if they didn't specify one
    let local_addr = if local_addr.port() != 0 {
        local_addr
    } else {
        let Some(new_port) =
            net_ns.get_random_free_port(protocol, *local_addr.ip(), peer_addr, rng)
        else {
            log::debug!("Association required an ephemeral port but none are available");
            return Err(Errno::EADDRINUSE.into());
        };

        log::debug!("Associating with generated ephemeral port {new_port}");

        // update the address with the same ip, but new port
        SocketAddrV4::new(*local_addr.ip(), new_port)
    };

    // make sure the port is available at this address for this protocol
    match net_ns.is_addr_in_use(protocol, local_addr, peer_addr) {
        Ok(true) => {
            log::debug!(
                "The provided addresses (local={local_addr}, peer={peer_addr}) are not available"
            );
            return Err(Errno::EADDRINUSE.into());
        }
        Err(_e) => return Err(Errno::EADDRNOTAVAIL.into()),
        Ok(false) => {}
    }

    if check_generic_peer {
        match net_ns.is_addr_in_use(
            protocol,
            local_addr,
            SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, 0),
        ) {
            Ok(true) => {
                log::debug!(
                    "The generic addresses (local={local_addr}, peer={}) are not available",
                    SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, 0)
                );
                return Err(Errno::EADDRINUSE.into());
            }
            Err(_e) => return Err(Errno::EADDRNOTAVAIL.into()),
            Ok(false) => {}
        }
    }

    // associate the interfaces corresponding to addr with socket
    let handle = unsafe { net_ns.associate_interface(&socket, protocol, local_addr, peer_addr) };

    Ok((local_addr, handle))
}

mod export {
    use super::*;

    use shadow_shim_helper_rs::emulated_time::CEmulatedTime;

    /// Decrement the ref count of the `InetSocket` object. The pointer must not be used after
    /// calling this function.
    #[no_mangle]
    pub extern "C-unwind" fn inetsocket_drop(socket: *const InetSocket) {
        assert!(!socket.is_null());
        drop(unsafe { Box::from_raw(socket.cast_mut()) });
    }

    /// Helper for GLib functions that take a `TaskObjectFreeFunc`. See [`inetsocket_drop`].
    #[no_mangle]
    pub extern "C-unwind" fn inetsocket_dropVoid(socket: *mut libc::c_void) {
        inetsocket_drop(socket.cast_const().cast())
    }

    /// Increment the ref count of the `InetSocket` object. The returned pointer will not be the
    /// same as the given pointer (they are distinct references), and they both must be dropped
    /// with `inetsocket_drop` separately later.
    #[no_mangle]
    pub extern "C-unwind" fn inetsocket_cloneRef(socket: *const InetSocket) -> *const InetSocket {
        let socket = unsafe { socket.as_ref() }.unwrap();
        Box::into_raw(Box::new(socket.clone()))
    }

    /// Compare two `InetSocket` objects by the addresses of the socket objects they point to. The
    /// pointers must be valid (and non-null).
    #[no_mangle]
    pub extern "C-unwind" fn inetsocket_eq(a: *const InetSocket, b: *const InetSocket) -> bool {
        let a = unsafe { a.as_ref() }.unwrap();
        let b = unsafe { b.as_ref() }.unwrap();

        a == b
    }

    /// Helper for GLib functions that take a `GCompareFunc`. See [`inetsocket_eq`].
    #[no_mangle]
    pub extern "C-unwind" fn inetsocket_eqVoid(
        a: *const libc::c_void,
        b: *const libc::c_void,
    ) -> bool {
        inetsocket_eq(a.cast(), b.cast())
    }

    /// Generate a hash identifying the `InetSocket`. The hash is generated from the socket's
    /// address.
    #[no_mangle]
    pub extern "C-unwind" fn inetsocket_hash(socket: *const InetSocket) -> u64 {
        use std::hash::{Hash, Hasher};

        let socket = unsafe { socket.as_ref() }.unwrap();

        let mut s = std::hash::DefaultHasher::new();
        socket.hash(&mut s);
        s.finish()
    }

    /// Helper for GLib functions that take a `GHashFunc`. See [`inetsocket_hash`].
    #[no_mangle]
    pub extern "C-unwind" fn inetsocket_hashVoid(socket: *const libc::c_void) -> libc::c_uint {
        // disregard some bytes of the hash
        inetsocket_hash(socket.cast()) as libc::c_uint
    }

    /// Returns a handle uniquely identifying the socket. There can be many `InetSocket`s that point
    /// to a single socket object (an `InetSocket` is just an enum that contains an `Arc` of the
    /// socket object), so the address of an `InetSocket` *does not* uniquely identify a socket.
    #[no_mangle]
    pub extern "C-unwind" fn inetsocket_getCanonicalHandle(socket: *const InetSocket) -> usize {
        let socket = unsafe { socket.as_ref() }.unwrap();
        socket.canonical_handle()
    }

    #[no_mangle]
    pub extern "C-unwind" fn inetsocket_pushInPacket(
        socket: *const InetSocket,
        packet: *mut c::Packet,
        recv_time: CEmulatedTime,
    ) {
        let socket = unsafe { socket.as_ref() }.unwrap();
        let recv_time = EmulatedTime::from_c_emutime(recv_time).unwrap();

        // we don't own the reference to the packet, so we need our own reference
        unsafe { c::packet_ref(packet) };
        let packet = PacketRc::from_raw(packet);

        crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
            CallbackQueue::queue_and_run(|cb_queue| {
                socket
                    .borrow_mut()
                    .push_in_packet(packet, cb_queue, recv_time);
            });
        });
    }

    #[no_mangle]
    pub extern "C-unwind" fn inetsocket_pullOutPacket(socket: *const InetSocket) -> *mut c::Packet {
        let socket = unsafe { socket.as_ref() }.unwrap();

        crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
            CallbackQueue::queue_and_run(|cb_queue| {
                socket
                    .borrow_mut()
                    .pull_out_packet(cb_queue)
                    .map(|p| p.into_inner())
                    .unwrap_or(std::ptr::null_mut())
            })
        })
    }

    /// Will return non-zero if socket doesn't have data to send (there is no priority).
    #[no_mangle]
    pub extern "C-unwind" fn inetsocket_peekNextPacketPriority(
        socket: *const InetSocket,
        priority_out: *mut u64,
    ) -> libc::c_int {
        let socket = unsafe { socket.as_ref() }.unwrap();
        let priority_out = unsafe { priority_out.as_mut() }.unwrap();
        if let Some(priority) = socket.borrow().peek_next_packet_priority() {
            *priority_out = priority;
            return 0;
        }
        -1
    }

    #[no_mangle]
    pub extern "C-unwind" fn inetsocket_hasDataToSend(socket: *const InetSocket) -> bool {
        let socket = unsafe { socket.as_ref() }.unwrap();
        socket.borrow().has_data_to_send()
    }

    /// Get a legacy C [`TCP`](c::TCP) pointer for the socket. Will panic if `socket` is not a
    /// legacy TCP socket or if `socket` is already mutably borrowed. Will never return `NULL`.
    #[no_mangle]
    pub extern "C-unwind" fn inetsocket_asLegacyTcp(socket: *const InetSocket) -> *mut c::TCP {
        let socket = unsafe { socket.as_ref() }.unwrap();

        #[allow(irrefutable_let_patterns)]
        let InetSocket::LegacyTcp(socket) = socket
        else {
            panic!("Socket was not a legacy TCP socket: {socket:?}");
        };

        let ptr = socket.borrow().as_legacy_tcp();
        // this should never be true
        assert!(!ptr.is_null());

        ptr
    }

    /// Decrement the ref count of the `InetSocketWeak` object. The pointer must not be used after
    /// calling this function.
    #[no_mangle]
    pub extern "C-unwind" fn inetsocketweak_drop(socket: *mut InetSocketWeak) {
        assert!(!socket.is_null());
        drop(unsafe { Box::from_raw(socket) });
    }

    /// Upgrade the weak reference. May return `NULL` if the socket has no remaining strong
    /// references and has been dropped. Returns an owned `InetSocket` that must be dropped as a
    /// `Box` later (for example using `inetsocket_drop`).
    #[no_mangle]
    pub extern "C-unwind" fn inetsocketweak_upgrade(
        socket: *const InetSocketWeak,
    ) -> *mut InetSocket {
        let socket = unsafe { socket.as_ref() }.unwrap();
        socket
            .upgrade()
            .map(Box::new)
            .map(Box::into_raw)
            .unwrap_or(std::ptr::null_mut())
    }
}
