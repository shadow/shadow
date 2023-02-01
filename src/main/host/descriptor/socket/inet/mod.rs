use std::net::SocketAddrV4;
use std::sync::Arc;

use atomic_refcell::AtomicRefCell;
use nix::errno::Errno;

use crate::cshadow as c;
use crate::host::descriptor::{FileMode, FileState, FileStatus, SyscallResult};
use crate::host::memory_manager::MemoryManager;
use crate::host::syscall_types::{PluginPtr, SysCallReg, SyscallError};
use crate::network::net_namespace::NetworkNamespace;
use crate::network::packet::Packet;
use crate::utility::callback_queue::CallbackQueue;
use crate::utility::sockaddr::SockaddrStorage;
use crate::utility::HostTreePointer;

use tcp::TcpSocket;

pub mod tcp;

#[derive(Clone)]
pub enum InetSocket {
    Tcp(Arc<AtomicRefCell<TcpSocket>>),
}

impl InetSocket {
    pub fn borrow(&self) -> InetSocketRef {
        match self {
            Self::Tcp(ref f) => InetSocketRef::Tcp(f.borrow()),
        }
    }

    pub fn try_borrow(&self) -> Result<InetSocketRef, atomic_refcell::BorrowError> {
        Ok(match self {
            Self::Tcp(ref f) => InetSocketRef::Tcp(f.try_borrow()?),
        })
    }

    pub fn borrow_mut(&self) -> InetSocketRefMut {
        match self {
            Self::Tcp(ref f) => InetSocketRefMut::Tcp(f.borrow_mut()),
        }
    }

    pub fn try_borrow_mut(&self) -> Result<InetSocketRefMut, atomic_refcell::BorrowMutError> {
        Ok(match self {
            Self::Tcp(ref f) => InetSocketRefMut::Tcp(f.try_borrow_mut()?),
        })
    }

    pub fn canonical_handle(&self) -> usize {
        match self {
            // usually we'd use `Arc::as_ptr()`, but we want to use the handle for the C `TCP`
            // object for consistency with the handle for the `LegacySocket`
            Self::Tcp(f) => f.borrow().canonical_handle(),
        }
    }

    pub fn bind(
        &self,
        addr: Option<&SockaddrStorage>,
        net_ns: &NetworkNamespace,
        rng: impl rand::Rng,
    ) -> SyscallResult {
        match self {
            Self::Tcp(socket) => TcpSocket::bind(socket, addr, net_ns, rng),
        }
    }

    pub fn connect(
        &self,
        addr: &SockaddrStorage,
        cb_queue: &mut CallbackQueue,
    ) -> Result<(), SyscallError> {
        match self {
            Self::Tcp(socket) => TcpSocket::connect(socket, addr, cb_queue),
        }
    }
}

impl std::fmt::Debug for InetSocket {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Tcp(_) => write!(f, "Tcp")?,
        }

        if let Ok(file) = self.try_borrow() {
            write!(
                f,
                "(state: {:?}, status: {:?})",
                file.state(),
                file.get_status()
            )
        } else {
            write!(f, "(already borrowed)")
        }
    }
}

pub enum InetSocketRef<'a> {
    Tcp(atomic_refcell::AtomicRef<'a, TcpSocket>),
}

pub enum InetSocketRefMut<'a> {
    Tcp(atomic_refcell::AtomicRefMut<'a, TcpSocket>),
}

// file functions
impl InetSocketRef<'_> {
    enum_passthrough!(self, (), Tcp;
        pub fn state(&self) -> FileState
    );
    enum_passthrough!(self, (), Tcp;
        pub fn mode(&self) -> FileMode
    );
    enum_passthrough!(self, (), Tcp;
        pub fn get_status(&self) -> FileStatus
    );
    enum_passthrough!(self, (), Tcp;
        pub fn has_open_file(&self) -> bool
    );
    enum_passthrough!(self, (), Tcp;
        pub fn supports_sa_restart(&self) -> bool
    );
}

// socket-specific functions
impl InetSocketRef<'_> {
    pub fn getpeername(&self) -> Result<Option<SockaddrStorage>, SyscallError> {
        match self {
            Self::Tcp(socket) => socket.getpeername().map(|opt| opt.map(Into::into)),
        }
    }

    pub fn getsockname(&self) -> Result<Option<SockaddrStorage>, SyscallError> {
        match self {
            Self::Tcp(socket) => socket.getsockname().map(|opt| opt.map(Into::into)),
        }
    }

    enum_passthrough!(self, (), Tcp;
        pub fn address_family(&self) -> nix::sys::socket::AddressFamily
    );
}

// inet socket-specific functions
impl InetSocketRef<'_> {
    enum_passthrough!(self, (), Tcp;
        pub fn peek_next_out_packet(&self) -> Option<Packet>
    );
    enum_passthrough!(self, (packet), Tcp;
        pub fn update_packet_header(&self, packet: &mut Packet)
    );
}

// file functions
impl InetSocketRefMut<'_> {
    enum_passthrough!(self, (), Tcp;
        pub fn state(&self) -> FileState
    );
    enum_passthrough!(self, (), Tcp;
        pub fn mode(&self) -> FileMode
    );
    enum_passthrough!(self, (), Tcp;
        pub fn get_status(&self) -> FileStatus
    );
    enum_passthrough!(self, (), Tcp;
        pub fn has_open_file(&self) -> bool
    );
    enum_passthrough!(self, (val), Tcp;
        pub fn set_has_open_file(&mut self, val: bool)
    );
    enum_passthrough!(self, (), Tcp;
        pub fn supports_sa_restart(&self) -> bool
    );
    enum_passthrough!(self, (cb_queue), Tcp;
        pub fn close(&mut self, cb_queue: &mut CallbackQueue) -> Result<(), SyscallError>
    );
    enum_passthrough!(self, (status), Tcp;
        pub fn set_status(&mut self, status: FileStatus)
    );
    enum_passthrough!(self, (request, arg_ptr, memory_manager), Tcp;
        pub fn ioctl(&mut self, request: u64, arg_ptr: PluginPtr, memory_manager: &mut MemoryManager) -> SyscallResult
    );
    enum_passthrough!(self, (ptr), Tcp;
        pub fn add_legacy_listener(&mut self, ptr: HostTreePointer<c::StatusListener>)
    );
    enum_passthrough!(self, (ptr), Tcp;
        pub fn remove_legacy_listener(&mut self, ptr: *mut c::StatusListener)
    );

    enum_passthrough_generic!(self, (bytes, offset, cb_queue), Tcp;
        pub fn read<W>(&mut self, bytes: W, offset: libc::off_t, cb_queue: &mut CallbackQueue) -> SyscallResult
        where W: std::io::Write + std::io::Seek
    );

    enum_passthrough_generic!(self, (source, offset, cb_queue), Tcp;
        pub fn write<R>(&mut self, source: R, offset: libc::off_t, cb_queue: &mut CallbackQueue) -> SyscallResult
        where R: std::io::Read + std::io::Seek
    );
}

// socket-specific functions
impl InetSocketRefMut<'_> {
    pub fn getpeername(&self) -> Result<Option<SockaddrStorage>, SyscallError> {
        match self {
            Self::Tcp(socket) => socket.getpeername().map(|opt| opt.map(Into::into)),
        }
    }

    pub fn getsockname(&self) -> Result<Option<SockaddrStorage>, SyscallError> {
        match self {
            Self::Tcp(socket) => socket.getsockname().map(|opt| opt.map(Into::into)),
        }
    }

    enum_passthrough!(self, (), Tcp;
        pub fn address_family(&self) -> nix::sys::socket::AddressFamily
    );

    enum_passthrough_generic!(self, (source, addr, cb_queue), Tcp;
        pub fn sendto<R>(&mut self, source: R, addr: Option<SockaddrStorage>, cb_queue: &mut CallbackQueue)
            -> SyscallResult
        where R: std::io::Read + std::io::Seek
    );

    enum_passthrough_generic!(self, (bytes, cb_queue), Tcp;
        pub fn recvfrom<W>(&mut self, bytes: W, cb_queue: &mut CallbackQueue)
            -> Result<(SysCallReg, Option<SockaddrStorage>), SyscallError>
        where W: std::io::Write + std::io::Seek
    );

    enum_passthrough!(self, (backlog, cb_queue), Tcp;
        pub fn listen(&mut self, backlog: i32, cb_queue: &mut CallbackQueue) -> Result<(), SyscallError>
    );

    pub fn accept(&mut self, cb_queue: &mut CallbackQueue) -> Result<InetSocket, SyscallError> {
        match self {
            Self::Tcp(socket) => socket.accept(cb_queue).map(InetSocket::Tcp),
        }
    }
}

// inet socket-specific functions
impl InetSocketRefMut<'_> {
    enum_passthrough!(self, (packet, cb_queue), Tcp;
        pub fn push_in_packet(&mut self, packet: Packet, cb_queue: &mut CallbackQueue)
    );
    enum_passthrough!(self, (cb_queue), Tcp;
        pub fn pull_out_packet(&mut self, cb_queue: &mut CallbackQueue) -> Option<Packet>
    );
    enum_passthrough!(self, (), Tcp;
        pub fn peek_next_out_packet(&self) -> Option<Packet>
    );
    enum_passthrough!(self, (packet), Tcp;
        pub fn update_packet_header(&self, packet: &mut Packet)
    );
}

impl std::fmt::Debug for InetSocketRef<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Tcp(_) => write!(f, "Tcp")?,
        }

        write!(
            f,
            "(state: {:?}, status: {:?})",
            self.state(),
            self.get_status()
        )
    }
}

impl std::fmt::Debug for InetSocketRefMut<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Tcp(_) => write!(f, "Tcp")?,
        }

        write!(
            f,
            "(state: {:?}, status: {:?})",
            self.state(),
            self.get_status()
        )
    }
}

/// Associate the socket with a network interface. If the local address is unspecified, the socket
/// will be associated with every available interface. If the local address has a port of 0, a
/// non-zero port will be chosen. The final local address will be returned. If the peer address is
/// unspecified and has a port of 0, the socket will receive packets from every peer address.
fn associate_socket(
    socket: InetSocket,
    local_addr: SocketAddrV4,
    peer_addr: SocketAddrV4,
    net_ns: &NetworkNamespace,
    rng: impl rand::Rng,
) -> Result<SocketAddrV4, SyscallError> {
    log::trace!("Trying to associate socket with addresses (local={local_addr}, peer={peer_addr})");

    if !local_addr.ip().is_unspecified() && net_ns.interface_borrow(*local_addr.ip()).is_none() {
        log::debug!(
            "No network interface exists for the provided local address {}",
            local_addr.ip(),
        );
        return Err(Errno::EINVAL.into());
    };

    let protocol = match socket {
        InetSocket::Tcp(_) => c::_ProtocolType_PTCP,
    };

    // get a free ephemeral port if they didn't specify one
    let local_addr = if local_addr.port() != 0 {
        local_addr
    } else {
        let Some(new_port) = net_ns.get_random_free_port(protocol, *local_addr.ip(), peer_addr, rng) else {
            log::debug!("Association required an ephemeral port but none are available");
            return Err(Errno::EADDRINUSE.into());
        };

        log::debug!("Associating with generated ephemeral port {new_port}");

        // update the address with the same ip, but new port
        SocketAddrV4::new(*local_addr.ip(), new_port)
    };

    // make sure the port is available at this address for this protocol
    if !net_ns.is_interface_available(protocol, local_addr, peer_addr) {
        log::debug!(
            "The provided addresses (local={local_addr}, peer={peer_addr}) are not available"
        );
        return Err(Errno::EADDRINUSE.into());
    }

    let socket = unsafe { c::compatsocket_fromInetSocket(&socket) };

    // associate the interfaces corresponding to addr with socket
    unsafe { net_ns.associate_interface(&socket, protocol, local_addr, peer_addr) };

    Ok(local_addr)
}

mod export {
    use super::*;

    /// Decrement the ref count of the `InetSocket` object. The pointer must not be used after
    /// calling this function.
    #[no_mangle]
    pub extern "C" fn inetsocket_drop(socket: *const InetSocket) {
        assert!(!socket.is_null());
        unsafe { Box::from_raw(socket as *mut InetSocket) };
    }

    /// Increment the ref count of the `InetSocket` object. The returned pointer will not be the
    /// same as the given pointer (they are distinct references), and they both must be dropped
    /// with `inetsocket_drop` separately later.
    #[no_mangle]
    pub extern "C" fn inetsocket_cloneRef(socket: *const InetSocket) -> *const InetSocket {
        let socket = unsafe { socket.as_ref() }.unwrap();
        Box::into_raw(Box::new(socket.clone()))
    }

    /// Returns a handle uniquely identifying the socket. There can be many `InetSocket`s that point
    /// to a single socket object (an `InetSocket` is just an enum that contains an `Arc` of the
    /// socket object), so the address of an `InetSocket` *does not* uniquely identify a socket.
    #[no_mangle]
    pub extern "C" fn inetsocket_getCanonicalHandle(socket: *const InetSocket) -> usize {
        let socket = unsafe { socket.as_ref() }.unwrap();
        socket.canonical_handle()
    }

    #[no_mangle]
    pub extern "C" fn inetsocket_pushInPacket(socket: *const InetSocket, packet: *mut c::Packet) {
        let socket = unsafe { socket.as_ref() }.unwrap();
        let packet = Packet::from_raw(packet);

        crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
            CallbackQueue::queue_and_run(|cb_queue| {
                socket.borrow_mut().push_in_packet(packet, cb_queue);
            });
        });
    }

    #[no_mangle]
    pub extern "C" fn inetsocket_pullOutPacket(socket: *const InetSocket) -> *mut c::Packet {
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

    #[no_mangle]
    pub extern "C" fn inetsocket_peekNextOutPacket(socket: *const InetSocket) -> *const c::Packet {
        let socket = unsafe { socket.as_ref() }.unwrap();
        socket
            .borrow()
            .peek_next_out_packet()
            .map(|p| p.borrow_inner().cast_const())
            .unwrap_or(std::ptr::null())
    }

    #[no_mangle]
    pub extern "C" fn inetsocket_updatePacketHeader(
        socket: *const InetSocket,
        packet: *mut c::Packet,
    ) {
        let socket = unsafe { socket.as_ref() }.unwrap();
        let mut packet = Packet::from_raw(packet);
        socket.borrow().update_packet_header(&mut packet);
        packet.into_inner();
    }
}
