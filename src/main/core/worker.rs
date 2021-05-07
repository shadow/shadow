use crate::core::support::simulation_time::SimulationTime;
use crate::cshadow;
use crate::host::host::Host;
use crate::host::memory_manager::MemoryManager;
use crate::host::process::Process;
use crate::host::thread::{CThread, Thread};
use std::cell::RefCell;

/// Worker context, capturing e.g. the current Process and Thread.
// This is currently just a marker, since we actually access the context through
// the C Worker APIs. Eventually it'll store the Worker and/or its stored
// context, though.
pub struct Worker {
    // This is just to track borrows for now; we retrieve the pointer
    // itself from the C Worker on-demand.
    active_process: RefCell<()>,
    active_thread: RefCell<()>,
    active_host: RefCell<()>,
}

std::thread_local! {
    static WORKER: Worker = Worker{
        active_process: RefCell::new(()),
        active_thread: RefCell::new(()),
        active_host: RefCell::new(()),
    }
}

impl Worker {
    /// Run `f` with a reference to the active process's memory.
    pub fn with_active_process_memory<F, R>(f: F) -> R
    where
        F: FnOnce(&MemoryManager) -> R,
    {
        let cprocess = unsafe { cshadow::worker_getActiveProcess() };
        let memory_manager =
            unsafe { &*(cshadow::process_getMemoryManager(cprocess) as *const MemoryManager) };

        WORKER.with(|worker| {
            // Once the process lives in Rust, the MemoryManager will be a
            // member of it, so accessing it will require holding a reference to
            // the process.
            let _borrow_process = worker.active_process.borrow();
            f(memory_manager)
        })
    }

    /// Run `f` with a reference to the active process's memory.
    pub fn with_active_process_memory_mut<F, R>(f: F) -> R
    where
        F: FnOnce(&mut MemoryManager) -> R,
    {
        let cprocess = unsafe { cshadow::worker_getActiveProcess() };
        let memory_manager =
            unsafe { &mut *(cshadow::process_getMemoryManager(cprocess) as *mut MemoryManager) };

        WORKER.with(|worker| {
            // Once the process lives in Rust, the MemoryManager will be a
            // member of it, so accessing it will require holding a reference to
            // the process.
            //
            // It *may* turn out to be too restrictive to hold a mutable
            // reference to the process while accessing memory, in which case
            // the process can store a RefCell<MemoryManager>, and which we
            // could simulate here with an immutable borrow of the process, and
            // a mutable borrow of a stand-in RefCell for the MemoryManager.
            let _borrow = worker.active_process.borrow_mut();
            f(memory_manager)
        })
    }

    /// Run `f` with a reference to the active process.
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

    /// Run `f` with a reference to the current process.
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

    /// Run `f` with a reference to the current thread.
    pub fn with_active_thread<F, R>(f: F) -> R
    where
        F: FnOnce(&dyn Thread) -> R,
    {
        let thread = unsafe {
            let cthread = cshadow::worker_getActiveThread();
            assert!(!cthread.is_null());
            CThread::new(cthread)
        };
        WORKER.with(|worker| {
            let _borrow = worker.active_thread.borrow();
            f(&thread)
        })
    }

    /// Run `f` with a reference to the current thread.
    pub fn with_active_thread_mut<F, R>(f: F) -> R
    where
        F: FnOnce(&mut dyn Thread) -> R,
    {
        let mut thread = unsafe {
            let cthread = cshadow::worker_getActiveThread();
            assert!(!cthread.is_null());
            CThread::new(cthread)
        };
        WORKER.with(|worker| {
            let _borrow = worker.active_thread.borrow_mut();
            f(&mut thread)
        })
    }

    /// Run `f` with a reference to the current Host, or return None if there is no current Host.
    pub fn with_active_host<F, R>(f: F) -> Option<R>
    where
        F: FnOnce(&Host) -> R,
    {
        if !Worker::is_alive() {
            return None;
        }
        let hostp = unsafe { cshadow::worker_getActiveHost() };
        if hostp.is_null() {
            return None;
        }
        let host = unsafe { Host::borrow_from_c(hostp) };
        WORKER.with(|worker| {
            let _borrow = worker.active_host.borrow();
            Some(f(&host))
        })
    }

    /// Whether currently running on a live Worker.
    pub fn is_alive() -> bool {
        unsafe { cshadow::worker_isAlive() != 0 }
    }

    /// Current simulation time, or None if not running on a live Worker.
    pub fn current_time() -> Option<SimulationTime> {
        if !Worker::is_alive() {
            return None;
        }
        Some(SimulationTime::from_c_simtime(unsafe {
            cshadow::worker_getCurrentTime()
        }))
    }

    /// Id of the current worker thread, or None if not running on a live Worker.
    pub fn thread_id() -> Option<i32> {
        if !Worker::is_alive() {
            return None;
        }
        Some(unsafe { cshadow::worker_getThreadID() })
    }
}
