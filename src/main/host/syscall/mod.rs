use crate::cshadow as c;
use crate::host::descriptor::{CompatDescriptor, FileState, PosixFile};
use crate::host::process::Process;

pub mod eventfd;
pub mod fcntl;
pub mod format;
pub mod handler;
pub mod ioctl;
pub mod socket;
pub mod unistd;

pub struct Trigger(c::Trigger);

impl From<c::Trigger> for Trigger {
    fn from(trigger: c::Trigger) -> Self {
        Self(trigger)
    }
}

impl From<Trigger> for c::Trigger {
    fn from(trigger: Trigger) -> Self {
        trigger.0
    }
}

impl Trigger {
    pub fn from_posix_file(file: PosixFile, status: FileState) -> Self {
        let file_ptr = Box::into_raw(Box::new(file));

        Self(c::Trigger {
            type_: c::_TriggerType_TRIGGER_POSIX_FILE,
            object: c::TriggerObject { as_file: file_ptr },
            status: status.into(),
        })
    }
}

impl c::SysCallReturn {
    pub fn from_errno(errno: nix::errno::Errno) -> Self {
        Self {
            state: c::SysCallReturnState_SYSCALL_DONE,
            retval: c::SysCallReg {
                as_i64: -(errno as i64),
            },
            cond: std::ptr::null_mut(),
        }
    }

    pub fn from_int(int: i64) -> Self {
        Self {
            state: c::SysCallReturnState_SYSCALL_DONE,
            retval: c::SysCallReg { as_i64: int },
            cond: std::ptr::null_mut(),
        }
    }
}

/// Returns the `CompatDescriptor` for the fd if it exists, otherwise returns EBADF.
pub fn get_descriptor(
    process: &Process,
    fd: impl TryInto<u32>,
) -> Result<&CompatDescriptor, nix::errno::Errno> {
    // check that fd is within bounds
    let fd: u32 = fd.try_into().map_err(|_| nix::errno::Errno::EBADF)?;

    match process.get_descriptor(fd) {
        Some(desc) => Ok(desc),
        None => Err(nix::errno::Errno::EBADF),
    }
}

/// Returns the `CompatDescriptor` for the fd if it exists, otherwise returns EBADF.
pub fn get_descriptor_mut(
    process: &mut Process,
    fd: impl TryInto<u32>,
) -> Result<&mut CompatDescriptor, nix::errno::Errno> {
    // check that fd is within bounds
    let fd: u32 = fd.try_into().map_err(|_| nix::errno::Errno::EBADF)?;

    match process.get_descriptor_mut(fd) {
        Some(desc) => Ok(desc),
        None => Err(nix::errno::Errno::EBADF),
    }
}
