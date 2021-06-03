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
use crate::utility::counter::Counter;
use crate::utility::notnull::*;
use std::cell::RefCell;
use std::sync::Arc;

#[derive(Copy, Clone, Debug)]
pub struct WorkerThreadID(u32);

struct ProcessInfo {
    id: ProcessId,
    native_pid: Pid,
}

struct ThreadInfo {
    #[allow(dead_code)]
    id: ThreadId,
    native_tid: Pid,
}

struct Clock {
    now: Option<SimulationTime>,
    last: Option<SimulationTime>,
    barrier: Option<SimulationTime>,
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

    clock: Clock,
    bootstrap_end_time: SimulationTime,

    // A counter for all syscalls made by processes freed by this worker.
    syscall_counter: Counter,
    // A counter for objects allocated by this worker.
    object_alloc_counter: Counter,
    // A counter for objects deallocated by this worker.
    object_dealloc_counter: Counter,

    // Owned pointer to legacy Worker bits.
    cworker: *mut cshadow::WorkerC,
}

std::thread_local! {
    // Initialized when the worker thread starts running. No shared ownership
    // or access from outside of the current thread.
    static WORKER: OnceCell<RefCell<Worker>> = OnceCell::new();
}

impl Worker {
    // Create worker for this thread.
    pub unsafe fn new_for_this_thread(
        cworker: *mut cshadow::WorkerC,
        worker_id: WorkerThreadID,
        bootstrap_end_time: SimulationTime,
    ) {
        WORKER.with(|worker| {
            let res = worker.set(RefCell::new(Self {
                worker_id,
                active_host_info: None,
                active_process_info: None,
                active_thread_info: None,
                clock: Clock {
                    now: None,
                    last: None,
                    barrier: None,
                },
                bootstrap_end_time,
                object_alloc_counter: Counter::new(),
                object_dealloc_counter: Counter::new(),
                syscall_counter: Counter::new(),
                cworker: notnull_mut(cworker),
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
        WORKER.with(|worker| {
            let mut worker = worker.get().unwrap().borrow_mut();
            let old = worker.active_host_info.replace(info);
            debug_assert!(old.is_none());
        });
    }

    /// Clear the currently-active Host.
    pub fn clear_active_host() {
        WORKER.with(|worker| {
            let mut worker = worker.get().unwrap().borrow_mut();
            let old = worker.active_host_info.take();
            debug_assert!(!old.is_none());
        });
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
        WORKER.with(|worker| {
            let mut worker = worker.get().unwrap().borrow_mut();
            let old = worker.active_process_info.replace(info);
            debug_assert!(old.is_none());
        });
    }

    /// Clear the currently-active Process.
    pub fn clear_active_process() {
        WORKER.with(|worker| {
            let mut worker = worker.get().unwrap().borrow_mut();
            let old = worker.active_process_info.take();
            debug_assert!(!old.is_none());
        });
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
        WORKER.with(|worker| {
            let mut worker = worker.get().unwrap().borrow_mut();
            let old = worker.active_thread_info.replace(info);
            debug_assert!(old.is_none());
        });
    }

    /// Clear the currently-active Thread.
    pub fn clear_active_thread() {
        WORKER.with(|worker| {
            let mut worker = worker.get().unwrap().borrow_mut();
            let old = worker.active_thread_info.take();
            debug_assert!(!old.is_none());
        });
    }

    /// Whether currently running on a live Worker.
    pub fn is_alive() -> bool {
        unsafe { cshadow::worker_isAlive() != 0 }
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

    fn set_round_end_time(t: SimulationTime) {
        WORKER.with(|w| w.get().unwrap().borrow_mut().clock.barrier.replace(t));
    }

    fn round_end_time() -> Option<SimulationTime> {
        WORKER.with(|w| w.get().unwrap().borrow().clock.barrier)
    }

    fn set_current_time(t: SimulationTime) {
        WORKER.with(|w| w.get().unwrap().borrow_mut().clock.now.replace(t));
    }

    fn clear_current_time() {
        WORKER.with(|w| w.get().unwrap().borrow_mut().clock.now.take());
    }

    pub fn current_time() -> Option<SimulationTime> {
        WORKER
            .with(|w| w.get().map(|w| w.borrow().clock.now))
            .flatten()
    }

    fn set_last_event_time(t: SimulationTime) {
        WORKER.with(|w| w.get().unwrap().borrow_mut().clock.last.replace(t));
    }

    fn bootstrap_end_time() -> SimulationTime {
        WORKER.with(|w| w.get().unwrap().borrow().bootstrap_end_time)
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
        bootstrap_end_time: cshadow::SimulationTime,
    ) {
        let bootstrap_end_time = SimulationTime::from_c_simtime(bootstrap_end_time).unwrap();
        unsafe {
            Worker::new_for_this_thread(
                notnull_mut(cworker),
                WorkerThreadID(worker_id.try_into().unwrap()),
                bootstrap_end_time,
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

    /// SAFETY: Returned pointer must not outlive `workerRefMut`.
    #[no_mangle]
    pub unsafe extern "C" fn workerrefmut_objectAllocCounter(
        worker_ref: *mut WorkerRefMut,
    ) -> *mut Counter {
        unsafe { (&mut worker_ref.as_mut().unwrap().0.object_alloc_counter) as *mut Counter }
    }

    /// SAFETY: Returned pointer must not outlive `workerRefMut`.
    #[no_mangle]
    pub unsafe extern "C" fn workerrefmut_objectDeallocCounter(
        worker_ref: *mut WorkerRefMut,
    ) -> *mut Counter {
        unsafe { (&mut worker_ref.as_mut().unwrap().0.object_dealloc_counter) as *mut Counter }
    }

    /// SAFETY: Returned pointer must not outlive `workerRefMut`.
    #[no_mangle]
    pub unsafe extern "C" fn workerrefmut_syscallCounter(
        worker_ref: *mut WorkerRefMut,
    ) -> *mut Counter {
        unsafe { (&mut worker_ref.as_mut().unwrap().0.object_dealloc_counter) as *mut Counter }
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
    pub unsafe extern "C" fn worker_setRoundEndTime(t: cshadow::SimulationTime) {
        Worker::set_round_end_time(SimulationTime::from_c_simtime(t).unwrap());
    }

    #[no_mangle]
    pub unsafe extern "C" fn _worker_getRoundEndTime() -> cshadow::SimulationTime {
        SimulationTime::to_c_simtime(Worker::round_end_time())
    }

    #[no_mangle]
    pub unsafe extern "C" fn worker_setCurrentTime(t: cshadow::SimulationTime) {
        if let Some(t) = SimulationTime::from_c_simtime(t) {
            Worker::set_current_time(t);
        } else {
            Worker::clear_current_time();
        }
    }

    #[no_mangle]
    pub unsafe extern "C" fn worker_getCurrentTime() -> cshadow::SimulationTime {
        SimulationTime::to_c_simtime(Worker::current_time())
    }

    #[no_mangle]
    pub unsafe extern "C" fn _worker_setLastEventTime(t: cshadow::SimulationTime) {
        Worker::set_last_event_time(SimulationTime::from_c_simtime(t).unwrap());
    }

    #[no_mangle]
    pub unsafe extern "C" fn worker_isBootstrapActive() -> bool {
        Worker::current_time().unwrap() < Worker::bootstrap_end_time()
    }
}
