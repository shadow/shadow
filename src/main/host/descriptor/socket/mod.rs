use std::sync::Arc;

use atomic_refcell::AtomicRefCell;

use crate::cshadow as c;
use crate::host::descriptor::{FileMode, FileState, FileStatus, SyscallResult};
use crate::host::memory_manager::MemoryManager;
use crate::host::syscall_types::{PluginPtr, SysCallReg, SyscallError};
use crate::utility::event_queue::EventQueue;

use unix::UnixSocketFile;

use nix::sys::socket::SockAddr;

pub mod abstract_unix_ns;
pub mod unix;

#[derive(Clone)]
pub enum SocketFile {
    Unix(Arc<AtomicRefCell<UnixSocketFile>>),
}

impl SocketFile {
    pub fn borrow(&self) -> SocketFileRef {
        match self {
            Self::Unix(ref f) => SocketFileRef::Unix(f.borrow()),
        }
    }

    pub fn try_borrow(&self) -> Result<SocketFileRef, atomic_refcell::BorrowError> {
        Ok(match self {
            Self::Unix(ref f) => SocketFileRef::Unix(f.try_borrow()?),
        })
    }

    pub fn borrow_mut(&self) -> SocketFileRefMut {
        match self {
            Self::Unix(ref f) => SocketFileRefMut::Unix(f.borrow_mut()),
        }
    }

    pub fn try_borrow_mut(&self) -> Result<SocketFileRefMut, atomic_refcell::BorrowMutError> {
        Ok(match self {
            Self::Unix(ref f) => SocketFileRefMut::Unix(f.try_borrow_mut()?),
        })
    }

    pub fn canonical_handle(&self) -> usize {
        match self {
            Self::Unix(f) => Arc::as_ptr(f) as usize,
        }
    }

    pub fn bind(
        socket: &Self,
        addr: Option<&nix::sys::socket::SockAddr>,
        rng: impl rand::Rng,
    ) -> SyscallResult {
        match socket {
            Self::Unix(socket) => UnixSocketFile::bind(socket, addr, rng),
        }
    }
}

impl std::fmt::Debug for SocketFile {
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

pub enum SocketFileRef<'a> {
    Unix(atomic_refcell::AtomicRef<'a, UnixSocketFile>),
}

pub enum SocketFileRefMut<'a> {
    Unix(atomic_refcell::AtomicRefMut<'a, UnixSocketFile>),
}

// file functions
impl SocketFileRef<'_> {
    enum_passthrough!(self, (), Unix;
        pub fn state(&self) -> FileState
    );
    enum_passthrough!(self, (), Unix;
        pub fn mode(&self) -> FileMode
    );
    enum_passthrough!(self, (), Unix;
        pub fn get_status(&self) -> FileStatus
    );
}

// socket-specific functions
impl SocketFileRef<'_> {
    pub fn get_peer_address(&self) -> Option<SockAddr> {
        match self {
            Self::Unix(socket) => socket.get_peer_address().map(|x| SockAddr::Unix(x)),
        }
    }

    pub fn get_bound_address(&self) -> Option<SockAddr> {
        match self {
            Self::Unix(socket) => socket.get_bound_address().map(|x| SockAddr::Unix(x)),
        }
    }

    enum_passthrough!(self, (), Unix;
        pub fn address_family(&self) -> nix::sys::socket::AddressFamily
    );
}

// file functions
impl SocketFileRefMut<'_> {
    enum_passthrough!(self, (), Unix;
        pub fn state(&self) -> FileState
    );
    enum_passthrough!(self, (), Unix;
        pub fn mode(&self) -> FileMode
    );
    enum_passthrough!(self, (), Unix;
        pub fn get_status(&self) -> FileStatus
    );
    enum_passthrough!(self, (event_queue), Unix;
        pub fn close(&mut self, event_queue: &mut EventQueue) -> Result<(), SyscallError>
    );
    enum_passthrough!(self, (status), Unix;
        pub fn set_status(&mut self, status: FileStatus)
    );
    enum_passthrough!(self, (request, arg_ptr, memory_manager), Unix;
        pub fn ioctl(&mut self, request: u64, arg_ptr: PluginPtr, memory_manager: &mut MemoryManager) -> SyscallResult
    );
    enum_passthrough!(self, (ptr), Unix;
        pub fn add_legacy_listener(&mut self, ptr: *mut c::StatusListener)
    );
    enum_passthrough!(self, (ptr), Unix;
        pub fn remove_legacy_listener(&mut self, ptr: *mut c::StatusListener)
    );

    enum_passthrough_generic!(self, (bytes, offset, event_queue), Unix;
        pub fn read<W>(&mut self, bytes: W, offset: libc::off_t, event_queue: &mut EventQueue) -> SyscallResult
        where W: std::io::Write + std::io::Seek
    );

    enum_passthrough_generic!(self, (source, offset, event_queue), Unix;
        pub fn write<R>(&mut self, source: R, offset: libc::off_t, event_queue: &mut EventQueue) -> SyscallResult
        where R: std::io::Read + std::io::Seek
    );
}

// socket-specific functions
impl SocketFileRefMut<'_> {
    pub fn get_peer_address(&self) -> Option<SockAddr> {
        match self {
            Self::Unix(socket) => socket.get_peer_address().map(|x| SockAddr::Unix(x)),
        }
    }

    pub fn get_bound_address(&self) -> Option<SockAddr> {
        match self {
            Self::Unix(socket) => socket.get_bound_address().map(|x| SockAddr::Unix(x)),
        }
    }

    enum_passthrough!(self, (), Unix;
        pub fn address_family(&self) -> nix::sys::socket::AddressFamily
    );

    enum_passthrough_generic!(self, (source, addr, event_queue), Unix;
        pub fn sendto<R>(&mut self, source: R, addr: Option<nix::sys::socket::SockAddr>, event_queue: &mut EventQueue)
            -> SyscallResult
        where R: std::io::Read + std::io::Seek
    );

    enum_passthrough_generic!(self, (bytes, event_queue), Unix;
        pub fn recvfrom<W>(&mut self, bytes: W, event_queue: &mut EventQueue)
            -> Result<(SysCallReg, Option<nix::sys::socket::SockAddr>), SyscallError>
        where W: std::io::Write + std::io::Seek
    );
}

impl std::fmt::Debug for SocketFileRef<'_> {
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

impl std::fmt::Debug for SocketFileRefMut<'_> {
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
