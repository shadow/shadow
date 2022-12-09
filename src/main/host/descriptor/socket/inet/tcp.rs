use std::net::{Ipv4Addr, SocketAddrV4};
use std::sync::Arc;

use atomic_refcell::AtomicRefCell;
use nix::errno::Errno;
use nix::sys::socket::SockaddrIn;

use crate::core::worker::Worker;
use crate::cshadow as c;
use crate::host::descriptor::{
    FileMode, FileState, FileStatus, StateListenerFilter, SyscallResult,
};
use crate::host::host::Host;
use crate::host::memory_manager::MemoryManager;
use crate::host::syscall_types::{PluginPtr, SysCallReg, SyscallError, TypedPluginPtr};
use crate::utility::callback_queue::{CallbackQueue, Handle};
use crate::utility::sockaddr::SockaddrStorage;
use crate::utility::{HostTreePointer, ObjectCounter};

pub struct TcpSocket {
    socket: HostTreePointer<c::TCP>,
    // should only be used by `OpenFile` to make sure there is only ever one `OpenFile` instance for
    // this file
    has_open_file: bool,
    _counter: ObjectCounter,
}

impl TcpSocket {
    pub fn new(status: FileStatus, host: &Host) -> Arc<AtomicRefCell<Self>> {
        let recv_buf_size = host.params.init_sock_recv_buf_size.try_into().unwrap();
        let send_buf_size = host.params.init_sock_send_buf_size.try_into().unwrap();

        let tcp = unsafe { c::tcp_new(host, recv_buf_size, send_buf_size) };
        let tcp = unsafe { Self::new_from_legacy(tcp) };

        tcp.borrow_mut().set_status(status);

        tcp
    }

    /// Takes ownership of the [`TCP`](c::TCP) reference.
    pub unsafe fn new_from_legacy(legacy_tcp: *mut c::TCP) -> Arc<AtomicRefCell<Self>> {
        assert!(!legacy_tcp.is_null());

        let socket = Self {
            socket: HostTreePointer::new(legacy_tcp),
            has_open_file: false,
            _counter: ObjectCounter::new("TcpSocket"),
        };

        Arc::new(AtomicRefCell::new(socket))
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

    pub fn get_status(&self) -> FileStatus {
        let o_flags = unsafe { c::legacyfile_getFlags(self.as_legacy_file()) };
        let o_flags =
            nix::fcntl::OFlag::from_bits(o_flags).expect("Not a valid OFlag: {o_flags:?}");
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

    pub fn address_family(&self) -> nix::sys::socket::AddressFamily {
        nix::sys::socket::AddressFamily::Inet
    }

    pub fn close(&mut self, _cb_queue: &mut CallbackQueue) -> Result<(), SyscallError> {
        Worker::with_active_host(|h| {
            unsafe { c::legacyfile_close(self.as_legacy_file(), h) };
        })
        .unwrap();
        Ok(())
    }

    pub fn bind(
        _socket: &Arc<AtomicRefCell<Self>>,
        _addr: Option<&SockaddrStorage>,
        _rng: impl rand::Rng,
    ) -> SyscallResult {
        todo!()
    }

    pub fn read<W>(
        &mut self,
        mut _bytes: W,
        _offset: libc::off_t,
        _cb_queue: &mut CallbackQueue,
    ) -> SyscallResult
    where
        W: std::io::Write + std::io::Seek,
    {
        // we could call TcpSocket::recvfrom() here, but for now we expect that there are no code
        // paths that would call TcpSocket::read() since the read() syscall handler should have
        // called TcpSocket::recvfrom() instead
        panic!("Called TcpSocket::read() on a TCP socket.");
    }

    pub fn write<R>(
        &mut self,
        mut _bytes: R,
        _offset: libc::off_t,
        _cb_queue: &mut CallbackQueue,
    ) -> SyscallResult
    where
        R: std::io::Read + std::io::Seek,
    {
        // we could call TcpSocket::sendto() here, but for now we expect that there are no code
        // paths that would call TcpSocket::write() since the write() syscall handler should have
        // called TcpSocket::sendto() instead
        panic!("Called TcpSocket::write() on a TCP socket");
    }

    pub fn sendto<R>(
        &mut self,
        _bytes: R,
        _addr: Option<SockaddrStorage>,
        _cb_queue: &mut CallbackQueue,
    ) -> SyscallResult
    where
        R: std::io::Read + std::io::Seek,
    {
        todo!()
    }

    pub fn recvfrom<W>(
        &mut self,
        _bytes: W,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<(SysCallReg, Option<SockaddrStorage>), SyscallError>
    where
        W: std::io::Write + std::io::Seek,
    {
        todo!()
    }

    pub fn ioctl(
        &mut self,
        request: u64,
        arg_ptr: PluginPtr,
        memory_manager: &mut MemoryManager,
    ) -> SyscallResult {
        match request {
            // equivalent to SIOCINQ
            libc::FIONREAD => {
                let len = unsafe { c::tcp_getInputBufferLength(self.as_legacy_tcp()) }
                    .try_into()
                    .unwrap();

                let arg_ptr = TypedPluginPtr::new::<libc::c_int>(arg_ptr, 1);
                memory_manager.copy_to_ptr(arg_ptr, &[len])?;

                Ok(0.into())
            }
            // equivalent to SIOCOUTQ
            libc::TIOCOUTQ => {
                let len = unsafe { c::tcp_getOutputBufferLength(self.as_legacy_tcp()) }
                    .try_into()
                    .unwrap();

                let arg_ptr = TypedPluginPtr::new::<libc::c_int>(arg_ptr, 1);
                memory_manager.copy_to_ptr(arg_ptr, &[len])?;

                Ok(0.into())
            }
            libc::SIOCOUTQNSD => {
                let len = unsafe { c::tcp_getNotSentBytes(self.as_legacy_tcp()) }
                    .try_into()
                    .unwrap();

                let arg_ptr = TypedPluginPtr::new::<libc::c_int>(arg_ptr, 1);
                memory_manager.copy_to_ptr(arg_ptr, &[len])?;

                Ok(0.into())
            }
            libc::FIONBIO => {
                panic!("This should have been handled by the ioctl syscall handler");
            }
            libc::TCGETS
            | libc::TCSETS
            | libc::TCSETSW
            | libc::TCSETSF
            | libc::TCGETA
            | libc::TCSETA
            | libc::TCSETAW
            | libc::TCSETAF
            | libc::TIOCGWINSZ
            | libc::TIOCSWINSZ => {
                // not a terminal
                Err(Errno::ENOTTY.into())
            }
            _ => {
                log::warn!("We do not yet handle ioctl request {request} on tcp sockets");
                Err(Errno::EINVAL.into())
            }
        }
    }

    pub fn listen(
        &mut self,
        _backlog: i32,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<(), SyscallError> {
        todo!();
    }

    pub fn connect(
        _socket: &Arc<AtomicRefCell<Self>>,
        _addr: &SockaddrStorage,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<(), SyscallError> {
        todo!();
    }

    pub fn accept(
        &mut self,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<Arc<AtomicRefCell<TcpSocket>>, SyscallError> {
        todo!()
    }

    pub fn add_listener(
        &mut self,
        _monitoring: FileState,
        _filter: StateListenerFilter,
        _notify_fn: impl Fn(FileState, FileState, &mut CallbackQueue) + Send + Sync + 'static,
    ) -> Handle<(FileState, FileState)> {
        todo!()
    }

    pub fn add_legacy_listener(&mut self, ptr: HostTreePointer<c::StatusListener>) {
        unsafe { c::legacyfile_addListener(self.as_legacy_file(), ptr.ptr()) };
    }

    pub fn remove_legacy_listener(&mut self, ptr: *mut c::StatusListener) {
        unsafe { c::legacyfile_removeListener(self.as_legacy_file(), ptr) };
    }

    pub fn state(&self) -> FileState {
        unsafe { c::legacyfile_getStatus(self.as_legacy_file()) }.into()
    }
}

impl std::ops::Drop for TcpSocket {
    fn drop(&mut self) {
        unsafe { c::legacyfile_unref(self.socket.ptr() as *mut libc::c_void) };
    }
}
