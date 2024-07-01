use linux_api::posix_types::kernel_mode_t;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;
use syscall_logger::log_syscall;

use crate::cshadow;
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::type_formatting::SyscallStringArg;
use crate::host::syscall::types::SyscallResult;

impl SyscallHandler {
    #[log_syscall(/* rv */ std::ffi::c_int, /* dirfd */ std::ffi::c_int, /* pathname */ SyscallStringArg,
                  /* flags */ linux_api::fcntl::OFlag, /* mode */ nix::sys::stat::Mode)]
    pub fn openat(
        ctx: &mut SyscallContext,
        _dir_fd: std::ffi::c_int,
        _path: ForeignPtr<()>,
        _flags: std::ffi::c_int,
        _mode: kernel_mode_t,
    ) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_openat, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn faccessat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_faccessat, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn fchmodat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_fchmodat, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn fchownat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_fchownat, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn futimesat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_futimesat, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn linkat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_linkat, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn mkdirat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_mkdirat, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn mknodat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_mknodat, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn readlinkat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_readlinkat, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn renameat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_renameat, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn renameat2(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_renameat2, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn symlinkat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_symlinkat, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn unlinkat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_unlinkat, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn utimensat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_utimensat, ctx)
    }
}
