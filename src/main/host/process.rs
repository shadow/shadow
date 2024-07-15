//! An emulated Linux process.

use std::cell::{Cell, Ref, RefCell, RefMut};
use std::collections::BTreeMap;
use std::ffi::{c_char, c_void, CStr, CString};
use std::fmt::Write;
use std::num::TryFromIntError;
use std::ops::{Deref, DerefMut};
use std::os::fd::AsRawFd;
use std::path::{Path, PathBuf};
use std::sync::atomic::Ordering;
use std::sync::Arc;
#[cfg(feature = "perf_timers")]
use std::time::Duration;

use linux_api::errno::Errno;
use linux_api::fcntl::OFlag;
use linux_api::posix_types::Pid;
use linux_api::sched::{CloneFlags, SuidDump};
use linux_api::signal::{
    defaultaction, siginfo_t, sigset_t, LinuxDefaultAction, SigActionFlags, Signal,
    SignalFromI32Error,
};
use log::{debug, trace, warn};
use rustix::process::{WaitOptions, WaitStatus};
use shadow_shim_helper_rs::explicit_drop::{ExplicitDrop, ExplicitDropper};
use shadow_shim_helper_rs::rootedcell::rc::RootedRc;
use shadow_shim_helper_rs::rootedcell::refcell::RootedRefCell;
use shadow_shim_helper_rs::rootedcell::Root;
use shadow_shim_helper_rs::shim_shmem::ProcessShmem;
use shadow_shim_helper_rs::simulation_time::SimulationTime;
use shadow_shim_helper_rs::syscall_types::{ForeignPtr, ManagedPhysicalMemoryAddr};
use shadow_shim_helper_rs::HostId;
use shadow_shmem::allocator::ShMemBlock;

use super::descriptor::descriptor_table::{DescriptorHandle, DescriptorTable};
use super::descriptor::listener::StateEventSource;
use super::descriptor::{FileSignals, FileState};
use super::host::Host;
use super::memory_manager::{MemoryManager, ProcessMemoryRef, ProcessMemoryRefMut};
use super::syscall::formatter::StraceFmtMode;
use super::syscall::types::ForeignArrayPtr;
use super::thread::{Thread, ThreadId};
use super::timer::Timer;
use crate::core::configuration::{ProcessFinalState, RunningVal};
use crate::core::work::task::TaskRef;
use crate::core::worker::Worker;
use crate::cshadow;
use crate::host::context::ProcessContext;
use crate::host::descriptor::Descriptor;
use crate::host::managed_thread::ManagedThread;
use crate::host::syscall::formatter::FmtOptions;
use crate::utility::callback_queue::CallbackQueue;
#[cfg(feature = "perf_timers")]
use crate::utility::perf_timer::PerfTimer;
use crate::utility::{self, debug_assert_cloexec};

/// Virtual pid of a shadow process
#[derive(Debug, PartialEq, Eq, Hash, Copy, Clone, Ord, PartialOrd)]
pub struct ProcessId(u32);

impl ProcessId {
    // The first Process to run after boot is the "init" process, and has pid=1.
    // In Shadow simulations, this roughly corresponds to Shadow itself. e.g.
    // processes spawned by Shadow itself have a parent pid of 1.
    pub const INIT: Self = ProcessId(1);

    /// Returns what the `ProcessId` would be of a `Process` whose thread
    /// group leader has id `thread_group_leader_tid`.
    pub fn from_thread_group_leader_tid(thread_group_leader_tid: ThreadId) -> Self {
        ProcessId::try_from(libc::pid_t::from(thread_group_leader_tid)).unwrap()
    }
}

impl std::fmt::Display for ProcessId {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "{}", self.0)
    }
}

impl TryFrom<u32> for ProcessId {
    type Error = TryFromIntError;

    fn try_from(val: u32) -> Result<Self, Self::Error> {
        // we don't actually want the value as a `pid_t`, we just want to make sure it can be
        // converted successfully
        let _ = libc::pid_t::try_from(val)?;
        Ok(ProcessId(val))
    }
}

impl TryFrom<libc::pid_t> for ProcessId {
    type Error = TryFromIntError;

    fn try_from(value: libc::pid_t) -> Result<Self, Self::Error> {
        Ok(ProcessId(value.try_into()?))
    }
}

impl From<ProcessId> for u32 {
    fn from(val: ProcessId) -> Self {
        val.0
    }
}

impl From<ProcessId> for libc::pid_t {
    fn from(val: ProcessId) -> Self {
        val.0.try_into().unwrap()
    }
}

impl From<ThreadId> for ProcessId {
    fn from(value: ThreadId) -> Self {
        ProcessId::try_from(libc::pid_t::from(value)).unwrap()
    }
}

#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum ExitStatus {
    Normal(i32),
    Signaled(Signal),
    /// The process was killed by Shadow rather than exiting "naturally" as part
    /// of the simulation. Currently this only happens when the process is still
    /// running when the simulation stop_time is reached.
    ///
    /// A signal delivered via `shutdown_signal` does not result in this status;
    /// e.g. if the process is killed directly by the signal the ExitStatus will
    /// be `Signaled`; if the process handles the signal and exits by calling
    /// `exit`, the status will be `Normal`.
    StoppedByShadow,
}

#[derive(Debug)]
struct StraceLogging {
    file: RootedRefCell<std::fs::File>,
    options: FmtOptions,
}

/// Parts of the process that are present in all states.
struct Common {
    id: ProcessId,
    host_id: HostId,

    // Parent pid (aka `ppid`), as returned e.g. by `getppid`.  This can change
    // at runtime if the original parent exits and is reaped.
    parent_pid: Cell<ProcessId>,

    // Process group id (aka `pgid`), as returned e.g. by `getpgid`.
    group_id: Cell<ProcessId>,

    // Session id, as returned e.g. by `getsid`.
    session_id: Cell<ProcessId>,

    // Signal to send to parent on death.
    exit_signal: Option<Signal>,

    // unique id of the program that this process should run
    name: CString,

    // the name of the executable as provided in shadow's config, for logging purposes
    plugin_name: CString,

    // absolute path to the process's working directory.
    // This must remain in sync with the actual working dir of the native process.
    // See https://github.com/shadow/shadow/issues/2960
    working_dir: CString,
}

impl Common {
    fn id(&self) -> ProcessId {
        self.id
    }

    fn physical_address(&self, vptr: ForeignPtr<()>) -> ManagedPhysicalMemoryAddr {
        // We currently don't keep a true system-wide virtual <-> physical address
        // mapping. Instead we simply assume that no shadow processes map the same
        // underlying physical memory, and that therefore (pid, virtual address)
        // uniquely defines a physical address.
        //
        // If we ever want to support futexes in memory shared between processes,
        // we'll need to change this.  The most foolproof way to do so is probably
        // to change ManagedPhysicalMemoryAddr to be a bigger struct that identifies where
        // the mapped region came from (e.g. what file), and the offset into that
        // region. Such "fat" physical pointers might make memory management a
        // little more cumbersome though, e.g. when using them as keys in the futex
        // table.
        //
        // Alternatively we could hash the region+offset to a 64-bit value, but
        // then we'd need to deal with potential collisions. On average we'd expect
        // a collision after 2**32 physical addresses; i.e. they *probably*
        // wouldn't happen in practice for realistic simulations.

        // Linux uses the bottom 48-bits for user-space virtual addresses, giving
        // us 16 bits for the pid.
        const PADDR_BITS: i32 = 64;
        const VADDR_BITS: i32 = 48;
        const PID_BITS: i32 = 16;
        assert_eq!(PADDR_BITS, PID_BITS + VADDR_BITS);

        let high_part: u64 = u64::from(u32::from(self.id())) << VADDR_BITS;
        assert_eq!(
            ProcessId::try_from((high_part >> VADDR_BITS) as u32),
            Ok(self.id())
        );

        let low_part = u64::from(vptr);
        assert_eq!(low_part >> VADDR_BITS, 0);

        ManagedPhysicalMemoryAddr::from(high_part | low_part)
    }

    fn name(&self) -> &str {
        self.name.to_str().unwrap()
    }

    pub fn thread_group_leader_id(&self) -> ThreadId {
        // tid of the thread group leader is equal to the pid.
        ThreadId::from(self.id())
    }
}

/// A process that is currently runnable.
pub struct RunnableProcess {
    common: Common,

    // Expected end state, if any. We'll report an error if this is present and
    // doesn't match the actual exit status.
    //
    // This will be None e.g. for processes created via `fork` instead of
    // spawned directly from Shadow's config file. In those cases it's the
    // parent's responsibility to reap and interpret the exit status.
    expected_final_state: Option<ProcessFinalState>,

    // Shared memory allocation for shared state with shim.
    shim_shared_mem_block: ShMemBlock<'static, ProcessShmem>,

    // Shared with forked Processes
    strace_logging: Option<Arc<StraceLogging>>,

    // The shim's log file. This gets dup'd into the ManagedProcess
    // where the shim can write to it directly. We persist it to handle the case
    // where we need to recreatea a ManagedProcess and have it continue writing
    // to the same file.
    //
    // Shared with forked Processes
    shimlog_file: Arc<std::fs::File>,

    // "dumpable" state, as manipulated via the prctl operations PR_SET_DUMPABLE
    // and PR_GET_DUMPABLE.
    dumpable: Cell<SuidDump>,

    native_pid: Pid,

    // timer that tracks the amount of CPU time we spend on plugin execution and processing
    #[cfg(feature = "perf_timers")]
    cpu_delay_timer: RefCell<PerfTimer>,
    #[cfg(feature = "perf_timers")]
    total_run_time: Cell<Duration>,

    itimer_real: RefCell<Timer>,

    // The `RootedRc` lets us hold a reference to a thread without holding a
    // reference to the thread list. e.g. this lets us implement the `clone`
    // syscall, which adds a thread to the list while we have a reference to the
    // parent thread.
    threads: RefCell<BTreeMap<ThreadId, RootedRc<RootedRefCell<Thread>>>>,

    // References to `Self::memory_manager` cached on behalf of C code using legacy
    // C memory access APIs.
    // TODO: Remove these when we've migrated Shadow off of the APIs that need
    // them (probably by migrating all the calling code to Rust).
    //
    // SAFETY: Must be before memory_manager for drop order.
    unsafe_borrow_mut: RefCell<Option<UnsafeBorrowMut>>,
    unsafe_borrows: RefCell<Vec<UnsafeBorrow>>,

    // `clone(2)` documents that if `CLONE_THREAD` is set, then `CLONE_VM` must
    // also be set. Hence all threads in a process always share the same virtual
    // address space, and hence we have a `MemoryManager` at the `Process` level
    // rather than the `Thread` level.
    // SAFETY: Must come after `unsafe_borrows` and `unsafe_borrow_mut`.
    // Boxed to avoid invalidating those if Self is moved.
    memory_manager: Box<RefCell<MemoryManager>>,

    // Listeners for child-events.
    // e.g. these listeners are notified when a child of this process exits.
    child_process_event_listeners: RefCell<StateEventSource>,
}

impl RunnableProcess {
    /// Spawn a `ManagedThread` corresponding to the given `exec` syscall
    /// parameters.  Intended for use by the `exec` syscall handlers. Whether it
    /// succeeds or fails, does *not* mutate `self`, though `self`'s strace and
    /// shim log files will be passed into the new `ManagedThread`.
    ///
    /// In case the native `exec` syscall fails, the corresponding error is returned.
    pub fn spawn_mthread_for_exec(
        &self,
        host: &Host,
        plugin_path: &CStr,
        argv: Vec<CString>,
        envv: Vec<CString>,
    ) -> Result<ManagedThread, Errno> {
        ManagedThread::spawn(
            plugin_path,
            argv,
            envv,
            self.strace_logging
                .as_ref()
                .map(|s| s.file.borrow(host.root()))
                .as_deref(),
            &self.shimlog_file,
            host.preload_paths(),
        )
    }

    /// Call after a thread has exited. Removes the thread and does corresponding cleanup and notifications.
    fn reap_thread(&self, host: &Host, threadrc: RootedRc<RootedRefCell<Thread>>) {
        let threadrc = ExplicitDropper::new(threadrc, |t| {
            t.explicit_drop_recursive(host.root(), host);
        });
        let thread = threadrc.borrow(host.root());

        assert!(!thread.is_running());

        // If the `clear_child_tid` attribute on the thread is set, and there are
        // any other threads left alive in the process, perform a futex wake on
        // that address. This mechanism is typically used in `pthread_join` etc.
        // See `set_tid_address(2)`.
        let clear_child_tid_pvp = thread.get_tid_address();
        if !clear_child_tid_pvp.is_null() && self.threads.borrow().len() > 0 {
            self.memory_manager
                .borrow_mut()
                .write(clear_child_tid_pvp, &0)
                .unwrap();

            // Wake the corresponding futex.
            let futexes = host.futextable_borrow();
            let addr = self
                .common
                .physical_address(clear_child_tid_pvp.cast::<()>());

            if let Some(futex) = futexes.get(addr) {
                futex.wake(1);
            }
        }
    }

    /// This cleans up memory references left over from legacy C code; usually
    /// a syscall handler.
    ///
    /// Writes the leftover mutable ref to memory (if any), and frees
    /// all memory refs.
    pub fn free_unsafe_borrows_flush(&self) -> Result<(), Errno> {
        self.unsafe_borrows.borrow_mut().clear();

        let unsafe_borrow_mut = self.unsafe_borrow_mut.borrow_mut().take();
        if let Some(borrow) = unsafe_borrow_mut {
            borrow.flush()
        } else {
            Ok(())
        }
    }

    /// This cleans up memory references left over from legacy C code; usually
    /// a syscall handler.
    ///
    /// Frees all memory refs without writing back to memory.
    pub fn free_unsafe_borrows_noflush(&self) {
        self.unsafe_borrows.borrow_mut().clear();

        let unsafe_borrow_mut = self.unsafe_borrow_mut.borrow_mut().take();
        if let Some(borrow) = unsafe_borrow_mut {
            borrow.noflush();
        }
    }

    #[track_caller]
    pub fn memory_borrow(&self) -> impl Deref<Target = MemoryManager> + '_ {
        self.memory_manager.borrow()
    }

    #[track_caller]
    pub fn memory_borrow_mut(&self) -> impl DerefMut<Target = MemoryManager> + '_ {
        self.memory_manager.borrow_mut()
    }

    pub fn strace_logging_options(&self) -> Option<FmtOptions> {
        self.strace_logging.as_ref().map(|x| x.options)
    }

    /// If strace logging is disabled, this function will do nothing and return `None`.
    pub fn with_strace_file<T>(&self, f: impl FnOnce(&mut std::fs::File) -> T) -> Option<T> {
        // TODO: get Host from caller. Would need t update syscall-logger.
        Worker::with_active_host(|host| {
            let strace_logging = self.strace_logging.as_ref()?;
            let mut file = strace_logging.file.borrow_mut(host.root());
            Some(f(&mut file))
        })
        .unwrap()
    }

    pub fn native_pid(&self) -> Pid {
        self.native_pid
    }

    #[track_caller]
    fn first_live_thread(&self, root: &Root) -> Option<Ref<RootedRc<RootedRefCell<Thread>>>> {
        Ref::filter_map(self.threads.borrow(), |threads| {
            threads.values().next().map(|thread| {
                // There shouldn't be any non-running threads in the table.
                assert!(thread.borrow(root).is_running());
                thread
            })
        })
        .ok()
    }

    /// Returns a dynamically borrowed reference to the first live thread.
    /// This is meant primarily for the MemoryManager.
    #[track_caller]
    pub fn first_live_thread_borrow(
        &self,
        root: &Root,
    ) -> Option<impl Deref<Target = RootedRc<RootedRefCell<Thread>>> + '_> {
        self.first_live_thread(root)
    }

    #[track_caller]
    fn thread(&self, virtual_tid: ThreadId) -> Option<Ref<RootedRc<RootedRefCell<Thread>>>> {
        Ref::filter_map(self.threads.borrow(), |threads| threads.get(&virtual_tid)).ok()
    }

    #[track_caller]
    pub fn thread_borrow(
        &self,
        virtual_tid: ThreadId,
    ) -> Option<impl Deref<Target = RootedRc<RootedRefCell<Thread>>> + '_> {
        self.thread(virtual_tid)
    }

    // Disposes of `self`, returning the internal `Common` for reuse.
    // Used internally when changing states.
    fn into_common(self) -> Common {
        // There shouldn't be any outstanding unsafe borrows when changing
        // states, since that would indicate C code might still have a pointer
        // to memory.
        assert!(self.unsafe_borrow_mut.take().is_none());
        assert!(self.unsafe_borrows.take().is_empty());

        self.common
    }

    /// Starts the CPU delay timer.
    /// Panics if the timer is already running.
    #[cfg(feature = "perf_timers")]
    pub fn start_cpu_delay_timer(&self) {
        self.cpu_delay_timer.borrow_mut().start()
    }

    /// Stop the timer and return the most recent (not cumulative) duration.
    /// Panics if the timer was not already running.
    #[cfg(feature = "perf_timers")]
    pub fn stop_cpu_delay_timer(&self, host: &Host) -> Duration {
        let mut timer = self.cpu_delay_timer.borrow_mut();
        timer.stop();
        let total_elapsed = timer.elapsed();
        let prev_total = self.total_run_time.replace(total_elapsed);
        let delta = total_elapsed - prev_total;

        if let Some(mut tracker) = host.tracker_borrow_mut() {
            unsafe {
                cshadow::tracker_addProcessingTimeNanos(
                    &mut *tracker,
                    delta.as_nanos().try_into().unwrap(),
                )
            };
            host.cpu_borrow_mut().add_delay(delta);
        }
        delta
    }

    fn interrupt_with_signal(&self, host: &Host, signal: Signal) {
        let threads = self.threads.borrow();
        for thread in threads.values() {
            let thread = thread.borrow(host.root());
            {
                let thread_shmem = thread.shmem();
                let host_lock = host.shim_shmem_lock_borrow().unwrap();
                let thread_shmem_protected = thread_shmem.protected.borrow(&host_lock.root);
                let blocked_signals = thread_shmem_protected.blocked_signals;
                if blocked_signals.has(signal) {
                    continue;
                }
            }
            let Some(mut cond) = thread.syscall_condition_mut() else {
                // Defensively handle this gracefully, but it probably shouldn't happen.
                // The only thread in the process not blocked on a syscall should be
                // the current-running thread (if any), but the caller should have
                // delivered the signal synchronously instead of using this function
                // in that case.
                warn!("thread {:?} has no syscall_condition. How?", thread.id());
                continue;
            };
            cond.wakeup_for_signal(host, signal);
            break;
        }
    }

    /// Send the signal described in `siginfo` to `process`. `current_thread`
    /// should be set if there is one (e.g. if this is being called from a syscall
    /// handler), and `None` otherwise (e.g. when called from a timer expiration event).
    ///
    /// An event will be scheduled to deliver the signal unless `current_thread`
    /// is set, and belongs to the process `self`, and doesn't have the signal
    /// blocked.  In that the signal will be processed synchronously when
    /// returning from the current syscall.
    pub fn signal(&self, host: &Host, current_thread: Option<&Thread>, siginfo_t: &siginfo_t) {
        let signal = match siginfo_t.signal() {
            Ok(s) => s,
            Err(SignalFromI32Error(0)) => return,
            Err(SignalFromI32Error(n)) => panic!("Bad signo {n}"),
        };

        // Scope for `process_shmem_protected`
        {
            let host_shmem = host.shim_shmem_lock_borrow().unwrap();
            let mut process_shmem_protected = self
                .shim_shared_mem_block
                .protected
                .borrow_mut(&host_shmem.root);
            // SAFETY: We don't try to call any of the function pointers.
            let action = unsafe { process_shmem_protected.signal_action(signal) };
            match unsafe { action.handler() } {
                linux_api::signal::SignalHandler::Handler(_) => (),
                linux_api::signal::SignalHandler::Action(_) => (),
                linux_api::signal::SignalHandler::SigIgn => return,
                linux_api::signal::SignalHandler::SigDfl => {
                    if defaultaction(signal) == LinuxDefaultAction::IGN {
                        return;
                    }
                }
            }

            if process_shmem_protected.pending_signals.has(signal) {
                // Signal is already pending. From signal(7):In the case where a
                // standard signal is already pending, the siginfo_t structure (see
                // sigaction(2)) associated with that signal is not overwritten on
                // arrival of subsequent instances of the same signal.
                return;
            }
            process_shmem_protected.pending_signals.add(signal);
            process_shmem_protected.set_pending_standard_siginfo(signal, siginfo_t);
        }

        if let Some(thread) = current_thread {
            if thread.process_id() == self.common.id() {
                let host_shmem = host.shim_shmem_lock_borrow().unwrap();
                let threadmem = thread.shmem();
                let threadprotmem = threadmem.protected.borrow(&host_shmem.root);
                if !threadprotmem.blocked_signals.has(signal) {
                    // Target process is this process, and current thread hasn't blocked
                    // the signal.  It will be delivered to this thread when it resumes.
                    return;
                }
            }
        }

        self.interrupt_with_signal(host, signal);
    }

    /// Adds a new thread to the process and schedules it to run.
    /// Intended for use by `clone`.
    pub fn add_thread(&self, host: &Host, thread: RootedRc<RootedRefCell<Thread>>) {
        let pid = self.common.id();
        let tid = thread.borrow(host.root()).id();
        self.threads.borrow_mut().insert(tid, thread);

        // Schedule thread to start. We're giving the caller's reference to thread
        // to the TaskRef here, which is why we don't increment its ref count to
        // create the TaskRef, but do decrement it on cleanup.
        let task = TaskRef::new(move |host| {
            host.resume(pid, tid);
        });
        host.schedule_task_with_delay(task, SimulationTime::ZERO);
    }

    /// Create a new `Process`, forked from `self`, with the thread `new_thread_group_leader`.
    pub fn new_forked_process(
        &self,
        host: &Host,
        flags: CloneFlags,
        exit_signal: Option<Signal>,
        new_thread_group_leader: RootedRc<RootedRefCell<Thread>>,
    ) -> RootedRc<RootedRefCell<Process>> {
        let new_tgl_tid;
        let native_pid;
        {
            let new_tgl = new_thread_group_leader.borrow(host.root());
            new_tgl_tid = new_tgl.id();
            native_pid = new_tgl.native_pid();
        }
        let pid = ProcessId::from_thread_group_leader_tid(new_tgl_tid);
        assert_eq!(
            pid,
            new_thread_group_leader.borrow(host.root()).process_id()
        );
        let plugin_name = self.common.plugin_name.clone();
        let name = make_name(host, plugin_name.to_str().unwrap(), pid);

        let parent_pid = if flags.contains(CloneFlags::CLONE_PARENT) {
            self.common.parent_pid.get()
        } else {
            self.common.id
        };

        // Process group is always inherited from the parent process.
        let process_group_id = self.common.group_id.get();

        // Session is always inherited from the parent process.
        let session_id = self.common.session_id.get();

        let common = Common {
            id: pid,
            host_id: host.id(),
            name,
            plugin_name,
            working_dir: self.common.working_dir.clone(),
            parent_pid: Cell::new(parent_pid),
            group_id: Cell::new(process_group_id),
            session_id: Cell::new(session_id),
            exit_signal,
        };

        // The child will log to the same strace log file. Entries contain thread IDs,
        // though it might be tricky to map those back to processes.
        let strace_logging = self.strace_logging.as_ref().cloned();

        // `fork(2)`:
        //  > The child does not inherit timers from its parent
        //  > (setitimer(2), alarm(2), timer_create(2)).
        let itimer_real = RefCell::new(Timer::new(move |host| itimer_real_expiration(host, pid)));

        let threads = RefCell::new(BTreeMap::from([(new_tgl_tid, new_thread_group_leader)]));

        let shim_shared_mem = ProcessShmem::new(
            &host.shim_shmem_lock_borrow().unwrap().root,
            host.shim_shmem().serialize(),
            host.id(),
            strace_logging
                .as_ref()
                .map(|x| x.file.borrow(host.root()).as_raw_fd()),
        );
        let shim_shared_mem_block = shadow_shmem::allocator::shmalloc(shim_shared_mem);

        let runnable_process = RunnableProcess {
            common,
            expected_final_state: None,
            shim_shared_mem_block,
            strace_logging,
            dumpable: self.dumpable.clone(),
            native_pid,
            #[cfg(feature = "perf_timers")]
            cpu_delay_timer: RefCell::new(PerfTimer::new()),
            #[cfg(feature = "perf_timers")]
            total_run_time: Cell::new(Duration::ZERO),
            itimer_real,
            threads,
            unsafe_borrow_mut: RefCell::new(None),
            unsafe_borrows: RefCell::new(Vec::new()),
            memory_manager: Box::new(RefCell::new(unsafe { MemoryManager::new(native_pid) })),
            child_process_event_listeners: Default::default(),
            shimlog_file: self.shimlog_file.clone(),
        };
        let child_process = Process {
            state: RefCell::new(Some(ProcessState::Runnable(runnable_process))),
        };
        RootedRc::new(host.root(), RootedRefCell::new(host.root(), child_process))
    }

    /// Shared memory for this process.
    pub fn shmem(&self) -> impl Deref<Target = ShMemBlock<'static, ProcessShmem>> + '_ {
        &self.shim_shared_mem_block
    }
}

impl ExplicitDrop for RunnableProcess {
    type ExplicitDropParam = Host;
    type ExplicitDropResult = ();

    fn explicit_drop(mut self, host: &Self::ExplicitDropParam) -> Self::ExplicitDropResult {
        let threads = std::mem::take(self.threads.get_mut());
        for thread in threads.into_values() {
            thread.explicit_drop_recursive(host.root(), host);
        }
    }
}

/// A process that has exited.
pub struct ZombieProcess {
    common: Common,

    exit_status: ExitStatus,
}

impl ZombieProcess {
    pub fn exit_status(&self) -> ExitStatus {
        self.exit_status
    }

    /// Process that can reap this zombie process, if any.
    pub fn reaper<'host>(
        &self,
        host: &'host Host,
    ) -> Option<impl Deref<Target = RootedRc<RootedRefCell<Process>>> + 'host> {
        let parent_pid = self.common.parent_pid.get();
        if parent_pid == ProcessId::INIT {
            return None;
        }
        let parentrc = host.process_borrow(parent_pid)?;

        // If the parent has *explicitly* ignored the exit signal, then it
        // doesn't reap.
        //
        // `waitpid(2)`:
        // > POSIX.1-2001 specifies that if the disposition of SIGCHLD is set to SIG_IGN or the SA_NOCLDWAIT flag is set for SIGCHLD  (see
        // > sigaction(2)),  then  children  that  terminate  do not become zombies and a call to wait() or waitpid() will block until all
        // > children have terminated, and then fail with errno set to ECHILD.  (The original POSIX standard left the behavior of  setting
        // > SIGCHLD to SIG_IGN unspecified.  Note that even though the default disposition of SIGCHLD is "ignore", explicitly setting the
        // > disposition to SIG_IGN results in different treatment of zombie process children.)
        //
        // TODO: validate that this applies to whatever signal is configured as the exit
        // signal, even if it's not SIGCHLD.
        if let Some(exit_signal) = self.common.exit_signal {
            let parent = parentrc.borrow(host.root());
            let parent_shmem = parent.shmem();
            let host_shmem_lock = host.shim_shmem_lock_borrow().unwrap();
            let parent_shmem_protected = parent_shmem.protected.borrow(&host_shmem_lock.root);
            // SAFETY: We don't dereference function pointers.
            let action = unsafe { parent_shmem_protected.signal_action(exit_signal) };
            if action.is_ignore() {
                return None;
            }
        }

        Some(parentrc)
    }

    fn notify_parent_of_exit(&self, host: &Host) {
        let Some(exit_signal) = self.common.exit_signal else {
            trace!("Not notifying parent of exit: no signal specified");
            return;
        };
        let parent_pid = self.common.parent_pid.get();
        if parent_pid == ProcessId::INIT {
            trace!("Not notifying parent of exit: parent is 'init'");
            return;
        }
        let Some(parent_rc) = host.process_borrow(parent_pid) else {
            trace!("Not notifying parent of exit: parent {parent_pid:?} not found");
            return;
        };
        let parent = parent_rc.borrow(host.root());
        let siginfo = self.exit_siginfo(exit_signal);

        let Some(parent_runnable) = parent.as_runnable() else {
            trace!("Not notifying parent of exit: {parent_pid:?} not running");
            debug_panic!("Non-running parent process shouldn't be possible.");
            #[allow(unreachable_code)]
            {
                return;
            }
        };
        parent_runnable.signal(host, None, &siginfo);
        CallbackQueue::queue_and_run(|q| {
            let mut parent_child_listeners =
                parent_runnable.child_process_event_listeners.borrow_mut();
            parent_child_listeners.notify_listeners(
                FileState::CHILD_EVENT,
                FileState::CHILD_EVENT,
                FileSignals::empty(),
                q,
            );
        });
    }

    /// Construct a siginfo containing information about how the process exited.
    /// Used internally to send a signal to the parent process, and by the
    /// `waitid` syscall handler.
    ///
    /// `exit_signal` is the signal to set in the `siginfo_t`.
    pub fn exit_siginfo(&self, exit_signal: Signal) -> siginfo_t {
        match self.exit_status {
            ExitStatus::Normal(exit_code) => siginfo_t::new_for_sigchld_exited(
                exit_signal,
                self.common.id.into(),
                0,
                exit_code,
                0,
                0,
            ),
            ExitStatus::Signaled(fatal_signal) => {
                // This ought to be `siginfo_t::new_for_sigchld_dumped` if
                // the child dumped core, but that depends on various other
                // system variables outside of our control. We always report
                // that no core was dropped for determinism.
                siginfo_t::new_for_sigchld_killed(
                    exit_signal,
                    self.common.id.into(),
                    0,
                    fatal_signal,
                    0,
                    0,
                )
            }

            ExitStatus::StoppedByShadow => unreachable!(),
        }
    }
}

/// Inner implementation of a simulated process.
enum ProcessState {
    Runnable(RunnableProcess),
    Zombie(ZombieProcess),
}

impl ProcessState {
    fn common(&self) -> &Common {
        match self {
            ProcessState::Runnable(r) => &r.common,
            ProcessState::Zombie(z) => &z.common,
        }
    }

    fn common_mut(&mut self) -> &mut Common {
        match self {
            ProcessState::Runnable(r) => &mut r.common,
            ProcessState::Zombie(z) => &mut z.common,
        }
    }

    fn as_runnable(&self) -> Option<&RunnableProcess> {
        match self {
            ProcessState::Runnable(r) => Some(r),
            ProcessState::Zombie(_) => None,
        }
    }

    fn as_runnable_mut(&mut self) -> Option<&mut RunnableProcess> {
        match self {
            ProcessState::Runnable(r) => Some(r),
            ProcessState::Zombie(_) => None,
        }
    }

    fn as_zombie(&self) -> Option<&ZombieProcess> {
        match self {
            ProcessState::Runnable(_) => None,
            ProcessState::Zombie(z) => Some(z),
        }
    }
}

impl ExplicitDrop for ProcessState {
    type ExplicitDropParam = Host;
    type ExplicitDropResult = ();

    fn explicit_drop(self, host: &Self::ExplicitDropParam) -> Self::ExplicitDropResult {
        match self {
            ProcessState::Runnable(r) => r.explicit_drop(host),
            ProcessState::Zombie(_) => (),
        }
    }
}

/// A simulated process.
pub struct Process {
    // Most of the implementation should be in [`ProcessState`].
    // This wrapper allows us to change the state.
    state: RefCell<Option<ProcessState>>,
}

fn itimer_real_expiration(host: &Host, pid: ProcessId) {
    let Some(process) = host.process_borrow(pid) else {
        debug!("Process {:?} no longer exists", pid);
        return;
    };
    let process = process.borrow(host.root());
    let Some(runnable) = process.as_runnable() else {
        debug!("Process {:?} no longer running", &*process.name());
        return;
    };
    let timer = runnable.itimer_real.borrow();
    // The siginfo_t structure only has an i32. Presumably we want to just truncate in
    // case of overflow.
    let expiration_count = timer.expiration_count() as i32;
    let siginfo_t = siginfo_t::new_for_timer(Signal::SIGALRM, 0, expiration_count);
    process.signal(host, None, &siginfo_t);
}

impl Process {
    fn common(&self) -> Ref<Common> {
        Ref::map(self.state.borrow(), |state| {
            state.as_ref().unwrap().common()
        })
    }

    fn common_mut(&self) -> RefMut<Common> {
        RefMut::map(self.state.borrow_mut(), |state| {
            state.as_mut().unwrap().common_mut()
        })
    }

    fn as_runnable(&self) -> Option<Ref<RunnableProcess>> {
        Ref::filter_map(self.state.borrow(), |state| {
            state.as_ref().unwrap().as_runnable()
        })
        .ok()
    }

    fn as_runnable_mut(&self) -> Option<RefMut<RunnableProcess>> {
        RefMut::filter_map(self.state.borrow_mut(), |state| {
            state.as_mut().unwrap().as_runnable_mut()
        })
        .ok()
    }

    /// Borrows a reference to the internal [`RunnableProcess`] if `self` is runnable.
    pub fn borrow_as_runnable(&self) -> Option<impl Deref<Target = RunnableProcess> + '_> {
        self.as_runnable()
    }

    fn as_zombie(&self) -> Option<Ref<ZombieProcess>> {
        Ref::filter_map(self.state.borrow(), |state| {
            state.as_ref().unwrap().as_zombie()
        })
        .ok()
    }

    /// Borrows a reference to the internal [`ZombieProcess`] if `self` is a zombie.
    pub fn borrow_as_zombie(&self) -> Option<impl Deref<Target = ZombieProcess> + '_> {
        self.as_zombie()
    }

    /// Spawn a new process. The process will be runnable via [`Self::resume`]
    /// once it has been added to the `Host`'s process list.
    pub fn spawn(
        host: &Host,
        plugin_name: CString,
        plugin_path: &CStr,
        argv: Vec<CString>,
        envv: Vec<CString>,
        pause_for_debugging: bool,
        strace_logging_options: Option<FmtOptions>,
        expected_final_state: ProcessFinalState,
    ) -> Result<RootedRc<RootedRefCell<Process>>, Errno> {
        debug!("starting process '{:?}'", plugin_name);

        let main_thread_id = host.get_new_thread_id();
        let process_id = ProcessId::from(main_thread_id);

        let desc_table = RootedRc::new(
            host.root(),
            RootedRefCell::new(host.root(), DescriptorTable::new()),
        );
        let itimer_real = RefCell::new(Timer::new(move |host| {
            itimer_real_expiration(host, process_id)
        }));

        let name = make_name(host, plugin_name.to_str().unwrap(), process_id);

        let mut file_basename = PathBuf::new();
        file_basename.push(host.data_dir_path());
        file_basename.push(format!(
            "{exe_name}.{id}",
            exe_name = plugin_name.to_str().unwrap(),
            id = u32::from(process_id)
        ));

        let strace_logging = strace_logging_options.map(|options| {
            let file =
                std::fs::File::create(Self::static_output_file_name(&file_basename, "strace"))
                    .unwrap();
            debug_assert_cloexec(&file);
            Arc::new(StraceLogging {
                file: RootedRefCell::new(host.root(), file),
                options,
            })
        });

        let shim_shared_mem = ProcessShmem::new(
            &host.shim_shmem_lock_borrow().unwrap().root,
            host.shim_shmem().serialize(),
            host.id(),
            strace_logging
                .as_ref()
                .map(|x| x.file.borrow(host.root()).as_raw_fd()),
        );
        let shim_shared_mem_block = shadow_shmem::allocator::shmalloc(shim_shared_mem);

        let working_dir = utility::pathbuf_to_nul_term_cstring(
            std::fs::canonicalize(host.data_dir_path()).unwrap(),
        );

        #[cfg(feature = "perf_timers")]
        let cpu_delay_timer = {
            let mut t = PerfTimer::new();
            t.stop();
            RefCell::new(t)
        };

        // TODO: measure execution time of creating the main_thread with
        // cpu_delay_timer? We previously did, but it's a little complex to do so,
        // and it shouldn't matter much.

        {
            let mut descriptor_table = desc_table.borrow_mut(host.root());
            Self::open_stdio_file_helper(
                &mut descriptor_table,
                libc::STDIN_FILENO.try_into().unwrap(),
                "/dev/null".into(),
                OFlag::O_RDONLY,
            );

            let name = Self::static_output_file_name(&file_basename, "stdout");
            Self::open_stdio_file_helper(
                &mut descriptor_table,
                libc::STDOUT_FILENO.try_into().unwrap(),
                name,
                OFlag::O_WRONLY,
            );

            let name = Self::static_output_file_name(&file_basename, "stderr");
            Self::open_stdio_file_helper(
                &mut descriptor_table,
                libc::STDERR_FILENO.try_into().unwrap(),
                name,
                OFlag::O_WRONLY,
            );
        }

        let shimlog_file = Arc::new(
            std::fs::File::create(Self::static_output_file_name(&file_basename, "shimlog"))
                .unwrap(),
        );
        debug_assert_cloexec(&shimlog_file);

        let mthread = ManagedThread::spawn(
            plugin_path,
            argv,
            envv,
            strace_logging
                .as_ref()
                .map(|s| s.file.borrow(host.root()))
                .as_deref(),
            &shimlog_file,
            host.preload_paths(),
        )?;
        let native_pid = mthread.native_pid();
        let main_thread =
            Thread::wrap_mthread(host, mthread, desc_table, process_id, main_thread_id).unwrap();

        debug!("process '{:?}' started", plugin_name);

        if pause_for_debugging {
            // will block until logger output has been flushed
            // there is a race condition where other threads may log between the
            // `eprintln` and `raise` below, but it should be rare
            log::logger().flush();

            // Use a single `eprintln` to ensure we hold the lock for the whole message.
            // Defensively pre-construct a single string so that `eprintln` is
            // more likely to use a single `write` call, to minimize the chance
            // of more lines being written to stdout in the meantime, and in
            // case of C code writing to `STDERR` directly without taking Rust's
            // lock.
            let msg = format!(
                "\
              \n** Pausing with SIGTSTP to enable debugger attachment to managed process\
              \n** '{plugin_name:?}' (pid {native_pid:?}).\
              \n** If running Shadow under Bash, resume Shadow by pressing Ctrl-Z to background\
              \n** this task, and then typing \"fg\".\
              \n** If running GDB, resume Shadow by typing \"signal SIGCONT\"."
            );
            eprintln!("{}", msg);

            rustix::process::kill_process(rustix::process::getpid(), rustix::process::Signal::Tstp)
                .unwrap();
        }

        let memory_manager = unsafe { MemoryManager::new(native_pid) };
        let threads = RefCell::new(BTreeMap::from([(
            main_thread_id,
            RootedRc::new(host.root(), RootedRefCell::new(host.root(), main_thread)),
        )]));

        let common = Common {
            id: process_id,
            host_id: host.id(),
            working_dir,
            name,
            plugin_name,
            parent_pid: Cell::new(ProcessId::INIT),
            group_id: Cell::new(ProcessId::INIT),
            session_id: Cell::new(ProcessId::INIT),
            // Exit signal is moot; since parent is INIT there will never
            // be a valid target for it.
            exit_signal: None,
        };
        Ok(RootedRc::new(
            host.root(),
            RootedRefCell::new(
                host.root(),
                Self {
                    state: RefCell::new(Some(ProcessState::Runnable(RunnableProcess {
                        common,
                        expected_final_state: Some(expected_final_state),
                        shim_shared_mem_block,
                        memory_manager: Box::new(RefCell::new(memory_manager)),
                        itimer_real,
                        strace_logging,
                        dumpable: Cell::new(SuidDump::SUID_DUMP_USER),
                        native_pid,
                        unsafe_borrow_mut: RefCell::new(None),
                        unsafe_borrows: RefCell::new(Vec::new()),
                        threads,
                        #[cfg(feature = "perf_timers")]
                        cpu_delay_timer,
                        #[cfg(feature = "perf_timers")]
                        total_run_time: Cell::new(Duration::ZERO),
                        child_process_event_listeners: Default::default(),
                        shimlog_file,
                    }))),
                },
            ),
        ))
    }

    pub fn id(&self) -> ProcessId {
        self.common().id
    }

    pub fn parent_id(&self) -> ProcessId {
        self.common().parent_pid.get()
    }

    pub fn set_parent_id(&self, pid: ProcessId) {
        self.common().parent_pid.set(pid)
    }

    pub fn group_id(&self) -> ProcessId {
        self.common().group_id.get()
    }

    pub fn set_group_id(&self, id: ProcessId) {
        self.common().group_id.set(id)
    }

    pub fn session_id(&self) -> ProcessId {
        self.common().session_id.get()
    }

    pub fn set_session_id(&self, id: ProcessId) {
        self.common().session_id.set(id)
    }

    pub fn host_id(&self) -> HostId {
        self.common().host_id
    }

    /// Get process's "dumpable" state, as manipulated by the prctl operations `PR_SET_DUMPABLE` and
    /// `PR_GET_DUMPABLE`.
    pub fn dumpable(&self) -> SuidDump {
        self.as_runnable().unwrap().dumpable.get()
    }

    /// Set process's "dumpable" state, as manipulated by the prctl operations `PR_SET_DUMPABLE` and
    /// `PR_GET_DUMPABLE`.
    pub fn set_dumpable(&self, val: SuidDump) {
        assert!(val == SuidDump::SUID_DUMP_DISABLE || val == SuidDump::SUID_DUMP_USER);
        self.as_runnable().unwrap().dumpable.set(val)
    }

    /// Deprecated wrapper for `RunnableProcess::start_cpu_delay_timer`
    #[cfg(feature = "perf_timers")]
    pub fn start_cpu_delay_timer(&self) {
        self.as_runnable().unwrap().start_cpu_delay_timer()
    }

    /// Deprecated wrapper for `RunnableProcess::stop_cpu_delay_timer`
    #[cfg(feature = "perf_timers")]
    pub fn stop_cpu_delay_timer(&self, host: &Host) -> Duration {
        self.as_runnable().unwrap().stop_cpu_delay_timer(host)
    }

    pub fn thread_group_leader_id(&self) -> ThreadId {
        self.common().thread_group_leader_id()
    }

    /// Resume execution of `tid` (if it exists).
    /// Should only be called from `Host::resume`.
    pub fn resume(&self, host: &Host, tid: ThreadId) {
        trace!("Continuing thread {} in process {}", tid, self.id());

        let threadrc = {
            let Some(runnable) = self.as_runnable() else {
                debug!("Process {} is no longer running", &*self.name());
                return;
            };
            let threads = runnable.threads.borrow();
            let Some(thread) = threads.get(&tid) else {
                debug!("Thread {} no longer exists", tid);
                return;
            };
            // Clone the thread reference, so that we don't hold a dynamically
            // borrowed reference to the thread list while running the thread.
            thread.clone(host.root())
        };
        let threadrc = ExplicitDropper::new(threadrc, |t| {
            t.explicit_drop_recursive(host.root(), host);
        });
        let thread = threadrc.borrow(host.root());

        Worker::set_active_thread(&threadrc);

        #[cfg(feature = "perf_timers")]
        self.start_cpu_delay_timer();

        Process::set_shared_time(host);

        // Discard any unapplied latency.
        // We currently only want this mechanism to force a yield if the thread itself
        // never yields; we don't want unapplied latency to accumulate and force a yield
        // under normal circumstances.
        host.shim_shmem_lock_borrow_mut()
            .unwrap()
            .unapplied_cpu_latency = SimulationTime::ZERO;

        let ctx = ProcessContext::new(host, self);
        let res = thread.resume(&ctx);

        #[cfg(feature = "perf_timers")]
        {
            let delay = self.stop_cpu_delay_timer(host);
            debug!("process '{}' ran for {:?}", &*self.name(), delay);
        }
        #[cfg(not(feature = "perf_timers"))]
        debug!("process '{}' done continuing", &*self.name());

        match res {
            crate::host::thread::ResumeResult::Blocked => {
                debug!(
                    "thread {tid} in process '{}' still running, but blocked",
                    &*self.name()
                );
            }
            crate::host::thread::ResumeResult::ExitedThread(return_code) => {
                debug!(
                    "thread {tid} in process '{}' exited with code {return_code}",
                    &*self.name(),
                );
                let (threadrc, last_thread) = {
                    let runnable = self.as_runnable().unwrap();
                    let mut threads = runnable.threads.borrow_mut();
                    let threadrc = threads.remove(&tid).unwrap();
                    (threadrc, threads.is_empty())
                };
                self.as_runnable().unwrap().reap_thread(host, threadrc);
                if last_thread {
                    self.handle_process_exit(host, false);
                }
            }
            crate::host::thread::ResumeResult::ExitedProcess => {
                debug!(
                    "Process {} exited while running thread {tid}",
                    &*self.name(),
                );
                self.handle_process_exit(host, false);
            }
        };

        Worker::clear_active_thread();
    }

    /// Terminate the Process.
    ///
    /// Should only be called from [`Host::free_all_applications`].
    pub fn stop(&self, host: &Host) {
        // Scope for `runnable`
        {
            let Some(runnable) = self.as_runnable() else {
                debug!("process {} has already stopped", &*self.name());
                return;
            };
            debug!("terminating process {}", &*self.name());

            #[cfg(feature = "perf_timers")]
            runnable.start_cpu_delay_timer();

            if let Err(err) = rustix::process::kill_process(
                runnable.native_pid().into(),
                rustix::process::Signal::Kill,
            ) {
                warn!("kill: {:?}", err);
            }

            #[cfg(feature = "perf_timers")]
            {
                let delay = runnable.stop_cpu_delay_timer(host);
                debug!("process '{}' stopped in {:?}", &*self.name(), delay);
            }
            #[cfg(not(feature = "perf_timers"))]
            debug!("process '{}' stopped", &*self.name());
        }

        // Mutates `self.state`, so we need to have dropped `runnable`.
        self.handle_process_exit(host, true);
    }

    /// See `RunnableProcess::signal`.
    ///
    /// No-op if the `self` is a `ZombieProcess`.
    pub fn signal(&self, host: &Host, current_thread: Option<&Thread>, siginfo_t: &siginfo_t) {
        // Using full-match here to force update if we add more states later.
        match self.state.borrow().as_ref().unwrap() {
            ProcessState::Runnable(r) => r.signal(host, current_thread, siginfo_t),
            ProcessState::Zombie(_) => {
                // Sending a signal to a zombie process is a no-op.
                debug!("Process {} no longer running", &*self.name());
            }
        }
    }

    fn open_stdio_file_helper(
        descriptor_table: &mut DescriptorTable,
        fd: DescriptorHandle,
        path: PathBuf,
        access_mode: OFlag,
    ) {
        let stdfile = unsafe { cshadow::regularfile_new() };
        let cwd = rustix::process::getcwd(Vec::new()).unwrap();
        let path = utility::pathbuf_to_nul_term_cstring(path);
        // "Convert" to libc int, assuming here that the kernel's `OFlag` values
        // are compatible with libc's values.
        // XXX: We're assuming here that the kernel and libc flags are ABI
        // compatible, which isn't guaranteed, but is mostly true in practice.
        // TODO: We probably ought to change `regularfile_open` and friends to
        // use a direct syscall instead of libc's wrappers, and explicitly take
        // the kernel version of flags, mode, etc.
        let access_mode = access_mode.bits();
        let errorcode = unsafe {
            cshadow::regularfile_open(
                stdfile,
                path.as_ptr(),
                access_mode | libc::O_CREAT | libc::O_TRUNC,
                libc::S_IRUSR | libc::S_IWUSR | libc::S_IRGRP | libc::S_IROTH,
                cwd.as_ptr(),
            )
        };
        if errorcode != 0 {
            panic!(
                "Opening {}: {:?}",
                path.to_str().unwrap(),
                linux_api::errno::Errno::try_from(-errorcode).unwrap()
            );
        }
        let desc = unsafe {
            Descriptor::from_legacy_file(
                stdfile as *mut cshadow::LegacyFile,
                linux_api::fcntl::OFlag::empty(),
            )
        };
        let prev = descriptor_table.register_descriptor_with_fd(desc, fd);
        assert!(prev.is_none());
        trace!(
            "Successfully opened fd {} at {}",
            fd,
            path.to_str().unwrap()
        );
    }

    // Needed during early init, before `Self` is created.
    fn static_output_file_name(file_basename: &Path, extension: &str) -> PathBuf {
        let mut path = file_basename.to_owned().into_os_string();
        path.push(".");
        path.push(extension);
        path.into()
    }

    pub fn name(&self) -> impl Deref<Target = str> + '_ {
        Ref::map(self.common(), |c| c.name.to_str().unwrap())
    }

    pub fn plugin_name(&self) -> impl Deref<Target = str> + '_ {
        Ref::map(self.common(), |c| c.plugin_name.to_str().unwrap())
    }

    /// Deprecated wrapper for `RunnableProcess::memory_borrow_mut`
    #[track_caller]
    pub fn memory_borrow_mut(&self) -> impl DerefMut<Target = MemoryManager> + '_ {
        std_util::nested_ref::NestedRefMut::map(self.as_runnable().unwrap(), |runnable| {
            runnable.memory_manager.borrow_mut()
        })
    }

    /// Deprecated wrapper for `RunnableProcess::memory_borrow`
    #[track_caller]
    pub fn memory_borrow(&self) -> impl Deref<Target = MemoryManager> + '_ {
        std_util::nested_ref::NestedRef::map(self.as_runnable().unwrap(), |runnable| {
            runnable.memory_manager.borrow()
        })
    }

    /// Deprecated wrapper for `RunnableProcess::strace_logging_options`
    pub fn strace_logging_options(&self) -> Option<FmtOptions> {
        self.as_runnable().unwrap().strace_logging_options()
    }

    /// Deprecated wrapper for `RunnableProcess::with_strace_file`
    pub fn with_strace_file<T>(&self, f: impl FnOnce(&mut std::fs::File) -> T) -> Option<T> {
        self.as_runnable().unwrap().with_strace_file(f)
    }

    /// Deprecated wrapper for `RunnableProcess::native_pid`
    pub fn native_pid(&self) -> Pid {
        self.as_runnable().unwrap().native_pid()
    }

    /// Deprecated wrapper for `RunnableProcess::realtime_timer_borrow`
    #[track_caller]
    pub fn realtime_timer_borrow(&self) -> impl Deref<Target = Timer> + '_ {
        std_util::nested_ref::NestedRef::map(self.as_runnable().unwrap(), |runnable| {
            runnable.itimer_real.borrow()
        })
    }

    /// Deprecated wrapper for `RunnableProcess::realtime_timer_borrow_mut`
    #[track_caller]
    pub fn realtime_timer_borrow_mut(&self) -> impl DerefMut<Target = Timer> + '_ {
        std_util::nested_ref::NestedRefMut::map(self.as_runnable().unwrap(), |runnable| {
            runnable.itimer_real.borrow_mut()
        })
    }

    /// Deprecated wrapper for `RunnableProcess::first_live_thread_borrow`
    #[track_caller]
    pub fn first_live_thread_borrow(
        &self,
        root: &Root,
    ) -> Option<impl Deref<Target = RootedRc<RootedRefCell<Thread>>> + '_> {
        std_util::nested_ref::NestedRef::filter_map(self.as_runnable()?, |runnable| {
            runnable.first_live_thread(root)
        })
    }

    /// Deprecated wrapper for `RunnableProcess::thread_borrow`
    pub fn thread_borrow(
        &self,
        virtual_tid: ThreadId,
    ) -> Option<impl Deref<Target = RootedRc<RootedRefCell<Thread>>> + '_> {
        std_util::nested_ref::NestedRef::filter_map(self.as_runnable()?, |runnable| {
            runnable.thread(virtual_tid)
        })
    }

    /// Deprecated wrapper for [`RunnableProcess::free_unsafe_borrows_flush`].
    pub fn free_unsafe_borrows_flush(&self) -> Result<(), Errno> {
        self.as_runnable().unwrap().free_unsafe_borrows_flush()
    }

    /// Deprecated wrapper for [`RunnableProcess::free_unsafe_borrows_noflush`].
    pub fn free_unsafe_borrows_noflush(&self) {
        self.as_runnable().unwrap().free_unsafe_borrows_noflush()
    }

    pub fn physical_address(&self, vptr: ForeignPtr<()>) -> ManagedPhysicalMemoryAddr {
        self.common().physical_address(vptr)
    }

    pub fn is_running(&self) -> bool {
        self.as_runnable().is_some()
    }

    /// Transitions `self` from a `RunnableProcess` to a `ZombieProcess`.
    fn handle_process_exit(&self, host: &Host, killed_by_shadow: bool) {
        debug!(
            "process '{}' has completed or is otherwise no longer running",
            &*self.name()
        );

        // Take and dispose of all of the threads.
        // TODO: consider doing this while the `self.state` mutable reference is held
        // as with the other cleanup below. Right now this breaks some C code that expects
        // to be able to lookup the thread's process name.
        {
            let runnable = self.as_runnable().unwrap();
            let threads = std::mem::take(&mut *runnable.threads.borrow_mut());
            for (_tid, threadrc) in threads.into_iter() {
                threadrc.borrow(host.root()).handle_process_exit();
                runnable.reap_thread(host, threadrc);
            }
        }

        // Intentionally hold the borrow on self.state to ensure the state
        // transition is "atomic".
        let mut opt_state = self.state.borrow_mut();

        let state = opt_state.take().unwrap();
        let ProcessState::Runnable(runnable) = state else {
            unreachable!("Tried to handle process exit of non-running process");
        };

        #[cfg(feature = "perf_timers")]
        debug!(
            "total runtime for process '{}' was {:?}",
            runnable.common.name(),
            runnable.total_run_time.get()
        );

        let wait_res: Option<WaitStatus> =
            rustix::process::waitpid(Some(runnable.native_pid().into()), WaitOptions::empty())
                .unwrap_or_else(|e| {
                    panic!("Error waiting for {:?}: {:?}", runnable.native_pid(), e)
                });
        let wait_status = wait_res.unwrap();
        let exit_status = if killed_by_shadow {
            if wait_status.terminating_signal()
                != Some(Signal::SIGKILL.as_i32().try_into().unwrap())
            {
                warn!("Unexpected waitstatus after killed by shadow: {wait_status:?}");
            }
            ExitStatus::StoppedByShadow
        } else if let Some(code) = wait_status.exit_status() {
            ExitStatus::Normal(code.try_into().unwrap())
        } else if let Some(signal) = wait_status.terminating_signal() {
            ExitStatus::Signaled(Signal::try_from(i32::try_from(signal).unwrap()).unwrap())
        } else {
            panic!(
                "Unexpected status: {wait_status:?} for pid {:?}",
                runnable.native_pid()
            );
        };

        let (main_result_string, log_level) = {
            let mut s = format!(
                "process '{name}' exited with status {exit_status:?}",
                name = runnable.common.name()
            );
            if let Some(expected_final_state) = runnable.expected_final_state {
                let actual_final_state = match exit_status {
                    ExitStatus::Normal(i) => ProcessFinalState::Exited { exited: i },
                    ExitStatus::Signaled(s) => ProcessFinalState::Signaled {
                        // This conversion will fail on realtime signals, but that
                        // should currently be impossible since we don't support
                        // sending realtime signals.
                        signaled: s.try_into().unwrap(),
                    },
                    ExitStatus::StoppedByShadow => ProcessFinalState::Running(RunningVal::Running),
                };
                if expected_final_state == actual_final_state {
                    (s, log::Level::Debug)
                } else {
                    Worker::increment_plugin_error_count();
                    write!(s, "; expected end state was {expected_final_state} but was {actual_final_state}").unwrap();
                    (s, log::Level::Error)
                }
            } else {
                (s, log::Level::Debug)
            }
        };
        log::log!(log_level, "{}", main_result_string);

        let zombie = ZombieProcess {
            common: runnable.into_common(),
            exit_status,
        };
        zombie.notify_parent_of_exit(host);

        *opt_state = Some(ProcessState::Zombie(zombie));
    }

    /// Deprecated wrapper for `RunnableProcess::add_thread`
    pub fn add_thread(&self, host: &Host, thread: RootedRc<RootedRefCell<Thread>>) {
        self.as_runnable().unwrap().add_thread(host, thread)
    }

    /// FIXME: still needed? Time is now updated more granularly in the Thread code
    /// when xferring control to/from shim.
    fn set_shared_time(host: &Host) {
        let mut host_shmem = host.shim_shmem_lock_borrow_mut().unwrap();
        host_shmem.max_runahead_time = Worker::max_event_runahead_time(host);
        host.shim_shmem()
            .sim_time
            .store(Worker::current_time().unwrap(), Ordering::Relaxed);
    }

    /// Deprecated wrapper for `RunnableProcess::shmem`
    pub fn shmem(&self) -> impl Deref<Target = ShMemBlock<'static, ProcessShmem>> + '_ {
        Ref::map(self.as_runnable().unwrap(), |r| &r.shim_shared_mem_block)
    }

    /// Resource usage, as returned e.g. by the `getrusage` syscall.
    pub fn rusage(&self) -> linux_api::resource::rusage {
        warn_once_then_debug!(
            "resource usage (rusage) tracking unimplemented; Returning bogus zeroed values"
        );
        // TODO: Actually track some of these.
        // Assuming we want to support `RUSAGE_THREAD` in the `getrusage`
        // syscall, we'll actually want to track at the thread level, and either
        // increment at both thread and process level at the points where we do
        // the tracking, or dynamically iterate over the threads here and sum
        // the results.
        linux_api::resource::rusage {
            ru_utime: linux_api::time::kernel_old_timeval {
                tv_sec: 0,
                tv_usec: 0,
            },
            ru_stime: linux_api::time::kernel_old_timeval {
                tv_sec: 0,
                tv_usec: 0,
            },
            ru_maxrss: 0,
            ru_ixrss: 0,
            ru_idrss: 0,
            ru_isrss: 0,
            ru_minflt: 0,
            ru_majflt: 0,
            ru_nswap: 0,
            ru_inblock: 0,
            ru_oublock: 0,
            ru_msgsnd: 0,
            ru_msgrcv: 0,
            ru_nsignals: 0,
            ru_nvcsw: 0,
            ru_nivcsw: 0,
        }
    }

    /// Signal that will be sent to parent process on exit. Typically `Some(SIGCHLD)`.
    pub fn exit_signal(&self) -> Option<Signal> {
        self.common().exit_signal
    }

    pub fn current_working_dir(&self) -> impl Deref<Target = CString> + '_ {
        Ref::map(self.common(), |common| &common.working_dir)
    }

    /// Set the process's working directory.
    /// This must be kept in sync with the actual working dir of the native process.
    /// See <https://github.com/shadow/shadow/issues/2960>
    // TODO: This ought to be at the thread level, to support `CLONE_FS`.
    pub fn set_current_working_dir(&self, path: CString) {
        self.common_mut().working_dir = path;
    }

    /// Update `self` to complete an `exec` syscall from thread `tid`, replacing
    /// the running managed process with `mthread`.
    pub fn update_for_exec(&mut self, host: &Host, tid: ThreadId, mthread: ManagedThread) {
        let Some(mut runnable) = self.as_runnable_mut() else {
            // This could happen if another event runs before the "execve completion" event
            // and kills the process. e.g. another thread in the process could run and
            // execute the `exit_group` syscall.
            log::debug!(
                "Process {:?} exited before it could complete execve",
                self.id()
            );
            mthread.kill_and_drop();
            return;
        };
        let old_native_pid = std::mem::replace(&mut runnable.native_pid, mthread.native_pid());

        // Kill the previous native process
        rustix::process::kill_process(old_native_pid.into(), rustix::process::Signal::Kill)
            .expect("Unable to send kill signal to managed process {old_native_pid:?}");
        let wait_res = rustix::process::waitpid(Some(old_native_pid.into()), WaitOptions::empty())
            .unwrap()
            .unwrap();
        assert_eq!(
            wait_res.terminating_signal(),
            Some(Signal::SIGKILL.as_i32().try_into().unwrap())
        );

        let execing_thread = runnable.threads.borrow_mut().remove(&tid).unwrap();

        // Dispose of all threads other than the thread that's running `exec`.
        for (_tid, thread) in runnable.threads.replace(BTreeMap::new()) {
            // Notify the ManagedThread that the native process has exited.
            thread.borrow(host.root()).mthread().handle_process_exit();

            thread.explicit_drop_recursive(host.root(), host);
        }

        // Recreate the `MemoryManager`
        {
            // We can't safely replace the memory manager if there are outstanding
            // unsafe references in C code. There shouldn't be any, though, since
            // this is only called from the `execve` and `execveat` syscall handlers,
            // which are in Rust.
            let unsafe_borrow_mut = runnable.unsafe_borrow_mut.borrow();
            let unsafe_borrows = runnable.unsafe_borrows.borrow();
            assert!(unsafe_borrow_mut.is_none());
            assert!(unsafe_borrows.is_empty());
            // Replace the MM, while still holding the references to the unsafe borrows
            // to ensure none exist.
            runnable
                .memory_manager
                .replace(unsafe { MemoryManager::new(mthread.native_pid()) });
        }

        let new_tid = runnable.common.thread_group_leader_id();
        log::trace!(
            "updating for exec; pid:{pid}, tid:{tid:?}, new_tid:{new_tid:?}",
            pid = runnable.common.id
        );
        execing_thread
            .borrow_mut(host.root())
            .update_for_exec(host, mthread, new_tid);

        runnable
            .threads
            .borrow_mut()
            .insert(new_tid, execing_thread);

        // Exit signal is reset to SIGCHLD.
        runnable.common.exit_signal = Some(Signal::SIGCHLD);

        // Reset signal actions to default.
        // `execve(2)`:
        // POSIX.1 specifies that the dispositions of any signals that
        // are ignored or set to the default are left unchanged.  POSIX.1
        // specifies one exception: if SIGCHLD is being ignored, then an
        // implementation may leave the disposition unchanged or reset it
        // to the default; Linux does the former.
        let host_shmem_prot = host.shim_shmem_lock_borrow_mut().unwrap();
        let mut shmem_prot = runnable
            .shim_shared_mem_block
            .protected
            .borrow_mut(&host_shmem_prot.root);
        for signal in Signal::standard_signals() {
            let current_action = unsafe { shmem_prot.signal_action(signal) };
            if !(current_action.is_default()
                || current_action.is_ignore()
                || signal == Signal::SIGCHLD && current_action.is_ignore())
            {
                unsafe {
                    *shmem_prot.signal_action_mut(signal) = linux_api::signal::sigaction::new_raw(
                        linux_api::signal::SignalHandler::SigDfl,
                        SigActionFlags::empty(),
                        sigset_t::EMPTY,
                        None,
                    )
                };
            }
        }
    }
}

impl Drop for Process {
    fn drop(&mut self) {
        // Should have been explicitly dropped.
        debug_assert!(self.state.borrow().is_none());
    }
}

impl ExplicitDrop for Process {
    type ExplicitDropParam = Host;
    type ExplicitDropResult = ();

    fn explicit_drop(mut self, host: &Self::ExplicitDropParam) -> Self::ExplicitDropResult {
        // Should normally only be dropped in the zombie state.
        debug_assert!(self.as_zombie().is_some() || std::thread::panicking());

        let state = self.state.get_mut().take().unwrap();
        state.explicit_drop(host);
    }
}

/// Tracks a memory reference made by a legacy C memory-read API.
struct UnsafeBorrow {
    // Must come before `manager`, so that it's dropped first, since it's
    // borrowed from it.
    _memory: ProcessMemoryRef<'static, u8>,
    _manager: Ref<'static, MemoryManager>,
}

impl UnsafeBorrow {
    /// Creates a raw readable pointer, and saves an instance of `Self` into
    /// `process` for later clean-up.
    ///
    /// # Safety
    ///
    /// The pointer is invalidated when one of the Process memory flush methods is called.
    unsafe fn readable_ptr(
        process: &Process,
        ptr: ForeignArrayPtr<u8>,
    ) -> Result<*const c_void, Errno> {
        let runnable = process.as_runnable().unwrap();
        let manager = runnable.memory_manager.borrow();
        // SAFETY: We ensure that the `memory` is dropped before the `manager`,
        // and `Process` ensures that this whole object is dropped before
        // `MemoryManager` can be moved, freed, etc.
        let manager = unsafe {
            std::mem::transmute::<Ref<'_, MemoryManager>, Ref<'static, MemoryManager>>(manager)
        };
        let memory = manager.memory_ref(ptr)?;
        let memory = unsafe {
            std::mem::transmute::<ProcessMemoryRef<'_, u8>, ProcessMemoryRef<'static, u8>>(memory)
        };
        let vptr = memory.as_ptr() as *mut c_void;
        runnable.unsafe_borrows.borrow_mut().push(Self {
            _manager: manager,
            _memory: memory,
        });
        Ok(vptr)
    }

    /// Creates a raw readable string, and saves an instance of `Self` into
    /// `process` for later clean-up.
    ///
    /// # Safety
    ///
    /// The pointer is invalidated when one of the Process memory flush methods is called.
    unsafe fn readable_string(
        process: &Process,
        ptr: ForeignArrayPtr<c_char>,
    ) -> Result<(*const c_char, libc::size_t), Errno> {
        let runnable = process.as_runnable().unwrap();
        let manager = runnable.memory_manager.borrow();
        // SAFETY: We ensure that the `memory` is dropped before the `manager`,
        // and `Process` ensures that this whole object is dropped before
        // `MemoryManager` can be moved, freed, etc.
        let manager = unsafe {
            std::mem::transmute::<Ref<'_, MemoryManager>, Ref<'static, MemoryManager>>(manager)
        };
        let ptr = ptr.cast_u8();
        let memory = manager.memory_ref_prefix(ptr)?;
        let memory = unsafe {
            std::mem::transmute::<ProcessMemoryRef<'_, u8>, ProcessMemoryRef<'static, u8>>(memory)
        };
        if !memory.contains(&0) {
            return Err(Errno::ENAMETOOLONG);
        }
        assert_eq!(std::mem::size_of::<c_char>(), std::mem::size_of::<u8>());
        let ptr = memory.as_ptr() as *const c_char;
        let len = memory.len();
        runnable.unsafe_borrows.borrow_mut().push(Self {
            _manager: manager,
            _memory: memory,
        });
        Ok((ptr, len))
    }
}

// Safety: Normally the Ref would make this non-Send, since it could end then
// end up trying to manipulate the source RefCell (which is !Sync) from multiple
// threads.  We ensure that these objects never escape Process, which itself is
// non-Sync, ensuring this doesn't happen.
//
// This is admittedly hand-wavy and making some assumptions about the
// implementation of RefCell, but this whole type is temporary scaffolding to
// support legacy C code.
unsafe impl Send for UnsafeBorrow {}

/// Tracks a memory reference made by a legacy C memory-write API.
struct UnsafeBorrowMut {
    // Must come before `manager`, so that it's dropped first, since it's
    // borrowed from it.
    memory: Option<ProcessMemoryRefMut<'static, u8>>,
    _manager: RefMut<'static, MemoryManager>,
}

impl UnsafeBorrowMut {
    /// Creates a raw writable pointer, and saves an instance of `Self` into
    /// `process` for later clean-up. The initial contents of the pointer is unspecified.
    ///
    /// # Safety
    ///
    /// The pointer is invalidated when one of the Process memory flush methods is called.
    unsafe fn writable_ptr(
        process: &Process,
        ptr: ForeignArrayPtr<u8>,
    ) -> Result<*mut c_void, Errno> {
        let runnable = process.as_runnable().unwrap();
        let manager = runnable.memory_manager.borrow_mut();
        // SAFETY: We ensure that the `memory` is dropped before the `manager`,
        // and `Process` ensures that this whole object is dropped before
        // `MemoryManager` can be moved, freed, etc.
        let mut manager = unsafe {
            std::mem::transmute::<RefMut<'_, MemoryManager>, RefMut<'static, MemoryManager>>(
                manager,
            )
        };
        let memory = manager.memory_ref_mut_uninit(ptr)?;
        let mut memory = unsafe {
            std::mem::transmute::<ProcessMemoryRefMut<'_, u8>, ProcessMemoryRefMut<'static, u8>>(
                memory,
            )
        };
        let vptr = memory.as_mut_ptr() as *mut c_void;
        let prev = runnable.unsafe_borrow_mut.borrow_mut().replace(Self {
            _manager: manager,
            memory: Some(memory),
        });
        assert!(prev.is_none());
        Ok(vptr)
    }

    /// Creates a raw mutable pointer, and saves an instance of `Self` into
    /// `process` for later clean-up.
    ///
    /// # Safety
    ///
    /// The pointer is invalidated when one of the Process memory flush methods is called.
    unsafe fn mutable_ptr(
        process: &Process,
        ptr: ForeignArrayPtr<u8>,
    ) -> Result<*mut c_void, Errno> {
        let runnable = process.as_runnable().unwrap();
        let manager = runnable.memory_manager.borrow_mut();
        // SAFETY: We ensure that the `memory` is dropped before the `manager`,
        // and `Process` ensures that this whole object is dropped before
        // `MemoryManager` can be moved, freed, etc.
        let mut manager = unsafe {
            std::mem::transmute::<RefMut<'_, MemoryManager>, RefMut<'static, MemoryManager>>(
                manager,
            )
        };
        let memory = manager.memory_ref_mut(ptr)?;
        let mut memory = unsafe {
            std::mem::transmute::<ProcessMemoryRefMut<'_, u8>, ProcessMemoryRefMut<'static, u8>>(
                memory,
            )
        };
        let vptr = memory.as_mut_ptr() as *mut c_void;
        let prev = runnable.unsafe_borrow_mut.borrow_mut().replace(Self {
            _manager: manager,
            memory: Some(memory),
        });
        assert!(prev.is_none());
        Ok(vptr)
    }

    /// Free this reference, writing back to process memory.
    fn flush(mut self) -> Result<(), Errno> {
        self.memory.take().unwrap().flush()
    }

    /// Free this reference without writing back to process memory.
    fn noflush(mut self) {
        self.memory.take().unwrap().noflush()
    }
}

// Safety: Normally the RefMut would make this non-Send, since it could end then
// end up trying to manipulate the source RefCell (which is !Sync) from multiple
// threads.  We ensure that these objects never escape Process, which itself is
// non-Sync, ensuring this doesn't happen.
//
// This is admittedly hand-wavy and making some assumptions about the implementation of
// RefCell, but this whole type is temporary scaffolding to support legacy C code.
unsafe impl Send for UnsafeBorrowMut {}

fn make_name(host: &Host, exe_name: &str, id: ProcessId) -> CString {
    CString::new(format!(
        "{host_name}.{exe_name}.{id}",
        host_name = host.name(),
        exe_name = exe_name,
        id = u32::from(id)
    ))
    .unwrap()
}

mod export {
    use std::os::raw::c_void;

    use libc::size_t;
    use log::trace;
    use shadow_shim_helper_rs::notnull::*;
    use shadow_shim_helper_rs::shim_shmem::export::ShimShmemProcess;
    use shadow_shim_helper_rs::syscall_types::UntypedForeignPtr;

    use super::*;
    use crate::utility::HostTreePointer;

    /// Copy `n` bytes from `src` to `dst`. Returns 0 on success or -EFAULT if any of
    /// the specified range couldn't be accessed. Always succeeds with n==0.
    #[no_mangle]
    pub extern "C-unwind" fn process_readPtr(
        proc: *const Process,
        dst: *mut c_void,
        src: UntypedForeignPtr,
        n: usize,
    ) -> i32 {
        let proc = unsafe { proc.as_ref().unwrap() };
        let src = ForeignArrayPtr::new(src.cast::<u8>(), n);
        let dst = unsafe { std::slice::from_raw_parts_mut(notnull_mut_debug(dst) as *mut u8, n) };

        match proc.memory_borrow().copy_from_ptr(dst, src) {
            Ok(_) => 0,
            Err(e) => {
                trace!("Couldn't read {:?} into {:?}: {:?}", src, dst, e);
                e.to_negated_i32()
            }
        }
    }

    /// Copy `n` bytes from `src` to `dst`. Returns 0 on success or -EFAULT if any of
    /// the specified range couldn't be accessed. The write is flushed immediately.
    #[no_mangle]
    pub unsafe extern "C-unwind" fn process_writePtr(
        proc: *const Process,
        dst: UntypedForeignPtr,
        src: *const c_void,
        n: usize,
    ) -> i32 {
        let proc = unsafe { proc.as_ref().unwrap() };
        let dst = ForeignArrayPtr::new(dst.cast::<u8>(), n);
        let src = unsafe { std::slice::from_raw_parts(notnull_debug(src) as *const u8, n) };
        match proc.memory_borrow_mut().copy_to_ptr(dst, src) {
            Ok(_) => 0,
            Err(e) => {
                trace!("Couldn't write {:?} into {:?}: {:?}", src, dst, e);
                e.to_negated_i32()
            }
        }
    }

    /// Make the data at plugin_src available in shadow's address space.
    ///
    /// The returned pointer is invalidated when one of the process memory flush
    /// methods is called; typically after a syscall has completed.
    #[no_mangle]
    pub unsafe extern "C-unwind" fn process_getReadablePtr(
        proc: *const Process,
        plugin_src: UntypedForeignPtr,
        n: usize,
    ) -> *const c_void {
        let proc = unsafe { proc.as_ref().unwrap() };
        let plugin_src = ForeignArrayPtr::new(plugin_src.cast::<u8>(), n);
        unsafe { UnsafeBorrow::readable_ptr(proc, plugin_src).unwrap_or(std::ptr::null()) }
    }

    /// Returns a writable pointer corresponding to the named region. The
    /// initial contents of the returned memory are unspecified.
    ///
    /// The returned pointer is invalidated when one of the process memory flush
    /// methods is called; typically after a syscall has completed.
    ///
    /// CAUTION: if the unspecified contents aren't overwritten, and the pointer
    /// isn't explicitly freed via `process_freePtrsWithoutFlushing`, those
    /// unspecified contents may be written back into process memory.
    #[no_mangle]
    pub unsafe extern "C-unwind" fn process_getWriteablePtr(
        proc: *const Process,
        plugin_src: UntypedForeignPtr,
        n: usize,
    ) -> *mut c_void {
        let proc = unsafe { proc.as_ref().unwrap() };
        let plugin_src = ForeignArrayPtr::new(plugin_src.cast::<u8>(), n);
        unsafe { UnsafeBorrowMut::writable_ptr(proc, plugin_src).unwrap_or(std::ptr::null_mut()) }
    }

    /// Returns a writeable pointer corresponding to the specified src. Use when
    /// the data at the given address needs to be both read and written.
    ///
    /// The returned pointer is invalidated when one of the process memory flush
    /// methods is called; typically after a syscall has completed.
    #[no_mangle]
    pub unsafe extern "C-unwind" fn process_getMutablePtr(
        proc: *const Process,
        plugin_src: UntypedForeignPtr,
        n: usize,
    ) -> *mut c_void {
        let proc = unsafe { proc.as_ref().unwrap() };
        let plugin_src = ForeignArrayPtr::new(plugin_src.cast::<u8>(), n);
        unsafe { UnsafeBorrowMut::mutable_ptr(proc, plugin_src).unwrap_or(std::ptr::null_mut()) }
    }

    /// Reads up to `n` bytes into `str`.
    ///
    /// Returns:
    /// strlen(str) on success.
    /// -ENAMETOOLONG if there was no NULL byte in the first `n` characters.
    /// -EFAULT if the string extends beyond the accessible address space.
    #[no_mangle]
    pub unsafe extern "C-unwind" fn process_readString(
        proc: *const Process,
        strbuf: *mut libc::c_char,
        ptr: UntypedForeignPtr,
        maxlen: libc::size_t,
    ) -> libc::ssize_t {
        let proc = unsafe { proc.as_ref().unwrap() };
        let memory_manager = proc.memory_borrow();
        let buf =
            unsafe { std::slice::from_raw_parts_mut(notnull_mut_debug(strbuf) as *mut u8, maxlen) };
        let cstr = match memory_manager
            .copy_str_from_ptr(buf, ForeignArrayPtr::new(ptr.cast::<u8>(), maxlen))
        {
            Ok(cstr) => cstr,
            Err(e) => return e.to_negated_i32() as isize,
        };
        cstr.to_bytes().len().try_into().unwrap()
    }

    /// Reads up to `n` bytes into `str`.
    ///
    /// Returns:
    /// strlen(str) on success.
    /// -ENAMETOOLONG if there was no NULL byte in the first `n` characters.
    /// -EFAULT if the string extends beyond the accessible address space.
    #[no_mangle]
    pub unsafe extern "C-unwind" fn process_getReadableString(
        proc: *const Process,
        plugin_src: UntypedForeignPtr,
        n: usize,
        out_str: *mut *const c_char,
        out_strlen: *mut size_t,
    ) -> i32 {
        let proc = unsafe { proc.as_ref().unwrap() };
        let ptr = ForeignArrayPtr::new(plugin_src.cast::<c_char>(), n);
        match unsafe { UnsafeBorrow::readable_string(proc, ptr) } {
            Ok((str, strlen)) => {
                assert!(!out_str.is_null());
                unsafe { out_str.write(str) };
                if !out_strlen.is_null() {
                    unsafe { out_strlen.write(strlen) };
                }
                0
            }
            Err(e) => e.to_negated_i32(),
        }
    }

    /// Returns the processID that was assigned to us in process_new
    #[no_mangle]
    pub unsafe extern "C-unwind" fn process_getProcessID(proc: *const Process) -> libc::pid_t {
        let proc = unsafe { proc.as_ref().unwrap() };
        proc.id().into()
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn process_getName(proc: *const Process) -> *const c_char {
        let proc = unsafe { proc.as_ref().unwrap() };
        proc.common().name.as_ptr()
    }

    /// Safety:
    ///
    /// The returned pointer is invalidated when the host shmem lock is released, e.g. via
    /// Host::unlock_shmem.
    #[no_mangle]
    pub unsafe extern "C-unwind" fn process_getSharedMem(
        proc: *const Process,
    ) -> *const ShimShmemProcess {
        let proc = unsafe { proc.as_ref().unwrap() };
        std::ptr::from_ref(proc.as_runnable().unwrap().shim_shared_mem_block.deref())
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn process_getWorkingDir(proc: *const Process) -> *const c_char {
        let proc = unsafe { proc.as_ref().unwrap() };
        proc.common().working_dir.as_ptr()
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn process_straceLoggingMode(
        proc: *const Process,
    ) -> StraceFmtMode {
        let proc = unsafe { proc.as_ref().unwrap() };
        proc.strace_logging_options().into()
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn process_getNativePid(proc: *const Process) -> libc::pid_t {
        let proc = unsafe { proc.as_ref().unwrap() };
        proc.native_pid().as_raw_nonzero().get()
    }

    /// Flushes and invalidates all previously returned readable/writable plugin
    /// pointers, as if returning control to the plugin. This can be useful in
    /// conjunction with `thread_nativeSyscall` operations that touch memory, or
    /// to gracefully handle failed writes.
    ///
    /// Returns 0 on success or a negative errno on failure.
    #[no_mangle]
    pub unsafe extern "C-unwind" fn process_flushPtrs(proc: *const Process) -> i32 {
        let proc = unsafe { proc.as_ref().unwrap() };
        match proc.free_unsafe_borrows_flush() {
            Ok(_) => 0,
            Err(e) => e.to_negated_i32(),
        }
    }

    /// Frees all readable/writable foreign pointers. Unlike process_flushPtrs, any
    /// previously returned writable pointer is *not* written back. Useful
    /// if an uninitialized writable pointer was obtained via `process_getWriteablePtr`,
    /// and we end up not wanting to write anything after all (in particular, don't
    /// write back whatever garbage data was in the uninialized bueffer).
    #[no_mangle]
    pub unsafe extern "C-unwind" fn process_freePtrsWithoutFlushing(proc: *const Process) {
        let proc = unsafe { proc.as_ref().unwrap() };
        proc.free_unsafe_borrows_noflush();
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn process_getThread(
        proc: *const Process,
        tid: libc::pid_t,
    ) -> *const Thread {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let tid = ThreadId::try_from(tid).unwrap();
            let Some(thread) = proc.thread_borrow(tid) else {
                return std::ptr::null();
            };
            let thread = thread.borrow(host.root());
            &*thread
        })
        .unwrap()
    }

    /// Returns a pointer to an arbitrary live thread in the process.
    #[no_mangle]
    pub unsafe extern "C-unwind" fn process_firstLiveThread(proc: *const Process) -> *const Thread {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let Some(thread) = proc.first_live_thread_borrow(host.root()) else {
                return std::ptr::null();
            };
            let thread = thread.borrow(host.root());
            &*thread
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn process_isRunning(proc: *const Process) -> bool {
        let proc = unsafe { proc.as_ref().unwrap() };
        proc.is_running()
    }

    // FIXME: still needed? Time is now updated more granularly in the Thread code
    // when xferring control to/from shim.
    #[no_mangle]
    pub unsafe extern "C-unwind" fn process_setSharedTime() {
        Worker::with_active_host(Process::set_shared_time).unwrap();
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn process_getPhysicalAddress(
        proc: *const Process,
        vptr: UntypedForeignPtr,
    ) -> ManagedPhysicalMemoryAddr {
        let proc = unsafe { proc.as_ref().unwrap() };
        proc.physical_address(vptr)
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn process_addChildEventListener(
        host: *const Host,
        process: *const Process,
        listener: *mut cshadow::StatusListener,
    ) {
        let host = unsafe { host.as_ref().unwrap() };
        let process = unsafe { process.as_ref().unwrap() };
        let listener = HostTreePointer::new_for_host(host.id(), listener);
        process
            .borrow_as_runnable()
            .unwrap()
            .child_process_event_listeners
            .borrow_mut()
            .add_legacy_listener(listener)
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn process_removeChildEventListener(
        _host: *const Host,
        process: *const Process,
        listener: *mut cshadow::StatusListener,
    ) {
        let process = unsafe { process.as_ref().unwrap() };
        process
            .borrow_as_runnable()
            .unwrap()
            .child_process_event_listeners
            .borrow_mut()
            .remove_legacy_listener(listener)
    }
}
