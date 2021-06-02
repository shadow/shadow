use nix::unistd::Pid;
use once_cell::unsync::OnceCell;

use crate::core::support::simulation_time::SimulationTime;
use crate::cshadow;
use crate::host::host::Host;
use crate::host::host::HostInfo;
use crate::host::process::Process;
use crate::host::process::ProcessId;
use crate::host::thread::ThreadId;
use crate::host::thread::{CThread, Thread};
use crate::utility::notnull::*;
use std::cell::RefCell;
use std::sync::Arc;

#[derive(Copy, Clone, Debug)]
pub struct WorkerThreadID(u32);

struct ProcessInfo {
    #[allow(dead_code)]
    id: ProcessId,
    native_pid: Pid,
}

struct ThreadInfo {
    #[allow(dead_code)]
    id: ThreadId,
    native_tid: Pid,
}

/// Worker context, containing 'global' information for the current thread.
pub struct Worker {
    worker_id: WorkerThreadID,

    // These store some information about the current Host, Process, and Thread,
    // when applicable. These are used to make this information available to
    // code that might not have access to the objects themselves, such as the
    // ShadowLogger.
    //
    // Because the HostInfo is relatively heavy-weight, we store it in an Arc.
    // Experimentally cloning the Arc is cheaper tha copying by value.
    active_host_info: Option<Arc<HostInfo>>,
    active_process_info: Option<ProcessInfo>,
    active_thread_info: Option<ThreadInfo>,

    // Owned pointer to legacy Worker bits.
    cworker: *mut cshadow::WorkerC,

    // For supporting legacy worker_getActive*.
    // Rust code should instead get these objects through the call stack to
    // ensure aliasing rules are obeyed.
    c_active_host: *mut cshadow::Host,
    c_active_process: *mut cshadow::Process,
    c_active_thread: *mut cshadow::Thread,
}

std::thread_local! {
    // Initialized when the worker thread starts running. No shared ownership
    // or access from outside of the current thread.
    static WORKER: OnceCell<RefCell<Worker>> = OnceCell::new();
}

impl Worker {
    // Create worker for this thread.
    pub unsafe fn new_for_this_thread(cworker: *mut cshadow::WorkerC, worker_id: WorkerThreadID) {
        WORKER.with(|worker| {
            let res = worker.set(RefCell::new(Self {
                worker_id,
                active_host_info: None,
                active_process_info: None,
                active_thread_info: None,
                cworker: notnull_mut(cworker),
                c_active_host: std::ptr::null_mut(),
                c_active_process: std::ptr::null_mut(),
                c_active_thread: std::ptr::null_mut(),
            }));
            assert!(res.is_ok(), "Worker already initialized");
        });
    }

    /// Run `f` with a reference to the current Host, or return None if there is no current Host.
    pub fn with_active_host_info<F, R>(f: F) -> Option<R>
    where
        F: FnOnce(&Arc<HostInfo>) -> R,
    {
        WORKER
            .try_with(|worker| worker.get()?.borrow().active_host_info.as_ref().map(f))
            .ok()
            .flatten()
    }

    /// Set the currently-active Host.
    pub fn set_active_host(host: &Host) {
        let info = host.info().clone();
        let c_host = notnull_mut_debug(host.chost());
        unsafe { cshadow::host_ref(c_host) };
        WORKER.with(|worker| {
            let mut worker = worker.get().unwrap().borrow_mut();
            let old = worker.active_host_info.replace(info);
            debug_assert!(old.is_none());
            debug_assert!(worker.c_active_host.is_null());
            worker.c_active_host = notnull_mut_debug(c_host);
        });
    }

    /// Clear the currently-active Host.
    pub fn clear_active_host() {
        let c_host = WORKER.with(|worker| {
            let mut worker = worker.get().unwrap().borrow_mut();
            let old = worker.active_host_info.take();
            debug_assert!(!old.is_none());
            let old = worker.c_active_host;
            debug_assert!(!old.is_null());
            worker.c_active_host = std::ptr::null_mut();
            old
        });
        unsafe { cshadow::host_unref(c_host) };
    }

    /// Set the currently-active Process.
    pub fn set_active_process(process: &Process) {
        debug_assert_eq!(
            process.host_id(),
            Worker::with_active_host_info(|h| h.id).unwrap()
        );
        let info = ProcessInfo {
            id: process.id(),
            native_pid: process.native_pid(),
        };
        let c_process = notnull_mut_debug(process.cprocess());
        unsafe { cshadow::process_ref(c_process) };
        WORKER.with(|worker| {
            let mut worker = worker.get().unwrap().borrow_mut();
            let old = worker.active_process_info.replace(info);
            debug_assert!(old.is_none());
            debug_assert!(worker.c_active_process.is_null());
            worker.c_active_process = c_process;
        });
    }

    /// Clear the currently-active Process.
    pub fn clear_active_process() {
        let c_process = WORKER.with(|worker| {
            let mut worker = worker.get().unwrap().borrow_mut();
            let old = worker.active_process_info.take();
            debug_assert!(!old.is_none());
            let old = worker.c_active_process;
            debug_assert!(!old.is_null());
            worker.c_active_process = std::ptr::null_mut();
            old
        });
        unsafe { cshadow::process_unref(c_process) };
    }

    /// Set the currently-active Thread.
    pub fn set_active_thread(thread: &CThread) {
        debug_assert_eq!(
            thread.host_id(),
            Worker::with_active_host_info(|h| h.id).unwrap()
        );
        debug_assert_eq!(thread.process_id(), Worker::active_process_id().unwrap());
        let info = ThreadInfo {
            id: thread.id(),
            native_tid: thread.system_tid(),
        };
        let c_thread = notnull_mut_debug(thread.cthread());
        unsafe { cshadow::thread_ref(c_thread) };
        WORKER.with(|worker| {
            let mut worker = worker.get().unwrap().borrow_mut();
            let old = worker.active_thread_info.replace(info);
            debug_assert!(old.is_none());
            debug_assert!(worker.c_active_thread.is_null());
            worker.c_active_thread = c_thread;
        });
    }

    /// Clear the currently-active Thread.
    pub fn clear_active_thread() {
        let c_thread = WORKER.with(|worker| {
            let mut worker = worker.get().unwrap().borrow_mut();
            let old = worker.active_thread_info.take();
            debug_assert!(!old.is_none());
            let old = worker.c_active_thread;
            debug_assert!(!old.is_null());
            worker.c_active_thread = std::ptr::null_mut();
            old
        });
        unsafe { cshadow::thread_unref(c_thread) };
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
        SimulationTime::from_c_simtime(unsafe { cshadow::worker_getCurrentTime() })
    }

    /// ID of this thread's Worker, if any.
    pub fn thread_id() -> Option<WorkerThreadID> {
        WORKER.with(|worker| worker.get().map(|w| w.borrow().worker_id))
    }

    pub fn active_process_native_pid() -> Option<nix::unistd::Pid> {
        WORKER
            .with(|worker| {
                worker.get().map(|w| {
                    w.borrow()
                        .active_process_info
                        .as_ref()
                        .map(|p| p.native_pid)
                })
            })
            .flatten()
    }

    pub fn active_process_id() -> Option<ProcessId> {
        WORKER
            .with(|worker| {
                worker
                    .get()
                    .map(|w| w.borrow().active_process_info.as_ref().map(|p| p.id))
            })
            .flatten()
    }

    pub fn active_thread_native_tid() -> Option<nix::unistd::Pid> {
        WORKER
            .with(|worker| {
                worker
                    .get()
                    .map(|w| w.borrow().active_thread_info.as_ref().map(|t| t.native_tid))
            })
            .flatten()
    }
}

impl Drop for Worker {
    fn drop(&mut self) {
        unsafe { cshadow::workerc_free(self.cworker) }
    }
}

mod export {
    use super::*;
    use std::convert::TryInto;

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
            let worker = unsafe { &*(worker as *const OnceCell<RefCell<Worker>>) };
            worker
                .get()
                .map(|w| Box::into_raw(Box::new(WorkerRefMut(w.borrow_mut()))))
                .unwrap_or(std::ptr::null_mut())
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
            let worker = unsafe { &*(worker as *const OnceCell<RefCell<Worker>>) };
            worker
                .get()
                .map(|w| Box::into_raw(Box::new(WorkerRef(w.borrow()))))
                .unwrap_or(std::ptr::null_mut())
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

    #[no_mangle]
    pub unsafe extern "C" fn worker_setActiveHost(host: *mut cshadow::Host) {
        if host.is_null() {
            Worker::clear_active_host();
        } else {
            let host = unsafe { Host::borrow_from_c(host) };
            Worker::set_active_host(&host);
        }
    }

    #[no_mangle]
    pub unsafe extern "C" fn worker_setActiveProcess(process: *mut cshadow::Process) {
        if process.is_null() {
            Worker::clear_active_process();
        } else {
            let process = unsafe { Process::borrow_from_c(notnull_mut_debug(process)) };
            Worker::set_active_process(&process);
        }
    }

    #[no_mangle]
    pub unsafe extern "C" fn worker_setActiveThread(thread: *mut cshadow::Thread) {
        if thread.is_null() {
            Worker::clear_active_thread();
        } else {
            let thread = unsafe { CThread::new(notnull_mut_debug(thread)) };
            Worker::set_active_thread(&thread);
        }
    }

    #[no_mangle]
    pub unsafe extern "C" fn worker_getActiveHost() -> *mut cshadow::Host {
        WORKER.with(|worker| {
            worker
                .get()
                .map(|w| w.borrow().c_active_host)
                .unwrap_or(std::ptr::null_mut())
        })
    }

    #[no_mangle]
    pub unsafe extern "C" fn worker_getActiveProcess() -> *mut cshadow::Process {
        WORKER.with(|worker| {
            worker
                .get()
                .map(|w| w.borrow().c_active_process)
                .unwrap_or(std::ptr::null_mut())
        })
    }

    #[no_mangle]
    pub unsafe extern "C" fn worker_getActiveThread() -> *mut cshadow::Thread {
        WORKER.with(|worker| {
            worker
                .get()
                .map(|w| w.borrow().c_active_thread)
                .unwrap_or(std::ptr::null_mut())
        })
    }
}
