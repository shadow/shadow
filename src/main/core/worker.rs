use crate::cshadow;
use crate::host::process::Process;
use std::cell::RefCell;

/// Worker context, capturing e.g. the current Process and Thread.
// This is currently just a marker, since we actually access the context through
// the C Worker APIs. Eventually it'll store the Worker and/or its stored
// context, though.
pub struct Worker {
    // This is just to track borrows for now; we retrieve the pointer
    // itself from the C Worker on-demand.
    active_process: RefCell<()>,
}

std::thread_local! {
    static WORKER: Worker = Worker{active_process: RefCell::new(())}
}

impl Worker {
    /// Run `f` with a reference to the current context.
    pub fn with_active_process<F, R>(f: F) -> R
    where
        F: FnOnce(&Process) -> R,
    {
        let process = unsafe {
            let cprocess = cshadow::worker_getActiveProcess();
            assert!(!cprocess.is_null());
            Process::borrow_from_c(cprocess)
        };
        WORKER.with(|worker| {
            let _borrow = worker.active_process.borrow();
            f(&process)
        })
    }

    /// Run `f` with a reference to the current context.
    pub fn with_active_process_mut<F, R>(f: F) -> R
    where
        F: FnOnce(&mut Process) -> R,
    {
        let mut process = unsafe {
            let cprocess = cshadow::worker_getActiveProcess();
            assert!(!cprocess.is_null());
            Process::borrow_from_c(cprocess)
        };
        WORKER.with(|worker| {
            let _borrow = worker.active_process.borrow_mut();
            f(&mut process)
        })
    }
}
