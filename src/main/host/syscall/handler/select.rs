use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::cshadow as c;
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::types::SyscallError;

impl SyscallHandler {
    log_syscall!(
        select,
        /* rv */ std::ffi::c_int,
        /* n */ std::ffi::c_int,
        /* inp */ *const std::ffi::c_void,
        /* outp */ *const std::ffi::c_void,
        /* exp */ *const std::ffi::c_void,
        /* tvp */ *const linux_api::time::kernel_old_timeval,
    );
    pub fn select(
        ctx: &mut SyscallContext,
        _n: std::ffi::c_int,
        _inp: ForeignPtr<linux_api::posix_types::kernel_fd_set>,
        _outp: ForeignPtr<linux_api::posix_types::kernel_fd_set>,
        _exp: ForeignPtr<linux_api::posix_types::kernel_fd_set>,
        _tvp: ForeignPtr<linux_api::time::kernel_old_timeval>,
    ) -> Result<std::ffi::c_int, SyscallError> {
        Self::legacy_syscall(c::syscallhandler_select, ctx)
    }

    log_syscall!(
        pselect6,
        /* rv */ std::ffi::c_int,
        /* n */ std::ffi::c_int,
        /* inp */ *const std::ffi::c_void,
        /* outp */ *const std::ffi::c_void,
        /* exp */ *const std::ffi::c_void,
        /* tsp */ *const linux_api::time::kernel_timespec,
        /* sig */ *const std::ffi::c_void,
    );
    pub fn pselect6(
        ctx: &mut SyscallContext,
        _n: std::ffi::c_int,
        _inp: ForeignPtr<linux_api::posix_types::kernel_fd_set>,
        _outp: ForeignPtr<linux_api::posix_types::kernel_fd_set>,
        _exp: ForeignPtr<linux_api::posix_types::kernel_fd_set>,
        _tsp: ForeignPtr<linux_api::time::kernel_timespec>,
        _sig: ForeignPtr<()>,
    ) -> Result<std::ffi::c_int, SyscallError> {
        Self::legacy_syscall(c::syscallhandler_pselect6, ctx)
    }
}
