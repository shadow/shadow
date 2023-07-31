use std::sync::Arc;

use atomic_refcell::AtomicRefCell;
use linux_api::ioctls::IoctlRequest;
use nix::sys::socket::{AddressFamily, Shutdown, SockaddrIn};
use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::cshadow as c;
use crate::host::descriptor::socket::{RecvmsgArgs, RecvmsgReturn, SendmsgArgs};
use crate::host::descriptor::{
    FileMode, FileState, FileStatus, OpenFile, StateEventSource, StateListenerFilter, SyscallResult,
};
use crate::host::memory_manager::MemoryManager;
use crate::host::network::interface::FifoPacketPriority;
use crate::host::network::namespace::NetworkNamespace;
use crate::host::syscall::io::IoVec;
use crate::host::syscall_types::SyscallError;
use crate::network::packet::PacketRc;
use crate::utility::callback_queue::{CallbackQueue, Handle};
use crate::utility::sockaddr::SockaddrStorage;
use crate::utility::{HostTreePointer, ObjectCounter};

pub struct TcpSocket {
    event_source: StateEventSource,
    status: FileStatus,
    state: FileState,
    // should only be used by `OpenFile` to make sure there is only ever one `OpenFile` instance for
    // this file
    has_open_file: bool,
    _counter: ObjectCounter,
}

impl TcpSocket {
    pub fn new(_status: FileStatus) -> Arc<AtomicRefCell<Self>> {
        todo!();
    }

    pub fn get_status(&self) -> FileStatus {
        self.status
    }

    pub fn set_status(&mut self, status: FileStatus) {
        self.status = status;
    }

    pub fn mode(&self) -> FileMode {
        FileMode::READ | FileMode::WRITE
    }

    pub fn has_open_file(&self) -> bool {
        self.has_open_file
    }

    pub fn supports_sa_restart(&self) -> bool {
        true
    }

    pub fn set_has_open_file(&mut self, val: bool) {
        self.has_open_file = val;
    }

    pub fn push_in_packet(
        &mut self,
        mut _packet: PacketRc,
        _cb_queue: &mut CallbackQueue,
        _recv_time: EmulatedTime,
    ) {
        todo!();
    }

    pub fn pull_out_packet(&mut self, _cb_queue: &mut CallbackQueue) -> Option<PacketRc> {
        todo!();
    }

    pub fn peek_next_packet_priority(&self) -> Option<FifoPacketPriority> {
        todo!();
    }

    pub fn has_data_to_send(&self) -> bool {
        todo!();
    }

    pub fn update_packet_header(&self, _packet: &mut PacketRc) {
        todo!();
    }

    pub fn getsockname(&self) -> Result<Option<SockaddrIn>, SyscallError> {
        todo!();
    }

    pub fn getpeername(&self) -> Result<Option<SockaddrIn>, SyscallError> {
        todo!();
    }

    pub fn address_family(&self) -> AddressFamily {
        AddressFamily::Inet
    }

    pub fn close(&mut self, _cb_queue: &mut CallbackQueue) -> Result<(), SyscallError> {
        todo!();
    }

    pub fn bind(
        _socket: &Arc<AtomicRefCell<Self>>,
        _addr: Option<&SockaddrStorage>,
        _net_ns: &NetworkNamespace,
        _rng: impl rand::Rng,
    ) -> SyscallResult {
        todo!();
    }

    pub fn readv(
        &mut self,
        _iovs: &[IoVec],
        _offset: Option<libc::off_t>,
        _flags: libc::c_int,
        _mem: &mut MemoryManager,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        // we could call TcpSocket::recvmsg() here, but for now we expect that there are no code
        // paths that would call TcpSocket::readv() since the readv() syscall handler should have
        // called TcpSocket::recvmsg() instead
        panic!("Called TcpSocket::readv() on a TCP socket");
    }

    pub fn writev(
        &mut self,
        _iovs: &[IoVec],
        _offset: Option<libc::off_t>,
        _flags: libc::c_int,
        _mem: &mut MemoryManager,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        // we could call TcpSocket::sendmsg() here, but for now we expect that there are no code
        // paths that would call TcpSocket::writev() since the writev() syscall handler should have
        // called TcpSocket::sendmsg() instead
        panic!("Called TcpSocket::writev() on a TCP socket");
    }

    pub fn sendmsg(
        _socket: &Arc<AtomicRefCell<Self>>,
        _args: SendmsgArgs,
        _mem: &mut MemoryManager,
        _net_ns: &NetworkNamespace,
        _rng: impl rand::Rng,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        todo!();
    }

    pub fn recvmsg(
        _socket: &Arc<AtomicRefCell<Self>>,
        _args: RecvmsgArgs,
        _mem: &mut MemoryManager,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<RecvmsgReturn, SyscallError> {
        todo!();
    }

    pub fn ioctl(
        &mut self,
        _request: IoctlRequest,
        _arg_ptr: ForeignPtr<()>,
        _mem: &mut MemoryManager,
    ) -> SyscallResult {
        todo!();
    }

    pub fn listen(
        _socket: &Arc<AtomicRefCell<Self>>,
        _backlog: i32,
        _net_ns: &NetworkNamespace,
        _rng: impl rand::Rng,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<(), SyscallError> {
        todo!();
    }

    pub fn connect(
        _socket: &Arc<AtomicRefCell<Self>>,
        _peer_addr: &SockaddrStorage,
        _net_ns: &NetworkNamespace,
        _rng: impl rand::Rng,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<(), SyscallError> {
        todo!();
    }

    pub fn accept(&mut self, _cb_queue: &mut CallbackQueue) -> Result<OpenFile, SyscallError> {
        todo!();
    }

    pub fn shutdown(
        &mut self,
        _how: Shutdown,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<(), SyscallError> {
        todo!();
    }

    pub fn getsockopt(
        &self,
        _level: libc::c_int,
        _optname: libc::c_int,
        _optval_ptr: ForeignPtr<()>,
        _optlen: libc::socklen_t,
        _mem: &mut MemoryManager,
    ) -> Result<libc::socklen_t, SyscallError> {
        todo!();
    }

    pub fn setsockopt(
        &mut self,
        _level: libc::c_int,
        _optname: libc::c_int,
        _optval_ptr: ForeignPtr<()>,
        _optlen: libc::socklen_t,
        _mem: &MemoryManager,
    ) -> Result<(), SyscallError> {
        todo!();
    }

    pub fn add_listener(
        &mut self,
        monitoring: FileState,
        filter: StateListenerFilter,
        notify_fn: impl Fn(FileState, FileState, &mut CallbackQueue) + Send + Sync + 'static,
    ) -> Handle<(FileState, FileState)> {
        self.event_source
            .add_listener(monitoring, filter, notify_fn)
    }

    pub fn add_legacy_listener(&mut self, ptr: HostTreePointer<c::StatusListener>) {
        self.event_source.add_legacy_listener(ptr);
    }

    pub fn remove_legacy_listener(&mut self, ptr: *mut c::StatusListener) {
        self.event_source.remove_legacy_listener(ptr);
    }

    pub fn state(&self) -> FileState {
        self.state
    }
}
