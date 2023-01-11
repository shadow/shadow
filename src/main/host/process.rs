use std::cell::{Cell, Ref, RefCell, RefMut};
use std::collections::BTreeMap;
use std::ffi::{c_char, c_void, CStr, CString};
use std::fmt::Write;
use std::num::TryFromIntError;
use std::ops::{Deref, DerefMut};
use std::os::fd::{AsRawFd, RawFd};
use std::os::unix::io::FromRawFd;
use std::path::PathBuf;
use std::sync::atomic::Ordering;

use log::{debug, error, info, trace, warn};
use nix::errno::Errno;
use nix::fcntl::OFlag;
use nix::sys::signal::Signal;
use nix::sys::stat::Mode;
use nix::unistd::Pid;
use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::rootedcell::rc::RootedRc;
use shadow_shim_helper_rs::rootedcell::refcell::RootedRefCell;
use shadow_shim_helper_rs::shim_shmem::ProcessShmem;
use shadow_shim_helper_rs::simulation_time::SimulationTime;
use shadow_shmem::allocator::ShMemBlock;

use crate::core::work::task::TaskRef;
use crate::core::worker::Worker;
use crate::cshadow;
use crate::host::descriptor::{CompatFile, Descriptor};
use crate::host::syscall::formatter::FmtOptions;
use crate::utility::callback_queue::CallbackQueue;
use crate::utility::{pathbuf_to_nul_term_cstring, SyncSendPointer};

use super::descriptor::descriptor_table::DescriptorTable;
use super::host::Host;
use super::memory_manager::{MemoryManager, ProcessMemoryRef, ProcessMemoryRefMut};
use super::syscall::formatter::StraceFmtMode;
use super::syscall_types::TypedPluginPtr;
use super::thread::{ThreadId, ThreadRef};
use super::timer::Timer;

use shadow_shim_helper_rs::HostId;

#[cfg(feature = "perf_timers")]
use crate::utility::perf_timer::PerfTimer;
#[cfg(feature = "perf_timers")]
use std::time::Duration;

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
pub type RustProcess = RootedRefCell<Process>;

pub struct Process {
    cprocess: SyncSendPointer<cshadow::Process>,
    id: ProcessId,
    host_id: HostId,

    // unique id of the program that this process should run
    name: CString,

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
    //
    // `ThreadRef` currently has mutable methods, so we need to be able to get a
    // mutable reference to it, so we use `RootedRefCell`. We could end up
    // dropping this if we change `ThreadRef` to use internal mutability
    // everywhere as we do with Process and Host. I suspect we'll actually want
    // to move in the other direction once we have less C code though and reduce
    // the amount of interior mutability.
    threads: RefCell<BTreeMap<ThreadId, RootedRc<RootedRefCell<ThreadRef>>>>,

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
    let mut siginfo: cshadow::siginfo_t = unsafe { std::mem::zeroed() };
    // The siginfo_t structure only has an i32. Presumably we want to just truncate in
    // case of overflow.
    let expiration_count = timer.expiration_count() as i32;
    unsafe { cshadow::process_initSiginfoForAlarm(&mut siginfo, expiration_count) };
    unsafe { cshadow::process_signal(process.cprocess(), std::ptr::null_mut(), &siginfo) };
}

impl Process {
    /// # Safety
    ///
    /// The returned `RootedRefCell<Self>` must not be moved out of its
    /// RootedRc. TODO: statically enforce by wrapping the RootedRefCell in
    /// `Pin`, and making Process `!Unpin`. Probably not worth the work though
    /// since the inner C pointer should be removed soon, and then we can remove
    /// this requirement.
    pub unsafe fn new(
        host: &Host,
        process_id: ProcessId,
        start_time: SimulationTime,
        stop_time: Option<SimulationTime>,
        plugin_name: &CStr,
        plugin_path: &CStr,
        mut envv: Vec<CString>,
        argv: Vec<CString>,
        pause_for_debugging: bool,
        use_legacy_working_dir: bool,
        use_shim_syscall_handler: bool,
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

        let strace_logging = strace_logging_options.map(|options| {
            let oflag = { OFlag::O_CREAT | OFlag::O_TRUNC | OFlag::O_WRONLY | OFlag::O_CLOEXEC };
            let mode = { Mode::S_IRUSR | Mode::S_IWUSR | Mode::S_IRGRP | Mode::S_IROTH };
            let filename = Self::static_output_file_name(name.to_str().unwrap(), host, "strace");
            let fd = nix::fcntl::open(&filename, oflag, mode).unwrap();

            StraceLogging {
                file: RefCell::new(unsafe { std::fs::File::from_raw_fd(fd) }),
                options,
            }
        });

        let shim_shared_mem = ProcessShmem::new(
            &host.shim_shmem_lock_borrow().unwrap().root,
            host.id(),
            strace_logging.as_ref().map(|x| x.file.borrow().as_raw_fd()),
        );
        let shim_shared_mem_block =
            shadow_shmem::allocator::Allocator::global().alloc(shim_shared_mem);

        let working_dir = pathbuf_to_nul_term_cstring(if use_legacy_working_dir {
            nix::unistd::getcwd().unwrap()
        } else {
            std::fs::canonicalize(host.data_dir_path()).unwrap()
        });

        // TODO: ensure no duplicate env vars.
        envv.push(
            CString::new(format!(
                "SHADOW_SHM_PROCESS_BLK={}",
                shim_shared_mem_block.serialize().encode_to_string()
            ))
            .unwrap(),
        );
        envv.push(
            CString::new(format!(
                "SHADOW_LOG_FILE={}",
                Self::static_output_file_name(name.to_str().unwrap(), host, "shimlog")
                    .to_str()
                    .unwrap()
            ))
            .unwrap(),
        );
        if !use_shim_syscall_handler {
            envv.push(CString::new("SHADOW_DISABLE_SHIM_SYSCALL=TRUE").unwrap());
        }

        #[cfg(feature = "perf_timers")]
        let cpu_delay_timer = {
            let mut t = PerfTimer::new();
            t.stop();
            RefCell::new(t)
        };

        // We've initialized all the parts of Self *except* for the cprocess.
        // `process_new` needs the Rust process though, so we create that now with
        // a NULL cprocess, then create the cprocess, then add it to the Self.
        let process = RootedRc::new(
            host.root(),
            RootedRefCell::new(
                host.root(),
                Self {
                    id: process_id,
                    host_id: host.id(),
                    // We set this to non-null below.
                    cprocess: unsafe { SyncSendPointer::new(std::ptr::null_mut()) },
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

        // We're storing a raw pointer to the inner RootedRefCell here. This is a bit
        // fragile, but should be ok since its never moved out of the enclosing RootedRc,
        // so its address won't change. Since the Rust Process owns the C Process, the
        // Rust Process will outlive the C Process.
        //
        // We could store the outer RootedRc, but then need to deal with a reference cycle.
        let process_refcell: &RustProcess = &process;
        let cprocess = unsafe {
            cshadow::process_new(
                process_refcell,
                host,
                process_id.try_into().unwrap(),
                pause_for_debugging,
            )
        };
        // SAFETY: The Process itself is wrapped in a RootedRefCell, which ensures
        // it can only be accessed by one thread at a time. Whenever we access its cprocess,
        // we ensure that the pointer doesn't "escape" in a way that would allow it to be
        // accessed by threads that don't have access to the enclosing Process.
        let cprocess = unsafe { SyncSendPointer::new(cprocess) };
        process.borrow_mut(host.root()).cprocess = cprocess;

        process
    }

    /// # Safety
    ///
    /// The returned pointer must not outlive the caller's reference to `self`,
    /// or be accessed by threads other than the caller's.
    pub unsafe fn cprocess(&self) -> *mut cshadow::Process {
        self.cprocess.ptr()
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
            process.borrow(host.root()).start(host);
        });
        host.schedule_task_at_emulated_time(task, self.start_time);

        if let Some(stop_time) = self.stop_time {
            let task = TaskRef::new(move |host| {
                let process = host.process_borrow(id).unwrap();
                let cprocess = unsafe { process.borrow(host.root()).cprocess() };
                unsafe { cshadow::process_stop(cprocess) };
            });
            host.schedule_task_at_emulated_time(task, stop_time);
        }
    }

    fn start(&self, host: &Host) {
        assert!(!self.is_running());

        self.open_stdio_file_helper(
            libc::STDIN_FILENO as u32,
            "/dev/null".into(),
            OFlag::O_RDONLY,
        );

        let name = self.output_file_name(host, "stdout");
        self.open_stdio_file_helper(libc::STDOUT_FILENO as u32, name, OFlag::O_WRONLY);

        let name = self.output_file_name(host, "stderr");
        self.open_stdio_file_helper(libc::STDERR_FILENO as u32, name, OFlag::O_WRONLY);

        // tid of first thread of a process is equal to the pid.
        let tid = ThreadId::from(self.id());
        let main_thread =
            unsafe { cshadow::thread_new(host, self.cprocess(), tid.try_into().unwrap()) };
        let main_thread_ref = RootedRc::new(
            host.root(),
            RootedRefCell::new(host.root(), unsafe { ThreadRef::new(main_thread) }),
        );
        // ThreadRef increments the reference count; we don't need the original
        // reference anymore.
        unsafe { cshadow::thread_unref(main_thread) };

        // Insert a *clone* of the reference, since we continue to use ours below.
        self.threads
            .borrow_mut()
            .insert(tid, main_thread_ref.clone(host.root()));

        info!("starting process '{}'", self.name());
        let main_thread = main_thread_ref.borrow(host.root());
        Worker::set_active_process(self);
        Worker::set_active_thread(&main_thread);

        #[cfg(feature = "perf_timers")]
        self.start_cpu_delay_timer();

        Process::set_shared_time(host);

        let argv_ptrs: Vec<*const i8> = self
            .argv
            .iter()
            .map(|x| x.as_ptr())
            // the last element of argv must be NULL
            .chain(std::iter::once(std::ptr::null()))
            .collect();

        let envv_ptrs: Vec<*const i8> = self
            .envv
            .iter()
            .map(|x| x.as_ptr())
            // the last element of envv must be NULL
            .chain(std::iter::once(std::ptr::null()))
            .collect();

        unsafe {
            cshadow::thread_run(
                main_thread.cthread(),
                self.plugin_path.as_ptr(),
                argv_ptrs.as_ptr(),
                envv_ptrs.as_ptr(),
                self.working_dir.as_ptr(),
                match &self.strace_logging {
                    Some(x) => x.file.borrow().as_raw_fd(),
                    None => -1,
                },
            )
        };

        let native_pid =
            Pid::from_raw(unsafe { cshadow::thread_getNativePid(main_thread.cthread()) });
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

        // `process_continue` actually starts running the thread and may make
        // syscalls, which may need to get a mutable reference to the thread, so
        // we need to drop the borrow here. Once `process_continue` and the thread
        // code is in Rust, we can pass the borrow through instead of dropping
        // it.
        let cthread = unsafe { main_thread.cthread() };
        drop(main_thread);
        main_thread_ref.safely_drop(host.root());

        unsafe { cshadow::process_continue(self.cprocess(), cthread) };
    }

    fn open_stdio_file_helper(
        &self,
        fd: u32,
        path: PathBuf,
        access_mode: OFlag,
    ) -> *mut cshadow::RegularFile {
        let stdfile = unsafe { cshadow::regularfile_new() };
        let cwd = nix::unistd::getcwd().unwrap();
        let path = pathbuf_to_nul_term_cstring(path);
        let cwd = pathbuf_to_nul_term_cstring(cwd);
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
        let prev = self.descriptor_table_borrow_mut().set(fd, desc);
        assert!(prev.is_none());
        trace!(
            "Successfully opened fd {} at {}",
            fd,
            path.to_str().unwrap()
        );
        stdfile
    }

    fn output_file_name(&self, host: &Host, basename: &str) -> PathBuf {
        Self::static_output_file_name(self.name(), host, basename)
    }

    // Needed during early init, before `Self` is created.
    fn static_output_file_name(name: &str, host: &Host, basename: &str) -> PathBuf {
        let mut dirname = host.data_dir_path().to_owned();
        let basename = format!("{}.{}", name, basename);
        dirname.push(basename);
        dirname
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

    pub fn thread_borrow(
        &self,
        virtual_tid: ThreadId,
    ) -> Option<impl Deref<Target = RootedRc<RootedRefCell<ThreadRef>>> + '_> {
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

    pub fn physical_address(&self, vptr: cshadow::PluginVirtualPtr) -> cshadow::PluginPhysicalPtr {
        // We currently don't keep a true system-wide virtual <-> physical address
        // mapping. Instead we simply assume that no shadow processes map the same
        // underlying physical memory, and that therefore (pid, virtual address)
        // uniquely defines a physical address.
        //
        // If we ever want to support futexes in memory shared between processes,
        // we'll need to change this.  The most foolproof way to do so is probably
        // to change PluginPhysicalPtr to be a bigger struct that identifies where
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

        let low_part: u64 = vptr.val;
        assert_eq!(low_part >> VADDR_BITS, 0);

        cshadow::PluginPhysicalPtr {
            val: high_part | low_part,
        }
    }

    /// Call after a thread has exited. Removes the thread and does corresponding cleanup and notifications.
    fn reap_thread(&self, host: &Host, tid: ThreadId) {
        let threadrc = self.threads.borrow_mut().remove(&tid).unwrap();
        let thread = threadrc.borrow(host.root());

        // If the `clear_child_tid` attribute on the thread is set, and there are
        // any other threads left alive in the process, perform a futex wake on
        // that address. This mechanism is typically used in `pthread_join` etc.
        // See `set_tid_address(2)`.
        let clear_child_tid_pvp = unsafe { cshadow::thread_getTidAddress(thread.cthread()) };
        if clear_child_tid_pvp.val != 0 && self.threads.borrow().len() > 0 && !self.is_exiting.get()
        {
            // Wait until the thread is really dead. It might not be dead yet if we
            // marked the thread dead after seeing the `exit` syscall - the managed
            // process might not have finished executing the native syscall yet.
            // TODO: Move this into the `exit` syscall handling and/or use the native
            // tidaddress mechanism to be notified when a thread has actually terminated.
            let native_pid = unsafe { cshadow::thread_getNativePid(thread.cthread()) };
            let native_tid = unsafe { cshadow::thread_getNativeTid(thread.cthread()) };
            loop {
                match tgkill(native_pid, native_tid, 0) {
                    Err(Errno::ESRCH) => {
                        trace!("Thread is done exiting; proceeding with cleanup");
                        break;
                    }
                    Err(e) => {
                        error!("Unexpected tgkill error: {:?}", e);
                        break;
                    }
                    Ok(()) if native_pid == native_tid => {
                        // Thread leader could be in a zombie state waiting for
                        // the other threads to exit.
                        let filename = format!("/proc/{native_pid}/stat");
                        let stat = match std::fs::read_to_string(filename) {
                            Err(e) => {
                                assert!(e.kind() == std::io::ErrorKind::NotFound);
                                trace!("tgl {native_pid} is fully dead");
                                break;
                            }
                            Ok(s) => s,
                        };
                        if stat.contains(") Z") {
                            trace!("tgl {native_pid} is a zombie");
                            break;
                        }
                        // Still alive and in a non-zombie state; continue
                    }
                    Ok(()) => {
                        // Thread is still alive; continue.
                    }
                };
                debug!("{native_pid}.{native_tid} still running; waiting for it to exit");
                std::thread::yield_now();
                // Check again
            }

            // Find a still-living thread to execute the memory-write.
            let threads = self.threads.borrow();
            let writer_thread = threads
                .values()
                .find(|t| unsafe { cshadow::thread_isRunning(t.borrow(host.root()).cthread()) })
                .unwrap();
            // MemoryCopier uses the active thread's tid to do the write; we need to set
            // that to the still-live thread we're using to do the write.
            Worker::clear_active_thread();
            Worker::set_active_thread(&writer_thread.borrow(host.root()));
            let typed_clear_child_tid_pvp =
                TypedPluginPtr::new::<libc::pid_t>(clear_child_tid_pvp.into(), 1);
            self.memory_borrow_mut()
                .copy_to_ptr(typed_clear_child_tid_pvp, &[0])
                .unwrap();
            // Restore active thread.
            Worker::clear_active_thread();
            Worker::set_active_thread(&thread);

            // Wake the corresponding futex.
            let mut futexes = host.futextable_borrow_mut();
            let futex = unsafe {
                cshadow::futextable_get(&mut *futexes, self.physical_address(clear_child_tid_pvp))
            };
            if !futex.is_null() {
                unsafe { cshadow::futex_wake(futex, 1) };
            }
        }

        // Compiler forces us to drop this before we can consume `threadrc`.
        drop(thread);

        threadrc.safely_drop(host.root());
    }

    fn has_started(&self) -> bool {
        self.native_pid.get().is_some()
    }

    pub fn is_running(&self) -> bool {
        !self.is_exiting.get() && self.threads.borrow().len() > 0
    }

    fn mark_as_exiting(&self) {
        self.is_exiting.set(true);
        trace!("Process {:?} marked as exiting", self.id());
    }

    fn handle_process_exit(&self, host: &Host) {
        loop {
            let (tid, thread) = {
                let threads = self.threads.borrow();
                let Some((tid, thread)) = threads.iter().next() else {
                    break;
                };
                // Conservatively leaving in the thread list, and cloning
                // the reference so that we don't hold a borrow over the list.
                (*tid, thread.clone(host.root()))
            };
            unsafe { cshadow::thread_handleProcessExit(thread.borrow(host.root()).cthread()) };
            self.reap_thread(host, tid);
            thread.safely_drop(host.root());
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

    fn get_and_log_return_code(&self, host: &Host) {
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
            Ok(WaitStatus::Signaled(_pid, signal, _core_dump)) => unsafe {
                cshadow::return_code_for_signal(signal as i32)
            },
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

        let exitcode_path = self.output_file_name(host, "exitcode");
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
        self.get_and_log_return_code(host);

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
            let threadrc = threads.get(&tid).unwrap();
            let thread = threadrc.borrow(host.root());
            if unsafe { cshadow::thread_isRunning(thread.cthread()) } {
                debug!(
                    "thread {} in process '{}' still running, but blocked",
                    thread.id(),
                    self.name()
                );
                return;
            }
            let return_code = unsafe { cshadow::thread_getReturnCode(thread.cthread()) };
            debug!(
                "thread {} in process '{}' exited with code {}",
                thread.id(),
                self.name(),
                return_code
            );
        }
        self.reap_thread(host, tid);
        self.check(host);
    }

    /// Adds a new thread to the process and schedules it to run.
    /// Intended for use by `clone`.
    pub fn add_thread(&self, host: &Host, thread: ThreadRef) {
        let pid = self.id();
        let tid = thread.id();
        let thread = RootedRc::new(host.root(), RootedRefCell::new(host.root(), thread));
        self.threads.borrow_mut().insert(tid, thread);

        // Schedule thread to start. We're giving the caller's reference to thread
        // to the TaskRef here, which is why we don't increment its ref count to
        // create the TaskRef, but do decrement it on cleanup.
        let task = TaskRef::new(move |host| {
            let (cprocess, cthread) = {
                let Some(process) = host.process_borrow(pid) else {
                    // This might happen if a thread calls `clone` and then `exit_group`
                    debug!("Process {:?} no longer exists. Can't start its thread {}.", pid, tid);
                    return;
                };
                let process = process.borrow(host.root());
                let threads = process.threads.borrow();
                let Some(thread) = threads.get(&tid) else {
                    // Maybe possible e.g. if a thread is targeted with `tgkill` before it
                    // gets a chance to start.
                    debug!("Thread {} no longer exists. Can't start it.", tid);
                    return;
                };
                let thread = thread.borrow(host.root());
                unsafe { (process.cprocess(), thread.cthread()) }
            };
            unsafe { cshadow::process_continue(cprocess, cthread) };
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
}

impl Drop for Process {
    fn drop(&mut self) {
        unsafe { cshadow::process_free(self.cprocess.ptr()) }
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
        ptr: TypedPluginPtr<u8>,
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
        ptr: TypedPluginPtr<c_char>,
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
        ptr: TypedPluginPtr<u8>,
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
        ptr: TypedPluginPtr<u8>,
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

fn tgkill(pid: libc::pid_t, tid: libc::pid_t, signo: i32) -> nix::Result<()> {
    let res = unsafe { libc::syscall(libc::SYS_tgkill, pid, tid, signo) };
    Errno::result(res).map(|i: i64| {
        assert_eq!(i, 0);
    })
}

mod export {
    use std::ffi::{c_char, c_int};
    use std::os::fd::AsRawFd;
    use std::os::raw::c_void;

    use libc::size_t;
    use log::{trace, warn};
    use nix::sys::signal::Signal;
    use shadow_shim_helper_rs::notnull::*;
    use shadow_shim_helper_rs::shim_shmem::export::{ShimShmemHostLock, ShimShmemProcess};

    use crate::core::worker::Worker;
    use crate::cshadow::CEmulatedTime;
    use crate::host::descriptor::socket::inet::InetSocket;
    use crate::host::descriptor::socket::Socket;
    use crate::host::descriptor::File;
    use crate::host::syscall_types::{PluginPtr, TypedPluginPtr};
    use crate::host::thread::ThreadRef;
    use crate::utility::callback_queue::CallbackQueue;

    use super::*;

    /// Register a `Descriptor`. This takes ownership of the descriptor and you must not access it
    /// after.
    #[no_mangle]
    pub extern "C" fn process_registerDescriptor(
        proc: *mut cshadow::Process,
        desc: *mut Descriptor,
    ) -> libc::c_int {
        let proc = unsafe { cshadow::process_getRustProcess(proc).as_ref().unwrap() };
        let desc = Descriptor::from_raw(desc).unwrap();

        let fd = Worker::with_active_host(|h| {
            proc.borrow(h.root())
                .descriptor_table_borrow_mut()
                .register_descriptor(*desc)
        })
        .unwrap();
        fd.try_into().unwrap()
    }

    /// Get a temporary reference to a descriptor.
    #[no_mangle]
    pub extern "C" fn process_getRegisteredDescriptor(
        proc: *mut cshadow::Process,
        handle: libc::c_int,
    ) -> *const Descriptor {
        let proc = unsafe { cshadow::process_getRustProcess(proc).as_ref().unwrap() };

        let handle: u32 = match handle.try_into() {
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
        proc: *mut cshadow::Process,
        handle: libc::c_int,
    ) -> *mut Descriptor {
        let proc = unsafe { cshadow::process_getRustProcess(proc).as_ref().unwrap() };

        let handle: u32 = match handle.try_into() {
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
        proc: *mut cshadow::Process,
        handle: libc::c_int,
    ) -> *mut cshadow::LegacyFile {
        let proc = unsafe { cshadow::process_getRustProcess(proc).as_ref().unwrap() };

        let handle: u32 = match handle.try_into() {
            Ok(i) => i,
            Err(_) => {
                log::debug!("Attempted to get a descriptor with handle {}", handle);
                return std::ptr::null_mut();
            }
        };

        Worker::with_active_host(|h| match proc.borrow(h.root()).descriptor_table_borrow().get(handle).map(|x| x.file()) {
            Some(CompatFile::Legacy(file)) => unsafe { file.ptr() },
            Some(CompatFile::New(file)) => {
                // we have a special case for the legacy C TCP objects
                if let File::Socket(Socket::Inet(InetSocket::Tcp(tcp))) = file.inner_file() {
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
    pub extern "C" fn _process_readPtr(
        proc: *const RustProcess,
        dst: *mut c_void,
        src: cshadow::PluginPtr,
        n: usize,
    ) -> i32 {
        let proc = unsafe { proc.as_ref().unwrap() };
        let src = TypedPluginPtr::new::<u8>(src.into(), n);
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
    pub unsafe extern "C" fn _process_writePtr(
        proc: *const RustProcess,
        dst: cshadow::PluginPtr,
        src: *const c_void,
        n: usize,
    ) -> i32 {
        let proc = unsafe { proc.as_ref().unwrap() };
        let dst = TypedPluginPtr::new::<u8>(dst.into(), n);
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
    pub unsafe extern "C" fn _process_getReadablePtr(
        proc: *const RustProcess,
        plugin_src: cshadow::PluginPtr,
        n: usize,
    ) -> *const c_void {
        let proc = unsafe { proc.as_ref().unwrap() };
        let plugin_src = TypedPluginPtr::new::<u8>(plugin_src.into(), n);
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
    pub unsafe extern "C" fn _process_getWriteablePtr(
        proc: *const RustProcess,
        plugin_src: cshadow::PluginPtr,
        n: usize,
    ) -> *mut c_void {
        let proc = unsafe { proc.as_ref().unwrap() };
        let plugin_src = TypedPluginPtr::new::<u8>(plugin_src.into(), n);
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
    pub unsafe extern "C" fn _process_getMutablePtr(
        proc: *const RustProcess,
        plugin_src: cshadow::PluginPtr,
        n: usize,
    ) -> *mut c_void {
        let proc = unsafe { proc.as_ref().unwrap() };
        let plugin_src = TypedPluginPtr::new::<u8>(plugin_src.into(), n);
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
    pub unsafe extern "C" fn _process_readString(
        proc: *const RustProcess,
        ptr: cshadow::PluginPtr,
        strbuf: *mut libc::c_char,
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
                .copy_str_from_ptr(buf, TypedPluginPtr::new::<u8>(PluginPtr::from(ptr), maxlen))
            {
                Ok(cstr) => cstr,
                Err(e) => return -(e as libc::ssize_t),
            };
            cstr.to_bytes().len().try_into().unwrap()
        })
        .unwrap()
    }

    /// Temporary; meant to be called from process.c.
    #[no_mangle]
    pub unsafe extern "C" fn _process_descriptorTableShutdownHelper(proc: *const RustProcess) {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|h| {
            proc.borrow(h.root())
                .descriptor_table_borrow_mut()
                .shutdown_helper();
        })
        .unwrap();
    }

    /// Close all descriptors. The `host` option is a legacy option for legacy files.
    /// Temporary; meant to be called from process.c.
    #[no_mangle]
    pub unsafe extern "C" fn _process_descriptorTableRemoveAndCloseAll(proc: *const RustProcess) {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let descriptors = proc
                .borrow(host.root())
                .descriptor_table_borrow_mut()
                .remove_all();
            CallbackQueue::queue_and_run(|cb_queue| {
                for desc in descriptors {
                    desc.close(host, cb_queue);
                }
            });
        })
        .unwrap();
    }

    /// Temporary; meant to be called from process.c.
    ///
    /// Store the given descriptor at the given index. Any previous descriptor that was
    /// stored there will be returned. This consumes a ref to the given descriptor as in
    /// add(), and any returned descriptor must be freed manually.
    #[no_mangle]
    pub unsafe extern "C" fn _process_descriptorTableSet(
        proc: *const RustProcess,
        index: c_int,
        desc: *mut Descriptor,
    ) -> *mut Descriptor {
        let proc = unsafe { proc.as_ref().unwrap() };
        let descriptor = Descriptor::from_raw(desc);

        Worker::with_active_host(|h| {
            let proc = proc.borrow(h.root());
            let mut table = proc.descriptor_table_borrow_mut();
            match table.set(index.try_into().unwrap(), *descriptor.unwrap()) {
                Some(d) => Descriptor::into_raw(Box::new(d)),
                None => std::ptr::null_mut(),
            }
        })
        .unwrap()
    }

    /// Temporary; meant to be called from process.c.
    #[no_mangle]
    pub unsafe extern "C" fn _process_createMemoryManager(
        proc: *const RustProcess,
        native_pid: libc::pid_t,
    ) {
        let proc = unsafe { proc.as_ref().unwrap() };
        let mman = unsafe { MemoryManager::new(nix::unistd::Pid::from_raw(native_pid)) };
        Worker::with_active_host(move |h| {
            let prev = proc
                .borrow(h.root())
                .memory_manager
                .borrow_mut()
                .replace(mman);
            assert!(prev.is_none());
        })
        .unwrap();
    }

    /// Reads up to `n` bytes into `str`.
    ///
    /// Returns:
    /// strlen(str) on success.
    /// -ENAMETOOLONG if there was no NULL byte in the first `n` characters.
    /// -EFAULT if the string extends beyond the accessible address space.
    #[no_mangle]
    pub unsafe extern "C" fn _process_getReadableString(
        proc: *const RustProcess,
        plugin_src: cshadow::PluginPtr,
        n: usize,
        out_str: *mut *const c_char,
        out_strlen: *mut size_t,
    ) -> i32 {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|h| {
            let proc = proc.borrow(h.root());
            let ptr = TypedPluginPtr::new::<c_char>(plugin_src.into(), n);
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
        proc: *mut cshadow::Process,
        thread: *mut cshadow::Thread,
        addr: cshadow::PluginPtr,
        len: usize,
        prot: i32,
        flags: i32,
        fd: i32,
        offset: i64,
    ) -> cshadow::SysCallReturn {
        let proc = unsafe { cshadow::process_getRustProcess(proc).as_ref().unwrap() };
        Worker::with_active_host(|h| {
            let proc = proc.borrow(h.root());
            let mut memory_manager = proc.memory_borrow_mut();
            let mut thread = unsafe { ThreadRef::new(notnull_mut_debug(thread)) };
            memory_manager
                .do_mmap(
                    &mut thread,
                    PluginPtr::from(addr),
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
        proc: *mut cshadow::Process,
        thread: *mut cshadow::Thread,
        addr: cshadow::PluginPtr,
        len: usize,
    ) -> cshadow::SysCallReturn {
        let proc = unsafe { cshadow::process_getRustProcess(proc).as_ref().unwrap() };
        Worker::with_active_host(|h| {
            let proc = proc.borrow(h.root());
            let mut memory_manager = proc.memory_borrow_mut();
            let mut thread = unsafe { ThreadRef::new(notnull_mut_debug(thread)) };
            memory_manager
                .handle_munmap(&mut thread, PluginPtr::from(addr), len)
                .into()
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn process_handleMremap(
        proc: *mut cshadow::Process,
        thread: *mut cshadow::Thread,
        old_addr: cshadow::PluginPtr,
        old_size: usize,
        new_size: usize,
        flags: i32,
        new_addr: cshadow::PluginPtr,
    ) -> cshadow::SysCallReturn {
        let proc = unsafe { cshadow::process_getRustProcess(proc).as_ref().unwrap() };
        Worker::with_active_host(|h| {
            let proc = proc.borrow(h.root());
            let mut memory_manager = proc.memory_borrow_mut();
            let mut thread = unsafe { ThreadRef::new(notnull_mut_debug(thread)) };
            memory_manager
                .handle_mremap(
                    &mut thread,
                    PluginPtr::from(old_addr),
                    old_size,
                    new_size,
                    flags,
                    PluginPtr::from(new_addr),
                )
                .into()
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn process_handleMprotect(
        proc: *mut cshadow::Process,
        thread: *mut cshadow::Thread,
        addr: cshadow::PluginPtr,
        size: usize,
        prot: i32,
    ) -> cshadow::SysCallReturn {
        let proc = unsafe { cshadow::process_getRustProcess(proc).as_ref().unwrap() };
        Worker::with_active_host(|h| {
            let proc = proc.borrow(h.root());
            let mut memory_manager = proc.memory_borrow_mut();
            let mut thread = unsafe { ThreadRef::new(notnull_mut_debug(thread)) };
            memory_manager
                .handle_mprotect(&mut thread, PluginPtr::from(addr), size, prot)
                .into()
        })
        .unwrap()
    }

    /// Fully handles the `brk` syscall, keeping the "heap" mapped in our shared mem file.
    #[no_mangle]
    pub unsafe extern "C" fn process_handleBrk(
        proc: *mut cshadow::Process,
        thread: *mut cshadow::Thread,
        plugin_src: cshadow::PluginPtr,
    ) -> cshadow::SysCallReturn {
        let proc = unsafe { cshadow::process_getRustProcess(proc).as_ref().unwrap() };
        Worker::with_active_host(|h| {
            let proc = proc.borrow(h.root());
            let mut memory_manager = proc.memory_borrow_mut();
            let mut thread = unsafe { ThreadRef::new(notnull_mut_debug(thread)) };
            memory_manager
                .handle_brk(&mut thread, PluginPtr::from(plugin_src))
                .into()
        })
        .unwrap()
    }

    /// Initialize the MemoryMapper if it isn't already initialized. `thread` must
    /// be running and ready to make native syscalls.
    #[no_mangle]
    pub unsafe extern "C" fn process_initMapperIfNeeded(
        proc: *mut cshadow::Process,
        thread: *mut cshadow::Thread,
    ) {
        let proc = unsafe { cshadow::process_getRustProcess(proc).as_ref().unwrap() };
        Worker::with_active_host(|h| {
            let proc = proc.borrow(h.root());
            let mut memory_manager = proc.memory_borrow_mut();
            if !memory_manager.has_mapper() {
                let mut thread = unsafe { ThreadRef::new(notnull_mut_debug(thread)) };
                memory_manager.init_mapper(&mut thread)
            }
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn process_resetMemoryManager(proc: *mut cshadow::Process) {
        let proc = unsafe { cshadow::process_getRustProcess(proc).as_ref().unwrap() };
        Worker::with_active_host(|h| {
            drop(proc.borrow(h.root()).memory_manager.borrow_mut().take());
        })
        .unwrap()
    }

    /// Returns the processID that was assigned to us in process_new
    #[no_mangle]
    pub unsafe extern "C" fn _process_getProcessID(proc: *const RustProcess) -> libc::pid_t {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|h| proc.borrow(h.root()).id().try_into().unwrap()).unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_getStartTime(proc: *const RustProcess) -> CEmulatedTime {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let start_time = proc.borrow(host.root()).start_time;
            EmulatedTime::to_c_emutime(Some(start_time))
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_getStopTime(proc: *const RustProcess) -> CEmulatedTime {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let stop_time = proc.borrow(host.root()).stop_time;
            EmulatedTime::to_c_emutime(stop_time)
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_getHostId(proc: *const RustProcess) -> HostId {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).host_id()).unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_getName(proc: *const RustProcess) -> *const c_char {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).name.as_ptr()).unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_getPluginName(proc: *const RustProcess) -> *const c_char {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).plugin_name.as_ptr()).unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_getPluginPath(proc: *const RustProcess) -> *const c_char {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).plugin_path.as_ptr()).unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_outputFileName(
        proc: *const RustProcess,
        host: *const Host,
        typ: *const c_char,
        dst: *mut c_char,
        dst_len: usize,
    ) {
        let proc = unsafe { proc.as_ref().unwrap() };
        let host = unsafe { host.as_ref().unwrap() };
        let typ = unsafe { CStr::from_ptr(typ).to_str().unwrap() };
        let name = proc.borrow(host.root()).output_file_name(host, typ);
        let name = pathbuf_to_nul_term_cstring(name);
        // XXX: Is this UB? Maybe the bytes at dst are never "undefined" from
        // Rust's perspective since any initialization or lack thereof is
        // invisible through FFI?
        let dst = unsafe { std::slice::from_raw_parts_mut(dst as *mut u8, dst_len) };
        let name = name.as_bytes_with_nul();
        dst[..name.len()].copy_from_slice(name);
    }

    /// Safety:
    ///
    /// The returned pointer is invalidated when the host shmem lock is released, e.g. via
    /// Host::unlock_shmem.
    #[no_mangle]
    pub unsafe extern "C" fn _process_getSharedMem(
        proc: *const RustProcess,
    ) -> *const ShimShmemProcess {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            proc.borrow(host.root()).shim_shared_mem_block.deref() as *const _
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_getWorkingDir(proc: *const RustProcess) -> *const c_char {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).working_dir.as_ptr()).unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_straceLoggingMode(proc: *const RustProcess) -> StraceFmtMode {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).strace_logging_options())
            .unwrap()
            .into()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_straceFd(proc: *const RustProcess) -> RawFd {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| match &proc.borrow(host.root()).strace_logging {
            Some(x) => x.file.borrow().as_raw_fd(),
            None => -1,
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_isExiting(proc: *const RustProcess) -> bool {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).is_exiting.get()).unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_markAsExiting(proc: *const RustProcess) {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let proc = proc.borrow(host.root());
            proc.mark_as_exiting()
        })
        .unwrap();
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_shouldPauseForDebugging(proc: *const RustProcess) -> bool {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).pause_for_debugging).unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_getDumpable(proc: *const RustProcess) -> u32 {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).dumpable.get()).unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_setDumpable(proc: *const RustProcess, val: u32) {
        assert!(val == cshadow::SUID_DUMP_DISABLE || val == cshadow::SUID_DUMP_USER);
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).dumpable.set(val)).unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_setReturnCode(proc: *const RustProcess, val: i32) {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let prev = proc.borrow(host.root()).return_code.replace(Some(val));
            assert_eq!(prev, None);
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_didLogReturnCode(proc: *const RustProcess) -> bool {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).return_code.get().is_some())
            .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_setWasKilledByShadow(proc: *const RustProcess) {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            proc.borrow(host.root()).killed_by_shadow.set(true);
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_wasKilledByShadow(proc: *const RustProcess) -> bool {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).killed_by_shadow.get()).unwrap()
    }

    #[cfg(feature = "perf_timers")]
    #[no_mangle]
    pub unsafe extern "C" fn _process_startCpuDelayTimer(proc: *const RustProcess) {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).start_cpu_delay_timer()).unwrap()
    }

    #[cfg(feature = "perf_timers")]
    #[no_mangle]
    pub unsafe extern "C" fn _process_stopCpuDelayTimer(proc: *const RustProcess) -> f64 {
        let proc = unsafe { proc.as_ref().unwrap() };
        let delta =
            Worker::with_active_host(|host| proc.borrow(host.root()).stop_cpu_delay_timer(host))
                .unwrap();
        delta.as_nanos() as f64
    }

    #[cfg(feature = "perf_timers")]
    #[no_mangle]
    pub unsafe extern "C" fn _process_getTotalRunTime(proc: *const RustProcess) -> f64 {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).total_run_time.get().as_nanos())
            .unwrap() as f64
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_getNativePid(proc: *const RustProcess) -> libc::pid_t {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| proc.borrow(host.root()).native_pid().unwrap().as_raw())
            .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_setNativePid(proc: *const RustProcess, pid: libc::pid_t) {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            proc.borrow(host.root())
                .native_pid
                .set(Some(Pid::from_raw(pid)))
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_hasStarted(proc: *const RustProcess) -> bool {
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
    pub unsafe extern "C" fn _process_flushPtrs(proc: *const RustProcess) -> i32 {
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

    /// Frees all readable/writable plugin pointers. Unlike process_flushPtrs, any
    /// previously returned writable pointer is *not* written back. Useful
    /// if an uninitialized writable pointer was obtained via `process_getWriteablePtr`,
    /// and we end up not wanting to write anything after all (in particular, don't
    /// write back whatever garbage data was in the uninialized bueffer).
    #[no_mangle]
    pub unsafe extern "C" fn _process_freePtrsWithoutFlushing(proc: *const RustProcess) {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let proc = proc.borrow(host.root());
            proc.free_unsafe_borrows_noflush();
        })
        .unwrap();
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_threadLeader(
        proc: *const RustProcess,
    ) -> *mut cshadow::Thread {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let proc = proc.borrow(host.root());
            let tid = ThreadId::from(proc.id());
            let threads = proc.threads.borrow();
            match threads.get(&tid) {
                Some(t) => unsafe { t.borrow(host.root()).cthread() },
                None => std::ptr::null_mut(),
            }
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_getThread(
        proc: *const RustProcess,
        tid: libc::pid_t,
    ) -> *mut cshadow::Thread {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let proc = proc.borrow(host.root());
            let tid = ThreadId::try_from(tid).unwrap();
            proc.thread_borrow(tid)
                .map(|x| unsafe { x.borrow(host.root()).cthread() })
                .unwrap_or(std::ptr::null_mut())
        })
        .unwrap()
    }

    /// Inserts the thread into the process's thread list. Caller retains
    /// ownership to its reference to `thread` (i.e. this function increments the reference
    /// count).
    #[no_mangle]
    pub unsafe extern "C" fn _process_insertThread(
        proc: *const RustProcess,
        thread: *mut cshadow::Thread,
    ) {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let proc = proc.borrow(host.root());
            let thread = unsafe { ThreadRef::new(thread) };
            let tid = thread.id();
            let thread = RootedRc::new(host.root(), RootedRefCell::new(host.root(), thread));
            proc.threads.borrow_mut().insert(tid, thread);
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_isRunning(proc: *const RustProcess) -> bool {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let proc = proc.borrow(host.root());
            proc.is_running()
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_numThreads(proc: *const RustProcess) -> usize {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let proc = proc.borrow(host.root());
            let threads = proc.threads.borrow();
            threads.len()
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_interruptWithSignal(
        proc: *const RustProcess,
        host_lock: *mut ShimShmemHostLock,
        signo: i32,
    ) {
        let proc = unsafe { proc.as_ref().unwrap() };
        let host_lock = unsafe { host_lock.as_mut().unwrap() };
        Worker::with_active_host(|host| {
            let proc = proc.borrow(host.root());
            let signal = Signal::try_from(signo).unwrap();
            let host_shmem = host.shim_shmem_lock_borrow().unwrap();
            let threads = proc.threads.borrow();
            for thread in threads.values() {
                let mut thread = thread.borrow_mut(host.root());
                {
                    let thread_shmem = thread.shmem();
                    let thread_shmem_protected =
                        thread_shmem.protected.borrow_mut(&host_shmem.root);
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
                cond.wakeup_for_signal(host_lock, signal);
                break;
            }
        })
        .unwrap();
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_handleProcessExit(proc: *const RustProcess) {
        let proc = unsafe { proc.as_ref().unwrap() };

        trace!("Handling process exit");
        Worker::with_active_host(|host| {
            let proc = proc.borrow(host.root());
            proc.handle_process_exit(host)
        })
        .unwrap();
    }

    // FIXME: still needed? Time is now updated more granularly in the Thread code
    // when xferring control to/from shim.
    #[no_mangle]
    pub unsafe extern "C" fn _process_setSharedTime() {
        Worker::with_active_host(Process::set_shared_time).unwrap();
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_getPhysicalAddress(
        proc: *const RustProcess,
        vptr: cshadow::PluginVirtualPtr,
    ) -> cshadow::PluginPhysicalPtr {
        let proc = unsafe { proc.as_ref().unwrap() };

        Worker::with_active_host(|host| {
            let proc = proc.borrow(host.root());
            proc.physical_address(vptr)
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_reapThread(
        proc: *const RustProcess,
        thread: *mut cshadow::Thread,
    ) {
        let proc = unsafe { proc.as_ref().unwrap() };
        let thread = unsafe { ThreadRef::new(thread) };
        Worker::with_active_host(|host| {
            let proc = proc.borrow(host.root());
            proc.reap_thread(host, thread.id())
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_terminate(proc: *const RustProcess) {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let proc = proc.borrow(host.root());
            proc.terminate(host)
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_getAndLogReturnCode(proc: *const RustProcess) {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let proc = proc.borrow(host.root());
            proc.get_and_log_return_code(host)
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_check(proc: *const RustProcess) {
        let proc = unsafe { proc.as_ref().unwrap() };
        Worker::with_active_host(|host| {
            let proc = proc.borrow(host.root());
            proc.check(host)
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn _process_checkThread(
        proc: *const RustProcess,
        thread: *mut cshadow::Thread,
    ) {
        let proc = unsafe { proc.as_ref().unwrap() };
        let thread = unsafe { ThreadRef::new(thread) };
        Worker::with_active_host(|host| {
            let proc = proc.borrow(host.root());
            proc.check_thread(host, thread.id())
        })
        .unwrap()
    }

    /// Adds a new thread to the process and schedules it to run.
    /// Intended for use by `clone`.
    ///
    /// Takes ownership of `thread`.
    #[no_mangle]
    pub unsafe extern "C" fn _process_addThread(
        proc: *const RustProcess,
        thread: *mut cshadow::Thread,
    ) {
        let proc = unsafe { proc.as_ref().unwrap() };
        let thread = unsafe { ThreadRef::new(thread) };
        // Since we're taking ownership, remove the caller's reference to the thread.
        unsafe { cshadow::thread_unref(thread.cthread()) };
        Worker::with_active_host(|host| {
            let proc = proc.borrow(host.root());
            proc.add_thread(host, thread)
        })
        .unwrap()
    }
}
