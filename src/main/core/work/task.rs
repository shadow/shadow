use std::sync::Arc;

use atomic_refcell::AtomicRefCell;

use crate::{
    core::worker::Worker,
    host::host::Host,
    utility::{IsSend, IsSync},
};

/// Mostly for interoperability with C APIs.
/// In Rust code that doesn't need to interact with C, it may make more sense
/// to directly use a `FnMut(&mut Host)` trait object.
pub struct TaskRef {
    inner: Arc<AtomicRefCell<dyn FnMut(&mut Host) + Send + Sync>>,
    #[cfg(debug_assertions)]
    magic: u32,
}

impl TaskRef {
    #[cfg(debug_assertions)]
    const MAGIC: u32 = 0xe0408897;

    pub fn new<T: 'static + FnMut(&mut Host) + Send + Sync>(f: T) -> Self {
        Worker::increment_object_alloc_counter("TaskRef");
        Self {
            inner: Arc::new(AtomicRefCell::new(f)),
            #[cfg(debug_assertions)]
            magic: Self::MAGIC,
        }
    }

    pub fn execute(&mut self, host: &mut Host) {
        let mut inner = self.inner.borrow_mut();
        inner(host)
    }

    #[cfg(debug_assertions)]
    fn drop_handle_magic(&mut self) {
        debug_assert!(self.magic == Self::MAGIC);
        unsafe { std::ptr::write_volatile(&mut self.magic, 0) };
    }

    #[cfg(not(debug_assertions))]
    fn drop_handle_magic(&mut self) {}
}

impl Drop for TaskRef {
    fn drop(&mut self) {
        Worker::increment_object_dealloc_counter("TaskRef");
        self.drop_handle_magic();
    }
}

impl Clone for TaskRef {
    fn clone(&self) -> Self {
        Worker::increment_object_alloc_counter("TaskRef");
        Self {
            inner: self.inner.clone(),
            #[cfg(debug_assertions)]
            magic: self.magic.clone(),
        }
    }
}

impl IsSend for TaskRef {}
impl IsSync for TaskRef {}

pub mod export {
    use super::*;

    use crate::{
        cshadow,
        host::host::{Host, HostId},
        utility::SyncSendPointer,
    };

    pub type TaskCallbackFunc =
        extern "C" fn(*mut cshadow::Host, *mut libc::c_void, *mut libc::c_void);
    pub type TaskObjectFreeFunc = Option<extern "C" fn(*mut libc::c_void)>;
    pub type TaskArgumentFreeFunc = Option<extern "C" fn(*mut libc::c_void)>;

    /// Compatibility struct for creating a `TaskRef` from function pointers.
    struct CTask {
        host_id: HostId,
        callback: TaskCallbackFunc,
        object: SyncSendPointer<libc::c_void>,
        argument: SyncSendPointer<libc::c_void>,
        object_free: TaskObjectFreeFunc,
        argument_free: TaskArgumentFreeFunc,
    }

    impl CTask {
        /// SAFETY: Objects pointed to by `object` and `argument` must be
        /// `Send`.
        unsafe fn new(
            host_id: HostId,
            callback: TaskCallbackFunc,
            object: *mut libc::c_void,
            argument: *mut libc::c_void,
            object_free: TaskObjectFreeFunc,
            argument_free: TaskArgumentFreeFunc,
        ) -> Self {
            // Suppress "never read" warning in release builds.
            let _ = host_id;
            Self {
                #[cfg(debug_assertions)]
                host_id,
                callback,
                object: SyncSendPointer(object),
                argument: SyncSendPointer(argument),
                object_free,
                argument_free,
            }
        }

        #[cfg(debug_assertions)]
        fn check_host_id(&self) {
            Worker::with_active_host_info(|i| {
                debug_assert_eq!(self.host_id, i.id);
            });
        }
        #[cfg(not(debug_assertions))]
        fn check_host_id(&self) {}

        /// SAFETY: Objects pointed to by `object` and `argument` must either
        /// be not accessible by other threads, or must be `Sync`.
        unsafe fn execute(&self, host: *mut cshadow::Host) {
            self.check_host_id();
            (self.callback)(host, self.object.0, self.argument.0)
        }
    }

    impl Drop for CTask {
        fn drop(&mut self) {
            self.check_host_id();
            if let Some(object_free) = self.object_free {
                object_free(self.object.0);
            }
            if let Some(argument_free) = self.argument_free {
                argument_free(self.argument.0);
            }
        }
    }

    /// Create a new reference-counted task.
    ///
    /// SAFETY: The underlying Task is assumed to be Send and Sync.
    /// It is the responsibility of the provided callbacks to access
    /// in a thread-safe way.
    ///
    /// `taskref_execute` and `taskref_drop` must only be called when the lock
    /// of the `host_id` that was passed to `host_id` is held. In the (typical)
    /// case where objects are only held within a single `Host`, the callbacks
    /// can hence safely assume that the current thread is the only one
    /// currently accessing these pointers.
    ///
    /// There must still be some coordination between the creator of the TaskRef
    /// and the callers of `taskref_execute` and `taskref_drop` to ensure that
    /// the callbacks don't conflict with other accesses in the same thread
    /// (e.g. that the caller isn't holding a Rust mutable reference to one of
    /// the pointers while the callback transforms the pointer into another Rust
    /// reference).
    #[no_mangle]
    pub unsafe extern "C" fn taskref_new(
        host_id: HostId,
        callback: TaskCallbackFunc,
        object: *mut libc::c_void,
        argument: *mut libc::c_void,
        object_free: TaskObjectFreeFunc,
        argument_free: TaskArgumentFreeFunc,
    ) -> *mut TaskRef {
        let objs = unsafe {
            CTask::new(
                host_id,
                callback,
                object,
                argument,
                object_free,
                argument_free,
            )
        };
        let task = TaskRef::new(move |host: &mut Host| unsafe { objs.execute(host.chost()) });
        // It'd be nice if we could use Arc::into_raw here, avoiding a level of
        // pointer indirection. Unfortunately that doesn't work because of the
        // internal dynamic Trait object, making the resulting pointer non-ABI
        // safe.
        Box::into_raw(Box::new(task))
    }

    /// Creates a new reference to the `Task`.
    #[no_mangle]
    pub unsafe extern "C" fn taskref_clone(task: *const TaskRef) -> *mut TaskRef {
        let task = unsafe { task.as_ref() }.unwrap();
        Box::into_raw(Box::new(task.clone()))
    }

    /// Destroys this reference to the `Task`, dropping the `Task` if no references remain.
    #[no_mangle]
    pub unsafe extern "C" fn taskref_drop(task: *mut TaskRef) {
        unsafe { Box::from_raw(task) };
    }

    /// Executes the task.
    #[no_mangle]
    pub unsafe extern "C" fn taskref_execute(task: *mut TaskRef, host: *mut cshadow::Host) {
        let task = unsafe { task.as_mut() }.unwrap();
        let mut host = unsafe { Host::borrow_from_c(host) };
        task.execute(&mut host);
    }
}
