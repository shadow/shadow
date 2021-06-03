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

    worker_pool: *mut cshadow::WorkerPool,
}

std::thread_local! {
    // Initialized when the worker thread starts running. No shared ownership
    // or access from outside of the current thread.
    static WORKER: OnceCell<RefCell<Worker>> = OnceCell::new();
}

impl Worker {
    // Create worker for this thread.
    pub unsafe fn new_for_this_thread(
        worker_pool: *mut cshadow::WorkerPool,
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
                worker_pool: notnull_mut(worker_pool),
            }));
            assert!(res.is_ok(), "Worker already initialized");
        });
    }

    /// Run `f` with a reference to the current Host, or return None if there is no current Host.
    pub fn with_active_host_info<F, R>(f: F) -> Option<R>
    where
        F: FnOnce(&Arc<HostInfo>) -> R,
    {
        Worker::with(|w| w.active_host_info.as_ref().map(f)).flatten()
    }

    /// Set the currently-active Host.
    pub fn set_active_host(host: &Host) {
        let info = host.info().clone();
        let old = Worker::with_mut(|w| w.active_host_info.replace(info)).unwrap();
        debug_assert!(old.is_none());
    }

    /// Clear the currently-active Host.
    pub fn clear_active_host() {
        let old = Worker::with_mut(|w| w.active_host_info.take()).unwrap();
        debug_assert!(old.is_some());
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
        let old = Worker::with_mut(|w| w.active_process_info.replace(info)).unwrap();
        debug_assert!(old.is_none());
    }

    /// Clear the currently-active Process.
    pub fn clear_active_process() {
        let old = Worker::with_mut(|w| w.active_process_info.take()).unwrap();
        debug_assert!(old.is_some());
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
        let old = Worker::with_mut(|w| w.active_thread_info.replace(info)).unwrap();
        debug_assert!(old.is_none());
    }

    /// Clear the currently-active Thread.
    pub fn clear_active_thread() {
        let old = Worker::with_mut(|w| w.active_thread_info.take());
        debug_assert!(!old.is_none());
    }

    /// Whether currently running on a live Worker.
    pub fn is_alive() -> bool {
        Worker::with(|_| ()).is_some()
    }

    /// ID of this thread's Worker, if any.
    pub fn thread_id() -> Option<WorkerThreadID> {
        Worker::with(|w| w.worker_id)
    }

    pub fn active_process_native_pid() -> Option<nix::unistd::Pid> {
        Worker::with(|w| w.active_process_info.as_ref().map(|p| p.native_pid)).flatten()
    }

    pub fn active_process_id() -> Option<ProcessId> {
        Worker::with(|w| w.active_process_info.as_ref().map(|p| p.id)).flatten()
    }

    pub fn active_thread_native_tid() -> Option<nix::unistd::Pid> {
        Worker::with(|w| w.active_thread_info.as_ref().map(|t| t.native_tid)).flatten()
    }

    fn set_round_end_time(t: SimulationTime) {
        Worker::with_mut(|w| w.clock.barrier.replace(t)).unwrap();
    }

    fn round_end_time() -> Option<SimulationTime> {
        Worker::with(|w| w.clock.barrier).flatten()
    }

    fn set_current_time(t: SimulationTime) {
        Worker::with_mut(|w| w.clock.now.replace(t)).unwrap();
    }

    fn clear_current_time() {
        Worker::with_mut(|w| w.clock.now.take());
    }

    pub fn current_time() -> Option<SimulationTime> {
        Worker::with(|w| w.clock.now).flatten()
    }

    fn set_last_event_time(t: SimulationTime) {
        Worker::with_mut(|w| w.clock.last.replace(t)).unwrap();
    }

    // Runs `f` with a shared reference to the current thread's Worker. Returns
    // None if this thread has no Worker object.
    fn with<F, O>(f: F) -> Option<O>
    where
        F: FnOnce(&Worker) -> O,
    {
        WORKER
            .try_with(|w| w.get().map(|w| f(&w.borrow())))
            .ok()
            .flatten()
    }

    // Runs `f` with a mutable reference to the current thread's Worker. Returns
    // None if this thread has no Worker object.
    fn with_mut<F, O>(f: F) -> Option<O>
    where
        F: FnOnce(&mut Worker) -> O,
    {
        WORKER
            .try_with(|w| w.get().map(|w| f(&mut w.borrow_mut())))
            .ok()
            .flatten()
    }
}

mod export {
    use super::*;
    use std::convert::TryInto;

    /// Initialize a Worker for this thread.
    #[no_mangle]
    pub unsafe extern "C" fn worker_newForThisThread(
        worker_pool: *mut cshadow::WorkerPool,
        worker_id: i32,
        bootstrap_end_time: cshadow::SimulationTime,
    ) {
        let bootstrap_end_time = SimulationTime::from_c_simtime(bootstrap_end_time).unwrap();
        unsafe {
            Worker::new_for_this_thread(
                notnull_mut(worker_pool),
                WorkerThreadID(worker_id.try_into().unwrap()),
                bootstrap_end_time,
            )
        }
    }

    /// Returns NULL if there is no live Worker.
    #[no_mangle]
    pub extern "C" fn _worker_objectAllocCounter() -> *mut Counter {
        Worker::with_mut(|w| &mut w.object_alloc_counter as *mut Counter)
            .unwrap_or(std::ptr::null_mut())
    }

    /// Returns NULL if there is no live Worker.
    #[no_mangle]
    pub extern "C" fn _worker_objectDeallocCounter() -> *mut Counter {
        Worker::with_mut(|w| &mut w.object_dealloc_counter as *mut Counter)
            .unwrap_or(std::ptr::null_mut())
    }

    /// Returns NULL if there is no live Worker.
    #[no_mangle]
    pub extern "C" fn _worker_syscallCounter() -> *mut Counter {
        Worker::with_mut(|w| &mut w.syscall_counter as *mut Counter).unwrap_or(std::ptr::null_mut())
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
    pub extern "C" fn worker_setRoundEndTime(t: cshadow::SimulationTime) {
        Worker::set_round_end_time(SimulationTime::from_c_simtime(t).unwrap());
    }

    #[no_mangle]
    pub extern "C" fn _worker_getRoundEndTime() -> cshadow::SimulationTime {
        SimulationTime::to_c_simtime(Worker::round_end_time())
    }

    #[no_mangle]
    pub extern "C" fn worker_setCurrentTime(t: cshadow::SimulationTime) {
        if let Some(t) = SimulationTime::from_c_simtime(t) {
            Worker::set_current_time(t);
        } else {
            Worker::clear_current_time();
        }
    }

    #[no_mangle]
    pub extern "C" fn worker_getCurrentTime() -> cshadow::SimulationTime {
        SimulationTime::to_c_simtime(Worker::current_time())
    }

    #[no_mangle]
    pub extern "C" fn _worker_setLastEventTime(t: cshadow::SimulationTime) {
        Worker::set_last_event_time(SimulationTime::from_c_simtime(t).unwrap());
    }

    #[no_mangle]
    pub extern "C" fn worker_isBootstrapActive() -> bool {
        Worker::with(|w| w.clock.now.unwrap() < w.bootstrap_end_time).unwrap()
    }

    #[no_mangle]
    pub extern "C" fn _worker_pool() -> *mut cshadow::WorkerPool {
        Worker::with_mut(|w| w.worker_pool).unwrap()
    }

    #[no_mangle]
    pub extern "C" fn worker_isAlive() -> bool {
        Worker::is_alive()
    }
}
