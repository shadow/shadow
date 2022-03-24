use crate::cshadow;
use crate::host::syscall::Trigger;

use std::marker::PhantomData;

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
    /// `ptr` must point to a valid object that will not be accessed by other threads
    /// for the lifetime of this object.
    pub fn borrow_from_c(ptr: *mut cshadow::SysCallCondition) -> Self {
        assert!(!ptr.is_null());
        Self {
            c_ptr: ptr,
            _phantom: PhantomData::default(),
        }
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
    /// `ptr` must point to a valid object that will not be accessed by other threads
    /// for the lifetime of this object.
    pub fn borrow_from_c(ptr: *mut cshadow::SysCallCondition) -> Self {
        assert!(!ptr.is_null());
        Self {
            condition: SysCallConditionRef::borrow_from_c(ptr),
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
    /// `ptr` must point to a valid object that will not be accessed by other threads
    /// for the lifetime of this object.
    pub unsafe fn consume_from_c(ptr: *mut cshadow::SysCallCondition) -> Self {
        assert!(!ptr.is_null());
        Self {
            condition: Some(SysCallConditionRefMut::borrow_from_c(ptr)),
        }
    }

    /// Constructor.
    // TODO: Add support for taking a Timer, ideally after we have a Rust
    // implementation or wrapper.
    pub fn new(trigger: Trigger) -> Self {
        SysCallCondition {
            condition: Some(SysCallConditionRefMut::borrow_from_c(unsafe {
                cshadow::syscallcondition_new(trigger.into())
            })),
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
