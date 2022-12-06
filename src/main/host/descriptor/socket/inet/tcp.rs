use std::sync::Arc;

use atomic_refcell::AtomicRefCell;
use nix::sys::socket::SockaddrIn;

use crate::cshadow as c;
use crate::host::descriptor::{
    FileMode, FileState, FileStatus, StateListenerFilter, SyscallResult,
};
use crate::host::memory_manager::MemoryManager;
use crate::host::syscall_types::{PluginPtr, SysCallReg, SyscallError};
use crate::utility::callback_queue::{CallbackQueue, Handle};
use crate::utility::sockaddr::SockaddrStorage;
use crate::utility::HostTreePointer;

pub struct TcpSocket {}

impl TcpSocket {
    pub fn new() -> Arc<AtomicRefCell<Self>> {
        todo!()
    }

    pub fn get_status(&self) -> FileStatus {
        todo!()
    }

    pub fn set_status(&mut self, _status: FileStatus) {
        todo!();
    }

    pub fn mode(&self) -> FileMode {
        todo!()
    }

    pub fn has_open_file(&self) -> bool {
        todo!()
    }

    pub fn supports_sa_restart(&self) -> bool {
        todo!()
    }

    pub fn set_has_open_file(&mut self, _val: bool) {
        todo!();
    }

    pub fn getsockname(&self) -> Result<Option<SockaddrIn>, SyscallError> {
        todo!()
    }

    pub fn getpeername(&self) -> Result<Option<SockaddrIn>, SyscallError> {
        todo!()
    }

    pub fn address_family(&self) -> nix::sys::socket::AddressFamily {
        todo!()
    }

    pub fn close(&mut self, _cb_queue: &mut CallbackQueue) -> Result<(), SyscallError> {
        todo!()
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
        _request: u64,
        _arg_ptr: PluginPtr,
        _memory_manager: &mut MemoryManager,
    ) -> SyscallResult {
        todo!()
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

    pub fn add_legacy_listener(&mut self, _ptr: HostTreePointer<c::StatusListener>) {
        todo!();
    }

    pub fn remove_legacy_listener(&mut self, _ptr: *mut c::StatusListener) {
        todo!();
    }

    pub fn state(&self) -> FileState {
        todo!()
    }
}
