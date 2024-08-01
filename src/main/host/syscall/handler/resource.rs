use linux_api::errno::Errno;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::types::SyscallError;

impl SyscallHandler {
    log_syscall!(
        prlimit64,
        /* rv */ std::ffi::c_int,
        /* pid */ linux_api::posix_types::kernel_pid_t,
        /* resource */ std::ffi::c_uint,
        /* new_rlim */ *const std::ffi::c_void,
        /* old_rlim */ *const std::ffi::c_void,
    );
    pub fn prlimit64(
        _ctx: &mut SyscallContext,
        pid: linux_api::posix_types::kernel_pid_t,
        resource: std::ffi::c_uint,
        _new_rlim: ForeignPtr<()>,
        _old_rlim: ForeignPtr<()>,
    ) -> Result<(), SyscallError> {
        log::trace!("prlimit64 called on pid {pid} for resource {resource}");

        // TODO: For determinism, we may want to enforce static limits for certain resources, like
        // RLIMIT_NOFILE. Some applications like Tor will change behavior depending on these limits.

        if pid == 0 {
            // process is calling prlimit on itself
            Err(SyscallError::Native)
        } else {
            // TODO: We do not currently support adjusting other processes limits. To support it, we
            // just need to find the native pid associated with pid, and call prlimit on the native
            // pid instead.
            Err(Errno::EOPNOTSUPP.into())
        }
    }
}
