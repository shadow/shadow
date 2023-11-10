use crate::cshadow as c;
use crate::host::descriptor::{File, FileState};

pub mod formatter;
pub mod handler;
pub mod io;
pub mod type_formatting;

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
    pub fn from_file(file: File, status: FileState) -> Self {
        let file_ptr = Box::into_raw(Box::new(file));

        Self(c::Trigger {
            type_: c::_TriggerType_TRIGGER_FILE,
            object: c::TriggerObject { as_file: file_ptr },
            status: status.into(),
        })
    }

    pub fn child() -> Self {
        Self(c::Trigger {
            type_: c::_TriggerType_TRIGGER_CHILD,
            object: c::TriggerObject {
                as_pointer: core::ptr::null_mut(),
            },
            status: c::_Status_STATUS_CHILD_EVENT,
        })
    }
}
