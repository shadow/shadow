use std::sync::Arc;

use atomic_refcell::AtomicRefCell;

use crate::cshadow as c;
use crate::host::descriptor::{FileMode, FileState, FileStatus, SyscallResult};
use crate::host::memory_manager::MemoryManager;
use crate::host::syscall_types::{PluginPtr, SysCallReg, SyscallError};
use crate::utility::callback_queue::CallbackQueue;
use crate::utility::HostTreePointer;

use unix::UnixSocket;

// https://github.com/shadow/shadow/issues/2093
#[allow(deprecated)]
use nix::sys::socket::SockAddr;

pub mod abstract_unix_ns;
pub mod unix;

#[derive(Clone)]
pub enum Socket {
    Unix(Arc<AtomicRefCell<UnixSocket>>),
}

impl Socket {
    pub fn borrow(&self) -> SocketRef {
        match self {
            Self::Unix(ref f) => SocketRef::Unix(f.borrow()),
        }
    }

    pub fn try_borrow(&self) -> Result<SocketRef, atomic_refcell::BorrowError> {
        Ok(match self {
            Self::Unix(ref f) => SocketRef::Unix(f.try_borrow()?),
        })
    }

    pub fn borrow_mut(&self) -> SocketRefMut {
        match self {
            Self::Unix(ref f) => SocketRefMut::Unix(f.borrow_mut()),
        }
    }

    pub fn try_borrow_mut(&self) -> Result<SocketRefMut, atomic_refcell::BorrowMutError> {
        Ok(match self {
            Self::Unix(ref f) => SocketRefMut::Unix(f.try_borrow_mut()?),
        })
    }

    pub fn canonical_handle(&self) -> usize {
        match self {
            Self::Unix(f) => Arc::as_ptr(f) as usize,
        }
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    pub fn bind(
        &self,
        addr: Option<&nix::sys::socket::SockAddr>,
        rng: impl rand::Rng,
    ) -> SyscallResult {
        match self {
            Self::Unix(socket) => UnixSocket::bind(socket, addr, rng),
        }
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    pub fn connect(
        &self,
        addr: &nix::sys::socket::SockAddr,
        cb_queue: &mut CallbackQueue,
    ) -> Result<(), SyscallError> {
        match self {
            Self::Unix(socket) => UnixSocket::connect(socket, addr, cb_queue),
        }
    }
}

impl std::fmt::Debug for Socket {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Unix(_) => write!(f, "Unix")?,
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
}

pub enum SocketRefMut<'a> {
    Unix(atomic_refcell::AtomicRefMut<'a, UnixSocket>),
}

// file functions
impl SocketRef<'_> {
    enum_passthrough!(self, (), Unix;
        pub fn state(&self) -> FileState
    );
    enum_passthrough!(self, (), Unix;
        pub fn mode(&self) -> FileMode
    );
    enum_passthrough!(self, (), Unix;
        pub fn get_status(&self) -> FileStatus
    );
    enum_passthrough!(self, (), Unix;
        pub fn has_open_file(&self) -> bool
    );
    enum_passthrough!(self, (), Unix;
        pub fn supports_sa_restart(&self) -> bool
    );
}

// socket-specific functions
impl SocketRef<'_> {
    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    pub fn getpeername(&self) -> Result<Option<SockAddr>, SyscallError> {
        match self {
            Self::Unix(socket) => socket.getpeername().map(|x| x.map(SockAddr::Unix)),
        }
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    pub fn getsockname(&self) -> Result<Option<SockAddr>, SyscallError> {
        match self {
            Self::Unix(socket) => socket.getsockname().map(|x| x.map(SockAddr::Unix)),
        }
    }

    enum_passthrough!(self, (), Unix;
        pub fn address_family(&self) -> nix::sys::socket::AddressFamily
    );
}

// file functions
impl SocketRefMut<'_> {
    enum_passthrough!(self, (), Unix;
        pub fn state(&self) -> FileState
    );
    enum_passthrough!(self, (), Unix;
        pub fn mode(&self) -> FileMode
    );
    enum_passthrough!(self, (), Unix;
        pub fn get_status(&self) -> FileStatus
    );
    enum_passthrough!(self, (), Unix;
        pub fn has_open_file(&self) -> bool
    );
    enum_passthrough!(self, (val), Unix;
        pub fn set_has_open_file(&mut self, val: bool)
    );
    enum_passthrough!(self, (), Unix;
        pub fn supports_sa_restart(&self) -> bool
    );
    enum_passthrough!(self, (cb_queue), Unix;
        pub fn close(&mut self, cb_queue: &mut CallbackQueue) -> Result<(), SyscallError>
    );
    enum_passthrough!(self, (status), Unix;
        pub fn set_status(&mut self, status: FileStatus)
    );
    enum_passthrough!(self, (request, arg_ptr, memory_manager), Unix;
        pub fn ioctl(&mut self, request: u64, arg_ptr: PluginPtr, memory_manager: &mut MemoryManager) -> SyscallResult
    );
    enum_passthrough!(self, (ptr), Unix;
        pub fn add_legacy_listener(&mut self, ptr: HostTreePointer<c::StatusListener>)
    );
    enum_passthrough!(self, (ptr), Unix;
        pub fn remove_legacy_listener(&mut self, ptr: *mut c::StatusListener)
    );

    enum_passthrough_generic!(self, (bytes, offset, cb_queue), Unix;
        pub fn read<W>(&mut self, bytes: W, offset: libc::off_t, cb_queue: &mut CallbackQueue) -> SyscallResult
        where W: std::io::Write + std::io::Seek
    );

    enum_passthrough_generic!(self, (source, offset, cb_queue), Unix;
        pub fn write<R>(&mut self, source: R, offset: libc::off_t, cb_queue: &mut CallbackQueue) -> SyscallResult
        where R: std::io::Read + std::io::Seek
    );
}

// socket-specific functions
impl SocketRefMut<'_> {
    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    pub fn getpeername(&self) -> Result<Option<SockAddr>, SyscallError> {
        match self {
            Self::Unix(socket) => socket.getpeername().map(|x| x.map(SockAddr::Unix)),
        }
    }

    // https://github.com/shadow/shadow/issues/2093
    #[allow(deprecated)]
    pub fn getsockname(&self) -> Result<Option<SockAddr>, SyscallError> {
        match self {
            Self::Unix(socket) => socket.getsockname().map(|x| x.map(SockAddr::Unix)),
        }
    }

    enum_passthrough!(self, (), Unix;
        pub fn address_family(&self) -> nix::sys::socket::AddressFamily
    );

    enum_passthrough_generic!(self, (source, addr, cb_queue), Unix;
        // https://github.com/shadow/shadow/issues/2093
        #[allow(deprecated)]
        pub fn sendto<R>(&mut self, source: R, addr: Option<nix::sys::socket::SockAddr>, cb_queue: &mut CallbackQueue)
            -> SyscallResult
        where R: std::io::Read + std::io::Seek
    );

    enum_passthrough_generic!(self, (bytes, cb_queue), Unix;
        // https://github.com/shadow/shadow/issues/2093
        #[allow(deprecated)]
        pub fn recvfrom<W>(&mut self, bytes: W, cb_queue: &mut CallbackQueue)
            -> Result<(SysCallReg, Option<nix::sys::socket::SockAddr>), SyscallError>
        where W: std::io::Write + std::io::Seek
    );

    enum_passthrough!(self, (backlog, cb_queue), Unix;
        pub fn listen(&mut self, backlog: i32, cb_queue: &mut CallbackQueue) -> Result<(), SyscallError>
    );

    pub fn accept(&mut self, cb_queue: &mut CallbackQueue) -> Result<Socket, SyscallError> {
        match self {
            Self::Unix(socket) => socket.accept(cb_queue).map(Socket::Unix),
        }
    }
}

impl std::fmt::Debug for SocketRef<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Unix(_) => write!(f, "Unix")?,
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
        }

        write!(
            f,
            "(state: {:?}, status: {:?})",
            self.state(),
            self.get_status()
        )
    }
}

/// Returns a nix socket address object where only the family is set.
// https://github.com/shadow/shadow/issues/2093
#[allow(deprecated)]
pub fn empty_sockaddr(family: nix::sys::socket::AddressFamily) -> nix::sys::socket::SockAddr {
    let family = family as libc::sa_family_t;
    let mut addr: nix::sys::socket::sockaddr_storage = unsafe { std::mem::zeroed() };
    addr.ss_family = family;
    // the size of ss_family will be 2 bytes on linux
    nix::sys::socket::sockaddr_storage_to_addr(&addr, 2).unwrap()
}
