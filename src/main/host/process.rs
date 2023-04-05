use std::cell::{Cell, Ref, RefCell, RefMut};
use std::collections::BTreeMap;
use std::ffi::{c_char, c_void, CStr, CString};
use std::fmt::Write;
use std::num::TryFromIntError;
use std::ops::{Deref, DerefMut};
use std::os::fd::{AsRawFd, RawFd};
use std::os::unix::ffi::OsStrExt;
use std::os::unix::io::FromRawFd;
use std::path::{Path, PathBuf};
use std::sync::atomic::Ordering;
#[cfg(feature = "perf_timers")]
use std::time::Duration;

use log::{debug, error, info, trace, warn};
use nix::errno::Errno;
use nix::fcntl::OFlag;
use nix::sys::signal as nixsignal;
use nix::sys::signal::Signal;
use nix::sys::stat::Mode;
use nix::unistd::Pid;
use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::rootedcell::rc::{RootedRc, RootedRcWeak};
use shadow_shim_helper_rs::rootedcell::refcell::RootedRefCell;
use shadow_shim_helper_rs::rootedcell::Root;
use shadow_shim_helper_rs::shim_shmem::ProcessShmem;
use shadow_shim_helper_rs::signals::{defaultaction, ShdKernelDefaultAction};
use shadow_shim_helper_rs::simulation_time::SimulationTime;
use shadow_shim_helper_rs::syscall_types::{ForeignPtr, ManagedPhysicalMemoryAddr};
use shadow_shim_helper_rs::HostId;
use shadow_shmem::allocator::ShMemBlock;

use super::descriptor::descriptor_table::{DescriptorHandle, DescriptorTable};
use super::host::Host;
use super::memory_manager::{MemoryManager, ProcessMemoryRef, ProcessMemoryRefMut};
use super::syscall::formatter::StraceFmtMode;
use super::syscall_types::ForeignArrayPtr;
use super::thread::{Thread, ThreadId};
use super::timer::Timer;
use crate::core::work::task::TaskRef;
use crate::core::worker::Worker;
use crate::cshadow;
use crate::host::context::ProcessContext;
use crate::host::descriptor::{CompatFile, Descriptor};
use crate::host::syscall::formatter::FmtOptions;
use crate::utility;
use crate::utility::callback_queue::CallbackQueue;
#[cfg(feature = "perf_timers")]
use crate::utility::perf_timer::PerfTimer;

/// Virtual pid of a shadow process
#[derive(Debug, PartialEq, Eq, Hash, Copy, Clone, Ord, PartialOrd)]
pub struct ProcessId(u32);

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

#[derive(Debug)]
struct StraceLogging {
    file: RefCell<std::fs::File>,
    options: FmtOptions,
}

/// Used for C interop.
pub type ProcessRefCell = RootedRefCell<Process>;

pub struct Process {
    // Occasionally we need to get to the enclosing RootedRc. Mostly for C compatibility.
    weak_rc: Option<RootedRcWeak<RootedRefCell<Process>>>,

    id: ProcessId,
    host_id: HostId,

    // unique id of the program that this process should run
    name: CString,

    // The basename (directory + file stem) for files that should be stored in the data directory.
    file_basename: PathBuf,

    // the name and path to the executable that we will exec
    plugin_name: CString,
    plugin_path: CString,

    // absolute path to the process's working directory
    working_dir: CString,

    // environment variables to pass to exec
    envv: Vec<CString>,

    // argument strings to pass to exec
    argv: Vec<CString>,

    // Shared memory allocation for shared state with shim.
    shim_shared_mem_block: ShMemBlock<'static, ProcessShmem>,

    // process boot and shutdown variables
    start_time: EmulatedTime,
    stop_time: Option<EmulatedTime>,

    strace_logging: Option<StraceLogging>,

    // Pause shadow after launching this process, to give the user time to attach gdb
    pause_for_debugging: bool,

    // "dumpable" state, as manipulated via the prctl operations PR_SET_DUMPABLE
    // and PR_GET_DUMPABLE.
    dumpable: Cell<u32>,

    // When true, threads are no longer runnable and should just be cleaned up.
    is_exiting: Cell<bool>,

    return_code: Cell<Option<i32>>,
    killed_by_shadow: Cell<bool>,

    native_pid: Cell<Option<Pid>>,

    // timer that tracks the amount of CPU time we spend on plugin execution and processing
    #[cfg(feature = "perf_timers")]
    cpu_delay_timer: RefCell<PerfTimer>,
    #[cfg(feature = "perf_timers")]
    total_run_time: Cell<Duration>,

    desc_table: RefCell<DescriptorTable>,
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

    // SAFETY: Must come after `unsafe_borrows` and `unsafe_borrow_mut`.
    // Boxed to avoid invalidating those if Self is moved.
    memory_manager: Box<RefCell<Option<MemoryManager>>>,
}

fn itimer_real_expiration(host: &Host, pid: ProcessId) {
    let Some(process) = host.process_borrow(pid) else {
        debug!("Process {:?} no longer exists", pid);
        return;
    };
    let process = process.borrow(host.root());
    let timer = process.itimer_real.borrow();
    let mut siginfo: libc::siginfo_t = unsafe { std::mem::zeroed() };
    // The siginfo_t structure only has an i32. Presumably we want to just truncate in
    // case of overflow.
    let expiration_count = timer.expiration_count() as i32;
    unsafe { cshadow::process_initSiginfoForAlarm(&mut siginfo, expiration_count) };
    process.signal(host, None, &siginfo);
}

impl Process {
    /// The returned object shouldn't be moved out of its enclosing RootedRc.
    /// Doing so breaks an internal weak reference to that RootedRc, which we
    /// use to access the enclosing RootedRc and RootedRefCell when interacting
    /// with C APIs.
    pub fn new(
        host: &Host,
        process_id: ProcessId,
        start_time: SimulationTime,
        stop_time: Option<SimulationTime>,
        plugin_name: &CStr,
        plugin_path: &CStr,
        envv: Vec<CString>,
        argv: Vec<CString>,
        pause_for_debugging: bool,
        use_legacy_working_dir: bool,
        strace_logging_options: Option<FmtOptions>,
    ) -> RootedRc<RootedRefCell<Self>> {
        debug_assert!(stop_time.is_none() || stop_time.unwrap() > start_time);

        let desc_table = RefCell::new(DescriptorTable::new());
        let memory_manager = Box::new(RefCell::new(None));
        let itimer_real = RefCell::new(Timer::new(move |host| {
            itimer_real_expiration(host, process_id)
        }));
        let plugin_name = plugin_name.to_owned();
        let plugin_path = plugin_path.to_owned();

        let name = CString::new(format!(
            "{host_name}.{exe_name}.{id}",
            host_name = host.name(),
            exe_name = plugin_name.to_str().unwrap(),
            id = u32::from(process_id)
        ))
        .unwrap();

        let mut file_basename = PathBuf::new();
        file_basename.push(host.data_dir_path());
        file_basename.push(format!(
            "{exe_name}.{id}",
            exe_name = plugin_name.to_str().unwrap(),
            id = u32::from(process_id)
        ));

        let strace_logging = strace_logging_options.map(|options| {
            let oflag = { OFlag::O_CREAT | OFlag::O_TRUNC | OFlag::O_WRONLY | OFlag::O_CLOEXEC };
            let mode = { Mode::S_IRUSR | Mode::S_IWUSR | Mode::S_IRGRP | Mode::S_IROTH };
            let filename = Self::static_output_file_name(&file_basename, "strace");
            let fd = nix::fcntl::open(&filename, oflag, mode).unwrap();

            StraceLogging {
                file: RefCell::new(unsafe { std::fs::File::from_raw_fd(fd) }),
                options,
            }
        });

        let shim_shared_mem = ProcessShmem::new(
            &host.shim_shmem_lock_borrow().unwrap().root,
            host.shim_shmem().serialize(),
            host.id(),
            strace_logging.as_ref().map(|x| x.file.borrow().as_raw_fd()),
        );
        let shim_shared_mem_block =
            shadow_shmem::allocator::Allocator::global().alloc(shim_shared_mem);

        let working_dir = utility::pathbuf_to_nul_term_cstring(if use_legacy_working_dir {
            nix::unistd::getcwd().unwrap()
        } else {
            std::fs::canonicalize(host.data_dir_path()).unwrap()
        });

        #[cfg(feature = "perf_timers")]
        let cpu_delay_timer = {
            let mut t = PerfTimer::new();
            t.stop();
            RefCell::new(t)
        };

        let process = RootedRc::new(
            host.root(),
            RootedRefCell::new(
                host.root(),
                Self {
                    id: process_id,
                    host_id: host.id(),
                    // We set this below.
                    weak_rc: None,
                    argv,
                    envv,
                    working_dir,
                    shim_shared_mem_block,
                    memory_manager,
                    desc_table,
                    itimer_real,
                    start_time: EmulatedTime::SIMULATION_START + start_time,
                    stop_time: stop_time.map(|t| EmulatedTime::SIMULATION_START + t),
                    name,
                    file_basename,
                    plugin_name,
                    plugin_path,
                    strace_logging,
                    pause_for_debugging,
                    dumpable: Cell::new(cshadow::SUID_DUMP_USER),
                    is_exiting: Cell::new(false),
                    return_code: Cell::new(None),
                    killed_by_shadow: Cell::new(false),
                    #[cfg(feature = "perf_timers")]
                    cpu_delay_timer,
                    #[cfg(feature = "perf_timers")]
                    total_run_time: Cell::new(Duration::ZERO),
                    native_pid: Cell::new(None),
                    unsafe_borrow_mut: RefCell::new(None),
                    unsafe_borrows: RefCell::new(Vec::new()),
                    threads: RefCell::new(BTreeMap::new()),
                },
            ),
        );

        let weak_rc = RootedRc::downgrade(&process, host.root());
        process.borrow_mut(host.root()).weak_rc = Some(weak_rc);

        process
    }

    /// # Safety
    ///
    /// The returned pointer is invalidated when all clones of the original
    /// `RootedRc` returned from `Self::new` are destroyed.
    pub unsafe fn cprocess(&self, host: &Host) -> *const ProcessRefCell {
        let Some(rc) = self.weak_rc.as_ref().unwrap().upgrade(host.root()) else {
            panic!("Couldn't get outer Arc");
        };
        let process: &ProcessRefCell = &rc;
        let rv = process as *const _;
        rc.safely_drop(host.root());
        rv
    }

    pub fn weak_rc(&self) -> &RootedRcWeak<RootedRefCell<Self>> {
        self.weak_rc.as_ref().unwrap()
    }

    pub fn id(&self) -> ProcessId {
        self.id
    }

    pub fn host_id(&self) -> HostId {
        self.host_id
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

    pub fn schedule(&self, host: &Host) {
        let id = self.id();
        match self.stop_time {
            Some(t) if self.start_time >= t => {
                info!(
                    "Not scheduling process with start:{:?} after stop:{:?}",
                    self.start_time, t
                );
                return;
            }
            _ => (),
        };
        let task = TaskRef::new(move |host| {
            let process = host.process_borrow(id).unwrap();
            let process = process.borrow(host.root());
            process.create_and_exec_thread_group_leader(host);
            process.resume(host, process.thread_group_leader_id());
        });
        host.schedule_task_at_emulated_time(task, self.start_time);

        if let Some(stop_time) = self.stop_time {
            let task = TaskRef::new(move |host| {
                let process = host.process_borrow(id).unwrap();
                let process = process.borrow(host.root());
                process.stop(host);
            });
            host.schedule_task_at_emulated_time(task, stop_time);
        }
    }

    fn thread_group_leader_id(&self) -> ThreadId {
        // tid of the thread group leader is equal to the pid.
        ThreadId::from(self.id())
    }

    // Creates the thread group leader. After return, the thread group leader
    // will be in `self.threads` and is ready to be run with `self.resume`.
    fn create_and_exec_thread_group_leader(&self, host: &Host) {
        assert!(!self.is_running());

        self.open_stdio_file_helper(
            libc::STDIN_FILENO.try_into().unwrap(),
            "/dev/null".into(),
            OFlag::O_RDONLY,
        );

        let name = self.output_file_name("stdout");
        self.open_stdio_file_helper(
            libc::STDOUT_FILENO.try_into().unwrap(),
            name,
            OFlag::O_WRONLY,
        );

        let name = self.output_file_name("stderr");
        self.open_stdio_file_helper(
            libc::STDERR_FILENO.try_into().unwrap(),
            name,
            OFlag::O_WRONLY,
        );

        // Create the main thread and add it to our thread list.
        let tid = self.thread_group_leader_id();
        self.threads
            .borrow_mut()
            .insert(tid, Thread::new(host, self, tid));

        let main_thread = self.thread_borrow(tid).unwrap();
        let main_thread = main_thread.borrow(host.root());

        info!("starting process '{}'", self.name());
        Worker::set_active_process(self);
        Worker::set_active_thread(&main_thread);

        #[cfg(feature = "perf_timers")]
        self.start_cpu_delay_timer();

        Process::set_shared_time(host);

        let shimlog_path =
            CString::new(self.output_file_name("shimlog").as_os_str().as_bytes()).unwrap();

        main_thread.run(
            &ProcessContext::new(host, self),
            &self.plugin_path,
            self.argv.clone(),
            self.envv.clone(),
            &self.working_dir,
            self.strace_logging
                .as_ref()
                .map(|s| s.file.borrow().as_raw_fd()),
            &shimlog_path,
        );

        let native_pid = main_thread.native_pid();
        self.native_pid.set(Some(native_pid));
        *self.memory_manager.borrow_mut() = Some(unsafe { MemoryManager::new(native_pid) });

        #[cfg(feature = "perf_timers")]
        {
            let elapsed = self.stop_cpu_delay_timer(host);
            info!("process '{}' started in {:?}", self.name(), elapsed);
        }
        #[cfg(not(feature = "perf_timers"))]
        info!("process '{}' started", self.name());

        Worker::clear_active_thread();
        Worker::clear_active_process();

        if self.pause_for_debugging {
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
              \n** '{name}' (pid {native_pid}).\
              \n** If running Shadow under Bash, resume Shadow by pressing Ctrl-Z to background\
              \n** this task, and then typing \"fg\".\
              \n** If running GDB, resume Shadow by typing \"signal SIGCONT\".",
                name = self.name(),
                native_pid = i32::from(native_pid)
            );
            eprintln!("{}", msg);

            nix::sys::signal::raise(Signal::SIGTSTP).unwrap();
        }
    }

    /// Resume execution of `tid` (if it exists).
    pub fn resume(&self, host: &Host, tid: ThreadId) {
        trace!("Continuing thread {} in process {}", tid, self.id());

        if !self.is_running() {
            return;
        }

        let threadrc = {
            let threads = self.threads.borrow();
            let Some(thread) = threads.get(&tid) else {
                debug!("Thread {} no longer exists", tid);
                return;
            };
            // Clone the thread reference, so that we don't hold a dynamically
            // borrowed reference to the thread list while running the thread.
            thread.clone(host.root())
        };
        let thread = threadrc.borrow(host.root());

        Worker::set_active_process(self);
        Worker::set_active_thread(&thread);

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
        thread.resume(&ctx);
        drop(thread);
        threadrc.safely_drop(host.root());

        #[cfg(feature = "perf_timers")]
        {
            let delay = self.stop_cpu_delay_timer(host);
            info!("process '{}' ran for {:?}", self.name(), delay);
        }
        #[cfg(not(feature = "perf_timers"))]
        debug!("process '{}' done continuing", self.name());

        if self.is_exiting.get() {
            self.handle_process_exit(host);
        } else {
            self.check_thread(host, tid);
        }

        Worker::clear_active_thread();
        Worker::clear_active_process();
    }

    pub fn stop(&self, host: &Host) {
        info!("terminating process {}", self.name());

        Worker::set_active_process(self);

        #[cfg(feature = "perf_timers")]
        self.start_cpu_delay_timer();

        self.terminate(host);

        #[cfg(feature = "perf_timers")]
        {
            let delay = self.stop_cpu_delay_timer(host);
            info!("process '{}' stopped in {:?}", self.name(), delay);
        }
        #[cfg(not(feature = "perf_timers"))]
        info!("process '{}' stopped", self.name());

        Worker::clear_active_process();

        self.check(host);
    }

    /// Send the signal described in `siginfo` to `process`. `current_thread`
    /// should be set if there is one (e.g. if this is being called from a syscall
    /// handler), and `None` otherwise (e.g. when called from a timer expiration event).
    ///
    /// An event will be scheduled to deliver the signal unless `current_thread`
    /// is set, and belongs to the process `self`, and doesn't have the signal
    /// blocked.  In that the signal will be processed synchronously when
    /// returning from the current syscall.
    pub fn signal(&self, host: &Host, current_thread: Option<&Thread>, siginfo: &libc::siginfo_t) {
        if siginfo.si_signo == 0 {
            return;
        }
        let signal = Signal::try_from(siginfo.si_signo).unwrap();

        {
            let host_shmem = host.shim_shmem_lock_borrow().unwrap();
            let mut process_shmem_protected = self
                .shim_shared_mem_block
                .protected
                .borrow_mut(&host_shmem.root);
            // SAFETY: We don't try to call any of the function pointers.
            let action = unsafe { process_shmem_protected.signal_action(signal) };
            let handler = action.handler();
            if handler == nixsignal::SigHandler::SigIgn
                || (handler == nixsignal::SigHandler::SigDfl
                    && defaultaction(signal) == ShdKernelDefaultAction::IGN)
            {
                return;
            }

            if process_shmem_protected.pending_signals.has(signal) {
                // Signal is already pending. From signal(7):In the case where a
                // standard signal is already pending, the siginfo_t structure (see
                // sigaction(2)) associated with that signal is not overwritten on
                // arrival of subsequent instances of the same signal.
                return;
            }
            process_shmem_protected.pending_signals.add(signal);
            process_shmem_protected.set_pending_standard_siginfo(signal, siginfo);
        }

        if let Some(thread) = current_thread {
            if thread.process_id() == self.id() {
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

    fn interrupt_with_signal(&self, host: &Host, signal: nixsignal::Signal) {
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

    fn open_stdio_file_helper(
        &self,
        fd: DescriptorHandle,
        path: PathBuf,
        access_mode: OFlag,
    ) -> *mut cshadow::RegularFile {
        let stdfile = unsafe { cshadow::regularfile_new() };
        let cwd = nix::unistd::getcwd().unwrap();
        let path = utility::pathbuf_to_nul_term_cstring(path);
        let cwd = utility::pathbuf_to_nul_term_cstring(cwd);
        let errorcode = unsafe {
            cshadow::regularfile_open(
                stdfile,
                path.as_ptr(),
                (access_mode | OFlag::O_CREAT | OFlag::O_TRUNC).bits(),
                (Mode::S_IRUSR | Mode::S_IWUSR | Mode::S_IRGRP | Mode::S_IROTH).bits(),
                cwd.as_ptr(),
            )
        };
        if errorcode != 0 {
            panic!(
                "Opening {}: {:?}",
                path.to_str().unwrap(),
                nix::errno::Errno::from_i32(-errorcode)
            );
        }
        let desc = unsafe {
            Descriptor::from_legacy_file(stdfile as *mut cshadow::LegacyFile, OFlag::empty())
        };
        let prev = self
            .descriptor_table_borrow_mut()
            .register_descriptor_with_fd(desc, fd);
        assert!(prev.is_none());
        trace!(
            "Successfully opened fd {} at {}",
            fd,
            path.to_str().unwrap()
        );
        stdfile
    }

    fn output_file_name(&self, extension: &str) -> PathBuf {
        Self::static_output_file_name(&self.file_basename, extension)
    }

    // Needed during early init, before `Self` is created.
    fn static_output_file_name(file_basename: &Path, extension: &str) -> PathBuf {
        let mut path = file_basename.to_owned().into_os_string();
        path.push(".");
        path.push(extension);
        path.into()
    }

    pub fn name(&self) -> &str {
        self.name.to_str().unwrap()
    }

    pub fn plugin_name(&self) -> &str {
        self.plugin_name.to_str().unwrap()
    }

    pub fn plugin_path(&self) -> &str {
        self.plugin_path.to_str().unwrap()
    }

    #[track_caller]
    pub fn memory_borrow_mut(&self) -> impl Deref<Target = MemoryManager> + DerefMut + '_ {
        RefMut::map(self.memory_manager.borrow_mut(), |mm| mm.as_mut().unwrap())
    }

    #[track_caller]
    pub fn memory_borrow(&self) -> impl Deref<Target = MemoryManager> + '_ {
        Ref::map(self.memory_manager.borrow(), |mm| mm.as_ref().unwrap())
    }

    pub fn strace_logging_options(&self) -> Option<FmtOptions> {
        self.strace_logging.as_ref().map(|x| x.options)
    }

    /// If strace logging is disabled, this function will do nothing and return `None`.
    pub fn with_strace_file<T>(&self, f: impl FnOnce(&mut std::fs::File) -> T) -> Option<T> {
        let Some(ref strace_logging) = self.strace_logging else {
            return None;
        };

        let mut file = strace_logging.file.borrow_mut();
        Some(f(&mut file))
    }

    #[track_caller]
    pub fn descriptor_table_borrow(&self) -> impl Deref<Target = DescriptorTable> + '_ {
        self.desc_table.borrow()
    }

    #[track_caller]
    pub fn descriptor_table_borrow_mut(
        &self,
    ) -> impl Deref<Target = DescriptorTable> + DerefMut + '_ {
        self.desc_table.borrow_mut()
    }

    pub fn native_pid(&self) -> Option<Pid> {
        self.native_pid.get()
    }

    #[track_caller]
    pub fn realtime_timer_borrow(&self) -> impl Deref<Target = Timer> + '_ {
        self.itimer_real.borrow()
    }

    #[track_caller]
    pub fn realtime_timer_borrow_mut(&self) -> impl Deref<Target = Timer> + DerefMut + '_ {
        self.itimer_real.borrow_mut()
    }

    /// Returns a dynamically borrowed reference to the first live thread.
    /// This is meant primarily for the MemoryManager.
    #[track_caller]
    pub fn first_live_thread_borrow(
        &self,
        root: &Root,
    ) -> Option<impl Deref<Target = RootedRc<RootedRefCell<Thread>>> + '_> {
        Ref::filter_map(self.threads.borrow(), |threads| {
            let thread = threads.values().next().map(|thread| {
                // There shouldn't be any non-running threads in the table.
                assert!(thread.borrow(root).is_running());
                thread
            });
            thread
        })
        .ok()
    }

    pub fn thread_borrow(
        &self,
        virtual_tid: ThreadId,
    ) -> Option<impl Deref<Target = RootedRc<RootedRefCell<Thread>>> + '_> {
        Ref::filter_map(self.threads.borrow(), |threads| threads.get(&virtual_tid)).ok()
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

    pub fn physical_address(&self, vptr: ForeignPtr<()>) -> ManagedPhysicalMemoryAddr {
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

    /// Call after a thread has exited. Removes the thread and does corresponding cleanup and notifications.
    fn reap_thread(&self, host: &Host, tid: ThreadId) {
        let threadrc = self.threads.borrow_mut().remove(&tid).unwrap();
        let thread = threadrc.borrow(host.root());

        assert!(!thread.is_running());

        // If the `clear_child_tid` attribute on the thread is set, and there are
        // any other threads left alive in the process, perform a futex wake on
        // that address. This mechanism is typically used in `pthread_join` etc.
        // See `set_tid_address(2)`.
        let clear_child_tid_pvp = thread.get_tid_address();
        if !clear_child_tid_pvp.is_null()
            && self.threads.borrow().len() > 0
            && !self.is_exiting.get()
        {
            self.memory_borrow_mut()
                .write(clear_child_tid_pvp, &0)
                .unwrap();

            // Wake the corresponding futex.
            let mut futexes = host.futextable_borrow_mut();
            let futex = unsafe {
                cshadow::futextable_get(
                    &mut *futexes,
                    self.physical_address(clear_child_tid_pvp.cast::<()>()),
                )
            };
            if !futex.is_null() {
                unsafe { cshadow::futex_wake(futex, 1) };
            }
        }

        drop(thread);
        threadrc.safely_drop(host.root());
    }

    fn has_started(&self) -> bool {
        self.native_pid.get().is_some()
    }

    pub fn is_running(&self) -> bool {
        !self.is_exiting.get() && self.threads.borrow().len() > 0
    }

    /// In some cases a running thread processes an action that will bring down the
    /// entire process. Calling this tells the Process to clean up other threads
    /// without trying to run them again, since otherwise the OS may kill the other
    /// thread tasks while we're in the middle of trying to execute them, which can
    /// be difficult to recover from cleanly.
    pub fn mark_as_exiting(&self) {
        self.is_exiting.set(true);
        trace!("Process {:?} marked as exiting", self.id());
    }

    fn handle_process_exit(&self, host: &Host) {
        loop {
            let (tid, threadrc) = {
                let threads = self.threads.borrow();
                let Some((tid, thread)) = threads.iter().next() else {
                    break;
                };
                // Conservatively leaving in the thread list, and cloning
                // the reference so that we don't hold a borrow over the list.
                (*tid, thread.clone(host.root()))
            };
            threadrc.borrow(host.root()).handle_process_exit();
            self.reap_thread(host, tid);
            threadrc.safely_drop(host.root());
        }
        self.check(host);
    }

    fn terminate(&self, host: &Host) {
        let Some(native_pid) = self.native_pid() else {
            trace!("Never started");
            return;
        };

        if !self.is_running() {
            trace!("Already dead");
            assert!(self.return_code.get().is_some());
        }

        trace!("Terminating");
        self.killed_by_shadow.set(true);
        if let Err(err) = nix::sys::signal::kill(native_pid, Signal::SIGKILL) {
            warn!("kill: {:?}", err);
        }

        self.mark_as_exiting();
        self.handle_process_exit(host);
    }

    fn get_and_log_return_code(&self) {
        if self.return_code.get().is_some() {
            return;
        }

        let Some(native_pid) = self.native_pid() else {
            error!("Process {name} with a start time of {start_time:?} did not start",
            name=self.name(), start_time=(self.start_time - EmulatedTime::SIMULATION_START));
            return;
        };

        use nix::sys::wait::WaitStatus;
        let return_code = match nix::sys::wait::waitpid(native_pid, None) {
            Ok(WaitStatus::Exited(_pid, code)) => code,
            Ok(WaitStatus::Signaled(_pid, signal, _core_dump)) => {
                utility::return_code_for_signal(signal)
            }
            Ok(status) => {
                warn!("Unexpected status: {status:?}");
                libc::EXIT_FAILURE
            }
            Err(e) => {
                warn!("waitpid: {e:?}");
                libc::EXIT_FAILURE
            }
        };
        self.return_code.set(Some(return_code));

        let exitcode_path = self.output_file_name("exitcode");
        let exitcode_contents = if self.killed_by_shadow.get() {
            // Process never died during the simulation; shadow chose to kill it;
            // typically because the simulation end time was reached.
            // Write out an empty exitcode file.
            String::new()
        } else {
            format!("{return_code}")
        };
        if let Err(e) = std::fs::write(exitcode_path, exitcode_contents) {
            warn!("Couldn't write exitcode file: {e:?}");
        }

        let main_result_string = {
            let mut s = format!("process '{name}'", name = self.name());
            if self.killed_by_shadow.get() {
                write!(s, " killed by Shadow").unwrap();
            } else {
                write!(s, " exited with code {return_code}").unwrap();
                if return_code == 0 {
                    write!(s, " (success)").unwrap();
                } else {
                    write!(s, " (error)").unwrap();
                }
            }
            s
        };

        // if there was no error or was intentionally killed
        // TODO: once we've implemented clean shutdown via SIGTERM,
        //       consider treating death by SIGKILL as a plugin error
        if return_code == 0 || self.killed_by_shadow.get() {
            info!("{}", main_result_string);
        } else {
            warn!("{}", main_result_string);
            Worker::increment_plugin_error_count();
        }
    }

    fn check(&self, host: &Host) {
        if self.is_running() || !self.has_started() {
            return;
        }

        info!(
            "process '{}' has completed or is otherwise no longer running",
            self.name()
        );
        self.get_and_log_return_code();

        #[cfg(feature = "perf_timers")]
        info!(
            "total runtime for process '{}' was {:?}",
            self.name(),
            self.total_run_time.get()
        );

        let mut descriptor_table = self.descriptor_table_borrow_mut();
        descriptor_table.shutdown_helper();
        let descriptors = descriptor_table.remove_all();
        CallbackQueue::queue_and_run(|cb_queue| {
            for desc in descriptors {
                desc.close(host, cb_queue);
            }
        });
    }

    fn check_thread(&self, host: &Host, tid: ThreadId) {
        {
            let threads = self.threads.borrow();
            let thread = threads.get(&tid).unwrap();
            let thread = thread.borrow(host.root());
            if thread.is_running() {
                debug!(
                    "thread {} in process '{}' still running, but blocked",
                    thread.id(),
                    self.name()
                );
                return;
            }
            let return_code = thread.return_code();
            debug!(
                "thread {} in process '{}' exited with code {return_code:?}",
                thread.id(),
                self.name(),
            );
        }
        self.reap_thread(host, tid);
        self.check(host);
    }

    /// Adds a new thread to the process and schedules it to run.
    /// Intended for use by `clone`.
    pub fn add_thread(&self, host: &Host, thread: RootedRc<RootedRefCell<Thread>>) {
        let pid = self.id();
        let tid = thread.borrow(host.root()).id();
        self.threads.borrow_mut().insert(tid, thread);

        // Schedule thread to start. We're giving the caller's reference to thread
        // to the TaskRef here, which is why we don't increment its ref count to
        // create the TaskRef, but do decrement it on cleanup.
        let task = TaskRef::new(move |host| {
            let Some(process) = host.process_borrow(pid) else {
                // This might happen if a thread calls `clone` and then `exit_group`
                debug!("Process {:?} no longer exists. Can't start its thread {}.", pid, tid);
                return;
            };
            let process = process.borrow(host.root());
            process.resume(host, tid);
        });
        host.schedule_task_with_delay(task, SimulationTime::ZERO);
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

    /// Shared memory for this process.
    pub fn shmem(&self) -> &ShMemBlock<'static, ProcessShmem> {
        &self.shim_shared_mem_block
    }
}

impl Drop for Process {
    fn drop(&mut self) {
        // TODO: is there a better way to handle this?
        // Adding `safely_drop` to Process itself is awkward; it'd need to take
        // the RootedRc and determine whether the Process itself was being
        // dropped or not.
        Worker::with_active_host(|host| self.weak_rc.take().unwrap().safely_drop(host.root()))
            .unwrap();
        self.free_unsafe_borrows_noflush();
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
        let manager = Ref::map(process.memory_manager.borrow(), |mm| mm.as_ref().unwrap());
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
        process.unsafe_borrows.borrow_mut().push(Self {
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
        let manager = Ref::map(process.memory_manager.borrow(), |mm| mm.as_ref().unwrap());
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
        process.unsafe_borrows.borrow_mut().push(Self {
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
        let manager = RefMut::map(process.memory_manager.borrow_mut(), |mm| {
            mm.as_mut().unwrap()
        });
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
        let prev = process.unsafe_borrow_mut.borrow_mut().replace(Self {
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
        let manager = RefMut::map(process.memory_manager.borrow_mut(), |mm| {
            mm.as_mut().unwrap()
        });
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
        let prev = process.unsafe_borrow_mut.borrow_mut().replace(Self {
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

mod export {
    use std::ffi::c_char;
    use std::os::fd::AsRawFd;
    use std::os::raw::c_void;

    use libc::size_t;
    use log::trace;
    use shadow_shim_helper_rs::notnull::*;
    use shadow_shim_helper_rs::shim_shmem::export::ShimShmemProcess;
    use shadow_shim_helper_rs::syscall_types::UntypedForeignPtr;

    use super::*;
    use crate::core::worker::Worker;
    use crate::host::context::ThreadContext;
    use crate::host::descriptor::socket::inet::InetSocket;
    use crate::host::descriptor::socket::Socket;
    use crate::host::descriptor::File;
    use crate::host::syscall_types::{ForeignArrayPtr, SyscallReturn};
    use crate::host::thread::Thread;

    /// Register a `Descriptor`. This takes ownership of the descriptor and you must not access it
    /// after.
    #[no_mangle]
    pub extern "C" fn process_registerDescriptor(
        proc: *const ProcessRefCell,
        desc: *mut Descriptor,
    ) -> libc::c_int {
        let proc = unsafe { proc.as_ref().unwrap() };
        let desc = Descriptor::from_raw(desc).unwrap();

        let fd = Worker::with_active_host(|h| {
            proc.borrow(h.root())
                .descriptor_table_borrow_mut()
                .register_descriptor(*desc)
                .unwrap()
        })
        .unwrap();
        fd.into()
    }

    /// Get a temporary reference to a descriptor.
    #[no_mangle]
    pub extern "C" fn process_getRegisteredDescriptor(
        proc: *const ProcessRefCell,
        handle: libc::c_int,
    ) -> *const Descriptor {
        let proc = unsafe { proc.as_ref().unwrap() };

        let handle = match handle.try_into() {
            Ok(i) => i,
            Err(_) => {
                log::debug!("Attempted to get a descriptor with handle {}", handle);
                return std::ptr::null();
            }
        };

        Worker::with_active_host(|h| {
            match proc.borrow(h.root()).descriptor_table_borrow().get(handle) {
                Some(d) => d as *const Descriptor,
                None => std::ptr::null(),
            }
        })
        .unwrap()
    }

    /// Get a temporary mutable reference to a descriptor.
    #[no_mangle]
    pub extern "C" fn process_getRegisteredDescriptorMut(
        proc: *const ProcessRefCell,
        handle: libc::c_int,
    ) -> *mut Descriptor {
        let proc = unsafe { proc.as_ref().unwrap() };

        let handle = match handle.try_into() {
            Ok(i) => i,
            Err(_) => {
                log::debug!("Attempted to get a descriptor with handle {}", handle);
                return std::ptr::null_mut();
            }
        };

        Worker::with_active_host(|h| {
            match proc
                .borrow(h.root())
                .descriptor_table_borrow_mut()
                .get_mut(handle)
            {
                Some(d) => d as *mut Descriptor,
                None => std::ptr::null_mut(),
            }
        })
        .unwrap()
    }

    /// Get a temporary reference to a legacy file.
    #[no_mangle]
    pub unsafe extern "C" fn process_getRegisteredLegacyFile(
        proc: *const ProcessRefCell,
        handle: libc::c_int,
    ) -> *mut cshadow::LegacyFile {
        let proc = unsafe { proc.as_ref().unwrap() };

        let handle = match handle.try_into() {
            Ok(i) => i,
            Err(_) => {
                log::debug!("Attempted to get a descriptor with handle {}", handle);
                return std::ptr::null_mut();
            }
        };

        Worker::with_active_host(|h| match proc.borrow(h.root()).descriptor_table_borrow().get(handle).map(|x| x.file()) {
            Some(CompatFile::Legacy(file)) => file.ptr(),
            Some(CompatFile::New(file)) => {
                // we have a special case for the legacy C TCP objects
                if let File::Socket(Socket::Inet(InetSocket::LegacyTcp(tcp))) = file.inner_file() {
                    tcp.borrow().as_legacy_file()
                } else {
                    log::warn!(
                        "A descriptor exists for fd={}, but it is not a legacy file. Returning NULL.",
                        handle
                    );
                    std::ptr::null_mut()
                }
            }
            None => std::ptr::null_mut(),
        }).unwrap()
    }

    /// Copy `n` bytes from `src` to `dst`. Returns 0 on success or EFAULT if any of
    /// the specified range couldn't be accessed. Always succeeds with n==0.
    #[no_mangle]
    pub extern "C" fn process_readPtr(
        proc: *const ProcessRefCell,
        dst: *mut c_void,
        src: UntypedForeignPtr,
        n: usize,
    ) -> i32 {
        let proc = unsafe { proc.as_ref().unwrap() };
        let src = ForeignArrayPtr::new(src.cast::<u8>(), n);
        let dst = unsafe { std::slice::from_raw_parts_mut(notnull_mut_debug(dst) as *mut u8, n) };

        Worker::with_active_host(|h| {
            match proc
                .borrow(h.root())
                .memory_borrow()
                .copy_from_ptr(dst, src)
            {
                Ok(_) => 0,
                Err(e) => {
                    trace!("Couldn't read {:?} into {:?}: {:?}", src, dst, e);
                    -(e as i32)
                }
            }
        })
        .unwrap()
    }

    /// Copy `n` bytes from `src` to `dst`. Returns 0 on success or EFAULT if any of
    /// the specified range couldn't be accessed. The write is flushed immediately.
    #[no_mangle]
    pub unsafe extern "C" fn process_writePtr(
        proc: *const ProcessRefCell,
        dst: UntypedForeignPtr,
        src: *const c_void,
        n: usize,
    ) -> i32 {
        let proc = unsafe { proc.as_ref().unwrap() };
        let dst = ForeignArrayPtr::new(dst.cast::<u8>(), n);
        let src = unsafe { std::slice::from_raw_parts(notnull_debug(src) as *const u8, n) };
        Worker::with_active_host(|h| {
            match proc
                .borrow(h.root())
                .memory_borrow_mut()
                .copy_to_ptr(dst, src)
            {
                Ok(_) => 0,
                Err(e) => {
                    trace!("Couldn't write {:?} into {:?}: {:?}", src, dst, e);
                    -(e as i32)
                }
            }
        })
        .unwrap()
    }

    /// Make the data at plugin_src available in shadow's address space.
    ///
    /// The returned pointer is invalidated when one of the process memory flush
    /// methods is called; typically after a syscall has completed.
    #[no_mangle]
    pub unsafe extern "C" fn process_getReadablePtr(
        proc: *const ProcessRefCell,
        plugin_src: UntypedForeignPtr,
        n: usize,
    ) -> *const c_void {
        let proc = unsafe { proc.as_ref().unwrap() };
        let plugin_src = ForeignArrayPtr::new(plugin_src.cast::<u8>(), n);
        Worker::with_active_host(|h| {
            let proc = proc.borrow(h.root());
            unsafe { UnsafeBorrow::readable_ptr(&proc, plugin_src).unwrap_or(std::ptr::null()) }
        })
        .unwrap()
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
    pub unsafe extern "C" fn process_getWriteablePtr(
        proc: *const ProcessRefCell,
        plugin_src: UntypedForeignPtr,
        n: usize,
    ) -> *mut c_void {
        let proc = unsafe { proc.as_ref().unwrap() };
        let plugin_src = ForeignArrayPtr::new(plugin_src.cast::<u8>(), n);
        Worker::with_active_host(|h| {
            let proc = proc.borrow(h.root());
            unsafe {
                UnsafeBorrowMut::writable_ptr(&proc, plugin_src).unwrap_or(std::ptr::null_mut())
            }
        })
        .unwrap()
    }

    /// Returns a writeable pointer corresponding to the specified src. Use when
    /// the data at the given address needs to be both read and written.
    ///
    /// The returned pointer is invalidated when one of the process memory flush
    /// methods is called; typically after a syscall has completed.
    #[no_mangle]
    pub unsafe extern "C" fn process_getMutablePtr(
        proc: *const ProcessRefCell,
        plugin_src: UntypedForeignPtr,
        n: usize,
    ) -> *mut c_void {
        let proc = unsafe { proc.as_ref().unwrap() };
        let plugin_src = ForeignArrayPtr::new(plugin_src.cast::<u8>(), n);
        Worker::with_active_host(|h| {
            let proc = proc.borrow(h.root());
            unsafe {
                UnsafeBorrowMut::mutable_ptr(&proc, plugin_src).unwrap_or(std::ptr::null_mut())
            }
        })
        .unwrap()
    }

    /// Reads up to `n` bytes into `str`.
    ///
    /// Returns:
    /// strlen(str) on success.
    /// -ENAMETOOLONG if there was no NULL byte in the first `n` characters.
    /// -EFAULT if the string extends beyond the accessible address space.
    #[no_mangle]
    pub unsafe extern "C" fn process_readString(
        proc: *const ProcessRefCell,
        strbuf: *mut libc::c_char,
        ptr: UntypedForeignPtr,
        maxlen: libc::size_t,
    ) -> libc::ssize_t {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|h| {
            let proc = proc.borrow(h.root());
            let memory_manager = proc.memory_borrow();
            let buf = unsafe {
                std::slice::from_raw_parts_mut(notnull_mut_debug(strbuf) as *mut u8, maxlen)
            };
            let cstr = match memory_manager
                .copy_str_from_ptr(buf, ForeignArrayPtr::new(ptr.cast::<u8>(), maxlen))
            {
                Ok(cstr) => cstr,
                Err(e) => return -(e as libc::ssize_t),
            };
            cstr.to_bytes().len().try_into().unwrap()
        })
        .unwrap()
    }

    /// Reads up to `n` bytes into `str`.
    ///
    /// Returns:
    /// strlen(str) on success.
    /// -ENAMETOOLONG if there was no NULL byte in the first `n` characters.
    /// -EFAULT if the string extends beyond the accessible address space.
    #[no_mangle]
    pub unsafe extern "C" fn process_getReadableString(
        proc: *const ProcessRefCell,
        plugin_src: UntypedForeignPtr,
        n: usize,
        out_str: *mut *const c_char,
        out_strlen: *mut size_t,
    ) -> i32 {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|h| {
            let proc = proc.borrow(h.root());
            let ptr = ForeignArrayPtr::new(plugin_src.cast::<c_char>(), n);
            match unsafe { UnsafeBorrow::readable_string(&proc, ptr) } {
                Ok((str, strlen)) => {
                    assert!(!out_str.is_null());
                    unsafe { out_str.write(str) };
                    if !out_strlen.is_null() {
                        unsafe { out_strlen.write(strlen) };
                    }
                    0
                }
                Err(e) => -(e as i32),
            }
        })
        .unwrap()
    }

    /// Fully handles the `mmap` syscall
    #[no_mangle]
    pub unsafe extern "C" fn process_handleMmap(
        proc: *const ProcessRefCell,
        thread: *const Thread,
        addr: UntypedForeignPtr,
        len: usize,
        prot: i32,
        flags: i32,
        fd: i32,
        offset: i64,
    ) -> SyscallReturn {
        let process = unsafe { proc.as_ref().unwrap() };
        let thread = unsafe { thread.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let process = process.borrow(host.root());
            let mut memory_manager = process.memory_borrow_mut();
            memory_manager
                .do_mmap(
                    &ThreadContext::new(host, &process, thread),
                    addr.cast::<u8>(),
                    len,
                    prot,
                    flags,
                    fd,
                    offset,
                )
                .into()
        })
        .unwrap()
    }

    /// Fully handles the `munmap` syscall
    #[no_mangle]
    pub unsafe extern "C" fn process_handleMunmap(
        proc: *const ProcessRefCell,
        thread: *const Thread,
        addr: UntypedForeignPtr,
        len: usize,
    ) -> SyscallReturn {
        let process = unsafe { proc.as_ref().unwrap() };
        let thread = unsafe { thread.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let process = process.borrow(host.root());
            let mut memory_manager = process.memory_borrow_mut();
            memory_manager
                .handle_munmap(
                    &ThreadContext::new(host, &process, thread),
                    addr.cast::<u8>(),
                    len,
                )
                .into()
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn process_handleMremap(
        proc: *const ProcessRefCell,
        thread: *const Thread,
        old_addr: UntypedForeignPtr,
        old_size: usize,
        new_size: usize,
        flags: i32,
        new_addr: UntypedForeignPtr,
    ) -> SyscallReturn {
        let process = unsafe { proc.as_ref().unwrap() };
        let thread = unsafe { thread.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let process = process.borrow(host.root());
            let mut memory_manager = process.memory_borrow_mut();
            memory_manager
                .handle_mremap(
                    &ThreadContext::new(host, &process, thread),
                    old_addr.cast::<u8>(),
                    old_size,
                    new_size,
                    flags,
                    new_addr.cast::<u8>(),
                )
                .into()
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn process_handleMprotect(
        proc: *const ProcessRefCell,
        thread: *const Thread,
        addr: UntypedForeignPtr,
        size: usize,
        prot: i32,
    ) -> SyscallReturn {
        let process = unsafe { proc.as_ref().unwrap() };
        let thread = unsafe { thread.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let process = process.borrow(host.root());
            let mut memory_manager = process.memory_borrow_mut();
            memory_manager
                .handle_mprotect(
                    &ThreadContext::new(host, &process, thread),
                    addr.cast::<u8>(),
                    size,
                    prot,
                )
                .into()
        })
        .unwrap()
    }

    /// Fully handles the `brk` syscall, keeping the "heap" mapped in our shared mem file.
    #[no_mangle]
    pub unsafe extern "C" fn process_handleBrk(
        proc: *const ProcessRefCell,
        thread: *const Thread,
        plugin_src: UntypedForeignPtr,
    ) -> SyscallReturn {
        let process = unsafe { proc.as_ref().unwrap() };
        let thread = unsafe { thread.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let process = process.borrow(host.root());
            let mut memory_manager = process.memory_borrow_mut();
            memory_manager
                .handle_brk(
                    &ThreadContext::new(host, &process, thread),
                    plugin_src.cast::<u8>(),
                )
                .into()
        })
        .unwrap()
    }

    /// Initialize the MemoryMapper if it isn't already initialized. `thread` must
    /// be running and ready to make native syscalls.
    #[no_mangle]
    pub unsafe extern "C" fn process_initMapperIfNeeded(
        proc: *const ProcessRefCell,
        thread: *const Thread,
    ) {
        let process = unsafe { proc.as_ref().unwrap() };
        let thread = unsafe { thread.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let process = process.borrow(host.root());
            let mut memory_manager = process.memory_borrow_mut();
            if !memory_manager.has_mapper() {
                memory_manager.init_mapper(&ThreadContext::new(host, &process, thread))
            }
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn process_resetMemoryManager(proc: *const ProcessRefCell) {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|h| {
            drop(proc.borrow(h.root()).memory_manager.borrow_mut().take());
        })
        .unwrap()
    }

    /// Returns the processID that was assigned to us in process_new
    #[no_mangle]
    pub unsafe extern "C" fn process_getProcessID(proc: *const ProcessRefCell) -> libc::pid_t {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|h| proc.borrow(h.root()).id().try_into().unwrap()).unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn process_getHostId(proc: *const ProcessRefCell) -> HostId {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).host_id()).unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn process_getName(proc: *const ProcessRefCell) -> *const c_char {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).name.as_ptr()).unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn process_getPluginName(proc: *const ProcessRefCell) -> *const c_char {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).plugin_name.as_ptr()).unwrap()
    }

    /// Safety:
    ///
    /// The returned pointer is invalidated when the host shmem lock is released, e.g. via
    /// Host::unlock_shmem.
    #[no_mangle]
    pub unsafe extern "C" fn process_getSharedMem(
        proc: *const ProcessRefCell,
    ) -> *const ShimShmemProcess {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            proc.borrow(host.root()).shim_shared_mem_block.deref() as *const _
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn process_getWorkingDir(proc: *const ProcessRefCell) -> *const c_char {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).working_dir.as_ptr()).unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn process_straceLoggingMode(
        proc: *const ProcessRefCell,
    ) -> StraceFmtMode {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).strace_logging_options())
            .unwrap()
            .into()
    }

    #[no_mangle]
    pub unsafe extern "C" fn process_straceFd(proc: *const ProcessRefCell) -> RawFd {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| match &proc.borrow(host.root()).strace_logging {
            Some(x) => x.file.borrow().as_raw_fd(),
            None => -1,
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn process_isExiting(proc: *const ProcessRefCell) -> bool {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).is_exiting.get()).unwrap()
    }

    /// In some cases a running thread processes an action that will bring down the
    /// entire process. Calling this tells the Process to clean up other threads
    /// without trying to run them again, since otherwise the OS may kill the other
    /// thread tasks while we're in the middle of trying to execute them, which can
    /// be difficult to recover from cleanly.
    #[no_mangle]
    pub unsafe extern "C" fn process_markAsExiting(proc: *const ProcessRefCell) {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let proc = proc.borrow(host.root());
            proc.mark_as_exiting()
        })
        .unwrap();
    }

    /// Get process's "dumpable" state, as manipulated by the prctl operations
    /// PR_SET_DUMPABLE and PR_GET_DUMPABLE.
    #[no_mangle]
    pub unsafe extern "C" fn process_getDumpable(proc: *const ProcessRefCell) -> u32 {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).dumpable.get()).unwrap()
    }

    /// Set process's "dumpable" state, as manipulated by the prctl operations
    /// PR_SET_DUMPABLE and PR_GET_DUMPABLE.
    #[no_mangle]
    pub unsafe extern "C" fn process_setDumpable(proc: *const ProcessRefCell, val: u32) {
        assert!(val == cshadow::SUID_DUMP_DISABLE || val == cshadow::SUID_DUMP_USER);
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).dumpable.set(val)).unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn process_getNativePid(proc: *const ProcessRefCell) -> libc::pid_t {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).native_pid().unwrap().as_raw())
            .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn process_hasStarted(proc: *const ProcessRefCell) -> bool {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).has_started()).unwrap()
    }

    /// Flushes and invalidates all previously returned readable/writable plugin
    /// pointers, as if returning control to the plugin. This can be useful in
    /// conjunction with `thread_nativeSyscall` operations that touch memory, or
    /// to gracefully handle failed writes.
    ///
    /// Returns 0 on success or a negative errno on failure.
    #[no_mangle]
    pub unsafe extern "C" fn process_flushPtrs(proc: *const ProcessRefCell) -> i32 {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let proc = proc.borrow(host.root());
            match proc.free_unsafe_borrows_flush() {
                Ok(_) => 0,
                Err(e) => -(e as i32),
            }
        })
        .unwrap()
    }

    /// Frees all readable/writable foreign pointers. Unlike process_flushPtrs, any
    /// previously returned writable pointer is *not* written back. Useful
    /// if an uninitialized writable pointer was obtained via `process_getWriteablePtr`,
    /// and we end up not wanting to write anything after all (in particular, don't
    /// write back whatever garbage data was in the uninialized bueffer).
    #[no_mangle]
    pub unsafe extern "C" fn process_freePtrsWithoutFlushing(proc: *const ProcessRefCell) {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let proc = proc.borrow(host.root());
            proc.free_unsafe_borrows_noflush();
        })
        .unwrap();
    }

    #[no_mangle]
    pub unsafe extern "C" fn process_getThread(
        proc: *const ProcessRefCell,
        tid: libc::pid_t,
    ) -> *const Thread {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let proc = proc.borrow(host.root());
            let tid = ThreadId::try_from(tid).unwrap();
            let Some(thread) = proc.thread_borrow(tid) else {
                return std::ptr::null();
            };
            let thread = thread.borrow(host.root());
            &*thread
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn process_isRunning(proc: *const ProcessRefCell) -> bool {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let proc = proc.borrow(host.root());
            proc.is_running()
        })
        .unwrap()
    }

    // FIXME: still needed? Time is now updated more granularly in the Thread code
    // when xferring control to/from shim.
    #[no_mangle]
    pub unsafe extern "C" fn process_setSharedTime() {
        Worker::with_active_host(Process::set_shared_time).unwrap();
    }

    #[no_mangle]
    pub unsafe extern "C" fn process_getPhysicalAddress(
        proc: *const ProcessRefCell,
        vptr: UntypedForeignPtr,
    ) -> ManagedPhysicalMemoryAddr {
        let proc = unsafe { proc.as_ref().unwrap() };

        Worker::with_active_host(|host| {
            let proc = proc.borrow(host.root());
            proc.physical_address(vptr)
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn process_terminate(proc: *const ProcessRefCell) {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let proc = proc.borrow(host.root());
            proc.terminate(host)
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn process_continue(proc: *const ProcessRefCell, thread_id: libc::pid_t) {
        let proc = unsafe { proc.as_ref().unwrap() };
        let tid = ThreadId::try_from(thread_id).unwrap();
        Worker::with_active_host(|host| {
            let proc = proc.borrow(host.root());
            proc.resume(host, tid)
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn process_stop(proc: *const ProcessRefCell) {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).stop(host)).unwrap()
    }

    /// Send the signal described in `siginfo` to `process`. `currentRunningThread`
    /// should be set if there is one (e.g. if this is being called from a syscall
    /// handler), and NULL otherwise (e.g. when called from a timer expiration event).
    #[no_mangle]
    pub unsafe extern "C" fn process_signal(
        target_proc: *const ProcessRefCell,
        current_running_thread: *const Thread,
        siginfo: *const libc::siginfo_t,
    ) {
        let target_proc = unsafe { target_proc.as_ref().unwrap() };
        let current_running_thread = unsafe { current_running_thread.as_ref() };
        let siginfo = unsafe { siginfo.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let target_proc = target_proc.borrow(host.root());
            target_proc.signal(host, current_running_thread, siginfo)
        })
        .unwrap()
    }
}
