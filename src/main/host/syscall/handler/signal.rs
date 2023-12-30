use shadow_shim_helper_rs::syscall_types::ForeignPtr;
use syscall_logger::log_syscall;

use crate::cshadow as c;
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::types::SyscallError;

impl SyscallHandler {
    #[log_syscall(/* rv */ std::ffi::c_int, /* pid */ linux_api::posix_types::kernel_pid_t,
                  /* sig */ std::ffi::c_int)]
    pub fn kill(
        ctx: &mut SyscallContext,
        _pid: linux_api::posix_types::kernel_pid_t,
        _sig: std::ffi::c_int,
    ) -> Result<(), SyscallError> {
        let rv = Self::legacy_syscall(c::syscallhandler_kill, ctx)?;
        assert_eq!(0, i32::from(rv));
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
