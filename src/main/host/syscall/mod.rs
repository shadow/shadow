use crate::cshadow as c;
use crate::host::descriptor::{FileState, OpenFile};

pub mod format;
pub mod handler;

// The helpers defined here are syscall-related but not handler-specific.

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
    pub fn from_open_file(file: OpenFile, status: FileState) -> Self {
        let file_ptr = Box::into_raw(Box::new(file));

        Self(c::Trigger {
            type_: c::_TriggerType_TRIGGER_OPEN_FILE,
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
