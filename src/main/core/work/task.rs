use std::sync::Arc;

use atomic_refcell::AtomicRefCell;

use crate::host::host::Host;

/// Mostly for interoperability with C APIs.
/// In Rust code that doesn't need to interact with C, it may make more sense
/// to directly use a `FnMut(&mut Host)` trait object.
#[derive(Clone)]
pub struct Task {
    inner: Arc<AtomicRefCell<dyn FnMut(&mut Host)>>,
    #[cfg(debug_assertions)]
    magic: u32,
}

impl Task {
    #[cfg(debug_assertions)]
    const MAGIC: u32 = 0xe0408897;

    pub fn new<T: 'static + FnMut(&mut Host)>(f: T) -> Self {
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

impl Drop for Task {
    fn drop(&mut self) {
        self.drop_handle_magic();
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
    pub unsafe extern "C" fn task_new(
        callback: TaskCallbackFunc,
        object: *mut libc::c_void,
        argument: *mut libc::c_void,
        object_free: TaskObjectFreeFunc,
        argument_free: TaskArgumentFreeFunc,
    ) -> *mut Task {
        let objs = CTask {
            callback,
            object,
            argument,
            object_free,
            argument_free,
        };
        let task = Task::new(move |host: &mut Host| objs.execute(host.chost()));
        // It'd be nice if we could use Arc::into_raw here, avoiding a level of
        // pointer indirection. Unfortunately that doesn't work because of the
        // internal dynamic Trait object, making the resulting pointer non-ABI
        // safe.
        Box::leak(Box::new(task))
    }

    /// Creates a new reference to the `Task`.
    #[no_mangle]
    pub unsafe extern "C" fn task_clone(task: *const Task) -> *mut Task {
        let task = unsafe { task.as_ref() }.unwrap();
        Box::leak(Box::new(task.clone()))
    }

    /// Destroys this reference to the `Task`, dropping the `Task` if no references remain.
    #[no_mangle]
    pub unsafe extern "C" fn task_drop(task: *mut Task) {
        unsafe { Box::from_raw(task) };
    }

    /// Executes the task.
    #[no_mangle]
    pub unsafe extern "C" fn task_execute(task: *mut Task, host: *mut cshadow::Host) {
        let task = unsafe { task.as_mut() }.unwrap();
        let mut host = unsafe { Host::borrow_from_c(host) };
        task.execute(&mut host);
    }
}
