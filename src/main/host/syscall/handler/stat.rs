use syscall_logger::log_syscall;

use crate::cshadow;
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::types::SyscallResult;

impl SyscallHandler {
    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn statx(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_statx, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn fstat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_fstat, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn fstatfs(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_fstatfs, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn newfstatat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_newfstatat, ctx)
    }
}
