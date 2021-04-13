use crate::cshadow;
use crate::host::syscall::Trigger;

/// Wrapper
#[derive(Debug, PartialEq, Eq)]
pub struct SysCallCondition {
    c_ptr: *mut cshadow::SysCallCondition,
}

impl SysCallCondition {
    /// "Steal" from a C pointer. i.e. doesn't increase ref count.
    /// `ptr` must point to a valid object that will not be accessed by other threads
    /// for the lifetime of this object.
    pub unsafe fn consume_from_c(ptr: *mut cshadow::SysCallCondition) -> Self {
        assert!(!ptr.is_null());
        Self { c_ptr: ptr }
    }

    /// Constructor.
    // TODO: Add support for taking a Timer, ideally after we have a Rust
    // implementation or wrapper.
    pub fn new(trigger: Trigger) -> Self {
        SysCallCondition {
            c_ptr: unsafe { cshadow::syscallcondition_new(trigger.into(), std::ptr::null_mut()) },
        }
    }

    /// "Steal" the inner pointer without unref'ing it.
    pub fn into_inner(mut self) -> *mut cshadow::SysCallCondition {
        let ptr = self.c_ptr;
        // We *don't* want Drop to deref
        self.c_ptr = std::ptr::null_mut() as *mut cshadow::SysCallCondition;
        ptr
    }
}

impl Drop for SysCallCondition {
    fn drop(&mut self) {
        if !self.c_ptr.is_null() {
            unsafe { cshadow::syscallcondition_unref(self.c_ptr) }
        }
    }
}
