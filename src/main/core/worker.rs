use crate::core::support::simulation_time::SimulationTime;
use crate::cshadow;
use crate::host::host::Host;
use crate::host::memory_manager::MemoryManager;
use crate::host::process::Process;
use crate::host::thread::{CThread, Thread};
use crate::utility::notnull::*;
use std::cell::RefCell;

#[derive(Copy, Clone, Debug)]
pub struct WorkerThreadID(u32);

/// Worker context, capturing e.g. the current Process and Thread.
pub struct Worker {
    worker_id: WorkerThreadID,

    // Owned pointer to legacy Worker bits.
    cworker: *mut cshadow::WorkerC,

    // This is just to track borrows for now; we retrieve the pointer
    // itself from the C Worker on-demand.
    active_process: RefCell<()>,
    active_thread: RefCell<()>,
    active_host: RefCell<()>,
}

std::thread_local! {
    // Initialized when the worker thread starts running. No shared ownership
    // or access from outside of the current thread.
    static WORKER: RefCell<Option<Worker>> = RefCell::new(None);
}

impl Worker {
    // Create worker for this thread.
    pub unsafe fn new_for_this_thread(cworker: *mut cshadow::WorkerC, worker_id: WorkerThreadID) {
        WORKER.with(|worker| {
            assert!(worker.borrow().is_none());
            *worker.borrow_mut() = Some(Self {
                worker_id,
                cworker: notnull_mut(cworker),

                active_process: RefCell::new(()),
                active_thread: RefCell::new(()),
                active_host: RefCell::new(()),
            })
        });
    }

    // Destroy worker for this thread.
    pub unsafe fn free_for_this_thread() {
        WORKER.with(|worker| {
            assert!(worker.borrow_mut().take().is_some());
        })
    }

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
            let worker = worker.borrow();
            let _borrow_process = worker.as_ref().unwrap().active_process.borrow();
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
            let worker = worker.borrow();
            let _borrow = worker.as_ref().unwrap().active_process.borrow_mut();
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
            let worker = worker.borrow();
            let _borrow = worker.as_ref().unwrap().active_process.borrow();
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
            let worker = worker.borrow();
            let _borrow = worker.as_ref().unwrap().active_process.borrow_mut();
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
            let worker = worker.borrow();
            let _borrow = worker.as_ref().unwrap().active_thread.borrow();
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
            let worker = worker.borrow();
            let _borrow = worker.as_ref().unwrap().active_thread.borrow_mut();
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
            let worker = worker.borrow();
            let _borrow = worker.as_ref().unwrap().active_host.borrow();
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

    /// ID of this thread's Worker, if any.
    pub fn thread_id() -> Option<WorkerThreadID> {
        WORKER.with(|worker| worker.borrow().as_ref().map(|w| w.worker_id))
    }
}

impl Drop for Worker {
    fn drop(&mut self) {
        unsafe { cshadow::workerc_free(self.cworker) }
    }
}

mod export {
    use super::*;
    use std::{
        cell::{Ref, RefMut},
        convert::TryInto,
    };

    /// Initialize a Worker for this thread.
    #[no_mangle]
    pub unsafe extern "C" fn worker_newForThisThread(
        cworker: *mut cshadow::WorkerC,
        worker_id: i32,
    ) {
        unsafe {
            Worker::new_for_this_thread(
                notnull_mut(cworker),
                WorkerThreadID(worker_id.try_into().unwrap()),
            )
        }
    }

    /// A borrowed mutable reference to the current thread's Worker.
    pub struct WorkerRefMut(std::cell::RefMut<'static, Worker>);

    /// A borrowed immutable reference to the current thread's Worker.
    pub struct WorkerRef(std::cell::Ref<'static, Worker>);

    /// If worker is alive, returns mutable reference to it. Otherwise returns NULL.
    /// SAFETY: Returned pointer is invalid after `worker_freeForThisThread` is called
    /// or when global destructors start running.
    #[no_mangle]
    pub unsafe extern "C" fn worker_borrowMut() -> *mut WorkerRefMut {
        WORKER.with(|worker| {
            // Cast to 'static lifetime.
            // SAFETY: Safe by the SAFETY preconditions of this method.
            let worker: &'static RefCell<Option<Worker>> =
                unsafe { &*(worker as *const RefCell<Option<Worker>>) };

            let worker: RefMut<Option<Worker>> = worker.borrow_mut();
            if worker.is_some() {
                let rc: RefMut<Worker> = RefMut::map(worker, |w| w.as_mut().unwrap());
                Box::into_raw(Box::new(WorkerRefMut(rc)))
            } else {
                std::ptr::null_mut()
            }
        })
    }

    /// Return a borrowed reference.
    #[no_mangle]
    pub unsafe extern "C" fn workerrefmut_free(worker: *mut WorkerRefMut) {
        unsafe { Box::from_raw(notnull_mut(worker)) };
    }

    /// SAFETY: Returned pointer must not outlive `workerRefMut`.
    #[no_mangle]
    pub unsafe extern "C" fn workerrefmut_raw(
        worker_ref: *mut WorkerRefMut,
    ) -> *mut cshadow::WorkerC {
        unsafe { worker_ref.as_mut().unwrap().0.cworker }
    }

    /// If worker is alive, returns an immutable reference to it. Otherwise returns NULL.
    /// SAFETY: Returned pointer is invalid after `worker_freeForThisThread` is called
    /// or when global destructors start running.
    #[no_mangle]
    pub unsafe extern "C" fn worker_borrow() -> *mut WorkerRef {
        WORKER.with(|worker| {
            // Cast to 'static lifetime.
            // SAFETY: Safe by the SAFETY preconditions of this method.
            let worker: &'static RefCell<Option<Worker>> =
                unsafe { &*(worker as *const RefCell<Option<Worker>>) };

            let worker: Ref<Option<Worker>> = worker.borrow();
            if worker.is_some() {
                let rc: Ref<Worker> = Ref::map(worker, |w| w.as_ref().unwrap());
                Box::into_raw(Box::new(WorkerRef(rc)))
            } else {
                std::ptr::null_mut()
            }
        })
    }

    /// Return a borrowed reference.
    #[no_mangle]
    pub unsafe extern "C" fn workerref_free(worker: *mut WorkerRef) {
        unsafe { Box::from_raw(worker) };
    }

    /// SAFETY: Returned pointer must not outlive `workerRef`.
    #[no_mangle]
    pub unsafe extern "C" fn workerref_raw(worker_ref: *mut WorkerRef) -> *const cshadow::WorkerC {
        unsafe { worker_ref.as_mut().unwrap().0.cworker }
    }

    /// ID of the current thread's Worker. Panics if the thread has no Worker.
    #[no_mangle]
    pub extern "C" fn worker_threadID() -> i32 {
        Worker::thread_id().unwrap().0.try_into().unwrap()
    }
}
