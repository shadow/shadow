use linux_api::errno::Errno;
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

    #[log_syscall(/* rv */ std::ffi::c_int, /* pid */ std::ffi::c_int,
                  /* head_ptr */ *const std::ffi::c_void, /* len_ptr */ *const libc::size_t)]
    pub fn get_robust_list(
        _ctx: &mut SyscallContext,
        _pid: std::ffi::c_int,
        _head_ptr: ForeignPtr<ForeignPtr<linux_api::futex::robust_list_head>>,
        _len_ptr: ForeignPtr<libc::size_t>,
    ) -> Result<(), SyscallError> {
        log::debug!("get_robust_list was called but we don't yet support it");
        Err(Errno::ENOSYS.into())
    }

    #[log_syscall(/* rv */ std::ffi::c_int, /* head */ *const std::ffi::c_void,
                  /* len */ libc::size_t)]
    pub fn set_robust_list(
        _ctx: &mut SyscallContext,
        _head: ForeignPtr<linux_api::futex::robust_list_head>,
        _len: libc::size_t,
    ) -> Result<(), SyscallError> {
        log::debug!("set_robust_list was called but we don't yet support it");
        Err(Errno::ENOSYS.into())
    }
}
