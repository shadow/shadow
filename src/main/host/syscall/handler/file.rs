use linux_api::posix_types::kernel_mode_t;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;
use syscall_logger::log_syscall;

use crate::cshadow;
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::type_formatting::SyscallStringArg;
use crate::host::syscall::types::SyscallResult;

impl SyscallHandler {
    #[log_syscall(/* rv */ std::ffi::c_int, /* pathname */ SyscallStringArg,
                  /* flags */ linux_api::fcntl::OFlag, /* mode */ nix::sys::stat::Mode)]
    pub fn open(
        ctx: &mut SyscallContext,
        _path: ForeignPtr<()>,
        _flags: std::ffi::c_int,
        _mode: kernel_mode_t,
    ) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_open, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn creat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_creat, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn fadvise64(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_fadvise64, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn fallocate(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_fallocate, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn fchmod(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_fchmod, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn fchown(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_fchown, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn fdatasync(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_fdatasync, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn fgetxattr(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_fgetxattr, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn flistxattr(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_flistxattr, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn flock(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_flock, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn fremovexattr(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_fremovexattr, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn fsetxattr(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_fsetxattr, ctx)
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
    pub fn fsync(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_fsync, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn ftruncate(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_ftruncate, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn getdents(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_getdents, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn getdents64(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_getdents64, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn lseek(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_lseek, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn readahead(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_readahead, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn sync_file_range(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_sync_file_range, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn syncfs(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_syncfs, ctx)
    }
}
