use std::marker::PhantomData;

use nix::sys::signal::Signal;

use super::host::Host;
use crate::cshadow;
use crate::host::descriptor::OpenFile;
use crate::host::syscall::Trigger;

/// An immutable reference to a syscall condition.
#[derive(Debug, PartialEq, Eq)]
pub struct SysCallConditionRef<'a> {
    c_ptr: *mut cshadow::SysCallCondition,
    _phantom: PhantomData<&'a ()>,
}

// do not define any mutable methods for this type
impl<'a> SysCallConditionRef<'a> {
    /// Borrows from a C pointer. i.e. doesn't increase the ref count, nor decrease the ref count
    /// when dropped.
    ///
    /// # Safety
    ///
    /// `ptr` must point to a valid object that will not be accessed by other threads
    /// for the lifetime of this object.
    pub unsafe fn borrow_from_c(ptr: *mut cshadow::SysCallCondition) -> Self {
        assert!(!ptr.is_null());
        Self {
            c_ptr: ptr,
            _phantom: PhantomData::default(),
        }
    }

    pub fn active_file(&self) -> Option<&OpenFile> {
        let file_ptr = unsafe { cshadow::syscallcondition_getActiveFile(self.c_ptr) };
        if file_ptr.is_null() {
            return None;
        }

        Some(unsafe { file_ptr.as_ref() }.unwrap())
    }
}

/// A mutable reference to a syscall condition.
#[derive(Debug, PartialEq, Eq)]
pub struct SysCallConditionRefMut<'a> {
    condition: SysCallConditionRef<'a>,
}

// any immutable methods should be implemented on SysCallConditionRef instead
impl<'a> SysCallConditionRefMut<'a> {
    /// Borrows from a C pointer. i.e. doesn't increase the ref count, nor decrease the ref count
    /// when dropped.
    ///
    /// # Safety
    ///
    /// `ptr` must point to a valid object that will not be accessed by other threads
    /// for the lifetime of this object.
    pub unsafe fn borrow_from_c(ptr: *mut cshadow::SysCallCondition) -> Self {
        assert!(!ptr.is_null());
        Self {
            condition: unsafe { SysCallConditionRef::borrow_from_c(ptr) },
        }
    }

    pub fn set_active_file(&mut self, file: OpenFile) {
        let file_ptr = Box::into_raw(Box::new(file));
        unsafe { cshadow::syscallcondition_setActiveFile(self.condition.c_ptr, file_ptr) };
    }

    pub fn wakeup_for_signal(&mut self, host: &Host, signal: Signal) -> bool {
        unsafe {
            cshadow::syscallcondition_wakeupForSignal(self.condition.c_ptr, host, signal as i32)
        }
    }
}

impl<'a> std::ops::Deref for SysCallConditionRefMut<'a> {
    type Target = SysCallConditionRef<'a>;

    fn deref(&self) -> &Self::Target {
        &self.condition
    }
}

/// An owned syscall condition.
#[derive(Debug, PartialEq, Eq)]
pub struct SysCallCondition {
    condition: Option<SysCallConditionRefMut<'static>>,
}

impl SysCallCondition {
    /// "Steal" from a C pointer. i.e. doesn't increase ref count, but will decrease the ref count
    /// when dropped.
    ///
    /// # Safety
    ///
    /// `ptr` must point to a valid object that will not be accessed by other threads
    /// for the lifetime of this object.
    pub unsafe fn consume_from_c(ptr: *mut cshadow::SysCallCondition) -> Self {
        assert!(!ptr.is_null());
        Self {
            condition: Some(unsafe { SysCallConditionRefMut::borrow_from_c(ptr) }),
        }
    }

    /// Constructor.
    // TODO: Add support for taking a Timer, ideally after we have a Rust
    // implementation or wrapper.
    pub fn new(trigger: Trigger) -> Self {
        SysCallCondition {
            condition: Some(unsafe {
                SysCallConditionRefMut::borrow_from_c(cshadow::syscallcondition_new(trigger.into()))
            }),
        }
    }

    /// "Steal" the inner pointer without unref'ing it.
    pub fn into_inner(mut self) -> *mut cshadow::SysCallCondition {
        let condition = self.condition.take().unwrap();
        condition.c_ptr
    }
}

impl Drop for SysCallCondition {
    fn drop(&mut self) {
        if let Some(condition) = &self.condition {
            if !condition.c_ptr.is_null() {
                unsafe { cshadow::syscallcondition_unref(condition.c_ptr) }
            }
        }
    }
}

impl std::ops::Deref for SysCallCondition {
    type Target = SysCallConditionRefMut<'static>;

    fn deref(&self) -> &Self::Target {
        self.condition.as_ref().unwrap()
    }
}

impl std::ops::DerefMut for SysCallCondition {
    fn deref_mut(&mut self) -> &mut Self::Target {
        self.condition.as_mut().unwrap()
    }
}
