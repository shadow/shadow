use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::cshadow as c;
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::types::SyscallError;

impl SyscallHandler {
    log_syscall!(
        poll,
        /* rv */ std::ffi::c_int,
        /* ufds */ *const std::ffi::c_void,
        /* nfds */ std::ffi::c_uint,
        /* timeout_msecs */ std::ffi::c_int,
    );
    pub fn poll(
        ctx: &mut SyscallContext,
        _ufds: ForeignPtr<linux_api::poll::pollfd>,
        _nfds: std::ffi::c_uint,
        _timeout_msecs: std::ffi::c_int,
    ) -> Result<std::ffi::c_int, SyscallError> {
        Self::legacy_syscall(c::syscallhandler_poll, ctx)
    }

    log_syscall!(
        ppoll,
        /* rv */ std::ffi::c_int,
        /* ufds */ *const std::ffi::c_void,
        /* nfds */ std::ffi::c_uint,
        /* tsp */ *const linux_api::time::kernel_timespec,
        /* sigmask */ *const std::ffi::c_void,
        /* sigsetsize */ libc::size_t,
    );
    pub fn ppoll(
        ctx: &mut SyscallContext,
        _ufds: ForeignPtr<linux_api::poll::pollfd>,
        _nfds: std::ffi::c_uint,
        _tsp: ForeignPtr<linux_api::time::kernel_timespec>,
        _sigmask: ForeignPtr<linux_api::signal::sigset_t>,
        _sigsetsize: libc::size_t,
    ) -> Result<std::ffi::c_int, SyscallError> {
        Self::legacy_syscall(c::syscallhandler_ppoll, ctx)
    }
}
