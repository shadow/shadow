//! A thread of a managed process.
//!
//! This contains the code where the simulator can create or communicate with a managed process.

use std::cell::{Cell, RefCell};
use std::ffi::{CStr, CString};
use std::io::Write;
use std::os::fd::AsRawFd;
use std::os::unix::prelude::OsStrExt;
use std::path::PathBuf;
use std::sync::{Arc, atomic};

use linux_api::errno::Errno;
use linux_api::posix_types::Pid;
use linux_api::sched::CloneFlags;
use linux_api::signal::tgkill;
use log::{Level, debug, error, log_enabled, trace};
use rand::Rng as _;
use rustix::pipe::PipeFlags;
use rustix::process::WaitOptions;
use shadow_shim_helper_rs::ipc::IPCData;
use shadow_shim_helper_rs::shim_event::{
    ShimEventAddThreadReq, ShimEventAddThreadRes, ShimEventStartRes, ShimEventSyscall,
    ShimEventSyscallComplete, ShimEventToShadow, ShimEventToShim,
};
use shadow_shim_helper_rs::syscall_types::{ForeignPtr, SyscallArgs, SyscallReg};
use shadow_shmem::allocator::ShMemBlock;
use vasi_sync::scchannel::SelfContainedChannelError;

use super::context::ThreadContext;
use super::host::Host;
use super::syscall::condition::SyscallCondition;
use crate::core::worker::{WORKER_SHARED, Worker};
use crate::cshadow;
use crate::host::syscall::handler::SyscallHandler;
use crate::host::syscall::types::{ForeignArrayPtr, SyscallReturn};
use crate::utility::{VerifyPluginPathError, inject_preloads, syscall, verify_plugin_path};

/// The ManagedThread's state after having been allowed to execute some code.
#[derive(Debug)]
#[must_use]
pub enum ResumeResult {
    /// Blocked on a SyscallCondition.
    Blocked(SyscallCondition),
    /// The native thread has exited with the given code.
    ExitedThread(i32),
    /// The thread's process has exited.
    ExitedProcess,
}

pub struct ManagedThread {
    ipc_shmem: Arc<ShMemBlock<'static, IPCData>>,
    is_running: Cell<bool>,
    return_code: Cell<Option<i32>>,

    /* holds the event for the most recent call from the plugin/shim */
    current_event: RefCell<ShimEventToShadow>,

    native_pid: linux_api::posix_types::Pid,
    native_tid: linux_api::posix_types::Pid,

    // Value storing the current CPU affinity of the thread (more precisely,
    // of the native thread backing this thread object). This value will be set
    // to AFFINITY_UNINIT if CPU pinning is not enabled or if the thread has
    // not yet been pinned to a CPU.
    affinity: Cell<i32>,
}

impl ManagedThread {
    pub fn native_pid(&self) -> linux_api::posix_types::Pid {
        self.native_pid
    }

    pub fn native_tid(&self) -> linux_api::posix_types::Pid {
        self.native_tid
    }

    /// Make the specified syscall on the native thread.
    ///
    /// Panics if the native thread is dead or dies during the syscall,
    /// including if the syscall itself is SYS_exit or SYS_exit_group.
    pub fn native_syscall(&self, ctx: &ThreadContext, n: i64, args: &[SyscallReg]) -> SyscallReg {
        let mut syscall_args = SyscallArgs {
            number: n,
            args: [SyscallReg::from(0u64); 6],
        };
        syscall_args.args[..args.len()].copy_from_slice(args);
        match self.continue_plugin(
            ctx.host,
            &ShimEventToShim::Syscall(ShimEventSyscall { syscall_args }),
        ) {
            ShimEventToShadow::SyscallComplete(res) => res.retval,
            other => panic!("Unexpected response from plugin: {other:?}"),
        }
    }

    pub fn spawn(
        plugin_path: &CStr,
        argv: Vec<CString>,
        envv: Vec<CString>,
        strace_file: Option<&std::fs::File>,
        log_file: &std::fs::File,
        injected_preloads: &[PathBuf],
    ) -> Result<Self, Errno> {
        debug!(
            "spawning new mthread '{plugin_path:?}' with environment '{envv:?}', arguments '{argv:?}'"
        );

        let envv = inject_preloads(envv, injected_preloads);

        debug!("env after preload injection: {envv:?}");

        let ipc_shmem = Arc::new(shadow_shmem::allocator::shmalloc(IPCData::new()));

        let child_pid =
            Self::spawn_native(plugin_path, argv, envv, strace_file, log_file, &ipc_shmem)?;

        // In Linux, the PID is equal to the TID of its first thread.
        let native_pid = child_pid;
        let native_tid = child_pid;

        // Configure the child_pid_watcher to close the IPC channel when the child dies.
        {
            let worker = WORKER_SHARED.borrow();
            let watcher = worker.as_ref().unwrap().child_pid_watcher();

            watcher.register_pid(child_pid);
            let ipc = ipc_shmem.clone();
            watcher.register_callback(child_pid, move |_pid| {
                ipc.from_plugin().close_writer();
            })
        };

        trace!(
            "waiting for start event from shim with native pid {:?}",
            native_pid
        );
        let start_req = ipc_shmem.from_plugin().receive().unwrap();
        match &start_req {
            ShimEventToShadow::StartReq(_) => {
                // Expected result; shim is ready to initialize.
            }
            ShimEventToShadow::ProcessDeath => {
                // The process died before initializing the shim.
                //
                // Reap the dead process and return an error.
                let status =
                    rustix::process::waitpid(Some(native_pid.into()), WaitOptions::empty())
                        .unwrap()
                        .unwrap();
                if status.exit_status() == Some(127) {
                    // posix_spawn(3):
                    // > If  the child  fails  in  any  of the
                    // > housekeeping steps described below, or fails to
                    // > execute the desired file, it exits with a status of
                    // > 127.
                    debug!("posix_spawn failed to exec the process");
                    // Assume that execve failed, and return a plausible reason
                    // why it might have done so.
                    // TODO: replace our usage of posix_spawn with a custom
                    // implementation that can return the execve failure code?
                    return Err(Errno::EPERM);
                }
                // TODO: handle more gracefully.
                // * The native stdout/stderr might have a clue as to
                // why the process died.  Consider logging a hint to
                // check it (currently in the corresponding shimlog), or
                // directly capture it and display it here.
                // https://github.com/shadow/shadow/issues/3142
                // * Consider logging a warning here and continuing on to handle
                // the managed process exit normally. e.g. when this happens
                // as part of an emulated `execve`, we might want to continue
                // the simulation.
                panic!("Child process died unexpectedly before initialization: {status:?}");
            }
            other => panic!("Unexpected result from shim: {other:?}"),
        };

        Ok(Self {
            ipc_shmem,
            is_running: Cell::new(true),
            return_code: Cell::new(None),
            current_event: RefCell::new(start_req),
            native_pid,
            native_tid,
            affinity: Cell::new(cshadow::AFFINITY_UNINIT),
        })
    }

    pub fn resume(
        &self,
        ctx: &ThreadContext,
        syscall_handler: &mut SyscallHandler,
    ) -> ResumeResult {
        debug_assert!(self.is_running());

        self.sync_affinity_with_worker();

        // Flush any pending writes, e.g. from a previous mthread that exited
        // without flushing.
        ctx.process.free_unsafe_borrows_flush().unwrap();

        loop {
            let mut current_event = self.current_event.borrow_mut();
            let last_event = *current_event;
            *current_event = match last_event {
                ShimEventToShadow::StartReq(start_req) => {
                    // Write the serialized thread shmem handle directly to shim
                    // memory.
                    ctx.process
                        .memory_borrow_mut()
                        .write(
                            start_req.thread_shmem_block_to_init,
                            &ctx.thread.shmem().serialize(),
                        )
                        .unwrap();

                    if !start_req.process_shmem_block_to_init.is_null() {
                        // Write the serialized process shmem handle directly to
                        // shim memory.
                        ctx.process
                            .memory_borrow_mut()
                            .write(
                                start_req.process_shmem_block_to_init,
                                &ctx.process.shmem().serialize(),
                            )
                            .unwrap();
                    }

                    if !start_req.initial_working_dir_to_init.is_null() {
                        // Write the working dir.
                        let mut mem = ctx.process.memory_borrow_mut();
                        let mut writer = mem.writer(ForeignArrayPtr::new(
                            start_req.initial_working_dir_to_init,
                            start_req.initial_working_dir_to_init_len,
                        ));
                        writer
                            .write_all(ctx.process.current_working_dir().to_bytes_with_nul())
                            .unwrap();
                        writer.flush().unwrap();
                    }

                    // send the message to the shim to call main().
                    trace!("sending start event code to shim");
                    self.continue_plugin(
                        ctx.host,
                        &ShimEventToShim::StartRes(ShimEventStartRes {
                            aux_at_random: ctx.host.random_mut().random(),
                        }),
                    )
                }
                ShimEventToShadow::ProcessDeath => {
                    // The native threads are all dead or zombies. Nothing to do but
                    // clean up.
                    self.cleanup_after_exit_initiated();
                    return ResumeResult::ExitedProcess;
                }
                ShimEventToShadow::Syscall(syscall) => {
                    // Emulate the given syscall.

                    // `exit` is tricky since it only exits the *mthread*, and we don't have a way
                    // to be notified that the mthread has exited. We have to "fire and forget"
                    // the command to execute the syscall natively.
                    //
                    // TODO: We could use a tid futex in shared memory, as set by
                    // `set_tid_address`, to block here until the thread has
                    // actually exited.
                    if syscall.syscall_args.number == libc::SYS_exit {
                        let return_code = syscall.syscall_args.args[0].into();
                        debug!("Short-circuiting syscall exit({return_code})");
                        self.return_code.set(Some(return_code));
                        // Tell mthread to go ahead and make the exit syscall itself.
                        // We *don't* call `_managedthread_continuePlugin` here,
                        // since that'd release the ShimSharedMemHostLock, and we
                        // aren't going to get a message back to know when it'd be
                        // safe to take it again.
                        self.ipc_shmem
                            .to_plugin()
                            .send(ShimEventToShim::SyscallDoNative);
                        self.cleanup_after_exit_initiated();
                        return ResumeResult::ExitedThread(return_code);
                    }

                    let scr = syscall_handler.syscall(ctx, &syscall.syscall_args).into();

                    // remove the mthread's old syscall condition since it's no longer needed
                    ctx.thread.cleanup_syscall_condition();

                    assert!(self.is_running());

                    // Flush any writes that legacy C syscallhandlers may have
                    // made.
                    ctx.process.free_unsafe_borrows_flush().unwrap();

                    match scr {
                        SyscallReturn::Block(b) => {
                            return ResumeResult::Blocked(unsafe {
                                SyscallCondition::consume_from_c(b.cond)
                            });
                        }
                        SyscallReturn::Done(d) => self.continue_plugin(
                            ctx.host,
                            &ShimEventToShim::SyscallComplete(ShimEventSyscallComplete {
                                retval: d.retval,
                                restartable: d.restartable,
                            }),
                        ),
                        SyscallReturn::Native => {
                            self.continue_plugin(ctx.host, &ShimEventToShim::SyscallDoNative)
                        }
                    }
                }
                ShimEventToShadow::AddThreadRes(res) => {
                    // We get here in the child process after forking.

                    // Child should have gotten 0 back from its native clone syscall.
                    assert_eq!(res.clone_res, 0);

                    // Complete the virtualized clone syscall.
                    self.continue_plugin(
                        ctx.host,
                        &ShimEventToShim::SyscallComplete(ShimEventSyscallComplete {
                            retval: 0.into(),
                            restartable: false,
                        }),
                    )
                }
                e @ ShimEventToShadow::SyscallComplete(_) => panic!("Unexpected event: {e:?}"),
            };
            assert!(self.is_running());
        }
    }

    pub fn handle_process_exit(&self) {
        // TODO: Only do this once per process; maybe by moving into `Process`.
        WORKER_SHARED
            .borrow()
            .as_ref()
            .unwrap()
            .child_pid_watcher()
            .unregister_pid(self.native_pid());

        self.cleanup_after_exit_initiated();
    }

    pub fn return_code(&self) -> Option<i32> {
        self.return_code.get()
    }

    pub fn is_running(&self) -> bool {
        self.is_running.get()
    }

    /// Execute the specified `clone` syscall in `self`, and use create a new
    /// `ManagedThread` object to manage it. The new thread will be managed
    /// by Shadow, and suitable for use with `Thread::wrap_mthread`.
    ///
    /// If the `clone` syscall fails, the native error is returned.
    pub fn native_clone(
        &self,
        ctx: &ThreadContext,
        flags: CloneFlags,
        child_stack: ForeignPtr<()>,
        ptid: ForeignPtr<libc::pid_t>,
        ctid: ForeignPtr<libc::pid_t>,
        newtls: libc::c_ulong,
    ) -> Result<ManagedThread, linux_api::errno::Errno> {
        let child_ipc_shmem = Arc::new(shadow_shmem::allocator::shmalloc(IPCData::new()));

        // Send the IPC block for the new mthread to use.
        let clone_res: i64 = match self.continue_plugin(
            ctx.host,
            &ShimEventToShim::AddThreadReq(ShimEventAddThreadReq {
                ipc_block: child_ipc_shmem.serialize(),
                flags: flags.bits(),
                child_stack,
                ptid: ptid.cast::<()>(),
                ctid: ctid.cast::<()>(),
                newtls,
            }),
        ) {
            ShimEventToShadow::AddThreadRes(ShimEventAddThreadRes { clone_res }) => clone_res,
            r => panic!("Unexpected result: {r:?}"),
        };
        let clone_res: SyscallReg = syscall::raw_return_value_to_result(clone_res)?;
        let child_native_tid = Pid::from_raw(libc::pid_t::from(clone_res)).unwrap();
        trace!("native clone treated tid {child_native_tid:?}");

        trace!(
            "waiting for start event from shim with native tid {:?}",
            child_native_tid
        );
        let start_req = child_ipc_shmem.from_plugin().receive().unwrap();
        match &start_req {
            ShimEventToShadow::StartReq(_) => (),
            other => panic!("Unexpected result from shim: {other:?}"),
        };

        let native_pid = if flags.contains(CloneFlags::CLONE_THREAD) {
            self.native_pid
        } else {
            child_native_tid
        };

        if !flags.contains(CloneFlags::CLONE_THREAD) {
            // Child is a new process; register it.
            WORKER_SHARED
                .borrow()
                .as_ref()
                .unwrap()
                .child_pid_watcher()
                .register_pid(native_pid);
        }

        // Register the child thread's IPC block with the ChildPidWatcher.
        {
            let child_ipc_shmem = child_ipc_shmem.clone();
            WORKER_SHARED
                .borrow()
                .as_ref()
                .unwrap()
                .child_pid_watcher()
                .register_callback(native_pid, move |_pid| {
                    child_ipc_shmem.from_plugin().close_writer();
                })
        };

        Ok(Self {
            ipc_shmem: child_ipc_shmem,
            is_running: Cell::new(true),
            return_code: Cell::new(None),
            current_event: RefCell::new(start_req),
            native_pid,
            native_tid: child_native_tid,
            // TODO: can we assume it's inherited from the current thread affinity?
            affinity: Cell::new(cshadow::AFFINITY_UNINIT),
        })
    }

    #[must_use]
    fn continue_plugin(&self, host: &Host, event: &ShimEventToShim) -> ShimEventToShadow {
        // Update shared state before transferring control.
        host.shim_shmem_lock_borrow_mut().unwrap().max_runahead_time =
            Worker::max_event_runahead_time(host);
        host.shim_shmem()
            .sim_time
            .store(Worker::current_time().unwrap(), atomic::Ordering::Relaxed);

        // Release lock so that plugin can take it. Reacquired in `wait_for_next_event`.
        host.unlock_shmem();

        self.ipc_shmem.to_plugin().send(*event);

        let event = match self.ipc_shmem.from_plugin().receive() {
            Ok(e) => e,
            Err(SelfContainedChannelError::WriterIsClosed) => ShimEventToShadow::ProcessDeath,
        };

        // Reacquire the shared memory lock, now that the shim has yielded control
        // back to us.
        host.lock_shmem();

        // Update time, which may have been incremented in the shim.
        let shim_time = host.shim_shmem().sim_time.load(atomic::Ordering::Relaxed);
        if log_enabled!(Level::Trace) {
            let worker_time = Worker::current_time().unwrap();
            if shim_time != worker_time {
                trace!(
                    "Updating time from {worker_time:?} to {shim_time:?} (+{:?})",
                    shim_time - worker_time
                );
            }
        }
        Worker::set_current_time(shim_time);

        event
    }

    /// To be called after we expect the native thread to have exited, or to
    /// exit imminently.
    fn cleanup_after_exit_initiated(&self) {
        if !self.is_running.get() {
            return;
        }
        self.wait_for_native_exit();
        trace!("child {:?} exited", self.native_tid());
        self.is_running.set(false);
    }

    /// Wait until the managed thread is no longer running.
    fn wait_for_native_exit(&self) {
        let native_pid = self.native_pid();
        let native_tid = self.native_tid();

        // We use `tgkill` and `/proc/x/stat` to detect whether the thread is still running,
        // looping until it doesn't.
        //
        // Alternatively we could use `set_tid_address` or `set_robust_list` to
        // be notified on a futex. Those are a bit underdocumented and fragile,
        // though. In practice this shouldn't have to loop significantly.
        trace!("Waiting for native thread {native_pid:?}.{native_tid:?} to exit");
        loop {
            if self.ipc_shmem.from_plugin().writer_is_closed() {
                // This indicates that the whole process has stopped executing;
                // no need to poll the individual thread.
                break;
            }
            match tgkill(native_pid, native_tid, None) {
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
                    let filename = format!("/proc/{}/stat", native_pid.as_raw_nonzero().get());
                    let stat = match std::fs::read_to_string(filename) {
                        Err(e) => {
                            assert!(e.kind() == std::io::ErrorKind::NotFound);
                            trace!("tgl {native_pid:?} is fully dead");
                            break;
                        }
                        Ok(s) => s,
                    };
                    if stat.contains(") Z") {
                        trace!("tgl {native_pid:?} is a zombie");
                        break;
                    }
                    // Still alive and in a non-zombie state; continue
                }
                Ok(()) => {
                    // Thread is still alive; continue.
                }
            };
            std::thread::yield_now();
        }
    }

    fn sync_affinity_with_worker(&self) {
        let current_affinity = scheduler::core_affinity()
            .map(|x| i32::try_from(x).unwrap())
            .unwrap_or(cshadow::AFFINITY_UNINIT);
        self.affinity.set(unsafe {
            cshadow::affinity_setProcessAffinity(
                self.native_tid().as_raw_nonzero().get(),
                current_affinity,
                self.affinity.get(),
            )
        });
    }

    fn spawn_native(
        plugin_path: &CStr,
        argv: Vec<CString>,
        envv: Vec<CString>,
        strace_file: Option<&std::fs::File>,
        shimlog_file: &std::fs::File,
        shmem_block: &ShMemBlock<IPCData>,
    ) -> Result<Pid, Errno> {
        // Preemptively check for likely reasons that execve might fail.
        // In particular we want to ensure that we  don't launch a statically
        // linked executable, since we'd then deadlock the whole simulation
        // waiting for the plugin to initialize.
        //
        // This is also helpful since we can't retrieve specific `execve` errors
        // through `posix_spawn`.
        fn map_verify_err(e: VerifyPluginPathError) -> Errno {
            match e {
                // execve(2): ENOENT The file pathname [...] does not exist.
                VerifyPluginPathError::NotFound => Errno::ENOENT,
                // execve(2): EACCES The file or a script interpreter is not a regular file.
                VerifyPluginPathError::NotFile => Errno::EACCES,
                // execve(2): EACCES Execute permission is denied for the file or a script or ELF interpreter.
                VerifyPluginPathError::NotExecutable => Errno::EACCES,
                // execve(2): ENOEXEC An executable is not in a recognized
                // format, is for the wrong architecture, or has some other
                // format error that means it cannot be executed.
                VerifyPluginPathError::UnknownFileType => Errno::ENOEXEC,
                VerifyPluginPathError::NotDynamicallyLinkedElf => Errno::ENOEXEC,
                VerifyPluginPathError::IncompatibleInterpreter(e) => map_verify_err(*e),
                // execve(2): EACCES Search permission is denied on a component
                // of the path prefix of pathname or the name of a script
                // interpreter.
                VerifyPluginPathError::PathPermissionDenied => Errno::EACCES,
                VerifyPluginPathError::UnhandledIoError(_) => {
                    // Arbitrary error that should be handled by callers.
                    Errno::ENOEXEC
                }
            }
        }
        verify_plugin_path(std::ffi::OsStr::from_bytes(plugin_path.to_bytes()))
            .map_err(map_verify_err)?;

        // posix_spawn is documented as taking pointers to *mutable* char for argv and
        // envv. It *probably* doesn't actually mutate them, but we
        // conservatively give it what it asks for. We have to "reconstitute"
        // the CString's after the fork + exec to deallocate them.
        let argv_ptrs: Vec<*mut i8> = argv
            .into_iter()
            .map(CString::into_raw)
            // the last element of argv must be NULL
            .chain(std::iter::once(std::ptr::null_mut()))
            .collect();
        let envv_ptrs: Vec<*mut i8> = envv
            .into_iter()
            .map(CString::into_raw)
            // the last element of argv must be NULL
            .chain(std::iter::once(std::ptr::null_mut()))
            .collect();

        let mut file_actions: libc::posix_spawn_file_actions_t = shadow_pod::zeroed();
        Errno::result_from_libc_errnum(unsafe {
            libc::posix_spawn_file_actions_init(&mut file_actions)
        })
        .unwrap();

        // Set up stdin
        let (stdin_reader, stdin_writer) = rustix::pipe::pipe_with(PipeFlags::CLOEXEC).unwrap();
        Errno::result_from_libc_errnum(unsafe {
            libc::posix_spawn_file_actions_adddup2(
                &mut file_actions,
                stdin_reader.as_raw_fd(),
                libc::STDIN_FILENO,
            )
        })
        .unwrap();

        // Dup straceFd; the dup'd descriptor won't have O_CLOEXEC set.
        //
        // Since dup2 is a no-op when the new and old file descriptors are equal, we have
        // to arrange to call dup2 twice - first to a temporary descriptor, and then back
        // to the original descriptor number.
        //
        // Here we use STDOUT_FILENO as the temporary descriptor, since we later
        // replace that below.
        //
        // Once we drop support for platforms with glibc older than 2.29, we *could*
        // consider taking advantage of a new feature that would let us just use a
        // single `posix_spawn_file_actions_adddup2` call with equal descriptors.
        // OTOH it's a non-standard extension, and I think ultimately uses the same
        // number of syscalls, so it might be better to continue using this slightly
        // more awkward method anyway.
        // https://github.com/bminor/glibc/commit/805334b26c7e6e83557234f2008497c72176a6cd
        // https://austingroupbugs.net/view.php?id=411
        if let Some(strace_file) = strace_file {
            Errno::result_from_libc_errnum(unsafe {
                libc::posix_spawn_file_actions_adddup2(
                    &mut file_actions,
                    strace_file.as_raw_fd(),
                    libc::STDOUT_FILENO,
                )
            })
            .unwrap();
            Errno::result_from_libc_errnum(unsafe {
                libc::posix_spawn_file_actions_adddup2(
                    &mut file_actions,
                    libc::STDOUT_FILENO,
                    strace_file.as_raw_fd(),
                )
            })
            .unwrap();
        }

        // set stdout/stderr as the shim log. This also clears the FD_CLOEXEC flag.
        Errno::result_from_libc_errnum(unsafe {
            libc::posix_spawn_file_actions_adddup2(
                &mut file_actions,
                shimlog_file.as_raw_fd(),
                libc::STDOUT_FILENO,
            )
        })
        .unwrap();
        Errno::result_from_libc_errnum(unsafe {
            libc::posix_spawn_file_actions_adddup2(
                &mut file_actions,
                shimlog_file.as_raw_fd(),
                libc::STDERR_FILENO,
            )
        })
        .unwrap();

        let mut spawn_attr: libc::posix_spawnattr_t = shadow_pod::zeroed();
        Errno::result_from_libc_errnum(unsafe { libc::posix_spawnattr_init(&mut spawn_attr) })
            .unwrap();

        // In versions of glibc before 2.24, we need this to tell posix_spawn
        // to use vfork instead of fork. In later versions it's a no-op.
        Errno::result_from_libc_errnum(unsafe {
            libc::posix_spawnattr_setflags(
                &mut spawn_attr,
                libc::POSIX_SPAWN_USEVFORK.try_into().unwrap(),
            )
        })
        .unwrap();

        let child_pid_res = {
            let mut child_pid = -1;
            Errno::result_from_libc_errnum(unsafe {
                libc::posix_spawn(
                    &mut child_pid,
                    plugin_path.as_ptr(),
                    &file_actions,
                    &spawn_attr,
                    argv_ptrs.as_ptr(),
                    envv_ptrs.as_ptr(),
                )
            })
            .map(|_| Pid::from_raw(child_pid).unwrap_or_else(|| panic!("Invalid pid: {child_pid}")))
        };

        // Write the serialized shmem descriptor to the stdin pipe. The pipe
        // buffer should be large enough that we can write it all without having
        // to wait for data to be read.
        if child_pid_res.is_ok() {
            // we avoid using the rustix write wrapper here, since we can't guarantee
            // that all bytes of the serialized shmem block are initd, and hence
            // can't safely construct the &[u8] that it wants.
            let serialized = shmem_block.serialize();
            let serialized_bytes = shadow_pod::as_u8_slice(&serialized);
            let written = Errno::result_from_libc_errno(-1, unsafe {
                libc::write(
                    stdin_writer.as_raw_fd(),
                    serialized_bytes.as_ptr().cast(),
                    serialized_bytes.len(),
                )
            })
            .unwrap();
            // TODO: loop if needed. Shouldn't be in practice, though.
            assert_eq!(written, isize::try_from(serialized_bytes.len()).unwrap());
        }

        Errno::result_from_libc_errnum(unsafe {
            libc::posix_spawn_file_actions_destroy(&mut file_actions)
        })
        .unwrap();
        Errno::result_from_libc_errnum(unsafe { libc::posix_spawnattr_destroy(&mut spawn_attr) })
            .unwrap();

        // Drop the cloned argv and env.
        drop(
            argv_ptrs
                .into_iter()
                .filter(|p| !p.is_null())
                .map(|p| unsafe { CString::from_raw(p) }),
        );
        drop(
            envv_ptrs
                .into_iter()
                .filter(|p| !p.is_null())
                .map(|p| unsafe { CString::from_raw(p) }),
        );

        debug!(
            "starting process {}, result: {child_pid_res:?}",
            plugin_path.to_str().unwrap()
        );

        child_pid_res
    }

    /// `ManagedThread` panics if dropped while the underlying process is still running,
    /// since otherwise that process could continue writing to shared memory regions
    /// that shadow reallocates.
    ///
    /// This method kills the process that `self` belongs to (not just the
    /// thread!) and then drops `self`.
    pub fn kill_and_drop(self) {
        if let Err(err) =
            rustix::process::kill_process(self.native_pid().into(), rustix::process::Signal::Kill)
        {
            log::warn!(
                "Couldn't kill managed process {:?}. kill: {:?}",
                self.native_pid(),
                err
            );
        }
        self.handle_process_exit();
    }
}

impl Drop for ManagedThread {
    fn drop(&mut self) {
        // Dropping while the thread is running is unsound because the running
        // thread still has access to shared memory regions that will be
        // deallocated, and potentially reallocated for another purpose. The
        // running thread accessing a deallocated or repurposed memory region
        // can cause numerous problems.
        assert!(!self.is_running());
    }
}
