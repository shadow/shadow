use std::sync::Arc;

use crate::{
    host::host::Host,
    utility::{IsSend, IsSync, Magic, ObjectCounter},
};

/// Mostly for interoperability with C APIs.
/// In Rust code that doesn't need to interact with C, it may make more sense
/// to directly use a `Fn(&mut Host)` trait object.
#[derive(Clone)]
pub struct TaskRef {
    magic: Magic<0xe0408897>,
    _counter: ObjectCounter,
    inner: Arc<dyn Fn(&mut Host) + Send + Sync>,
}

impl TaskRef {
    pub fn new<T: 'static + Fn(&mut Host) + Send + Sync>(f: T) -> Self {
        Self {
            inner: Arc::new(f),
            magic: Magic::new(),
            _counter: ObjectCounter::new("TaskRef"),
        }
    }

    pub fn execute(&self, host: &mut Host) {
        self.magic.debug_check();
        (self.inner)(host)
    }
}

impl IsSend for TaskRef {}
impl IsSync for TaskRef {}

pub mod export {
    use super::*;

    use crate::{
        cshadow,
        host::host::{Host, HostId},
        utility::{notnull::notnull_mut, HostTreePointer, SyncSendPointer},
    };

    pub type TaskCallbackFunc =
        extern "C" fn(*mut cshadow::Host, *mut libc::c_void, *mut libc::c_void);
    pub type TaskObjectFreeFunc = Option<extern "C" fn(*mut libc::c_void)>;
    pub type TaskArgumentFreeFunc = Option<extern "C" fn(*mut libc::c_void)>;

    /// Compatibility struct for creating a `TaskRef` from function pointers.
    struct CTaskHostTreePtrs {
        callback: TaskCallbackFunc,
        object: HostTreePointer<libc::c_void>,
        argument: HostTreePointer<libc::c_void>,
        object_free: TaskObjectFreeFunc,
        argument_free: TaskArgumentFreeFunc,
    }

    impl CTaskHostTreePtrs {
        /// SAFETY: Given that the host lock is held when execution of a
        /// callback starts, they must not cause `object` or `argument` to be
        /// dereferenced without the host lock held. (e.g. by releasing the host
        /// lock or exfiltrating the pointers to be dereferenced by other code
        /// that might not hold the lock).
        unsafe fn new(
            callback: TaskCallbackFunc,
            object: HostTreePointer<libc::c_void>,
            argument: HostTreePointer<libc::c_void>,
            object_free: TaskObjectFreeFunc,
            argument_free: TaskArgumentFreeFunc,
        ) -> Self {
            Self {
                callback,
                object,
                argument,
                object_free,
                argument_free,
            }
        }

        /// Panics if host lock for `object` and `argument` aren't held.
        fn execute(&self, host: *mut cshadow::Host) {
            (self.callback)(host, unsafe { self.object.ptr() }, unsafe {
                self.argument.ptr()
            })
        }
    }

    impl Drop for CTaskHostTreePtrs {
        fn drop(&mut self) {
            if let Some(object_free) = self.object_free {
                let ptr = unsafe { self.object.ptr() };
                object_free(ptr);
            }
            if let Some(argument_free) = self.argument_free {
                let ptr = unsafe { self.argument.ptr() };
                argument_free(ptr);
            }
        }
    }

    /// Compatibility struct for creating a `TaskRef` from function pointers.
    struct CTaskSyncSendPtrs {
        callback: TaskCallbackFunc,
        object: SyncSendPointer<libc::c_void>,
        argument: SyncSendPointer<libc::c_void>,
        object_free: TaskObjectFreeFunc,
        argument_free: TaskArgumentFreeFunc,
    }

    impl CTaskSyncSendPtrs {
        /// SAFETY: callbacks must be safe to call from another thread,
        /// with the given `object` and `argument`. If `object` and/or `argument`
        /// require the host lock to be held by the calling thread to access safely,
        /// use CTaskHostTreePtrs instead.
        unsafe fn new(
            callback: TaskCallbackFunc,
            object: SyncSendPointer<libc::c_void>,
            argument: SyncSendPointer<libc::c_void>,
            object_free: TaskObjectFreeFunc,
            argument_free: TaskArgumentFreeFunc,
        ) -> Self {
            Self {
                callback,
                object,
                argument,
                object_free,
                argument_free,
            }
        }

        /// Panics if host lock for `object` and `argument` aren't held.
        fn execute(&self, host: *mut cshadow::Host) {
            (self.callback)(host, self.object.ptr(), self.argument.ptr())
        }
    }

    impl Drop for CTaskSyncSendPtrs {
        fn drop(&mut self) {
            if let Some(object_free) = self.object_free {
                let ptr = self.object.ptr();
                object_free(ptr);
            }
            if let Some(argument_free) = self.argument_free {
                let ptr = self.argument.ptr();
                argument_free(ptr);
            }
        }
    }

    /// Create a new reference-counted task that can only be executed on the
    /// given host. The callbacks can safely assume that they will only be called
    /// with the lock for the specified host held.
    ///
    /// SAFETY:
    /// * `object` and `argument` must meet the requirements
    ///    for `HostTreePointer::new`.
    /// * Given that the host lock is held when execution of a callback
    ///   starts, they must not cause `object` or `argument` to be dereferenced
    ///   without the host lock held. (e.g. by releasing the host lock or exfiltrating
    ///   the pointers to be dereferenced by other code that might not hold the lock).
    ///
    /// There must still be some coordination between the creator of the TaskRef
    /// and the callers of `taskref_execute` and `taskref_drop` to ensure that
    /// the callbacks don't conflict with other accesses in the same thread
    /// (e.g. that the caller isn't holding a Rust mutable reference to one of
    /// the pointers while the callback transforms the pointer into another Rust
    /// reference).
    #[no_mangle]
    pub unsafe extern "C" fn taskref_new_bound(
        host_id: HostId,
        callback: TaskCallbackFunc,
        object: *mut libc::c_void,
        argument: *mut libc::c_void,
        object_free: TaskObjectFreeFunc,
        argument_free: TaskArgumentFreeFunc,
    ) -> *mut TaskRef {
        let objs = unsafe {
            CTaskHostTreePtrs::new(
                callback,
                HostTreePointer::new_for_host(host_id, object),
                HostTreePointer::new_for_host(host_id, argument),
                object_free,
                argument_free,
            )
        };
        let task = TaskRef::new(move |host: &mut Host| objs.execute(host.chost()));
        // It'd be nice if we could use Arc::into_raw here, avoiding a level of
        // pointer indirection. Unfortunately that doesn't work because of the
        // internal dynamic Trait object, making the resulting pointer non-ABI
        // safe.
        Box::into_raw(Box::new(task))
    }

    /// Create a new reference-counted task that may be executed on any Host.
    ///
    /// SAFETY:
    /// * The callbacks must be safe to call with `object` and `argument`
    ///   with *any* Host. (e.g. even if task is expected to execute on another Host,
    ///   must be safe to execute or free the Task from the current Host.)
    ///
    /// There must still be some coordination between the creator of the TaskRef
    /// and the callers of `taskref_execute` and `taskref_drop` to ensure that
    /// the callbacks don't conflict with other accesses in the same thread
    /// (e.g. that the caller isn't holding a Rust mutable reference to one of
    /// the pointers while the callback transforms the pointer into another Rust
    /// reference).
    #[no_mangle]
    pub unsafe extern "C" fn taskref_new_unbound(
        callback: TaskCallbackFunc,
        object: *mut libc::c_void,
        argument: *mut libc::c_void,
        object_free: TaskObjectFreeFunc,
        argument_free: TaskArgumentFreeFunc,
    ) -> *mut TaskRef {
        let objs = unsafe {
            CTaskSyncSendPtrs::new(
                callback,
                SyncSendPointer::new(object),
                SyncSendPointer::new(argument),
                object_free,
                argument_free,
            )
        };
        let task = TaskRef::new(move |host: &mut Host| objs.execute(host.chost()));
        // It'd be nice if we could use Arc::into_raw here, avoiding a level of
        // pointer indirection. Unfortunately that doesn't work because of the
        // internal dynamic Trait object, making the resulting pointer non-ABI
        // safe.
        Box::into_raw(Box::new(task))
    }

    /// Creates a new reference to the `Task`.
    ///
    /// SAFETY: `task` must be a valid pointer.
    #[no_mangle]
    pub unsafe extern "C" fn taskref_clone(task: *const TaskRef) -> *mut TaskRef {
        let task = unsafe { task.as_ref() }.unwrap();
        Box::into_raw(Box::new(task.clone()))
    }

    /// Destroys this reference to the `Task`, dropping the `Task` if no references remain.
    ///
    /// Panics if task's Host lock isn't held.
    ///
    /// SAFETY: `task` must be legally dereferencable.
    #[no_mangle]
    pub unsafe extern "C" fn taskref_drop(task: *mut TaskRef) {
        unsafe { Box::from_raw(notnull_mut(task)) };
    }

    /// Executes the task.
    ///
    /// Panics if task's Host lock isn't held.
    ///
    /// SAFETY: `task` must be legally dereferencable.
    #[no_mangle]
    pub unsafe extern "C" fn taskref_execute(task: *mut TaskRef, host: *mut cshadow::Host) {
        let task = unsafe { task.as_mut() }.unwrap();
        let mut host = unsafe { Host::borrow_from_c(host) };
        task.execute(&mut host);
    }
}
