use std::ffi::CStr;
use std::net::{Ipv4Addr, SocketAddrV4};
use std::sync::Arc;

use atomic_refcell::AtomicRefCell;
use linux_api::errno::Errno;
use linux_api::ioctls::IoctlRequest;
use linux_api::socket::Shutdown;
use nix::sys::socket::{MsgFlags, SockaddrIn};
use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::core::worker::Worker;
use crate::cshadow as c;
use crate::host::descriptor::listener::{StateListenHandle, StateListenerFilter};
use crate::host::descriptor::socket::inet::{self, InetSocket};
use crate::host::descriptor::socket::{RecvmsgArgs, RecvmsgReturn, SendmsgArgs, Socket};
use crate::host::descriptor::{
    CompatFile, File, FileMode, FileSignals, FileState, FileStatus, OpenFile, SyscallResult,
};
use crate::host::host::Host;
use crate::host::memory_manager::MemoryManager;
use crate::host::network::interface::FifoPacketPriority;
use crate::host::network::namespace::NetworkNamespace;
use crate::host::syscall::io::{write_partial, IoVec};
use crate::host::syscall::types::{ForeignArrayPtr, SyscallError};
use crate::host::thread::ThreadId;
use crate::network::packet::PacketRc;
use crate::utility::callback_queue::CallbackQueue;
use crate::utility::sockaddr::SockaddrStorage;
use crate::utility::{HostTreePointer, ObjectCounter};

pub struct LegacyTcpSocket {
    socket: HostTreePointer<c::TCP>,
    // should only be used by `OpenFile` to make sure there is only ever one `OpenFile` instance for
    // this file
    has_open_file: bool,
    /// Did the last connect() call block, and if so what thread?
    thread_of_blocked_connect: Option<ThreadId>,
    _counter: ObjectCounter,
}

impl LegacyTcpSocket {
    pub fn new(status: FileStatus, host: &Host) -> Arc<AtomicRefCell<Self>> {
        let recv_buf_size = host.params.init_sock_recv_buf_size.try_into().unwrap();
        let send_buf_size = host.params.init_sock_send_buf_size.try_into().unwrap();

        let tcp = unsafe { c::tcp_new(host, recv_buf_size, send_buf_size) };
        let tcp = unsafe { Self::new_from_legacy(tcp) };

        tcp.borrow_mut().set_status(status);

        tcp
    }

    /// Takes ownership of the [`TCP`](c::TCP) reference.
    ///
    /// # Safety
    ///
    /// `legacy_tcp` must be safely dereferenceable, and not directly accessed again.
    pub unsafe fn new_from_legacy(legacy_tcp: *mut c::TCP) -> Arc<AtomicRefCell<Self>> {
        assert!(!legacy_tcp.is_null());

        let socket = Self {
            socket: HostTreePointer::new(legacy_tcp),
            has_open_file: false,
            thread_of_blocked_connect: None,
            _counter: ObjectCounter::new("LegacyTcpSocket"),
        };

        let rv = Arc::new(AtomicRefCell::new(socket));

        let inet_socket = InetSocket::LegacyTcp(rv.clone());
        let inet_socket = Box::into_raw(Box::new(inet_socket.downgrade()));
        unsafe { c::tcp_setRustSocket(legacy_tcp, inet_socket) };

        rv
    }

    /// Get a canonical handle for this socket. We use the address of the `TCP` object so that the
    /// rust socket and legacy socket have the same handle.
    pub fn canonical_handle(&self) -> usize {
        self.as_legacy_tcp() as usize
    }

    /// Get the [`c::TCP`] pointer.
    pub fn as_legacy_tcp(&self) -> *mut c::TCP {
        unsafe { self.socket.ptr() }
    }

    /// Get the [`c::TCP`] pointer as a [`c::LegacySocket`] pointer.
    pub fn as_legacy_socket(&self) -> *mut c::LegacySocket {
        self.as_legacy_tcp() as *mut c::LegacySocket
    }

    /// Get the [`c::TCP`] pointer as a [`c::LegacyFile`] pointer.
    pub fn as_legacy_file(&self) -> *mut c::LegacyFile {
        self.as_legacy_tcp() as *mut c::LegacyFile
    }

    pub fn status(&self) -> FileStatus {
        let o_flags = unsafe { c::legacyfile_getFlags(self.as_legacy_file()) };
        let o_flags =
            linux_api::fcntl::OFlag::from_bits(o_flags).expect("Not a valid OFlag: {o_flags:?}");
        let (status, extra_flags) = FileStatus::from_o_flags(o_flags);
        assert!(
            extra_flags.is_empty(),
            "Rust wrapper doesn't support {extra_flags:?} flags",
        );
        status
    }

    pub fn set_status(&mut self, status: FileStatus) {
        let o_flags = status.as_o_flags().bits();
        unsafe { c::legacyfile_setFlags(self.as_legacy_file(), o_flags) };
    }

    pub fn mode(&self) -> FileMode {
        FileMode::READ | FileMode::WRITE
    }

    pub fn has_open_file(&self) -> bool {
        self.has_open_file
    }

    pub fn supports_sa_restart(&self) -> bool {
        // TODO: false if a timeout has been set via setsockopt
        true
    }

    pub fn set_has_open_file(&mut self, val: bool) {
        self.has_open_file = val;
    }

    pub fn push_in_packet(
        &mut self,
        packet: PacketRc,
        _cb_queue: &mut CallbackQueue,
        _recv_time: EmulatedTime,
    ) {
        Worker::with_active_host(|host| {
            // the C code should ref the inner `Packet`, so it's fine to drop the `PacketRc`
            unsafe {
                c::legacysocket_pushInPacket(self.as_legacy_socket(), host, packet.borrow_inner())
            };
        })
        .unwrap();
    }

    pub fn pull_out_packet(&mut self, _cb_queue: &mut CallbackQueue) -> Option<PacketRc> {
        let packet = Worker::with_active_host(|host| unsafe {
            c::legacysocket_pullOutPacket(self.as_legacy_socket(), host)
        })
        .unwrap();

        if packet.is_null() {
            return None;
        }

        Worker::with_active_host(|host| unsafe {
            c::tcp_networkInterfaceIsAboutToSendPacket(self.as_legacy_tcp(), host, packet);
        })
        .unwrap();

        Some(PacketRc::from_raw(packet))
    }

    fn peek_packet(&self) -> Option<PacketRc> {
        let packet = unsafe { c::legacysocket_peekNextOutPacket(self.as_legacy_socket()) };

        if packet.is_null() {
            return None;
        }

        let packet = PacketRc::from_raw(packet);
        unsafe { c::packet_ref(packet.borrow_inner()) }
        Some(packet)
    }

    pub fn peek_next_packet_priority(&self) -> Option<FifoPacketPriority> {
        self.peek_packet().map(|p| p.priority())
    }

    pub fn has_data_to_send(&self) -> bool {
        self.peek_packet().is_some()
    }

    pub fn getsockname(&self) -> Result<Option<SockaddrIn>, SyscallError> {
        let mut ip: libc::in_addr_t = 0;
        let mut port: libc::in_port_t = 0;

        // should return ip and port in network byte order
        let okay =
            unsafe { c::legacysocket_getSocketName(self.as_legacy_socket(), &mut ip, &mut port) };
        if okay != 1 {
            return Ok(Some(SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, 0).into()));
        }

        let ip = Ipv4Addr::from(u32::from_be(ip));
        let port = u16::from_be(port);
        let addr = SocketAddrV4::new(ip, port);

        Ok(Some(addr.into()))
    }

    pub fn getpeername(&self) -> Result<Option<SockaddrIn>, SyscallError> {
        let mut ip: libc::in_addr_t = 0;
        let mut port: libc::in_port_t = 0;

        // should return ip and port in network byte order
        let okay =
            unsafe { c::legacysocket_getPeerName(self.as_legacy_socket(), &mut ip, &mut port) };
        if okay != 1 {
            return Err(Errno::ENOTCONN.into());
        }

        let ip = Ipv4Addr::from(u32::from_be(ip));
        let port = u16::from_be(port);
        let addr = SocketAddrV4::new(ip, port);

        Ok(Some(addr.into()))
    }

    pub fn address_family(&self) -> linux_api::socket::AddressFamily {
        linux_api::socket::AddressFamily::AF_INET
    }

    pub fn close(&mut self, _cb_queue: &mut CallbackQueue) -> Result<(), SyscallError> {
        Worker::with_active_host(|h| {
            unsafe { c::legacyfile_close(self.as_legacy_file(), h) };
        })
        .unwrap();
        Ok(())
    }

    pub fn bind(
        socket: &Arc<AtomicRefCell<Self>>,
        addr: Option<&SockaddrStorage>,
        net_ns: &NetworkNamespace,
        rng: impl rand::Rng,
    ) -> SyscallResult {
        // if the address pointer was NULL
        let Some(addr) = addr else {
            return Err(Errno::EFAULT.into());
        };

        // if not an inet socket address
        let Some(addr) = addr.as_inet() else {
            return Err(Errno::EINVAL.into());
        };

        let addr: SocketAddrV4 = (*addr).into();

        // if the socket is already bound
        {
            let socket = socket.borrow();
            let socket = socket.as_legacy_socket();
            if unsafe { c::legacysocket_isBound(socket) } == 1 {
                return Err(Errno::EINVAL.into());
            }
        }

        // make sure the socket doesn't have a peer
        {
            // Since we're not bound, we're not connected and have no peer. We may have a peer in
            // the future if `connect()` is called on this socket.
            let socket = socket.borrow();
            let socket = socket.as_legacy_socket();
            assert_eq!(0, unsafe {
                c::legacysocket_getPeerName(socket, std::ptr::null_mut(), std::ptr::null_mut())
            });
        }

        // this will allow us to receive packets from any peer
        let peer_addr = SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, 0);

        // associate the socket
        let (addr, handle) = inet::associate_socket(
            InetSocket::LegacyTcp(Arc::clone(socket)),
            addr,
            peer_addr,
            /* check_generic_peer= */ true,
            net_ns,
            rng,
        )?;

        // the handle normally disassociates the socket when dropped, but the C TCP code does it's
        // own manual disassociation, so we'll just let it do its own thing
        std::mem::forget(handle);

        // update the socket's local address
        let socket = socket.borrow_mut();
        let socket = socket.as_legacy_socket();
        unsafe {
            c::legacysocket_setSocketName(
                socket,
                u32::from(*addr.ip()).to_be(),
                addr.port().to_be(),
            )
        };

        Ok(0.into())
    }

    pub fn readv(
        &mut self,
        _iovs: &[IoVec],
        _offset: Option<libc::off_t>,
        _flags: libc::c_int,
        _mem: &mut MemoryManager,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        // we could call LegacyTcpSocket::recvmsg() here, but for now we expect that there are no
        // code paths that would call LegacyTcpSocket::readv() since the readv() syscall handler
        // should have called LegacyTcpSocket::recvmsg() instead
        panic!("Called LegacyTcpSocket::readv() on a TCP socket.");
    }

    pub fn writev(
        &mut self,
        _iovs: &[IoVec],
        _offset: Option<libc::off_t>,
        _flags: libc::c_int,
        _mem: &mut MemoryManager,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        // we could call LegacyTcpSocket::sendmsg() here, but for now we expect that there are no
        // code paths that would call LegacyTcpSocket::writev() since the writev() syscall handler
        // should have called LegacyTcpSocket::sendmsg() instead
        panic!("Called LegacyTcpSocket::writev() on a TCP socket");
    }

    pub fn sendmsg(
        socket: &Arc<AtomicRefCell<Self>>,
        args: SendmsgArgs,
        mem: &mut MemoryManager,
        _net_ns: &NetworkNamespace,
        _rng: impl rand::Rng,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        let socket_ref = socket.borrow_mut();
        let tcp = socket_ref.as_legacy_tcp();

        if socket_ref.state().contains(FileState::CLOSED) {
            // A file that is referenced in the descriptor table should never be a closed file. File
            // handles (fds) are handles to open files, so if we have a file handle to a closed
            // file, then there's an error somewhere in Shadow. Shadow's TCP sockets do close
            // themselves even if there are still file handles (see `_tcp_endOfFileSignalled`), so
            // we can't make this a panic.
            log::warn!("Sending on a closed TCP socket");
            return Err(Errno::EBADF.into());
        }

        let Some(mut flags) = MsgFlags::from_bits(args.flags) else {
            log::warn!("Unrecognized send flags: {:#b}", args.flags);
            return Err(Errno::EINVAL.into());
        };

        if socket_ref.status().contains(FileStatus::NONBLOCK) {
            flags.insert(MsgFlags::MSG_DONTWAIT);
        }

        // run in a closure so that an early return doesn't skip checking if we should block
        let result = (|| {
            let mut bytes_sent = 0;

            for iov in args.iovs {
                let errcode = unsafe { c::tcp_getConnectionError(tcp) };

                log::trace!("Connection error state is currently {errcode}");

                #[allow(clippy::if_same_then_else)]
                if errcode > 0 {
                    // connect() was not called yet
                    // TODO: Can they can piggy back a connect() on sendto() if they provide an
                    // address for the connection?
                    if bytes_sent == 0 {
                        return Err(Errno::EPIPE);
                    } else {
                        break;
                    }
                } else if errcode == 0 {
                    // They connected, but never read the success code with a second call to
                    // connect(). That's OK, proceed to send as usual.
                } else if errcode == -libc::EISCONN {
                    // they are connected, and we can send now
                } else if errcode == -libc::EALREADY {
                    // connection in progress
                    // TODO: should we wait, or just return -EALREADY?
                    if bytes_sent == 0 {
                        return Err(Errno::EWOULDBLOCK);
                    } else {
                        break;
                    }
                }

                // SAFETY: We're passing an immutable pointer to the memory manager. We should not
                // have any other mutable references to the memory manager at this point.
                let rv = Worker::with_active_host(|host| unsafe {
                    c::tcp_sendUserData(
                        tcp,
                        host,
                        iov.base.cast::<()>(),
                        iov.len.try_into().unwrap(),
                        0,
                        0,
                        mem,
                    )
                })
                .unwrap();

                if rv < 0 {
                    if bytes_sent == 0 {
                        return Err(Errno::try_from(-rv).unwrap());
                    } else {
                        break;
                    }
                }

                bytes_sent += rv;

                if usize::try_from(rv).unwrap() < iov.len {
                    // stop if we didn't write all of the data in the iov
                    break;
                }
            }

            Ok(bytes_sent)
        })();

        // if the syscall would block and we don't have the MSG_DONTWAIT flag
        if result == Err(Errno::EWOULDBLOCK) && !flags.contains(MsgFlags::MSG_DONTWAIT) {
            return Err(SyscallError::new_blocked_on_file(
                File::Socket(Socket::Inet(InetSocket::LegacyTcp(socket.clone()))),
                FileState::WRITABLE,
                socket_ref.supports_sa_restart(),
            ));
        }

        Ok(result?.try_into().unwrap())
    }

    pub fn recvmsg(
        socket: &Arc<AtomicRefCell<Self>>,
        mut args: RecvmsgArgs,
        mem: &mut MemoryManager,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<RecvmsgReturn, SyscallError> {
        let socket_ref = socket.borrow_mut();
        let tcp = socket_ref.as_legacy_tcp();

        if socket_ref.state().contains(FileState::CLOSED) {
            // A file that is referenced in the descriptor table should never be a closed file. File
            // handles (fds) are handles to open files, so if we have a file handle to a closed
            // file, then there's an error somewhere in Shadow. Shadow's TCP sockets do close
            // themselves even if there are still file handles (see `_tcp_endOfFileSignalled`), so
            // we can't make this a panic.
            if unsafe { c::tcp_getConnectionError(tcp) != -libc::EISCONN } {
                // connection error will be -ENOTCONN when reading is done
                log::warn!("Receiving on a closed TCP socket");
                return Err(Errno::EBADF.into());
            }
        }

        let Some(mut flags) = MsgFlags::from_bits(args.flags) else {
            log::warn!("Unrecognized recv flags: {:#b}", args.flags);
            return Err(Errno::EINVAL.into());
        };

        if socket_ref.status().contains(FileStatus::NONBLOCK) {
            flags.insert(MsgFlags::MSG_DONTWAIT);
        }

        // run in a closure so that an early return doesn't skip checking if we should block
        let result = (|| {
            let mut bytes_read = 0;

            // want to make sure we run the loop at least once so that we can return any errors
            if args.iovs.is_empty() {
                const EMPTY_IOV: IoVec = IoVec {
                    base: ForeignPtr::null(),
                    len: 0,
                };
                args.iovs = std::slice::from_ref(&EMPTY_IOV);
            }

            for iov in args.iovs {
                let errcode = unsafe { c::tcp_getConnectionError(tcp) };

                if errcode > 0 {
                    // connect() was not called yet
                    if bytes_read == 0 {
                        return Err(Errno::ENOTCONN);
                    } else {
                        break;
                    }
                } else if errcode == -libc::EALREADY {
                    // Connection in progress
                    if bytes_read == 0 {
                        return Err(Errno::EWOULDBLOCK);
                    } else {
                        break;
                    }
                }

                // SAFETY: We're passing a mutable pointer to the memory manager. We should not have
                // any other mutable references to the memory manager at this point.
                let rv = Worker::with_active_host(|host| unsafe {
                    c::tcp_receiveUserData(
                        tcp,
                        host,
                        iov.base.cast::<()>(),
                        iov.len.try_into().unwrap(),
                        std::ptr::null_mut(),
                        std::ptr::null_mut(),
                        mem,
                    )
                })
                .unwrap();

                if rv < 0 {
                    if bytes_read == 0 {
                        return Err(Errno::try_from(-rv).unwrap());
                    } else {
                        break;
                    }
                }

                bytes_read += rv;

                if usize::try_from(rv).unwrap() < iov.len {
                    // stop if we didn't receive all of the data in the iov
                    break;
                }
            }

            Ok(RecvmsgReturn {
                return_val: bytes_read.try_into().unwrap(),
                addr: None,
                msg_flags: 0,
                control_len: 0,
            })
        })();

        // if the syscall would block and we don't have the MSG_DONTWAIT flag
        if result.as_ref().err() == Some(&Errno::EWOULDBLOCK)
            && !flags.contains(MsgFlags::MSG_DONTWAIT)
        {
            return Err(SyscallError::new_blocked_on_file(
                File::Socket(Socket::Inet(InetSocket::LegacyTcp(socket.clone()))),
                FileState::READABLE,
                socket_ref.supports_sa_restart(),
            ));
        }

        Ok(result?)
    }

    pub fn ioctl(
        &mut self,
        request: IoctlRequest,
        arg_ptr: ForeignPtr<()>,
        memory_manager: &mut MemoryManager,
    ) -> SyscallResult {
        match request {
            // equivalent to SIOCINQ
            IoctlRequest::FIONREAD => {
                let len = unsafe { c::tcp_getInputBufferLength(self.as_legacy_tcp()) }
                    .try_into()
                    .unwrap();

                let arg_ptr = arg_ptr.cast::<libc::c_int>();
                memory_manager.write(arg_ptr, &len)?;

                Ok(0.into())
            }
            // equivalent to SIOCOUTQ
            IoctlRequest::TIOCOUTQ => {
                let len = unsafe { c::tcp_getOutputBufferLength(self.as_legacy_tcp()) }
                    .try_into()
                    .unwrap();

                let arg_ptr = arg_ptr.cast::<libc::c_int>();
                memory_manager.write(arg_ptr, &len)?;

                Ok(0.into())
            }
            IoctlRequest::SIOCOUTQNSD => {
                let len = unsafe { c::tcp_getNotSentBytes(self.as_legacy_tcp()) }
                    .try_into()
                    .unwrap();

                let arg_ptr = arg_ptr.cast::<libc::c_int>();
                memory_manager.write(arg_ptr, &len)?;

                Ok(0.into())
            }
            // this isn't supported by tcp
            IoctlRequest::SIOCGSTAMP => Err(Errno::ENOENT.into()),
            IoctlRequest::FIONBIO => {
                panic!("This should have been handled by the ioctl syscall handler");
            }
            IoctlRequest::TCGETS
            | IoctlRequest::TCSETS
            | IoctlRequest::TCSETSW
            | IoctlRequest::TCSETSF
            | IoctlRequest::TCGETA
            | IoctlRequest::TCSETA
            | IoctlRequest::TCSETAW
            | IoctlRequest::TCSETAF
            | IoctlRequest::TIOCGWINSZ
            | IoctlRequest::TIOCSWINSZ => {
                // not a terminal
                Err(Errno::ENOTTY.into())
            }
            request => {
                warn_once_then_debug!(
                    "We do not yet handle ioctl request {request:?} on tcp sockets"
                );
                Err(Errno::EINVAL.into())
            }
        }
    }

    pub fn stat(&self) -> Result<linux_api::stat::stat, SyscallError> {
        warn_once_then_debug!("We do not yet handle stat calls on tcp sockets");
        Err(Errno::EINVAL.into())
    }

    pub fn listen(
        socket: &Arc<AtomicRefCell<Self>>,
        backlog: i32,
        net_ns: &NetworkNamespace,
        rng: impl rand::Rng,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<(), SyscallError> {
        let socket_ref = socket.borrow();

        // only listen on the socket if it is not used for other functions
        let is_listening_allowed =
            unsafe { c::tcp_isListeningAllowed(socket_ref.as_legacy_tcp()) } == 1;
        if !is_listening_allowed {
            log::debug!("Cannot listen on previously used socket");
            return Err(Errno::EOPNOTSUPP.into());
        }

        // if we are already listening, just update the backlog and return 0
        let is_valid_listener = unsafe { c::tcp_isValidListener(socket_ref.as_legacy_tcp()) } == 1;
        if is_valid_listener {
            log::trace!("Socket already set up as a listener; updating backlog");
            unsafe { c::tcp_updateServerBacklog(socket_ref.as_legacy_tcp(), backlog) };
            return Ok(());
        }

        // a listening socket must be bound
        let is_bound = unsafe { c::legacysocket_isBound(socket_ref.as_legacy_socket()) } == 1;
        if !is_bound {
            log::trace!("Implicitly binding listener socket");

            // implicit bind: bind to all interfaces at an ephemeral port
            let local_addr = SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, 0);

            // this will allow us to receive packets from any peer address
            let peer_addr = SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, 0);

            // associate the socket
            let (local_addr, handle) = super::associate_socket(
                super::InetSocket::LegacyTcp(socket.clone()),
                local_addr,
                peer_addr,
                /* check_generic_peer= */ true,
                net_ns,
                rng,
            )?;

            // the handle normally disassociates the socket when dropped, but the C TCP code does
            // it's own manual disassociation, so we'll just let it do its own thing
            std::mem::forget(handle);

            unsafe {
                c::legacysocket_setSocketName(
                    socket_ref.as_legacy_socket(),
                    u32::from(*local_addr.ip()).to_be(),
                    local_addr.port().to_be(),
                )
            };
        }

        // we are allowed to listen but not already listening; start now
        Worker::with_active_host(|host| {
            unsafe {
                c::tcp_enterServerMode(
                    socket_ref.as_legacy_tcp(),
                    host,
                    Worker::active_process_id().unwrap().into(),
                    backlog,
                )
            };
        })
        .unwrap();

        Ok(())
    }

    pub fn connect(
        socket: &Arc<AtomicRefCell<Self>>,
        peer_addr: &SockaddrStorage,
        net_ns: &NetworkNamespace,
        rng: impl rand::Rng,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<(), SyscallError> {
        let mut socket_ref = socket.borrow_mut();

        if let Some(tid) = socket_ref.thread_of_blocked_connect {
            // check if there is already a blocking connect() call on another thread
            if tid != Worker::active_thread_id().unwrap() {
                // connect(2) says "Generally,  connection-based protocol sockets may successfully
                // connect() only once", but the application is attempting to call connect() in two
                // threads on a blocking socket at the same time. Let's just return an error and
                // hope no one ever does this.
                log::warn!("Two threads are attempting to connect() on a blocking socket");
                return Err(Errno::EBADFD.into());
            }
        }

        let Some(peer_addr) = peer_addr.as_inet() else {
            return Err(Errno::EINVAL.into());
        };

        let mut peer_addr: std::net::SocketAddrV4 = (*peer_addr).into();

        // https://stackoverflow.com/a/22425796
        if peer_addr.ip().is_unspecified() {
            peer_addr.set_ip(std::net::Ipv4Addr::LOCALHOST);
        }

        let host_default_ip = net_ns.default_ip;

        // NOTE: it would be nice to use `Ipv4Addr::is_loopback` in this code rather than comparing
        // to `Ipv4Addr::LOCALHOST`, but the rest of Shadow probably can't handle other loopback
        // addresses (ex: 127.0.0.2) and it's probably best not to change this behaviour

        // make sure we will be able to route this later
        // TODO: should we just send the SYN and let the connection fail normally?
        if peer_addr.ip() != &std::net::Ipv4Addr::LOCALHOST {
            let is_routable = Worker::is_routable(host_default_ip.into(), (*peer_addr.ip()).into());

            if !is_routable {
                // can't route it - there is no node with this address
                log::warn!(
                    "Attempting to connect to address '{peer_addr}' for which no host exists"
                );
                return Err(Errno::ECONNREFUSED.into());
            }
        }

        // a connected tcp socket must be bound
        let is_bound = unsafe { c::legacysocket_isBound(socket_ref.as_legacy_socket()) } == 1;
        if !is_bound {
            log::trace!("Implicitly binding listener socket");

            // implicit bind: bind to an ephemeral port (use default interface unless the remote
            // peer is on loopback)
            let local_addr = if peer_addr.ip() == &std::net::Ipv4Addr::LOCALHOST {
                SocketAddrV4::new(Ipv4Addr::LOCALHOST, 0)
            } else {
                SocketAddrV4::new(host_default_ip, 0)
            };

            // associate the socket
            let (local_addr, handle) = super::associate_socket(
                super::InetSocket::LegacyTcp(socket.clone()),
                local_addr,
                peer_addr,
                /* check_generic_peer= */ true,
                net_ns,
                rng,
            )?;

            // the handle normally disassociates the socket when dropped, but the C TCP code does
            // it's own manual disassociation, so we'll just let it do its own thing
            std::mem::forget(handle);

            unsafe {
                c::legacysocket_setSocketName(
                    socket_ref.as_legacy_socket(),
                    u32::from(*local_addr.ip()).to_be(),
                    local_addr.port().to_be(),
                )
            };
        }

        unsafe {
            c::legacysocket_setPeerName(
                socket_ref.as_legacy_socket(),
                u32::from(*peer_addr.ip()).to_be(),
                peer_addr.port().to_be(),
            )
        };

        // now we are ready to connect
        let errcode = Worker::with_active_host(|host| unsafe {
            c::legacysocket_connectToPeer(
                socket_ref.as_legacy_socket(),
                host,
                u32::from(*peer_addr.ip()).to_be(),
                peer_addr.port().to_be(),
                libc::AF_INET as u16,
            )
        })
        .unwrap();

        assert!(errcode <= 0);

        let mut errcode = if errcode < 0 {
            Err(Errno::try_from(-errcode).unwrap())
        } else {
            Ok(())
        };

        if !socket_ref.status().contains(FileStatus::NONBLOCK) {
            // this is a blocking connect call
            if errcode == Err(Errno::EINPROGRESS) {
                // This is the first time we ever called connect, and so we need to wait for the
                // 3-way handshake to complete. We will wait indefinitely for a success or failure.

                let err = SyscallError::new_blocked_on_file(
                    File::Socket(Socket::Inet(InetSocket::LegacyTcp(Arc::clone(socket)))),
                    FileState::ACTIVE | FileState::WRITABLE,
                    socket_ref.supports_sa_restart(),
                );

                // block the current thread
                socket_ref.thread_of_blocked_connect = Some(Worker::active_thread_id().unwrap());
                return Err(err);
            }

            // if we were previously blocked (we checked the thread ID above) and are now connected
            if socket_ref.thread_of_blocked_connect.is_some() && errcode == Err(Errno::EISCONN) {
                // it was EINPROGRESS, but is now a successful blocking connect
                errcode = Ok(());
            }
        }

        // make sure we return valid error codes for connect
        if errcode == Err(Errno::ECONNRESET) || errcode == Err(Errno::ENOTCONN) {
            errcode = Err(Errno::EISCONN);
        }
        // EALREADY is well defined in man page, but Linux returns EINPROGRESS
        if errcode == Err(Errno::EALREADY) {
            errcode = Err(Errno::EINPROGRESS);
        }

        socket_ref.thread_of_blocked_connect = None;
        errcode.map_err(Into::into)
    }

    pub fn accept(
        &mut self,
        _net_ns: &NetworkNamespace,
        _rng: impl rand::Rng,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<OpenFile, SyscallError> {
        let is_valid_listener = unsafe { c::tcp_isValidListener(self.as_legacy_tcp()) } == 1;

        // we must be listening in order to accept
        if !is_valid_listener {
            log::debug!("Socket is not listening");
            return Err(Errno::EINVAL.into());
        }

        let mut peer_addr: libc::sockaddr_in = shadow_pod::zeroed();
        peer_addr.sin_family = libc::AF_INET as u16;
        let mut accepted_fd = -1;

        // now we can check if we have anything to accept
        let errcode = Worker::with_active_host(|host| unsafe {
            c::tcp_acceptServerPeer(
                self.as_legacy_tcp(),
                host,
                &mut peer_addr.sin_addr.s_addr,
                &mut peer_addr.sin_port,
                &mut accepted_fd,
            )
        })
        .unwrap();

        assert!(errcode <= 0);

        if errcode < 0 {
            log::trace!("TCP error when accepting connection");
            return Err(Errno::try_from(-errcode).unwrap().into());
        }

        // we accepted something!
        assert!(accepted_fd >= 0);

        // The rust socket syscall interface expects us to return the socket object so that it can
        // add it to the descriptor table, but the TCP code has already added it to the descriptor
        // table (see https://github.com/shadow/shadow/issues/1780). We'll remove the socket from
        // the descriptor table, return it to the syscall handler, and let the syscall handler
        // re-add it to the descriptor table. It may end up with a different fd handle, but that
        // should be fine since nothing should be relying on the socket having a specific/fixed fd
        // handle.

        let new_descriptor = Worker::with_active_host(|host| {
            Worker::with_active_thread(|thread| {
                thread
                    .descriptor_table_borrow_mut(host)
                    .deregister_descriptor(accepted_fd.try_into().unwrap())
                    .unwrap()
            })
        })
        .unwrap()
        .unwrap();

        let CompatFile::New(open_file) = new_descriptor.into_file() else {
            panic!("The TCP code should have added the TCP socket to the descriptor table as a rust socket");
        };

        // sanity check: make sure new socket peer address matches address returned from
        // tcp_acceptServerPeer() above
        {
            let File::Socket(Socket::Inet(InetSocket::LegacyTcp(new_socket))) =
                open_file.inner_file()
            else {
                panic!("Expected this to be a LegacyTcpSocket");
            };

            let new_socket = new_socket.borrow();

            let mut ip: libc::in_addr_t = 0;
            let mut port: libc::in_port_t = 0;

            // should return ip and port in network byte order
            let okay = unsafe {
                c::legacysocket_getPeerName(new_socket.as_legacy_socket(), &mut ip, &mut port)
            };

            assert_eq!(okay, 1);
            assert_eq!(ip, peer_addr.sin_addr.s_addr);
            assert_eq!(port, peer_addr.sin_port);
        }

        Ok(open_file)
    }

    pub fn shutdown(
        &mut self,
        how: Shutdown,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<(), SyscallError> {
        let how = match how {
            Shutdown::SHUT_RD => libc::SHUT_RD,
            Shutdown::SHUT_WR => libc::SHUT_WR,
            Shutdown::SHUT_RDWR => libc::SHUT_RDWR,
        };

        let errcode = Worker::with_active_host(|host| unsafe {
            c::tcp_shutdown(self.as_legacy_tcp(), host, how)
        })
        .unwrap();

        assert!(errcode <= 0);

        if errcode < 0 {
            return Err(Errno::try_from(-errcode).unwrap().into());
        }

        Ok(())
    }

    pub fn getsockopt(
        &self,
        level: libc::c_int,
        optname: libc::c_int,
        optval_ptr: ForeignPtr<()>,
        optlen: libc::socklen_t,
        memory_manager: &mut MemoryManager,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<libc::socklen_t, SyscallError> {
        match (level, optname) {
            (libc::SOL_TCP, libc::TCP_INFO) => {
                let mut info = shadow_pod::zeroed();
                unsafe { c::tcp_getInfo(self.as_legacy_tcp(), &mut info) };

                let optval_ptr = optval_ptr.cast::<crate::cshadow::tcp_info>();
                let bytes_written =
                    write_partial(memory_manager, &info, optval_ptr, optlen as usize)?;

                Ok(bytes_written as libc::socklen_t)
            }
            (libc::SOL_TCP, libc::TCP_NODELAY) => {
                // shadow doesn't support nagle's algorithm, so shadow always behaves as if
                // TCP_NODELAY is enabled
                let val = 1;

                let optval_ptr = optval_ptr.cast::<libc::c_int>();
                let bytes_written =
                    write_partial(memory_manager, &val, optval_ptr, optlen as usize)?;

                Ok(bytes_written as libc::socklen_t)
            }
            (libc::SOL_TCP, libc::TCP_CONGESTION) => {
                // the value of TCP_CA_NAME_MAX in linux
                const CONG_NAME_MAX: usize = 16;

                if optval_ptr.is_null() {
                    return Err(Errno::EINVAL.into());
                }

                let name: *const libc::c_char =
                    unsafe { c::tcpcong_nameStr(c::tcp_cong(self.as_legacy_tcp())) };
                assert!(!name.is_null(), "shadow's congestion type has no name");
                let name = unsafe { CStr::from_ptr(name) };
                let name = name.to_bytes_with_nul();

                let bytes_to_copy = *[optlen as usize, CONG_NAME_MAX, name.len()]
                    .iter()
                    .min()
                    .unwrap();

                let name = &name[..bytes_to_copy];
                let optval_ptr = optval_ptr.cast::<u8>();
                let optval_ptr = ForeignArrayPtr::new(optval_ptr, bytes_to_copy);

                memory_manager.copy_to_ptr(optval_ptr, name)?;

                // the len value returned by linux seems to be independent from the actual string length
                Ok(std::cmp::min(optlen as usize, CONG_NAME_MAX) as libc::socklen_t)
            }
            (libc::SOL_SOCKET, libc::SO_SNDBUF) => {
                let sndbuf_size: libc::c_int =
                    unsafe { c::legacysocket_getOutputBufferSize(self.as_legacy_socket()) }
                        .try_into()
                        .unwrap();

                let optval_ptr = optval_ptr.cast::<libc::c_int>();
                let bytes_written =
                    write_partial(memory_manager, &sndbuf_size, optval_ptr, optlen as usize)?;

                Ok(bytes_written as libc::socklen_t)
            }
            (libc::SOL_SOCKET, libc::SO_RCVBUF) => {
                let rcvbuf_size: libc::c_int =
                    unsafe { c::legacysocket_getInputBufferSize(self.as_legacy_socket()) }
                        .try_into()
                        .unwrap();

                let optval_ptr = optval_ptr.cast::<libc::c_int>();
                let bytes_written =
                    write_partial(memory_manager, &rcvbuf_size, optval_ptr, optlen as usize)?;

                Ok(bytes_written as libc::socklen_t)
            }
            (libc::SOL_SOCKET, libc::SO_ERROR) => {
                // return error for failed connect() attempts
                let conn_err = unsafe { c::tcp_getConnectionError(self.as_legacy_tcp()) };

                let error = if conn_err == -libc::ECONNRESET || conn_err == -libc::ECONNREFUSED {
                    // result is a positive errcode
                    -conn_err
                } else {
                    0
                };

                let optval_ptr = optval_ptr.cast::<libc::c_int>();
                let bytes_written =
                    write_partial(memory_manager, &error, optval_ptr, optlen as usize)?;

                Ok(bytes_written as libc::socklen_t)
            }
            (libc::SOL_SOCKET, libc::SO_DOMAIN) => {
                let domain = libc::AF_INET;

                let optval_ptr = optval_ptr.cast::<libc::c_int>();
                let bytes_written =
                    write_partial(memory_manager, &domain, optval_ptr, optlen as usize)?;

                Ok(bytes_written as libc::socklen_t)
            }
            (libc::SOL_SOCKET, libc::SO_TYPE) => {
                let sock_type = libc::SOCK_STREAM;

                let optval_ptr = optval_ptr.cast::<libc::c_int>();
                let bytes_written =
                    write_partial(memory_manager, &sock_type, optval_ptr, optlen as usize)?;

                Ok(bytes_written as libc::socklen_t)
            }
            (libc::SOL_SOCKET, libc::SO_PROTOCOL) => {
                let protocol = libc::IPPROTO_TCP;

                let optval_ptr = optval_ptr.cast::<libc::c_int>();
                let bytes_written =
                    write_partial(memory_manager, &protocol, optval_ptr, optlen as usize)?;

                Ok(bytes_written as libc::socklen_t)
            }
            (libc::SOL_SOCKET, libc::SO_ACCEPTCONN) => {
                let is_listener = unsafe { c::tcp_isValidListener(self.as_legacy_tcp()) };

                let optval_ptr = optval_ptr.cast::<libc::c_int>();
                let bytes_written =
                    write_partial(memory_manager, &is_listener, optval_ptr, optlen as usize)?;

                Ok(bytes_written as libc::socklen_t)
            }
            _ => {
                log_once_per_value_at_level!(
                    (level, optname),
                    (i32, i32),
                    log::Level::Warn,
                    log::Level::Debug,
                    "getsockopt called with unsupported level {level} and opt {optname}"
                );
                Err(Errno::ENOPROTOOPT.into())
            }
        }
    }

    pub fn setsockopt(
        &mut self,
        level: libc::c_int,
        optname: libc::c_int,
        optval_ptr: ForeignPtr<()>,
        optlen: libc::socklen_t,
        memory_manager: &MemoryManager,
    ) -> Result<(), SyscallError> {
        match (level, optname) {
            (libc::SOL_TCP, libc::TCP_NODELAY) => {
                // Shadow doesn't support nagle's algorithm, so Shadow always behaves as if
                // TCP_NODELAY is enabled. Some programs will fail if `setsockopt(fd, SOL_TCP,
                // TCP_NODELAY, &1, sizeof(int))` returns an error, so we treat this as a no-op for
                // compatibility.

                type OptType = libc::c_int;

                if usize::try_from(optlen).unwrap() < std::mem::size_of::<OptType>() {
                    return Err(Errno::EINVAL.into());
                }

                let optval_ptr = optval_ptr.cast::<OptType>();
                let enable = memory_manager.read(optval_ptr)?;

                if enable != 0 {
                    // wants to enable TCP_NODELAY
                    log::debug!("Ignoring TCP_NODELAY");
                } else {
                    // wants to disable TCP_NODELAY
                    log::warn!("Cannot disable TCP_NODELAY since shadow does not implement Nagle's algorithm.");
                    return Err(Errno::ENOPROTOOPT.into());
                }
            }
            (libc::SOL_TCP, libc::TCP_CONGESTION) => {
                // the value of TCP_CA_NAME_MAX in linux
                const CONG_NAME_MAX: usize = 16;

                let mut name = [0u8; CONG_NAME_MAX];

                let optlen = std::cmp::min(optlen as usize, CONG_NAME_MAX);
                let name = &mut name[..optlen];

                let optval_ptr = optval_ptr.cast::<u8>();
                let optval_ptr = ForeignArrayPtr::new(optval_ptr, optlen);
                memory_manager.copy_from_ptr(name, optval_ptr)?;

                // truncate the name at the first NUL character if there is one, but don't include
                // the NUL since in linux the strings don't need a NUL
                let name = name
                    .iter()
                    .position(|x| *x == 0)
                    .map(|x| &name[..x])
                    .unwrap_or(name);

                let reno = unsafe { CStr::from_ptr(c::TCP_CONG_RENO_NAME) }.to_bytes();

                if name != reno {
                    log::warn!("Shadow sockets only support '{reno:?}' for TCP_CONGESTION");
                    return Err(Errno::ENOENT.into());
                }

                // shadow doesn't support other congestion types, so do nothing
            }
            (libc::SOL_SOCKET, libc::SO_SNDBUF) => {
                type OptType = libc::c_int;

                if usize::try_from(optlen).unwrap() < std::mem::size_of::<OptType>() {
                    return Err(Errno::EINVAL.into());
                }

                let optval_ptr = optval_ptr.cast::<OptType>();
                let val: u64 = memory_manager
                    .read(optval_ptr)?
                    .try_into()
                    .or(Err(Errno::EINVAL))?;

                // linux kernel doubles this value upon setting
                let val = val * 2;

                // Linux also has limits SOCK_MIN_SNDBUF (slightly greater than 4096) and the sysctl
                // max limit. We choose a reasonable lower limit for Shadow. The minimum limit in
                // man 7 socket is incorrect.
                let val = std::cmp::max(val, 4096);

                // This upper limit was added as an arbitrarily high number so that we don't change
                // Shadow's behaviour, but also prevents an application from setting this to
                // something unnecessarily large like INT_MAX.
                let val = std::cmp::min(val, 268435456); // 2^28 = 256 MiB

                unsafe { c::legacysocket_setOutputBufferSize(self.as_legacy_socket(), val) };
                unsafe { c::tcp_disableSendBufferAutotuning(self.as_legacy_tcp()) };
            }
            (libc::SOL_SOCKET, libc::SO_RCVBUF) => {
                type OptType = libc::c_int;

                if usize::try_from(optlen).unwrap() < std::mem::size_of::<OptType>() {
                    return Err(Errno::EINVAL.into());
                }

                let optval_ptr = optval_ptr.cast::<OptType>();
                let val: u64 = memory_manager
                    .read(optval_ptr)?
                    .try_into()
                    .or(Err(Errno::EINVAL))?;

                // linux kernel doubles this value upon setting
                let val = val * 2;

                // Linux also has limits SOCK_MIN_RCVBUF (slightly greater than 2048) and the sysctl
                // max limit. We choose a reasonable lower limit for Shadow. The minimum limit in
                // man 7 socket is incorrect.
                let val = std::cmp::max(val, 2048);

                // This upper limit was added as an arbitrarily high number so that we don't change
                // Shadow's behaviour, but also prevents an application from setting this to
                // something unnecessarily large like INT_MAX.
                let val = std::cmp::min(val, 268435456); // 2^28 = 256 MiB

                unsafe { c::legacysocket_setInputBufferSize(self.as_legacy_socket(), val) };
                unsafe { c::tcp_disableReceiveBufferAutotuning(self.as_legacy_tcp()) };
            }
            (libc::SOL_SOCKET, libc::SO_REUSEADDR) => {
                // TODO: implement this, tor and tgen use it
                log::trace!("setsockopt SO_REUSEADDR not yet implemented");
            }
            (libc::SOL_SOCKET, libc::SO_REUSEPORT) => {
                // TODO: implement this, tgen uses it
                log::trace!("setsockopt SO_REUSEPORT not yet implemented");
            }
            (libc::SOL_SOCKET, libc::SO_KEEPALIVE) => {
                // TODO: implement this, libevent uses it in
                // evconnlistener_new_bind()
                log::trace!("setsockopt SO_KEEPALIVE not yet implemented");
            }
            (libc::SOL_SOCKET, libc::SO_BROADCAST) => {
                // TODO: implement this, pkg.go.dev/net uses it
                log::trace!("setsockopt SO_BROADCAST not yet implemented");
            }
            _ => {
                log_once_per_value_at_level!(
                    (level, optname),
                    (i32, i32),
                    log::Level::Warn,
                    log::Level::Debug,
                    "setsockopt called with unsupported level {level} and opt {optname}"
                );
                return Err(Errno::ENOPROTOOPT.into());
            }
        }

        Ok(())
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
        let event_source = unsafe { c::legacyfile_getEventSource(self.as_legacy_file()) };
        let event_source = unsafe { event_source.as_ref() }.unwrap();

        Worker::with_active_host(|host| {
            let mut event_source = event_source.borrow_mut(host.root());
            event_source.add_listener(monitoring_state, monitoring_signals, filter, notify_fn)
        })
        .unwrap()
    }

    pub fn add_legacy_listener(&mut self, ptr: HostTreePointer<c::StatusListener>) {
        unsafe { c::legacyfile_addListener(self.as_legacy_file(), ptr.ptr()) };
    }

    pub fn remove_legacy_listener(&mut self, ptr: *mut c::StatusListener) {
        unsafe { c::legacyfile_removeListener(self.as_legacy_file(), ptr) };
    }

    pub fn state(&self) -> FileState {
        unsafe { c::legacyfile_getStatus(self.as_legacy_file()) }
    }
}

impl std::ops::Drop for LegacyTcpSocket {
    fn drop(&mut self) {
        unsafe { c::legacyfile_unref(self.socket.ptr() as *mut libc::c_void) };
    }
}
