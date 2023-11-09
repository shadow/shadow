use linux_api::posix_types::kernel_mode_t;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;
use syscall_logger::log_syscall;

use crate::cshadow;
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::type_formatting::SyscallStringArg;
use crate::host::syscall_types::SyscallResult;

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
}
