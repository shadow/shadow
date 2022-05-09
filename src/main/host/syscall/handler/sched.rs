use crate::host::context::ThreadContext;
use crate::host::syscall::handler::SyscallHandler;
use crate::host::syscall_types::{SysCallArgs, SyscallResult};

use syscall_logger::log_syscall;

impl SyscallHandler {
    #[log_syscall(/* rv */ i32)]
    pub fn sched_yield(&self, _ctx: &mut ThreadContext, _args: &SysCallArgs) -> SyscallResult {
        // Do nothing. We already yield and reschedule after some number of
        // unblocked syscalls.
        Ok(0.into())
    }
}
