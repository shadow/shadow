use linux_api::errno::Errno;
use linux_api::posix_types::kernel_mode_t;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::cshadow;
use crate::host::descriptor::CompatFile;
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::type_formatting::SyscallStringArg;
use crate::host::syscall::types::{SyscallError, SyscallResult};
use crate::host::syscall::File;

impl SyscallHandler {
    log_syscall!(
        open,
        /* rv */ std::ffi::c_int,
        /* pathname */ SyscallStringArg,
        /* flags */ linux_api::fcntl::OFlag,
        /* mode */ nix::sys::stat::Mode,
    );
    pub fn open(
        ctx: &mut SyscallContext,
        _path: ForeignPtr<()>,
        _flags: std::ffi::c_int,
        _mode: kernel_mode_t,
    ) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_open, ctx)
    }

    log_syscall!(creat, /* rv */ std::ffi::c_int);
    pub fn creat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_creat, ctx)
    }

    log_syscall!(fadvise64, /* rv */ std::ffi::c_int);
    pub fn fadvise64(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_fadvise64, ctx)
    }

    log_syscall!(fallocate, /* rv */ std::ffi::c_int);
    pub fn fallocate(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_fallocate, ctx)
    }

    log_syscall!(fchmod, /* rv */ std::ffi::c_int);
    pub fn fchmod(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_fchmod, ctx)
    }

    log_syscall!(fchown, /* rv */ std::ffi::c_int);
    pub fn fchown(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_fchown, ctx)
    }

    log_syscall!(fdatasync, /* rv */ std::ffi::c_int);
    pub fn fdatasync(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_fdatasync, ctx)
    }

    log_syscall!(fgetxattr, /* rv */ std::ffi::c_int);
    pub fn fgetxattr(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_fgetxattr, ctx)
    }

    log_syscall!(flistxattr, /* rv */ std::ffi::c_int);
    pub fn flistxattr(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_flistxattr, ctx)
    }

    log_syscall!(flock, /* rv */ std::ffi::c_int);
    pub fn flock(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_flock, ctx)
    }

    log_syscall!(fremovexattr, /* rv */ std::ffi::c_int);
    pub fn fremovexattr(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_fremovexattr, ctx)
    }

    log_syscall!(fsetxattr, /* rv */ std::ffi::c_int);
    pub fn fsetxattr(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_fsetxattr, ctx)
    }

    log_syscall!(fsync, /* rv */ std::ffi::c_int);
    pub fn fsync(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_fsync, ctx)
    }

    log_syscall!(ftruncate, /* rv */ std::ffi::c_int);
    pub fn ftruncate(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_ftruncate, ctx)
    }

    log_syscall!(getdents, /* rv */ std::ffi::c_int);
    pub fn getdents(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_getdents, ctx)
    }

    log_syscall!(getdents64, /* rv */ std::ffi::c_int);
    pub fn getdents64(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_getdents64, ctx)
    }

    log_syscall!(
        lseek,
        /* rv */ std::ffi::c_int,
        /* fd */ std::ffi::c_uint,
        /* offset */ linux_api::posix_types::kernel_off_t,
        /* whence */ std::ffi::c_uint,
    );
    pub fn lseek(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_uint,
        _offset: linux_api::posix_types::kernel_off_t,
        _whence: std::ffi::c_uint,
    ) -> Result<linux_api::posix_types::kernel_off_t, SyscallError> {
        let desc_table = ctx.objs.thread.descriptor_table_borrow(ctx.objs.host);

        let file = {
            match Self::get_descriptor(&desc_table, fd)?.file() {
                CompatFile::New(file) => file,
                // if it's a legacy file, use the C syscall handler instead
                CompatFile::Legacy(_) => {
                    drop(desc_table);
                    return Self::legacy_syscall(cshadow::syscallhandler_lseek, ctx);
                }
            }
        };

        match file.inner_file() {
            File::Pipe(_) => Err(Errno::ESPIPE.into()),
            _ => {
                warn_once_then_debug!("lseek() is not implemented for this type");
                Err(Errno::ENOTSUP.into())
            }
        }
    }

    log_syscall!(readahead, /* rv */ std::ffi::c_int);
    pub fn readahead(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_readahead, ctx)
    }

    log_syscall!(sync_file_range, /* rv */ std::ffi::c_int);
    pub fn sync_file_range(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_sync_file_range, ctx)
    }

    log_syscall!(syncfs, /* rv */ std::ffi::c_int);
    pub fn syncfs(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_syncfs, ctx)
    }
}
