use atomic_refcell::{AtomicRef, AtomicRefCell};
use nix::unistd::Pid;
use once_cell::sync::Lazy;

use crate::core::controller::ShadowStatusBarState;
use crate::core::scheduler::runahead::Runahead;
use crate::core::sim_config::Bandwidth;
use crate::core::work::event::Event;
use crate::core::work::event_queue::ThreadSafeEventQueue;
use crate::cshadow;
use crate::host::host::{Host, HostId, HostInfo};
use crate::host::process::{Process, ProcessId};
use crate::host::thread::{CThread, Thread, ThreadId};
use crate::network::network_graph::{IpAssignment, RoutingInfo};
use crate::utility::childpid_watcher::ChildPidWatcher;
use crate::utility::counter::Counter;
use crate::utility::notnull::*;
use crate::utility::status_bar;
use crate::utility::SyncSendPointer;
use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::simulation_time::SimulationTime;

use std::cell::{Cell, RefCell};
use std::collections::HashMap;
use std::sync::atomic::AtomicBool;
use std::sync::atomic::AtomicU32;
use std::sync::Arc;
use std::sync::Mutex;

static USE_OBJECT_COUNTERS: AtomicBool = AtomicBool::new(false);

// global counters to be used when there is no worker active
static ALLOC_COUNTER: Lazy<Mutex<Counter>> = Lazy::new(|| Mutex::new(Counter::new()));
static DEALLOC_COUNTER: Lazy<Mutex<Counter>> = Lazy::new(|| Mutex::new(Counter::new()));
static SYSCALL_COUNTER: Lazy<Mutex<Counter>> = Lazy::new(|| Mutex::new(Counter::new()));

// thread-local global state
std::thread_local! {
    // Initialized when the worker thread starts running. No shared ownership
    // or access from outside of the current thread.
    static WORKER: once_cell::unsync::OnceCell<RefCell<Worker>> = once_cell::unsync::OnceCell::new();
}

// shared global state
// Must not mutably borrow when the simulation is running. Worker threads should access it through
// `Worker::shared`.
pub static WORKER_SHARED: Lazy<AtomicRefCell<Option<WorkerShared>>> =
    Lazy::new(|| AtomicRefCell::new(None));

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

    // A shared reference to the state in `WORKER_SHARED`.
    shared: AtomicRef<'static, WorkerShared>,

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

impl Worker {
    // Create worker for this thread.
    pub unsafe fn new_for_this_thread(
        worker_pool: *mut cshadow::WorkerPool,
        worker_id: WorkerThreadID,
    ) {
        WORKER.with(|worker| {
            let res = worker.set(RefCell::new(Self {
                worker_id,
                shared: AtomicRef::map(WORKER_SHARED.borrow(), |x| x.as_ref().unwrap()),
                active_host_info: None,
                active_process_info: None,
                active_thread_info: None,
                clock: Clock {
                    now: None,
                    barrier: None,
                },
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

    pub fn update_lowest_used_latency(t: SimulationTime) {
        assert!(t != SimulationTime::ZERO);

        Worker::with(|w| {
            let min_latency_cache = w.min_latency_cache.get();
            if min_latency_cache.is_none() || t < min_latency_cache.unwrap() {
                w.min_latency_cache.set(Some(t));
                w.shared.update_lowest_used_latency(t);
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

#[derive(Debug)]
pub struct WorkerShared {
    pub ip_assignment: IpAssignment<u32>,
    pub routing_info: RoutingInfo<u32>,
    pub host_bandwidths: HashMap<std::net::IpAddr, Bandwidth>,
    pub dns: SyncSendPointer<cshadow::DNS>,
    // allows for easy updating of the status bar's state
    pub status_logger_state: Option<Arc<status_bar::Status<ShadowStatusBarState>>>,
    // number of plugins that failed with a non-zero exit code
    pub num_plugin_errors: AtomicU32,
    // calculates the runahead for the next simulation round
    pub runahead: Runahead,
    pub child_pid_watcher: ChildPidWatcher,
    pub event_queues: HashMap<HostId, Arc<ThreadSafeEventQueue>>,
    pub bootstrap_end_time: EmulatedTime,
    pub sim_end_time: EmulatedTime,
}

impl WorkerShared {
    pub fn dns(&self) -> *mut cshadow::DNS {
        self.dns.ptr()
    }

    pub fn latency(&self, src: std::net::IpAddr, dst: std::net::IpAddr) -> Option<SimulationTime> {
        let src = self.ip_assignment.get_node(src)?;
        let dst = self.ip_assignment.get_node(dst)?;

        Some(SimulationTime::from_nanos(
            self.routing_info.path(src, dst)?.latency_ns,
        ))
    }

    pub fn reliability(&self, src: std::net::IpAddr, dst: std::net::IpAddr) -> Option<f32> {
        let src = self.ip_assignment.get_node(src)?;
        let dst = self.ip_assignment.get_node(dst)?;

        Some(1.0 - self.routing_info.path(src, dst)?.packet_loss)
    }

    pub fn bandwidth(&self, ip: std::net::IpAddr) -> Option<&Bandwidth> {
        self.host_bandwidths.get(&ip)
    }

    pub fn increment_packet_count(&self, src: std::net::IpAddr, dst: std::net::IpAddr) {
        let src = self.ip_assignment.get_node(src).unwrap();
        let dst = self.ip_assignment.get_node(dst).unwrap();

        self.routing_info.increment_packet_count(src, dst)
    }

    pub fn is_routable(&self, src: std::net::IpAddr, dst: std::net::IpAddr) -> bool {
        if self.ip_assignment.get_node(src).is_none() {
            return false;
        }

        if self.ip_assignment.get_node(dst).is_none() {
            return false;
        }

        // the network graph is required to be a connected graph, so they must be routable
        true
    }

    pub fn increment_plugin_error_count(&self) {
        let old_count = self
            .num_plugin_errors
            .fetch_add(1, std::sync::atomic::Ordering::SeqCst);

        self.update_status_logger(|state| {
            // there is a race condition here, so use the max
            let new_value = old_count + 1;
            state.num_failed_processes = std::cmp::max(state.num_failed_processes, new_value);
        });
    }

    pub fn plugin_error_count(&self) -> u32 {
        self.num_plugin_errors
            .load(std::sync::atomic::Ordering::SeqCst)
    }

    /// Update the status logger. If the status logger is disabled, this will be a no-op.
    pub fn update_status_logger(&self, f: impl FnOnce(&mut ShadowStatusBarState)) {
        if let Some(ref logger_state) = self.status_logger_state {
            logger_state.update(f);
        }
    }

    pub fn get_runahead(&self) -> SimulationTime {
        self.runahead.get()
    }

    /// Should only be called from the thread-local worker.
    fn update_lowest_used_latency(&self, min_path_latency: SimulationTime) {
        self.runahead.update_lowest_used_latency(min_path_latency);
    }

    /// Get the pid watcher.
    pub fn child_pid_watcher(&self) -> &ChildPidWatcher {
        &self.child_pid_watcher
    }

    pub fn push_to_host(&self, host: HostId, event: Event) {
        let event_queue = self.event_queues.get(&host).unwrap();
        event_queue.0.lock().unwrap().push(event);
    }
}

impl std::ops::Drop for WorkerShared {
    fn drop(&mut self) {
        unsafe { cshadow::dns_free(self.dns.ptr()) };
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

    use shadow_shim_helper_rs::emulated_time::CEmulatedTime;
    use shadow_shim_helper_rs::simulation_time::CSimulationTime;

    #[no_mangle]
    pub extern "C" fn worker_getDNS() -> *mut cshadow::DNS {
        Worker::with_mut(|w| w.shared.dns()).unwrap()
    }

    #[no_mangle]
    pub extern "C" fn worker_getLatency(
        src: libc::in_addr_t,
        dst: libc::in_addr_t,
    ) -> CSimulationTime {
        let src = std::net::IpAddr::V4(u32::from_be(src).into());
        let dst = std::net::IpAddr::V4(u32::from_be(dst).into());

        let latency = Worker::with_mut(|w| w.shared.latency(src, dst)).unwrap();
        SimulationTime::to_c_simtime(latency)
    }

    #[no_mangle]
    pub extern "C" fn worker_getReliability(
        src: libc::in_addr_t,
        dst: libc::in_addr_t,
    ) -> libc::c_float {
        let src = std::net::IpAddr::V4(u32::from_be(src).into());
        let dst = std::net::IpAddr::V4(u32::from_be(dst).into());

        Worker::with_mut(|w| w.shared.reliability(src, dst).unwrap()).unwrap()
    }

    #[no_mangle]
    pub extern "C" fn worker_getBandwidthDownBytes(ip: libc::in_addr_t) -> u64 {
        let ip = std::net::IpAddr::V4(u32::from_be(ip).into());
        Worker::with_mut(|w| w.shared.bandwidth(ip).unwrap().down_bytes).unwrap()
    }

    #[no_mangle]
    pub extern "C" fn worker_getBandwidthUpBytes(ip: libc::in_addr_t) -> u64 {
        let ip = std::net::IpAddr::V4(u32::from_be(ip).into());
        Worker::with_mut(|w| w.shared.bandwidth(ip).unwrap().up_bytes).unwrap()
    }

    #[no_mangle]
    pub extern "C" fn worker_isRoutable(src: libc::in_addr_t, dst: libc::in_addr_t) -> bool {
        let src = std::net::IpAddr::V4(u32::from_be(src).into());
        let dst = std::net::IpAddr::V4(u32::from_be(dst).into());

        Worker::with_mut(|w| w.shared.is_routable(src, dst)).unwrap()
    }

    #[no_mangle]
    pub extern "C" fn worker_incrementPacketCount(src: libc::in_addr_t, dst: libc::in_addr_t) {
        let src = std::net::IpAddr::V4(u32::from_be(src).into());
        let dst = std::net::IpAddr::V4(u32::from_be(dst).into());

        Worker::with_mut(|w| w.shared.increment_packet_count(src, dst)).unwrap()
    }

    #[no_mangle]
    pub extern "C" fn worker_incrementPluginErrors() {
        Worker::with_mut(|w| w.shared.increment_plugin_error_count()).unwrap()
    }

    /// SAFETY: The returned pointer must not be accessed after this worker thread has exited.
    #[no_mangle]
    pub unsafe extern "C" fn worker_getChildPidWatcher() -> *const ChildPidWatcher {
        Worker::with_mut(|w| w.shared.child_pid_watcher() as *const _).unwrap()
    }

    /// Takes ownership of the event.
    #[no_mangle]
    pub extern "C" fn worker_pushToHost(host: cshadow::HostId, event: *mut Event) {
        assert!(!event.is_null());
        let event = unsafe { Box::from_raw(event) };
        assert!(event.time() >= Worker::round_end_time().unwrap());
        Worker::with_mut(|w| w.shared.push_to_host(host.into(), *event)).unwrap();
    }

    /// Initialize a Worker for this thread.
    #[no_mangle]
    pub unsafe extern "C" fn worker_newForThisThread(
        worker_pool: *mut cshadow::WorkerPool,
        worker_id: i32,
    ) {
        unsafe {
            Worker::new_for_this_thread(
                notnull_mut(worker_pool),
                WorkerThreadID(worker_id.try_into().unwrap()),
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
    pub extern "C" fn worker_setRoundEndTime(t: CSimulationTime) {
        Worker::set_round_end_time(EmulatedTime::from_abs_simtime(
            SimulationTime::from_c_simtime(t).unwrap(),
        ));
    }

    #[no_mangle]
    pub extern "C" fn _worker_getRoundEndTime() -> CSimulationTime {
        SimulationTime::to_c_simtime(Worker::round_end_time().map(|t| t.to_abs_simtime()))
    }

    #[no_mangle]
    pub extern "C" fn worker_setCurrentEmulatedTime(t: CEmulatedTime) {
        Worker::set_current_time(EmulatedTime::from_c_emutime(t).unwrap());
    }

    #[no_mangle]
    pub extern "C" fn worker_clearCurrentTime() {
        Worker::clear_current_time();
    }

    #[no_mangle]
    pub extern "C" fn worker_getCurrentSimulationTime() -> CSimulationTime {
        SimulationTime::to_c_simtime(Worker::current_time().map(|t| t.to_abs_simtime()))
    }

    #[no_mangle]
    pub extern "C" fn worker_getCurrentEmulatedTime() -> CEmulatedTime {
        EmulatedTime::to_c_emutime(Worker::current_time())
    }

    #[no_mangle]
    pub extern "C" fn worker_updateLowestUsedLatency(min_path_latency: CSimulationTime) {
        let min_path_latency = SimulationTime::from_c_simtime(min_path_latency).unwrap();
        Worker::update_lowest_used_latency(min_path_latency);
    }

    #[no_mangle]
    pub extern "C" fn worker_isBootstrapActive() -> bool {
        Worker::with(|w| w.clock.now.unwrap() < w.shared.bootstrap_end_time).unwrap()
    }

    #[no_mangle]
    pub extern "C" fn worker_isSimCompleted() -> bool {
        Worker::with(|w| w.clock.now.unwrap() >= w.shared.sim_end_time).unwrap()
    }

    #[no_mangle]
    pub extern "C" fn _worker_pool() -> *mut cshadow::WorkerPool {
        Worker::with_mut(|w| w.worker_pool).unwrap()
    }

    #[no_mangle]
    pub extern "C" fn worker_isAlive() -> bool {
        Worker::is_alive()
    }

    #[no_mangle]
    pub extern "C" fn worker_resolveIPToAddress(ip: libc::in_addr_t) -> *const cshadow::Address {
        Worker::with(|w| {
            let dns = w.shared.dns.ptr();
            unsafe { cshadow::dns_resolveIPToAddress(dns, ip) }
        })
        .unwrap()
    }

    #[no_mangle]
    pub extern "C" fn worker_resolveNameToAddress(
        name: *const libc::c_char,
    ) -> *const cshadow::Address {
        Worker::with(|w| {
            let dns = w.shared.dns.ptr();
            unsafe { cshadow::dns_resolveNameToAddress(dns, name) }
        })
        .unwrap()
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
