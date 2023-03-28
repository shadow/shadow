use std::cell::{Cell, RefCell};
use std::ffi::{CStr, CString};
use std::fs::File;
use std::os::fd::{FromRawFd, RawFd};
use std::sync::{atomic, Arc};

use log::{debug, info, log_enabled, trace, Level};
use nix::errno::Errno;
use nix::fcntl::OFlag;
use nix::sys::stat::Mode;
use nix::unistd::getpid;
use shadow_shim_helper_rs::ipc::IPCData;
use shadow_shim_helper_rs::shim_event::{
    ShimEvent, ShimEventAddThreadReq, ShimEventSyscall, ShimEventSyscallComplete,
};
use shadow_shim_helper_rs::syscall_types::{ForeignPtr, SysCallArgs, SysCallReg};
use shadow_shmem::allocator::ShMemBlock;
use vasi_sync::scchannel::SelfContainedChannelError;

use super::host::Host;
use super::process::Process;
use super::syscall_condition::SysCallCondition;
use super::thread::Thread;
use crate::core::scheduler;
use crate::core::worker::{Worker, WORKER_SHARED};
use crate::cshadow;
use crate::host::syscall_types::SyscallReturn;
use crate::utility::{childpid_watcher, pod, syscall};

pub struct ManagedThread {
    ipc_shmem: Arc<ShMemBlock<'static, IPCData>>,
    is_running: Cell<bool>,
    return_code: Cell<Option<i32>>,

    /* holds the event for the most recent call from the plugin/shim */
    current_event: RefCell<ShimEvent>,

    notification_handle: Option<childpid_watcher::WatchHandle>,

    native_pid: Option<nix::unistd::Pid>,
    native_tid: Option<nix::unistd::Pid>,

    // Value storing the current CPU affinity of the thread (more precisely,
    // of the native thread backing this thread object). This value will be set
    // to AFFINITY_UNINIT if CPU pinning is not enabled or if the thread has
    // not yet been pinned to a CPU.
    affinity: Cell<i32>,
}

impl ManagedThread {
    /// Create a new `ManagedThread`.
    // While this currently doesn't take any arguments, it wouldn't be
    // surprising if we end up needing to add some.
    #[allow(clippy::new_without_default)]
    pub fn new() -> Self {
        let ipc_shmem =
            Arc::new(shadow_shmem::allocator::Allocator::global().alloc(IPCData::new()));
        Self {
            ipc_shmem,
            is_running: Cell::new(false),
            return_code: Cell::new(None),
            current_event: RefCell::new(ShimEvent::Null),
            notification_handle: None,
            native_pid: None,
            native_tid: None,
            affinity: Cell::new(cshadow::AFFINITY_UNINIT),
        }
    }

    pub fn native_pid(&self) -> nix::unistd::Pid {
        self.native_pid.unwrap()
    }

    pub fn native_tid(&self) -> nix::unistd::Pid {
        self.native_tid.unwrap()
    }

    pub fn native_syscall(
        &self,
        host: &Host,
        process: &Process,
        n: i64,
        args: &[SysCallReg],
    ) -> SysCallReg {
        {
            let mut syscall_args = SysCallArgs {
                number: n,
                args: [SysCallReg::from(0u64); 6],
            };
            syscall_args.args[..args.len()].copy_from_slice(args);
            self.continue_plugin(host, &ShimEvent::Syscall(ShimEventSyscall { syscall_args }));
        }

        match self.wait_for_next_event(host) {
            Ok(ShimEvent::SyscallComplete(res)) => res.retval,
            Err(SelfContainedChannelError::WriterIsClosed) => {
                trace!("Plugin exited while executing native syscall {n}");
                process.mark_as_exiting();
                self.cleanup();
                SysCallReg::from(-(Errno::ESRCH as i64))
            }
            other => {
                panic!("Unexpected response from plugin: {other:?}");
            }
        }
    }

    pub fn run(
        &mut self,
        host: &Host,
        plugin_path: &CStr,
        argv: Vec<CString>,
        mut envv: Vec<CString>,
        working_dir: &CStr,
        strace_fd: Option<RawFd>,
        log_path: &CStr,
    ) {
        envv.push(
            CString::new(format!(
                "SHADOW_IPC_BLK={}",
                self.ipc_shmem.serialize().encode_to_string()
            ))
            .unwrap(),
        );
        envv.push(CString::new(format!("SHADOW_PID={}", getpid().as_raw())).unwrap());
        envv.push(CString::new(format!("SHADOW_TSC_HZ={}", host.tsc().cyclesPerSecond)).unwrap());

        info!("forking new mthread with environment '{envv:?}', arguments '{argv:?}', and working directory '{working_dir:?}'");

        let shimlog_fd = nix::fcntl::open(
            log_path,
            OFlag::O_WRONLY | OFlag::O_CREAT | OFlag::O_CLOEXEC,
            Mode::S_IRUSR | Mode::S_IWUSR | Mode::S_IRGRP | Mode::S_IROTH | Mode::S_IWOTH,
        )
        .unwrap();

        let child_pid = self.spawn(plugin_path, argv, envv, working_dir, strace_fd, shimlog_fd);

        // should be opened in the shim, so no need for it anymore
        nix::unistd::close(shimlog_fd).unwrap();

        // In Linux, the PID is equal to the TID of its first thread.
        self.native_pid = Some(child_pid);
        self.native_tid = Some(child_pid);

        // Configure the child_pid_watcher to close the IPC channel when the child dies.
        let handle = {
            let ipc = self.ipc_shmem.clone();
            WORKER_SHARED
                .borrow()
                .as_ref()
                .unwrap()
                .child_pid_watcher()
                .register_callback(child_pid, move |_pid| {
                    ipc.from_plugin().close_writer();
                })
        };
        self.notification_handle = Some(handle);

        // When `continue`d we'll tell the plugin to start executing.
        *self.current_event.get_mut() = ShimEvent::Start;

        self.is_running.set(true);
    }

    pub fn resume(
        &self,
        thread: &Thread,
        process: &Process,
        host: &Host,
    ) -> Option<SysCallCondition> {
        debug_assert!(self.is_running());

        self.sync_affinity_with_worker();

        // Flush any pending writes, e.g. from a previous mthread that exited
        // without flushing.
        process.free_unsafe_borrows_flush().unwrap();

        loop {
            match *self.current_event.borrow() {
                e @ ShimEvent::Null => panic!("Unexpected event {e:?}"),
                e @ ShimEvent::Start => {
                    // send the message to the shim to call main().
                    // The plugin will run until it makes a blocking call.
                    trace!("sending start event code to {}", self.native_pid.unwrap());
                    self.continue_plugin(host, &e);
                }
                ShimEvent::ProcessDeath => {
                    // The native threads are all dead or zombies. Nothing to do but
                    // clean up.
                    process.mark_as_exiting();
                    self.cleanup();
                    return None;
                }
                ShimEvent::Syscall(syscall) => {
                    // Emulate the given syscall.

                    // `exit` is tricky since it only exits the *mthread*, and we don't have a way
                    // to be notified that the mthread has exited. We have to "fire and forget"
                    // the command to execute the syscall natively.
                    //
                    // TODO: We could use a tid futex in shared memory, as set by
                    // `set_tid_address`, to block here until the thread has
                    // actually exited.
                    if syscall.syscall_args.number == libc::SYS_exit {
                        self.return_code
                            .set(Some(syscall.syscall_args.args[0].into()));
                        // Tell mthread to go ahead and make the exit syscall itself.
                        // We *don't* call `_managedthread_continuePlugin` here,
                        // since that'd release the ShimSharedMemHostLock, and we
                        // aren't going to get a message back to know when it'd be
                        // safe to take it again.
                        self.ipc_shmem.to_plugin().send(ShimEvent::SyscallDoNative);
                        self.cleanup();
                        return None;
                    }

                    let scr = unsafe {
                        cshadow::syscallhandler_make_syscall(
                            thread.csyscallhandler(),
                            &syscall.syscall_args,
                        )
                    };

                    // remove the mthread's old syscall condition since it's no longer needed
                    thread.cleanup_syscall_condition();

                    if !self.is_running() {
                        return None;
                    }

                    // Flush any writes that legacy C syscallhandlers may have
                    // made.
                    process.free_unsafe_borrows_flush().unwrap();

                    match scr {
                        SyscallReturn::Block(b) => {
                            return Some(unsafe { SysCallCondition::consume_from_c(b.cond) })
                        }
                        SyscallReturn::Done(d) => self.continue_plugin(
                            host,
                            &ShimEvent::SyscallComplete(ShimEventSyscallComplete {
                                retval: d.retval,
                                restartable: d.restartable,
                            }),
                        ),
                        SyscallReturn::Native => {
                            self.continue_plugin(host, &ShimEvent::SyscallDoNative)
                        }
                    };
                }
                e @ ShimEvent::SyscallComplete(_) => self.continue_plugin(host, &e),
                e @ ShimEvent::SyscallDoNative
                | e @ ShimEvent::AddThreadReq(_)
                | e @ ShimEvent::AddThreadParentRes => panic!("Unexpected event: {e:?}"),
            }
            assert!(self.is_running());

            *self.current_event.borrow_mut() = match self.wait_for_next_event(host) {
                Ok(e) => e,
                Err(SelfContainedChannelError::WriterIsClosed) => ShimEvent::ProcessDeath,
            };
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

        if !self.is_running() {
            return;
        }

        self.cleanup();
    }

    pub fn return_code(&self) -> Option<i32> {
        self.return_code.get()
    }

    pub fn is_running(&self) -> bool {
        self.is_running.get()
    }

    // FIXME: return Result, propagating clone error instead of panicking.
    pub fn handle_clone_syscall(
        &self,
        host: &Host,
        process: &Process,
        flags: libc::c_ulong,
        child_stack: ForeignPtr,
        ptid: ForeignPtr,
        ctid: ForeignPtr,
        newtls: libc::c_ulong,
    ) -> ManagedThread {
        let child_ipc_shmem =
            Arc::new(shadow_shmem::allocator::Allocator::global().alloc(IPCData::new()));
        let child_notification_handle = {
            let child_ipc_shmem = child_ipc_shmem.clone();
            WORKER_SHARED
                .borrow()
                .as_ref()
                .unwrap()
                .child_pid_watcher()
                .register_callback(self.native_pid(), move |_pid| {
                    child_ipc_shmem.from_plugin().close_writer();
                })
        };

        // Send the IPC block for the new mthread to use.
        self.continue_plugin(
            host,
            &ShimEvent::AddThreadReq(ShimEventAddThreadReq {
                ipc_block: child_ipc_shmem.serialize(),
            }),
        );
        match self.wait_for_next_event(host) {
            Ok(ShimEvent::AddThreadParentRes) => (),
            r => panic!("Unexpected result: {r:?}"),
        };

        // Create the new managed thread.
        // FIXME: we shouldn't pass the original ctid and ptid through; should use our own
        // values (e.g. in shared memory) and emulate.
        let child_native_tid = libc::pid_t::from(
            syscall::raw_return_value_to_result(
                self.native_syscall(
                    host,
                    process,
                    libc::SYS_clone,
                    &[
                        flags.into(),
                        child_stack.into(),
                        ptid.into(),
                        ctid.into(),
                        newtls.into(),
                    ],
                )
                .into(),
            )
            .unwrap(),
        );
        trace!("native clone treated tid {child_native_tid}");

        Self {
            ipc_shmem: child_ipc_shmem,
            is_running: Cell::new(true),
            return_code: Cell::new(None),
            current_event: RefCell::new(ShimEvent::Start),
            notification_handle: Some(child_notification_handle),
            native_pid: self.native_pid,
            native_tid: Some(nix::unistd::Pid::from_raw(child_native_tid)),
            // TODO: can we assume it's inherited from the current thread affinity?
            affinity: Cell::new(cshadow::AFFINITY_UNINIT),
        }
    }

    fn continue_plugin(&self, host: &Host, event: &ShimEvent) {
        // Update shared state before transferring control.
        host.shim_shmem_lock_borrow_mut().unwrap().max_runahead_time =
            Worker::max_event_runahead_time(host);
        host.shim_shmem()
            .sim_time
            .store(Worker::current_time().unwrap(), atomic::Ordering::Relaxed);

        // Release lock so that plugin can take it. Reacquired in `wait_for_next_event`.
        host.unlock_shmem();

        self.ipc_shmem.to_plugin().send(*event);
    }

    fn wait_for_next_event(&self, host: &Host) -> Result<ShimEvent, SelfContainedChannelError> {
        let event = self.ipc_shmem.from_plugin().receive();

        // The managed mthread has yielded control back to us. Reacquire the shared
        // memory lock, which we released in `continue_plugin`.
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

    fn cleanup(&self) {
        trace!("child {:?} exited", self.native_tid());
        self.is_running.set(false);
    }

    fn sync_affinity_with_worker(&self) {
        let current_affinity = scheduler::core_affinity()
            .map(|x| i32::try_from(x).unwrap())
            .unwrap_or(cshadow::AFFINITY_UNINIT);
        self.affinity.set(unsafe {
            cshadow::affinity_setProcessAffinity(
                self.native_tid().into(),
                current_affinity,
                self.affinity.get(),
            )
        });
    }

    fn spawn(
        &self,
        plugin_path: &CStr,
        argv: Vec<CString>,
        mut envv: Vec<CString>,
        working_dir: &CStr,
        strace_fd: Option<RawFd>,
        shimlog_fd: RawFd,
    ) -> nix::unistd::Pid {
        // Tell the shim to change the working dir.
        //
        // TODO: Instead use posix_spawn_file_actions_addchdir_np, which was added
        // in glibc 2.29. We should be able to do so once we've dropped support
        // for some platforms, as planned for the shadow 3.0 release.
        // https://github.com/shadow/shadow/discussions/2496
        envv.push(
            CString::new(format!(
                "SHADOW_WORKING_DIR={}",
                working_dir.to_str().unwrap()
            ))
            .unwrap(),
        );

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

        // For childpidwatcher. We use `O_CLOEXEC` to prevent them from leaking
        // into a concurrently forked child, which would otherwise prevent us from detecting
        // the closure of the write end of the pipe when this forked process exits.
        let (pipe_read_fd, pipe_write_fd) = nix::unistd::pipe2(OFlag::O_CLOEXEC).unwrap();

        let mut file_actions: libc::posix_spawn_file_actions_t = pod::zeroed();
        Errno::result(unsafe { libc::posix_spawn_file_actions_init(&mut file_actions) }).unwrap();

        // Dup the write end of the pipe; the dup'd descriptor won't have O_CLOEXEC set.
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
        Errno::result(unsafe {
            libc::posix_spawn_file_actions_adddup2(
                &mut file_actions,
                pipe_write_fd,
                libc::STDOUT_FILENO,
            )
        })
        .unwrap();
        Errno::result(unsafe {
            libc::posix_spawn_file_actions_adddup2(
                &mut file_actions,
                libc::STDOUT_FILENO,
                pipe_write_fd,
            )
        })
        .unwrap();

        // Likewise for straceFd.
        if let Some(strace_fd) = strace_fd {
            Errno::result(unsafe {
                libc::posix_spawn_file_actions_adddup2(
                    &mut file_actions,
                    strace_fd,
                    libc::STDOUT_FILENO,
                )
            })
            .unwrap();
            Errno::result(unsafe {
                libc::posix_spawn_file_actions_adddup2(
                    &mut file_actions,
                    libc::STDOUT_FILENO,
                    strace_fd,
                )
            })
            .unwrap();
        }

        // set stdout/stderr as the shim log. This also clears the FD_CLOEXEC flag.
        Errno::result(unsafe {
            libc::posix_spawn_file_actions_adddup2(
                &mut file_actions,
                shimlog_fd,
                libc::STDOUT_FILENO,
            )
        })
        .unwrap();
        Errno::result(unsafe {
            libc::posix_spawn_file_actions_adddup2(
                &mut file_actions,
                shimlog_fd,
                libc::STDERR_FILENO,
            )
        })
        .unwrap();

        let mut spawn_attr: libc::posix_spawnattr_t = pod::zeroed();
        Errno::result(unsafe { libc::posix_spawnattr_init(&mut spawn_attr) }).unwrap();

        // In versions of glibc before 2.24, we need this to tell posix_spawn
        // to use vfork instead of fork. In later versions it's a no-op.
        Errno::result(unsafe {
            libc::posix_spawnattr_setflags(
                &mut spawn_attr,
                libc::POSIX_SPAWN_USEVFORK.try_into().unwrap(),
            )
        })
        .unwrap();

        let child_pid = {
            let mut child_pid = -1;
            Errno::result(unsafe {
                libc::posix_spawn(
                    &mut child_pid,
                    plugin_path.as_ptr(),
                    &file_actions,
                    &spawn_attr,
                    argv_ptrs.as_ptr(),
                    envv_ptrs.as_ptr(),
                )
            })
            .unwrap();
            nix::unistd::Pid::from_raw(child_pid)
        };

        Errno::result(unsafe { libc::posix_spawn_file_actions_destroy(&mut file_actions) })
            .unwrap();
        Errno::result(unsafe { libc::posix_spawnattr_destroy(&mut spawn_attr) }).unwrap();

        // close the write-end of the pipe, so that the child's copy is the
        // last remaining one, allowing the read-end to be notified when the child
        // exits.
        nix::unistd::close(pipe_write_fd).unwrap();

        // register the read-end of the pipe, so that we'll be notified of the
        // child's death when the write-end is closed.
        WORKER_SHARED
            .borrow()
            .as_ref()
            .unwrap()
            .child_pid_watcher()
            .register_pid(child_pid, unsafe { File::from_raw_fd(pipe_read_fd) });

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
            "started process {} with PID {child_pid:?}",
            plugin_path.to_str().unwrap()
        );

        child_pid
    }
}