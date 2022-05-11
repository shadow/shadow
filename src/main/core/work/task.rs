use std::sync::Arc;

use atomic_refcell::AtomicRefCell;

use crate::{core::worker::Worker, host::host::Host};

/// Mostly for interoperability with C APIs.
/// In Rust code that doesn't need to interact with C, it may make more sense
/// to directly use a `FnMut(&mut Host)` trait object.
pub struct TaskRef {
    inner: Arc<AtomicRefCell<dyn FnMut(&mut Host)>>,
    #[cfg(debug_assertions)]
    magic: u32,
}

impl TaskRef {
    #[cfg(debug_assertions)]
    const MAGIC: u32 = 0xe0408897;

    pub fn new<T: 'static + FnMut(&mut Host)>(f: T) -> Self {
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

pub mod export {
    use super::*;

    use crate::{cshadow, host::host::Host};

    pub type TaskCallbackFunc =
        extern "C" fn(*mut cshadow::Host, *mut libc::c_void, *mut libc::c_void);
    pub type TaskObjectFreeFunc = Option<extern "C" fn(*mut libc::c_void)>;
    pub type TaskArgumentFreeFunc = Option<extern "C" fn(*mut libc::c_void)>;

    struct CTask {
        callback: TaskCallbackFunc,
        object: *mut libc::c_void,
        argument: *mut libc::c_void,
        object_free: TaskObjectFreeFunc,
        argument_free: TaskArgumentFreeFunc,
    }

    impl CTask {
        fn execute(&self, host: *mut cshadow::Host) {
            (self.callback)(host, self.object, self.argument)
        }
    }

    impl Drop for CTask {
        fn drop(&mut self) {
            if let Some(object_free) = self.object_free {
                object_free(self.object);
            }
            if let Some(argument_free) = self.argument_free {
                argument_free(self.argument);
            }
        }
    }

    #[no_mangle]
    pub unsafe extern "C" fn taskref_new(
        callback: TaskCallbackFunc,
        object: *mut libc::c_void,
        argument: *mut libc::c_void,
        object_free: TaskObjectFreeFunc,
        argument_free: TaskArgumentFreeFunc,
    ) -> *mut TaskRef {
        let objs = CTask {
            callback,
            object,
            argument,
            object_free,
            argument_free,
        };
        let task = TaskRef::new(move |host: &mut Host| objs.execute(host.chost()));
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
