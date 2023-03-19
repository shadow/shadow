use std::sync::Arc;

use atomic_refcell::AtomicRefCell;
use nix::sys::socket::Shutdown;

use crate::cshadow as c;
use crate::host::descriptor::{FileMode, FileState, FileStatus, OpenFile, SyscallResult};
use crate::host::memory_manager::MemoryManager;
use crate::host::syscall::io::{IoVec, MmsgHdr};
use crate::host::syscall_types::{PluginPtr, SysCallReg, SyscallError, TypedPluginPtr};
use crate::network::net_namespace::NetworkNamespace;
use crate::utility::callback_queue::CallbackQueue;
use crate::utility::sockaddr::SockaddrStorage;
use crate::utility::HostTreePointer;

use inet::{InetSocket, InetSocketRef, InetSocketRefMut};
use unix::UnixSocket;

pub mod abstract_unix_ns;
pub mod inet;
pub mod unix;

#[derive(Clone)]
pub enum Socket {
    Unix(Arc<AtomicRefCell<UnixSocket>>),
    Inet(InetSocket),
}

impl Socket {
    pub fn borrow(&self) -> SocketRef {
        match self {
            Self::Unix(ref f) => SocketRef::Unix(f.borrow()),
            Self::Inet(ref f) => SocketRef::Inet(f.borrow()),
        }
    }

    pub fn try_borrow(&self) -> Result<SocketRef, atomic_refcell::BorrowError> {
        Ok(match self {
            Self::Unix(ref f) => SocketRef::Unix(f.try_borrow()?),
            Self::Inet(ref f) => SocketRef::Inet(f.try_borrow()?),
        })
    }

    pub fn borrow_mut(&self) -> SocketRefMut {
        match self {
            Self::Unix(ref f) => SocketRefMut::Unix(f.borrow_mut()),
            Self::Inet(ref f) => SocketRefMut::Inet(f.borrow_mut()),
        }
    }

    pub fn try_borrow_mut(&self) -> Result<SocketRefMut, atomic_refcell::BorrowMutError> {
        Ok(match self {
            Self::Unix(ref f) => SocketRefMut::Unix(f.try_borrow_mut()?),
            Self::Inet(ref f) => SocketRefMut::Inet(f.try_borrow_mut()?),
        })
    }

    pub fn canonical_handle(&self) -> usize {
        match self {
            Self::Unix(f) => Arc::as_ptr(f) as usize,
            Self::Inet(ref f) => f.canonical_handle(),
        }
    }

    pub fn bind(
        &self,
        addr: Option<&SockaddrStorage>,
        net_ns: &NetworkNamespace,
        rng: impl rand::Rng,
    ) -> SyscallResult {
        match self {
            Self::Unix(socket) => UnixSocket::bind(socket, addr, net_ns, rng),
            Self::Inet(socket) => InetSocket::bind(socket, addr, net_ns, rng),
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
            Self::Unix(socket) => UnixSocket::listen(socket, backlog, net_ns, rng, cb_queue),
            Self::Inet(socket) => InetSocket::listen(socket, backlog, net_ns, rng, cb_queue),
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
            Self::Unix(socket) => UnixSocket::connect(socket, addr, net_ns, rng, cb_queue),
            Self::Inet(socket) => InetSocket::connect(socket, addr, net_ns, rng, cb_queue),
        }
    }

    pub fn sendmsg(
        &self,
        args: SendmsgArgs,
        memory_manager: &mut MemoryManager,
        cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        match self {
            Self::Unix(socket) => UnixSocket::sendmsg(socket, args, memory_manager, cb_queue),
            Self::Inet(socket) => InetSocket::sendmsg(socket, args, memory_manager, cb_queue),
        }
    }

    pub fn recvmsg(
        &self,
        args: RecvmsgArgs,
        memory_manager: &mut MemoryManager,
        cb_queue: &mut CallbackQueue,
    ) -> Result<RecvmsgReturn, SyscallError> {
        match self {
            Self::Unix(socket) => UnixSocket::recvmsg(socket, args, memory_manager, cb_queue),
            Self::Inet(socket) => InetSocket::recvmsg(socket, args, memory_manager, cb_queue),
        }
    }
}

impl std::fmt::Debug for Socket {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Unix(_) => write!(f, "Unix")?,
            Self::Inet(_) => write!(f, "Inet")?,
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

pub enum SocketRef<'a> {
    Unix(atomic_refcell::AtomicRef<'a, UnixSocket>),
    Inet(InetSocketRef<'a>),
}

pub enum SocketRefMut<'a> {
    Unix(atomic_refcell::AtomicRefMut<'a, UnixSocket>),
    Inet(InetSocketRefMut<'a>),
}

// file functions
impl SocketRef<'_> {
    enum_passthrough!(self, (), Unix, Inet;
        pub fn state(&self) -> FileState
    );
    enum_passthrough!(self, (), Unix, Inet;
        pub fn mode(&self) -> FileMode
    );
    enum_passthrough!(self, (), Unix, Inet;
        pub fn get_status(&self) -> FileStatus
    );
    enum_passthrough!(self, (), Unix, Inet;
        pub fn has_open_file(&self) -> bool
    );
    enum_passthrough!(self, (), Unix, Inet;
        pub fn supports_sa_restart(&self) -> bool
    );
}

// socket-specific functions
impl SocketRef<'_> {
    pub fn getpeername(&self) -> Result<Option<SockaddrStorage>, SyscallError> {
        match self {
            Self::Unix(socket) => socket.getpeername().map(|opt| opt.map(Into::into)),
            Self::Inet(socket) => socket.getpeername().map(|opt| opt.map(Into::into)),
        }
    }

    pub fn getsockname(&self) -> Result<Option<SockaddrStorage>, SyscallError> {
        match self {
            Self::Unix(socket) => socket.getsockname().map(|opt| opt.map(Into::into)),
            Self::Inet(socket) => socket.getsockname().map(|opt| opt.map(Into::into)),
        }
    }

    enum_passthrough!(self, (), Unix, Inet;
        pub fn address_family(&self) -> nix::sys::socket::AddressFamily
    );

    enum_passthrough!(self, (level, optname, optval_ptr, optlen, memory_manager), Unix, Inet;
        pub fn getsockopt(&self, level: libc::c_int, optname: libc::c_int, optval_ptr: PluginPtr,
                          optlen: libc::socklen_t, memory_manager: &mut MemoryManager)
        -> Result<libc::socklen_t, SyscallError>
    );

    enum_passthrough!(self, (level, optname, optval_ptr, optlen, memory_manager), Unix, Inet;
        pub fn setsockopt(&self, level: libc::c_int, optname: libc::c_int, optval_ptr: PluginPtr,
                          optlen: libc::socklen_t, memory_manager: &MemoryManager)
        -> Result<(), SyscallError>
    );
}

// file functions
impl SocketRefMut<'_> {
    enum_passthrough!(self, (), Unix, Inet;
        pub fn state(&self) -> FileState
    );
    enum_passthrough!(self, (), Unix, Inet;
        pub fn mode(&self) -> FileMode
    );
    enum_passthrough!(self, (), Unix, Inet;
        pub fn get_status(&self) -> FileStatus
    );
    enum_passthrough!(self, (), Unix, Inet;
        pub fn has_open_file(&self) -> bool
    );
    enum_passthrough!(self, (val), Unix, Inet;
        pub fn set_has_open_file(&mut self, val: bool)
    );
    enum_passthrough!(self, (), Unix, Inet;
        pub fn supports_sa_restart(&self) -> bool
    );
    enum_passthrough!(self, (cb_queue), Unix, Inet;
        pub fn close(&mut self, cb_queue: &mut CallbackQueue) -> Result<(), SyscallError>
    );
    enum_passthrough!(self, (status), Unix, Inet;
        pub fn set_status(&mut self, status: FileStatus)
    );
    enum_passthrough!(self, (request, arg_ptr, memory_manager), Unix, Inet;
        pub fn ioctl(&mut self, request: u64, arg_ptr: PluginPtr, memory_manager: &mut MemoryManager) -> SyscallResult
    );
    enum_passthrough!(self, (ptr), Unix, Inet;
        pub fn add_legacy_listener(&mut self, ptr: HostTreePointer<c::StatusListener>)
    );
    enum_passthrough!(self, (ptr), Unix, Inet;
        pub fn remove_legacy_listener(&mut self, ptr: *mut c::StatusListener)
    );
    enum_passthrough!(self, (iovs, offset, flags, mem, cb_queue), Unix, Inet;
        pub fn readv(&mut self, iovs: &[IoVec], offset: Option<libc::off_t>, flags: libc::c_int,
                     mem: &mut MemoryManager, cb_queue: &mut CallbackQueue) -> Result<libc::ssize_t, SyscallError>
    );
    enum_passthrough!(self, (iovs, offset, flags, mem, cb_queue), Unix, Inet;
        pub fn writev(&mut self, iovs: &[IoVec], offset: Option<libc::off_t>, flags: libc::c_int,
                      mem: &mut MemoryManager, cb_queue: &mut CallbackQueue) -> Result<libc::ssize_t, SyscallError>
    );
}

// socket-specific functions
impl SocketRefMut<'_> {
    pub fn getpeername(&self) -> Result<Option<SockaddrStorage>, SyscallError> {
        match self {
            Self::Unix(socket) => socket.getpeername().map(|opt| opt.map(Into::into)),
            Self::Inet(socket) => socket.getpeername().map(|opt| opt.map(Into::into)),
        }
    }

    pub fn getsockname(&self) -> Result<Option<SockaddrStorage>, SyscallError> {
        match self {
            Self::Unix(socket) => socket.getsockname().map(|opt| opt.map(Into::into)),
            Self::Inet(socket) => socket.getsockname().map(|opt| opt.map(Into::into)),
        }
    }

    enum_passthrough!(self, (), Unix, Inet;
        pub fn address_family(&self) -> nix::sys::socket::AddressFamily
    );

    enum_passthrough!(self, (level, optname, optval_ptr, optlen, memory_manager), Unix, Inet;
        pub fn getsockopt(&self, level: libc::c_int, optname: libc::c_int, optval_ptr: PluginPtr,
                          optlen: libc::socklen_t, memory_manager: &mut MemoryManager)
        -> Result<libc::socklen_t, SyscallError>
    );

    enum_passthrough!(self, (level, optname, optval_ptr, optlen, memory_manager), Unix, Inet;
        pub fn setsockopt(&self, level: libc::c_int, optname: libc::c_int, optval_ptr: PluginPtr,
                          optlen: libc::socklen_t, memory_manager: &MemoryManager)
        -> Result<(), SyscallError>
    );

    pub fn accept(&mut self, cb_queue: &mut CallbackQueue) -> Result<OpenFile, SyscallError> {
        match self {
            Self::Unix(socket) => socket.accept(cb_queue),
            Self::Inet(socket) => socket.accept(cb_queue),
        }
    }

    enum_passthrough!(self, (how, cb_queue), Unix, Inet;
        pub fn shutdown(&mut self, how: Shutdown, cb_queue: &mut CallbackQueue) -> Result<(), SyscallError>
    );
}

impl std::fmt::Debug for SocketRef<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Unix(_) => write!(f, "Unix")?,
            Self::Inet(_) => write!(f, "Inet")?,
        }

        write!(
            f,
            "(state: {:?}, status: {:?})",
            self.state(),
            self.get_status()
        )
    }
}

impl std::fmt::Debug for SocketRefMut<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Unix(_) => write!(f, "Unix")?,
            Self::Inet(_) => write!(f, "Inet")?,
        }

        write!(
            f,
            "(state: {:?}, status: {:?})",
            self.state(),
            self.get_status()
        )
    }
}

pub struct SendmsgArgs<'a> {
    pub addr: Option<SockaddrStorage>,
    pub iovs: &'a [IoVec],
    pub control_ptr: TypedPluginPtr<u8>,
    pub flags: libc::c_int,
}

pub struct RecvmsgArgs<'a> {
    pub iovs: &'a [IoVec],
    pub control_ptr: TypedPluginPtr<u8>,
    pub flags: libc::c_int,
}

pub struct RecvmsgReturn {
    pub bytes_read: libc::ssize_t,
    pub addr: Option<SockaddrStorage>,
    pub msg_flags: libc::c_int,
    pub control_len: libc::size_t,
}
