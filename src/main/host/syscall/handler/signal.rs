use linux_api::errno::Errno;
use linux_api::signal::{defaultaction, siginfo_t, LinuxDefaultAction, Signal, SignalHandler};
use shadow_shim_helper_rs::explicit_drop::{ExplicitDrop, ExplicitDropper};
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::cshadow as c;
use crate::host::process::Process;
use crate::host::syscall::handler::{SyscallContext, SyscallHandler, ThreadContext};
use crate::host::syscall::types::SyscallError;
use crate::host::thread::Thread;

impl SyscallHandler {
    log_syscall!(
        kill,
        /* rv */ std::ffi::c_int,
        /* pid */ linux_api::posix_types::kernel_pid_t,
        /* sig */ std::ffi::c_int,
    );
    pub fn kill(
        ctx: &mut SyscallContext,
        pid: linux_api::posix_types::kernel_pid_t,
        sig: std::ffi::c_int,
    ) -> Result<(), Errno> {
        log::trace!("kill called on pid {pid} with signal {sig}");

        let pid = if pid == -1 {
            // kill(2): If pid equals -1, then sig is sent to every process for which the calling
            // process has permission to send signals, except for process 1.
            //
            // Currently unimplemented, and unlikely to be needed in the context of a shadow
            // simulation.
            log::warn!("kill with pid=-1 unimplemented");
            return Err(Errno::ENOTSUP);
        } else if pid == 0 {
            // kill(2): If pid equals 0, then sig is sent to every process in the process group of
            // the calling process.
            //
            // Currently every emulated process is in its own process group.
            //
            // FIXME: The above comment is no longer true since implementing fork(). See
            // https://github.com/shadow/shadow/issues/3315
            ctx.objs.process.id()
        } else if pid < -1 {
            // kill(2): If pid is less than -1, then sig is sent to every process in the process
            // group whose ID is -pid.
            //
            // Currently every emulated process is in its own process group, where pgid=pid.
            //
            // FIXME: The above comment is no longer true since implementing fork(). See
            // https://github.com/shadow/shadow/issues/3315
            (-pid).try_into().or(Err(Errno::ESRCH))?
        } else {
            pid.try_into().or(Err(Errno::ESRCH))?
        };

        let Some(target_process) = ctx.objs.host.process_borrow(pid) else {
            log::debug!("Process {pid} not found");
            return Err(Errno::ESRCH);
        };
        let target_process = &*target_process.borrow(ctx.objs.host.root());

        Self::signal_process(ctx.objs, target_process, sig)
    }

    /// Send a signal to `target_process` from the thread and process in `objs`. A signal of 0 will
    /// be ignored.
    fn signal_process(
        objs: &ThreadContext,
        target_process: &Process,
        signal: std::ffi::c_int,
    ) -> Result<(), Errno> {
        if signal == 0 {
            return Ok(());
        }

        let Ok(signal) = Signal::try_from(signal) else {
            return Err(Errno::EINVAL);
        };

        if signal.is_realtime() {
            log::warn!("Unimplemented signal {signal:?}");
            return Err(Errno::ENOTSUP);
        }

        let sender_pid = objs.process.id().into();
        let siginfo = siginfo_t::new_for_kill(signal, sender_pid, 0);

        target_process.signal(objs.host, Some(objs.thread), &siginfo);

        Ok(())
    }

    log_syscall!(
        tkill,
        /* rv */ std::ffi::c_int,
        /* pid */ linux_api::posix_types::kernel_pid_t,
        /* sig */ std::ffi::c_int,
    );
    pub fn tkill(
        ctx: &mut SyscallContext,
        tid: linux_api::posix_types::kernel_pid_t,
        sig: std::ffi::c_int,
    ) -> Result<(), Errno> {
        log::trace!("tkill called on tid {tid} with signal {sig}");

        let tid = tid.try_into().or(Err(Errno::ESRCH))?;

        let Some(target_thread) = ctx.objs.host.thread_cloned_rc(tid) else {
            return Err(Errno::ESRCH);
        };
        let target_thread = ExplicitDropper::new(target_thread, |value| {
            value.explicit_drop(ctx.objs.host.root())
        });
        let target_thread = &*target_thread.borrow(ctx.objs.host.root());

        Self::signal_thread(ctx.objs, target_thread, sig)
    }

    log_syscall!(
        tgkill,
        /* rv */ std::ffi::c_int,
        /* tgid */ linux_api::posix_types::kernel_pid_t,
        /* pid */ linux_api::posix_types::kernel_pid_t,
        /* sig */ std::ffi::c_int,
    );
    pub fn tgkill(
        ctx: &mut SyscallContext,
        tgid: linux_api::posix_types::kernel_pid_t,
        tid: linux_api::posix_types::kernel_pid_t,
        sig: std::ffi::c_int,
    ) -> Result<(), Errno> {
        log::trace!("tgkill called on tgid {tgid} and tid {tid} with signal {sig}");

        let tgid = tgid.try_into().or(Err(Errno::ESRCH))?;
        let tid = tid.try_into().or(Err(Errno::ESRCH))?;

        let Some(target_thread) = ctx.objs.host.thread_cloned_rc(tid) else {
            return Err(Errno::ESRCH);
        };
        let target_thread = ExplicitDropper::new(target_thread, |value| {
            value.explicit_drop(ctx.objs.host.root())
        });
        let target_thread = &*target_thread.borrow(ctx.objs.host.root());

        if target_thread.process_id() != tgid {
            return Err(Errno::ESRCH);
        }

        Self::signal_thread(ctx.objs, target_thread, sig)
    }

    /// Send a signal to `target_thread` from the thread and process in `objs`. A signal of 0 will
    /// be ignored.
    fn signal_thread(
        objs: &ThreadContext,
        target_thread: &Thread,
        signal: std::ffi::c_int,
    ) -> Result<(), Errno> {
        if signal == 0 {
            return Ok(());
        }

        let Ok(signal) = Signal::try_from(signal) else {
            return Err(Errno::EINVAL);
        };

        if signal.is_realtime() {
            log::warn!("Unimplemented signal {signal:?}");
            return Err(Errno::ENOTSUP);
        }

        // need to scope the shmem lock since `wakeup_for_signal` below takes its own shmem lock
        let mut cond = {
            let shmem_lock = &*objs.host.shim_shmem_lock_borrow().unwrap();

            let target_process = objs
                .host
                .process_borrow(target_thread.process_id())
                .unwrap();
            let target_process = &*target_process.borrow(objs.host.root());

            let process_shmem = target_process.borrow_as_runnable().unwrap();
            let process_shmem = &*process_shmem.shmem();
            let process_protected = process_shmem.protected.borrow(&shmem_lock.root);

            let thread_shmem = target_thread.shmem();
            let mut thread_protected = thread_shmem.protected.borrow_mut(&shmem_lock.root);

            let action = unsafe { process_protected.signal_action(signal) };
            let action_handler = unsafe { action.handler() };

            let signal_is_ignored = match action_handler {
                SignalHandler::SigIgn => true,
                SignalHandler::SigDfl => defaultaction(signal) == LinuxDefaultAction::IGN,
                _ => false,
            };

            if signal_is_ignored {
                // don't deliver an ignored signal
                return Ok(());
            }

            if thread_protected.pending_signals.has(signal) {
                // Signal is already pending. From signal(7): In the case where a standard signal is
                // already pending, the siginfo_t structure (see sigaction(2)) associated with that
                // signal is not overwritten on arrival of subsequent instances of the same signal.
                return Ok(());
            }

            thread_protected.pending_signals.add(signal);

            let sender_pid = objs.process.id();
            let sender_tid = objs.thread.id();

            let siginfo = siginfo_t::new_for_tkill(signal, sender_pid.into(), 0);

            thread_protected.set_pending_standard_siginfo(signal, &siginfo);

            if sender_tid == target_thread.id() {
                // Target is the current thread. It'll be handled synchronously when the current
                // syscall returns (if it's unblocked).
                return Ok(());
            }

            if thread_protected.blocked_signals.has(signal) {
                // Target thread has the signal blocked. We'll leave it pending, but no need to
                // schedule an event to process the signal. It'll get processed synchronously when
                // the thread executes a syscall that would unblock the signal.
                return Ok(());
            }

            let Some(cond) = target_thread.syscall_condition_mut() else {
                // We may be able to get here if a thread is signalled before it runs for the first
                // time. Just return; the signal will be delivered when the thread runs.
                return Ok(());
            };

            cond
        };

        let was_scheduled = cond.wakeup_for_signal(objs.host, signal);

        // it won't be scheduled if the signal is blocked, but we previously checked if the signal
        // was blocked above
        assert!(was_scheduled);

        Ok(())
    }

    log_syscall!(
        rt_sigaction,
        /* rv */ std::ffi::c_int,
        /* sig */ std::ffi::c_int,
        /* act */ *const std::ffi::c_void,
        /* oact */ *const std::ffi::c_void,
        /* sigsetsize */ libc::size_t,
    );
    pub fn rt_sigaction(
        ctx: &mut SyscallContext,
        _sig: std::ffi::c_int,
        _act: ForeignPtr<linux_api::signal::sigaction>,
        _oact: ForeignPtr<linux_api::signal::sigaction>,
        _sigsetsize: libc::size_t,
    ) -> Result<(), SyscallError> {
        let rv: i32 = Self::legacy_syscall(c::syscallhandler_rt_sigaction, ctx)?;
        assert_eq!(rv, 0);
        Ok(())
    }

    log_syscall!(
        rt_sigprocmask,
        /* rv */ std::ffi::c_int,
        /* how */ std::ffi::c_int,
        /* nset */ *const std::ffi::c_void,
        /* oset */ *const std::ffi::c_void,
        /* sigsetsize */ libc::size_t,
    );
    pub fn rt_sigprocmask(
        ctx: &mut SyscallContext,
        _how: std::ffi::c_int,
        _nset: ForeignPtr<linux_api::signal::sigset_t>,
        _oset: ForeignPtr<linux_api::signal::sigset_t>,
        _sigsetsize: libc::size_t,
    ) -> Result<(), SyscallError> {
        let rv: i32 = Self::legacy_syscall(c::syscallhandler_rt_sigprocmask, ctx)?;
        assert_eq!(rv, 0);
        Ok(())
    }

    log_syscall!(
        sigaltstack,
        /* rv */ std::ffi::c_int,
        /* uss */ *const std::ffi::c_void,
        /* uoss */ *const std::ffi::c_void,
    );
    pub fn sigaltstack(
        ctx: &mut SyscallContext,
        _uss: ForeignPtr<linux_api::signal::stack_t>,
        _uoss: ForeignPtr<linux_api::signal::stack_t>,
    ) -> Result<(), SyscallError> {
        let rv: i32 = Self::legacy_syscall(c::syscallhandler_sigaltstack, ctx)?;
        assert_eq!(rv, 0);
        Ok(())
    }
}
