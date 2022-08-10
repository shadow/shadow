use nix::unistd::Pid;
use once_cell::sync::Lazy;
use once_cell::unsync::OnceCell;

use crate::core::support::emulated_time::EmulatedTime;
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

use std::cell::{Cell, RefCell};
use std::sync::atomic::AtomicBool;
use std::sync::Arc;
use std::sync::Mutex;

static USE_OBJECT_COUNTERS: AtomicBool = AtomicBool::new(false);

// counters to be used when there is no worker active
static ALLOC_COUNTER: Lazy<Mutex<Counter>> = Lazy::new(|| Mutex::new(Counter::new()));
static DEALLOC_COUNTER: Lazy<Mutex<Counter>> = Lazy::new(|| Mutex::new(Counter::new()));
static SYSCALL_COUNTER: Lazy<Mutex<Counter>> = Lazy::new(|| Mutex::new(Counter::new()));

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
    now: Option<EmulatedTime>,
    barrier: Option<EmulatedTime>,
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
    bootstrap_end_time: EmulatedTime,

    // This value is not the minimum latency of the simulation, but just a saved copy of this
    // worker's minimum latency.
    min_latency_cache: Cell<Option<SimulationTime>>,

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
        bootstrap_end_time: EmulatedTime,
    ) {
        WORKER.with(|worker| {
            let res = worker.set(RefCell::new(Self {
                worker_id,
                active_host_info: None,
                active_process_info: None,
                active_thread_info: None,
                clock: Clock {
                    now: None,
                    barrier: None,
                },
                bootstrap_end_time,
                min_latency_cache: Cell::new(None),
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

    fn set_round_end_time(t: EmulatedTime) {
        Worker::with_mut(|w| w.clock.barrier.replace(t)).unwrap();
    }

    fn round_end_time() -> Option<EmulatedTime> {
        Worker::with(|w| w.clock.barrier).flatten()
    }

    fn set_current_time(t: EmulatedTime) {
        Worker::with_mut(|w| w.clock.now.replace(t)).unwrap();
    }

    fn clear_current_time() {
        Worker::with_mut(|w| w.clock.now.take()).unwrap();
    }

    pub fn current_time() -> Option<EmulatedTime> {
        Worker::with(|w| w.clock.now).flatten()
    }

    pub fn update_min_host_runahead(t: SimulationTime) {
        assert!(t != SimulationTime::ZERO);

        Worker::with(|w| {
            let min_latency_cache = w.min_latency_cache.get();
            if min_latency_cache.is_none() || t < min_latency_cache.unwrap() {
                w.min_latency_cache.set(Some(t));
                unsafe {
                    cshadow::workerpool_updateMinHostRunahead(
                        w.worker_pool,
                        SimulationTime::to_c_simtime(Some(t)),
                    )
                };
            }
        })
        .unwrap();
    }

    // Runs `f` with a shared reference to the current thread's Worker. Returns
    // None if this thread has no Worker object.
    #[must_use]
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
    #[must_use]
    fn with_mut<F, O>(f: F) -> Option<O>
    where
        F: FnOnce(&mut Worker) -> O,
    {
        WORKER
            .try_with(|w| w.get().map(|w| f(&mut w.borrow_mut())))
            .ok()
            .flatten()
    }

    pub fn increment_object_alloc_counter(s: &str) {
        if !USE_OBJECT_COUNTERS.load(std::sync::atomic::Ordering::Relaxed) {
            return;
        }

        Worker::with_mut(|w| {
            w.object_alloc_counter.add_one(s);
        })
        .unwrap_or_else(|| {
            // no live worker; fall back to the shared counter
            ALLOC_COUNTER.lock().unwrap().add_one(s);
        });
    }

    pub fn increment_object_dealloc_counter(s: &str) {
        if !USE_OBJECT_COUNTERS.load(std::sync::atomic::Ordering::Relaxed) {
            return;
        }

        Worker::with_mut(|w| {
            w.object_dealloc_counter.add_one(s);
        })
        .unwrap_or_else(|| {
            // no live worker; fall back to the shared counter
            DEALLOC_COUNTER.lock().unwrap().add_one(s);
        });
    }

    pub fn worker_add_syscall_counts(syscall_counts: &Counter) {
        Worker::with_mut(|w| {
            w.syscall_counter.add_counter(syscall_counts);
        })
        .unwrap_or_else(|| {
            // no live worker; fall back to the shared counter
            SYSCALL_COUNTER.lock().unwrap().add_counter(syscall_counts);

            // while we handle this okay, this probably indicates an issue somewhere else in the
            // code so panic only in debug builds
            debug_panic!("Trying to add syscall counts when there is no worker");
        });
    }
}

/// Enable object counters. Should be called near the beginning of the program.
pub fn enable_object_counters() {
    USE_OBJECT_COUNTERS.store(true, std::sync::atomic::Ordering::Relaxed);
}

pub fn with_global_syscall_counter<T>(f: impl FnOnce(&Counter) -> T) -> T {
    let counter = SYSCALL_COUNTER.lock().unwrap();
    f(&counter)
}

pub fn with_global_object_counters<T>(f: impl FnOnce(&Counter, &Counter) -> T) -> T {
    let alloc_counter = ALLOC_COUNTER.lock().unwrap();
    let dealloc_counter = DEALLOC_COUNTER.lock().unwrap();
    f(&alloc_counter, &dealloc_counter)
}

mod export {
    use super::*;

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
                EmulatedTime::from_abs_simtime(bootstrap_end_time),
            )
        }
    }

    /// Returns NULL if there is no live Worker.
    #[no_mangle]
    pub extern "C" fn _worker_objectAllocCounter() -> *mut Counter {
        Worker::with_mut(|w| &mut w.object_alloc_counter as *mut Counter)
            .unwrap_or(std::ptr::null_mut())
    }

    /// Implementation for counting allocated objects. Do not use this function directly.
    /// Use worker_count_allocation instead from the call site.
    #[no_mangle]
    pub extern "C" fn worker_increment_object_alloc_counter(object_name: *const libc::c_char) {
        assert!(!object_name.is_null());

        let s = unsafe { std::ffi::CStr::from_ptr(object_name) };
        let s = s.to_str().unwrap();
        Worker::increment_object_alloc_counter(s);
    }

    /// Returns NULL if there is no live Worker.
    #[no_mangle]
    pub extern "C" fn _worker_objectDeallocCounter() -> *mut Counter {
        Worker::with_mut(|w| &mut w.object_dealloc_counter as *mut Counter)
            .unwrap_or(std::ptr::null_mut())
    }

    /// Implementation for counting deallocated objects. Do not use this function directly.
    /// Use worker_count_deallocation instead from the call site.
    #[no_mangle]
    pub extern "C" fn worker_increment_object_dealloc_counter(object_name: *const libc::c_char) {
        assert!(!object_name.is_null());

        let s = unsafe { std::ffi::CStr::from_ptr(object_name) };
        let s = s.to_str().unwrap();
        Worker::increment_object_dealloc_counter(s);
    }

    /// Returns NULL if there is no live Worker.
    #[no_mangle]
    pub extern "C" fn _worker_syscallCounter() -> *mut Counter {
        Worker::with_mut(|w| &mut w.syscall_counter as *mut Counter).unwrap_or(std::ptr::null_mut())
    }

    /// Aggregate the given syscall counts in a worker syscall counter.
    #[no_mangle]
    pub extern "C" fn worker_add_syscall_counts(syscall_counts: *const Counter) {
        assert!(!syscall_counts.is_null());
        let syscall_counts = unsafe { syscall_counts.as_ref() }.unwrap();

        Worker::worker_add_syscall_counts(syscall_counts);
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
        Worker::set_round_end_time(EmulatedTime::from_abs_simtime(
            SimulationTime::from_c_simtime(t).unwrap(),
        ));
    }

    #[no_mangle]
    pub extern "C" fn _worker_getRoundEndTime() -> cshadow::SimulationTime {
        SimulationTime::to_c_simtime(Worker::round_end_time().map(|t| t.to_abs_simtime()))
    }

    #[no_mangle]
    pub extern "C" fn worker_setCurrentEmulatedTime(t: cshadow::EmulatedTime) {
        Worker::set_current_time(EmulatedTime::from_c_emutime(t).unwrap());
    }

    #[no_mangle]
    pub extern "C" fn worker_clearCurrentTime() {
        Worker::clear_current_time();
    }

    #[no_mangle]
    pub extern "C" fn worker_getCurrentSimulationTime() -> cshadow::SimulationTime {
        SimulationTime::to_c_simtime(Worker::current_time().map(|t| t.to_abs_simtime()))
    }

    #[no_mangle]
    pub extern "C" fn worker_getCurrentEmulatedTime() -> cshadow::EmulatedTime {
        EmulatedTime::to_c_emutime(Worker::current_time())
    }

    #[no_mangle]
    pub extern "C" fn worker_updateMinHostRunahead(t: cshadow::SimulationTime) {
        Worker::update_min_host_runahead(SimulationTime::from_c_simtime(t).unwrap());
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

    /// Add the counters to their global counterparts, and clear the provided counters.
    #[no_mangle]
    pub extern "C" fn worker_addToGlobalAllocCounters(
        alloc_counter: *mut Counter,
        dealloc_counter: *mut Counter,
    ) {
        let alloc_counter = unsafe { alloc_counter.as_mut() }.unwrap();
        let dealloc_counter = unsafe { dealloc_counter.as_mut() }.unwrap();

        let mut global_alloc_counter = ALLOC_COUNTER.lock().unwrap();
        let mut global_dealloc_counter = DEALLOC_COUNTER.lock().unwrap();

        global_alloc_counter.add_counter(alloc_counter);
        global_dealloc_counter.add_counter(dealloc_counter);

        *alloc_counter = Counter::new();
        *dealloc_counter = Counter::new();
    }

    /// Add the counters to their global counterparts, and clear the provided counters.
    #[no_mangle]
    pub extern "C" fn worker_addToGlobalSyscallCounter(syscall_counter: *mut Counter) {
        let syscall_counter = unsafe { syscall_counter.as_mut() }.unwrap();

        let mut global_syscall_counter = SYSCALL_COUNTER.lock().unwrap();
        global_syscall_counter.add_counter(&syscall_counter);
        *syscall_counter = Counter::new();
    }
}
