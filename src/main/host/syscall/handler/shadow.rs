use syscall_logger::log_syscall;

use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall_types::SyscallError;

impl SyscallHandler {
    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn shadow_yield(_ctx: &mut SyscallContext) -> Result<(), SyscallError> {
        Ok(())
    }
}
