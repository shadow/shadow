use linux_api::posix_types::kernel_mode_t;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::cshadow;
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::type_formatting::SyscallStringArg;
use crate::host::syscall::types::SyscallResult;

impl SyscallHandler {
    log_syscall!(
        openat,
        /* rv */ std::ffi::c_int,
        /* dirfd */ std::ffi::c_int,
        /* pathname */ SyscallStringArg,
        /* flags */ linux_api::fcntl::OFlag,
        /* mode */ nix::sys::stat::Mode,
    );
    pub fn openat(
        ctx: &mut SyscallContext,
        _dir_fd: std::ffi::c_int,
        _path: ForeignPtr<()>,
        _flags: std::ffi::c_int,
        _mode: kernel_mode_t,
    ) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_openat, ctx)
    }

    log_syscall!(
        faccessat,
        /* rv */ std::ffi::c_int,
        /* dfd */ std::ffi::c_int,
        /* filename */ SyscallStringArg,
        /* mode */ std::ffi::c_int,
    );
    pub fn faccessat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_faccessat, ctx)
    }

    log_syscall!(
        faccessat2,
        /* rv */ std::ffi::c_int,
        /* dfd */ std::ffi::c_int,
        /* filename */ SyscallStringArg,
        /* mode */ std::ffi::c_int,
        /* flags */ std::ffi::c_int
    );
    pub fn faccessat2(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_faccessat2, ctx)
    }

    log_syscall!(
        fchmodat,
        /* rv */ std::ffi::c_int,
        /* dfd */ std::ffi::c_int,
        /* filename */ SyscallStringArg,
        /* mode */ linux_api::types::umode_t
    );
    pub fn fchmodat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_fchmodat, ctx)
    }

    log_syscall!(
        fchmodat2,
        /* rv */ std::ffi::c_int,
        /* dfd */ std::ffi::c_int,
        /* filename */ SyscallStringArg,
        /* mode */ linux_api::types::umode_t,
        /* flags */ std::ffi::c_uint
    );
    pub fn fchmodat2(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_fchmodat2, ctx)
    }

    log_syscall!(
        fchownat,
        /* rv */ std::ffi::c_int,
        /* dfd */ std::ffi::c_int,
        /* filename */ SyscallStringArg,
        /* user */ std::ffi::c_int,
        /* group */ std::ffi::c_int,
        /* flag */ std::ffi::c_int
    );
    pub fn fchownat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_fchownat, ctx)
    }

    log_syscall!(
        futimesat,
        /* rv */ std::ffi::c_int,
        /* dfd */ std::ffi::c_int,
        /* filename */ SyscallStringArg,
        /* utimes */ *const linux_api::time::kernel_old_timeval
    );
    pub fn futimesat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_futimesat, ctx)
    }

    log_syscall!(
        linkat,
        /* rv */ std::ffi::c_int,
        /* olddfd */ std::ffi::c_int,
        /* oldname */ SyscallStringArg,
        /* newdfd */ std::ffi::c_int,
        /* newname */ SyscallStringArg,
        /* flags */ std::ffi::c_int
    );
    pub fn linkat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_linkat, ctx)
    }

    log_syscall!(
        mkdirat,
        /* rv */ std::ffi::c_int,
        /* dfd */ std::ffi::c_int,
        /* pathname */ SyscallStringArg,
        /* mode */ linux_api::types::umode_t
    );
    pub fn mkdirat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_mkdirat, ctx)
    }

    log_syscall!(
        mknodat,
        /* rv */ std::ffi::c_int,
        /* dfd */ std::ffi::c_int,
        /* pathname */ SyscallStringArg,
        /* mode */ linux_api::types::umode_t,
        /* dev */ std::ffi::c_uint
    );
    pub fn mknodat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_mknodat, ctx)
    }

    log_syscall!(
        readlinkat,
        /* rv */ std::ffi::c_int,
        /* dirfd */ std::ffi::c_int,
        /* pathname */ SyscallStringArg,
        /* buf */ *const std::ffi::c_char,
        /* bufsize */ std::ffi::c_int,
    );
    pub fn readlinkat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_readlinkat, ctx)
    }

    log_syscall!(
        renameat,
        /* rv */ std::ffi::c_int,
        /* olddfd */ std::ffi::c_int,
        /* oldname */ SyscallStringArg,
        /* newdfd */ std::ffi::c_int,
        /* newname */ SyscallStringArg
    );
    pub fn renameat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_renameat, ctx)
    }

    log_syscall!(
        renameat2,
        /* rv */ std::ffi::c_int,
        /* olddfd */ std::ffi::c_int,
        /* oldname */ SyscallStringArg,
        /* newdfd */ std::ffi::c_int,
        /* newname */ SyscallStringArg,
        /* flags */ std::ffi::c_int
    );
    pub fn renameat2(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_renameat2, ctx)
    }

    log_syscall!(
        symlinkat,
        /* rv */ std::ffi::c_int,
        /* oldname */ SyscallStringArg,
        /* newdfd */ std::ffi::c_int,
        /* newname */ SyscallStringArg
    );
    pub fn symlinkat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_symlinkat, ctx)
    }

    log_syscall!(
        unlinkat,
        /* rv */ std::ffi::c_int,
        /* dfd */ std::ffi::c_int,
        /* pathname */ SyscallStringArg,
        /* flag */ std::ffi::c_int
    );
    pub fn unlinkat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_unlinkat, ctx)
    }

    log_syscall!(
        utimensat,
        /* rv */ std::ffi::c_int,
        /* dfd */ std::ffi::c_int,
        /* filename */ SyscallStringArg,
        /* utimes */ *const linux_api::time::kernel_timespec,
        /* flags */ std::ffi::c_int
    );
    pub fn utimensat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_utimensat, ctx)
    }
}
