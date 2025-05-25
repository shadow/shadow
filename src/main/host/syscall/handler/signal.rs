use linux_api::errno::Errno;
use linux_api::signal::{
    LinuxDefaultAction, SigAltStackFlags, SigProcMaskAction, Signal, SignalHandler, defaultaction,
    siginfo_t,
};
use shadow_shim_helper_rs::explicit_drop::{ExplicitDrop, ExplicitDropper};
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::host::process::Process;
use crate::host::syscall::handler::{SyscallContext, SyscallHandler, ThreadContext};
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
        sig: std::ffi::c_int,
        act: ForeignPtr<linux_api::signal::sigaction>,
        oact: ForeignPtr<linux_api::signal::sigaction>,
        sigsetsize: libc::size_t,
    ) -> Result<(), Errno> {
        // rt_sigaction(2):
        // > Consequently, a new system call, rt_sigaction(), was added to support an enlarged
        // > sigset_t type. The new system call takes a fourth argument, size_t sigsetsize, which
        // > specifies the size in bytes of the signal sets in act.sa_mask and oldact.sa_mask. This
        // > argument is currently required to have the value sizeof(sigset_t) (or the error EINVAL
        // > results)
        // Assuming by "sizeof(sigset_t)" it means the kernel's `linux_sigset_t` and not glibc's
        // `sigset_t`...
        if sigsetsize != size_of::<linux_api::signal::sigset_t>() {
            return Err(Errno::EINVAL);
        }

        let Ok(sig) = Signal::try_from(sig) else {
            return Err(Errno::EINVAL);
        };

        let shmem_lock = ctx.objs.host.shim_shmem_lock_borrow().unwrap();
        let process_shmem = ctx.objs.process.shmem();
        let mut process_protected = process_shmem.protected.borrow_mut(&shmem_lock.root);

        if !oact.is_null() {
            let old_action = unsafe { process_protected.signal_action(sig) };
            ctx.objs
                .process
                .memory_borrow_mut()
                .write(oact, old_action)?;
        }

        if act.is_null() {
            // nothing left to do
            return Ok(());
        }

        if sig == Signal::SIGKILL || sig == Signal::SIGSTOP {
            return Err(Errno::EINVAL);
        }

        let new_action = ctx.objs.process.memory_borrow().read(act)?;
        unsafe { *process_protected.signal_action_mut(sig) = new_action };

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
        how: std::ffi::c_int,
        nset: ForeignPtr<linux_api::signal::sigset_t>,
        oset: ForeignPtr<linux_api::signal::sigset_t>,
        sigsetsize: libc::size_t,
    ) -> Result<(), Errno> {
        // From sigprocmask(2): This argument is currently required to have a fixed architecture
        // specific value (equal to sizeof(kernel_sigset_t)).
        // We use `sigset_t` from `linux_api`, which is a wrapper around `linux_sigset_t` from
        // the kernel and should be equivalent to `kernel_sigset_t`.
        if sigsetsize != size_of::<linux_api::signal::sigset_t>() {
            warn_once_then_debug!("Bad sigsetsize {sigsetsize}");
            return Err(Errno::EINVAL);
        }

        let shmem_lock = ctx.objs.host.shim_shmem_lock_borrow().unwrap();
        let thread_shmem = ctx.objs.thread.shmem();
        let mut thread_protected = thread_shmem.protected.borrow_mut(&shmem_lock.root);

        let current_set = thread_protected.blocked_signals;

        if !oset.is_null() {
            ctx.objs
                .process
                .memory_borrow_mut()
                .write(oset, &current_set)?;
        }

        if nset.is_null() {
            // nothing left to do
            return Ok(());
        }

        let set = ctx.objs.process.memory_borrow().read(nset)?;

        let set = match SigProcMaskAction::try_from(how) {
            Ok(SigProcMaskAction::SIG_BLOCK) => set | current_set,
            Ok(SigProcMaskAction::SIG_UNBLOCK) => !set & current_set,
            Ok(SigProcMaskAction::SIG_SETMASK) => set,
            Err(_) => return Err(Errno::EINVAL),
        };

        thread_protected.blocked_signals = set;

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
        uss: ForeignPtr<linux_api::signal::stack_t>,
        uoss: ForeignPtr<linux_api::signal::stack_t>,
    ) -> Result<(), Errno> {
        let shmem_lock = ctx.objs.host.shim_shmem_lock_borrow().unwrap();
        let thread_shmem = ctx.objs.thread.shmem();
        let mut thread_protected = thread_shmem.protected.borrow_mut(&shmem_lock.root);

        let old_ss = unsafe { *thread_protected.sigaltstack() };

        if !uss.is_null() {
            if old_ss.flags_retain().contains(SigAltStackFlags::SS_ONSTACK) {
                // sigaltstack(2): EPERM An attempt was made to change the
                // alternate signal stack while it was active.
                return Err(Errno::EPERM);
            }

            let mut new_ss = ctx.objs.process.memory_borrow().read(uss)?;
            if new_ss.flags_retain().contains(SigAltStackFlags::SS_DISABLE) {
                // sigaltstack(2): To disable an existing stack, specify ss.ss_flags
                // as SS_DISABLE. In this case, the kernel ignores any other flags
                // in ss.ss_flags and the remaining fields in ss.
                new_ss = shadow_pod::zeroed();
                new_ss.ss_flags = SigAltStackFlags::SS_DISABLE.bits();
            }

            let unrecognized_flags = new_ss
                .flags_retain()
                .difference(SigAltStackFlags::SS_DISABLE | SigAltStackFlags::SS_AUTODISARM);

            if !unrecognized_flags.is_empty() {
                warn_once_then_debug!(
                    "Unrecognized signal stack flags {unrecognized_flags:?} in {:?}",
                    new_ss.flags_retain(),
                );
                // Unrecognized flag.
                return Err(Errno::EINVAL);
            }

            *unsafe { thread_protected.sigaltstack_mut() } = new_ss;
        }

        if !uoss.is_null() {
            ctx.objs.process.memory_borrow_mut().write(uoss, &old_ss)?;
        }

        Ok(())
    }
}

#[cfg(test)]
mod test {
    use super::*;

    /// The bitflags crate does some weird things with unrecognized flags, so this test ensures that
    /// they work as expected for how we use them above.
    #[test]
    fn unrecognized_flags_difference() {
        let foo = 1 << 28;
        // ensure this flag is unused
        assert_eq!(SigAltStackFlags::from_bits(foo), None);
        let foo = SigAltStackFlags::from_bits_retain(foo);
        assert_eq!(
            foo.difference(SigAltStackFlags::SS_DISABLE).bits(),
            (1 << 28) & !SigAltStackFlags::SS_DISABLE.bits(),
        );
        assert_eq!(
            (foo - SigAltStackFlags::SS_DISABLE).bits(),
            (1 << 28) & !SigAltStackFlags::SS_DISABLE.bits(),
        );
    }

    #[test]
    fn unrecognized_flags_empty() {
        let foo = 1 << 28;
        assert_ne!(foo, 0);
        // ensure this flag is unused
        assert_eq!(SigAltStackFlags::from_bits(foo), None);
        let foo = SigAltStackFlags::from_bits_retain(foo);
        assert_ne!(foo.bits(), 0);
        assert!(!foo.is_empty());
    }
}
