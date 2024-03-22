use linux_api::errno::Errno;
use linux_api::signal::{siginfo_t, Signal};
use shadow_shim_helper_rs::syscall_types::ForeignPtr;
use syscall_logger::log_syscall;

use crate::cshadow as c;
use crate::host::process::Process;
use crate::host::syscall::handler::{SyscallContext, SyscallHandler, ThreadContext};
use crate::host::syscall::types::SyscallError;

impl SyscallHandler {
    #[log_syscall(/* rv */ std::ffi::c_int, /* pid */ linux_api::posix_types::kernel_pid_t,
                  /* sig */ std::ffi::c_int)]
    pub fn kill(
        ctx: &mut SyscallContext,
        pid: linux_api::posix_types::kernel_pid_t,
        sig: std::ffi::c_int,
    ) -> Result<(), SyscallError> {
        log::trace!("kill called on pid {pid} with signal {sig}");

        let pid = if pid == -1 {
            // kill(2): If pid equals -1, then sig is sent to every process for which the calling
            // process has permission to send signals, except for process 1.
            //
            // Currently unimplemented, and unlikely to be needed in the context of a shadow
            // simulation.
            log::warn!("kill with pid=-1 unimplemented");
            return Err(Errno::ENOTSUP.into());
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
            return Err(Errno::ESRCH.into());
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
    ) -> Result<(), SyscallError> {
        if signal == 0 {
            return Ok(());
        }

        let Ok(signal) = Signal::try_from(signal) else {
            return Err(Errno::EINVAL.into());
        };

        if signal.is_realtime() {
            log::warn!("Unimplemented signal {signal:?}");
            return Err(Errno::ENOTSUP.into());
        }

        let sender_pid = objs.process.id().into();
        let siginfo = siginfo_t::new_for_kill(signal, sender_pid, 0);

        target_process.signal(objs.host, Some(objs.thread), &siginfo);

        Ok(())
    }

    #[log_syscall(/* rv */ std::ffi::c_int, /* pid */ linux_api::posix_types::kernel_pid_t,
                  /* sig */ std::ffi::c_int)]
    pub fn tkill(
        ctx: &mut SyscallContext,
        _pid: linux_api::posix_types::kernel_pid_t,
        _sig: std::ffi::c_int,
    ) -> Result<(), SyscallError> {
        let rv = Self::legacy_syscall(c::syscallhandler_tkill, ctx)?;
        assert_eq!(0, i32::from(rv));
        Ok(())
    }

    #[log_syscall(/* rv */ std::ffi::c_int, /* tgid */ linux_api::posix_types::kernel_pid_t,
                  /* pid */ linux_api::posix_types::kernel_pid_t, /* sig */ std::ffi::c_int)]
    pub fn tgkill(
        ctx: &mut SyscallContext,
        _tgid: linux_api::posix_types::kernel_pid_t,
        _pid: linux_api::posix_types::kernel_pid_t,
        _sig: std::ffi::c_int,
    ) -> Result<(), SyscallError> {
        let rv = Self::legacy_syscall(c::syscallhandler_tgkill, ctx)?;
        assert_eq!(0, i32::from(rv));
        Ok(())
    }

    #[log_syscall(/* rv */ std::ffi::c_int, /* sig */ std::ffi::c_int,
                  /* act */ *const std::ffi::c_void, /* oact */ *const std::ffi::c_void,
                  /* sigsetsize */ libc::size_t)]
    pub fn rt_sigaction(
        ctx: &mut SyscallContext,
        _sig: std::ffi::c_int,
        _act: ForeignPtr<linux_api::signal::sigaction>,
        _oact: ForeignPtr<linux_api::signal::sigaction>,
        _sigsetsize: libc::size_t,
    ) -> Result<(), SyscallError> {
        let rv = Self::legacy_syscall(c::syscallhandler_rt_sigaction, ctx)?;
        assert_eq!(0, i32::from(rv));
        Ok(())
    }

    #[log_syscall(/* rv */ std::ffi::c_int, /* how */ std::ffi::c_int,
                  /* nset */ *const std::ffi::c_void, /* oset */ *const std::ffi::c_void,
                  /* sigsetsize */ libc::size_t)]
    pub fn rt_sigprocmask(
        ctx: &mut SyscallContext,
        _how: std::ffi::c_int,
        _nset: ForeignPtr<linux_api::signal::sigset_t>,
        _oset: ForeignPtr<linux_api::signal::sigset_t>,
        _sigsetsize: libc::size_t,
    ) -> Result<(), SyscallError> {
        let rv = Self::legacy_syscall(c::syscallhandler_rt_sigprocmask, ctx)?;
        assert_eq!(0, i32::from(rv));
        Ok(())
    }

    #[log_syscall(/* rv */ std::ffi::c_int, /* uss */ *const std::ffi::c_void,
                  /* uoss */ *const std::ffi::c_void)]
    pub fn sigaltstack(
        ctx: &mut SyscallContext,
        _uss: ForeignPtr<linux_api::signal::stack_t>,
        _uoss: ForeignPtr<linux_api::signal::stack_t>,
    ) -> Result<(), SyscallError> {
        let rv = Self::legacy_syscall(c::syscallhandler_sigaltstack, ctx)?;
        assert_eq!(0, i32::from(rv));
        Ok(())
    }
}
