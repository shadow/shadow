use syscall_logger::log_syscall;

use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall_types::SyscallError;

impl SyscallHandler {
    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn shadow_yield(_ctx: &mut SyscallContext) -> Result<(), SyscallError> {
        Ok(())
    }

    #[log_syscall(/* rv */ std::ffi::c_int)]
    pub fn shadow_init_memory_manager(ctx: &mut SyscallContext) -> Result<(), SyscallError> {
        if !ctx.objs.host.params.use_mem_mapper {
            log::trace!("Not initializing memory mapper");
            return Ok(());
        }

        log::trace!("Initializing memory mapper");

        let mut memory_manager = ctx.objs.process.memory_borrow_mut();
        if !memory_manager.has_mapper() {
            memory_manager.init_mapper(ctx.objs)
        }

        Ok(())
    }
}
