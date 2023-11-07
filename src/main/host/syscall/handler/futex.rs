use shadow_shim_helper_rs::syscall_types::ForeignPtr;
use syscall_logger::log_syscall;

use crate::cshadow as c;
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall_types::SyscallError;

impl SyscallHandler {
    #[log_syscall(/* rv */ std::ffi::c_int, /* uaddr */ *const u32, /* op */ std::ffi::c_int,
                  /* val */ u32, /* utime */ *const std::ffi::c_void, /* uaddr2 */ *const u32,
                  /* val3 */ u32)]
    pub fn futex(
        ctx: &mut SyscallContext,
        _uaddr: ForeignPtr<u32>,
        _op: std::ffi::c_int,
        _val: u32,
        _utime: ForeignPtr<linux_api::time::kernel_timespec>,
        _uaddr2: ForeignPtr<u32>,
        _val3: u32,
    ) -> Result<std::ffi::c_int, SyscallError> {
        Ok(Self::legacy_syscall(c::syscallhandler_futex, ctx)?.into())
    }
}
