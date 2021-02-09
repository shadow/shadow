use atomic_refcell::AtomicRefCell;
use std::sync::Arc;

use crate::cshadow as c;
use crate::host::descriptor::{CompatDescriptor, FileStatus, PosixFile};

pub mod unistd;

struct Trigger(c::Trigger);

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
    pub fn from_posix_file(file: &Arc<AtomicRefCell<PosixFile>>, status: FileStatus) -> Self {
        let file_ptr = Arc::into_raw(Arc::clone(file));

        Self(c::Trigger {
            type_: c::_TriggerType_TRIGGER_POSIX_FILE,
            object: c::TriggerObject {
                as_file: file_ptr as *const c::PosixFileArc,
            },
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

/// Returns a pointer to the `CompatDescriptor` for the fd. The pointer will never be NULL.
pub fn get_descriptor(
    fd: libc::c_int,
    process: *mut c::Process,
) -> Result<*mut CompatDescriptor, nix::errno::Errno> {
    // check that fd is within bounds
    if fd < 0 {
        return Err(nix::errno::Errno::EBADF);
    }

    // check that the fd exists
    let desc = unsafe { c::process_getRegisteredCompatDescriptor(process, fd) };
    if desc.is_null() {
        return Err(nix::errno::Errno::EBADF);
    }

    Ok(desc)
}
