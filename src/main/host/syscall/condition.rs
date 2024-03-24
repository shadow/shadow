use std::marker::PhantomData;

use linux_api::signal::Signal;
use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::util::SendPointer;

use crate::cshadow;
use crate::host::descriptor::OpenFile;
use crate::host::host::Host;
use crate::host::syscall::Trigger;

/// An immutable reference to a syscall condition.
#[derive(Debug, PartialEq, Eq)]
pub struct SyscallConditionRef<'a> {
    c_ptr: SendPointer<cshadow::SysCallCondition>,
    _phantom: PhantomData<&'a ()>,
}

// do not define any mutable methods for this type
impl<'a> SyscallConditionRef<'a> {
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
            c_ptr: unsafe { SendPointer::new(ptr) },
            _phantom: PhantomData,
        }
    }

    pub fn active_file(&self) -> Option<&OpenFile> {
        let file_ptr = unsafe { cshadow::syscallcondition_getActiveFile(self.c_ptr.ptr()) };
        if file_ptr.is_null() {
            return None;
        }

        Some(unsafe { file_ptr.as_ref() }.unwrap())
    }

    pub fn timeout(&self) -> Option<EmulatedTime> {
        let timeout = unsafe { cshadow::syscallcondition_getTimeout(self.c_ptr.ptr()) };
        EmulatedTime::from_c_emutime(timeout)
    }
}

/// A mutable reference to a syscall condition.
#[derive(Debug, PartialEq, Eq)]
pub struct SyscallConditionRefMut<'a> {
    condition: SyscallConditionRef<'a>,
}

// any immutable methods should be implemented on SyscallConditionRef instead
impl<'a> SyscallConditionRefMut<'a> {
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
            condition: unsafe { SyscallConditionRef::borrow_from_c(ptr) },
        }
    }

    pub fn set_active_file(&mut self, file: OpenFile) {
        let file_ptr = Box::into_raw(Box::new(file));
        unsafe { cshadow::syscallcondition_setActiveFile(self.condition.c_ptr.ptr(), file_ptr) };
    }

    pub fn wakeup_for_signal(&mut self, host: &Host, signal: Signal) -> bool {
        unsafe {
            cshadow::syscallcondition_wakeupForSignal(
                self.condition.c_ptr.ptr(),
                host,
                signal.into(),
            )
        }
    }

    pub fn set_timeout(&mut self, timeout: Option<EmulatedTime>) {
        let timeout = EmulatedTime::to_c_emutime(timeout);
        unsafe { cshadow::syscallcondition_setTimeout(self.c_ptr.ptr(), timeout) };
    }
}

impl<'a> std::ops::Deref for SyscallConditionRefMut<'a> {
    type Target = SyscallConditionRef<'a>;

    fn deref(&self) -> &Self::Target {
        &self.condition
    }
}

/// An owned syscall condition.
#[derive(Debug, PartialEq, Eq)]
pub struct SysCallCondition {
    condition: Option<SyscallConditionRefMut<'static>>,
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
            condition: Some(unsafe { SyscallConditionRefMut::borrow_from_c(ptr) }),
        }
    }

    /// Constructor.
    // TODO: Add support for taking a Timer, ideally after we have a Rust
    // implementation or wrapper.
    pub fn new(trigger: Trigger) -> Self {
        SysCallCondition {
            condition: Some(unsafe {
                SyscallConditionRefMut::borrow_from_c(cshadow::syscallcondition_new(trigger.into()))
            }),
        }
    }

    /// Create a new syscall condition that triggers a wakeup on the calling thread only after the
    /// `abs_wakeup_time` has been reached.
    ///
    /// Panics if `abs_wakeup_time` is before the current emulated time.
    pub fn new_from_wakeup_time(abs_wakeup_time: EmulatedTime) -> Self {
        SysCallCondition {
            condition: Some(unsafe {
                SyscallConditionRefMut::borrow_from_c(cshadow::syscallcondition_newWithAbsTimeout(
                    EmulatedTime::to_c_emutime(Some(abs_wakeup_time)),
                ))
            }),
        }
    }

    /// "Steal" the inner pointer without unref'ing it.
    pub fn into_inner(mut self) -> *mut cshadow::SysCallCondition {
        let condition = self.condition.take().unwrap();
        condition.c_ptr.ptr()
    }
}

impl Drop for SysCallCondition {
    fn drop(&mut self) {
        if let Some(condition) = &self.condition {
            if !condition.c_ptr.ptr().is_null() {
                unsafe { cshadow::syscallcondition_unref(condition.c_ptr.ptr()) }
            }
        }
    }
}

impl std::ops::Deref for SysCallCondition {
    type Target = SyscallConditionRefMut<'static>;

    fn deref(&self) -> &Self::Target {
        self.condition.as_ref().unwrap()
    }
}

impl std::ops::DerefMut for SysCallCondition {
    fn deref_mut(&mut self) -> &mut Self::Target {
        self.condition.as_mut().unwrap()
    }
}
