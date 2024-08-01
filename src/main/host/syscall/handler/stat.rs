use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::cshadow;
use crate::host::descriptor::CompatFile;
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::types::{SyscallError, SyscallResult};

impl SyscallHandler {
    log_syscall!(statx, /* rv */ std::ffi::c_int);
    pub fn statx(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_statx, ctx)
    }

    log_syscall!(
        fstat,
        /* rv */ std::ffi::c_int,
        /* fd */ std::ffi::c_uint,
        /* statbuf */ *const linux_api::stat::stat,
    );
    pub fn fstat(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_uint,
        statbuf_ptr: ForeignPtr<linux_api::stat::stat>,
    ) -> Result<(), SyscallError> {
        let desc_table = ctx.objs.thread.descriptor_table_borrow(ctx.objs.host);
        let file = match Self::get_descriptor(&desc_table, fd)?.file() {
            CompatFile::New(file) => file.clone(),
            // if it's a legacy file, use the C syscall handler instead
            CompatFile::Legacy(_) => {
                drop(desc_table);
                let rv: i32 = Self::legacy_syscall(cshadow::syscallhandler_fstat, ctx)?;
                assert_eq!(rv, 0);
                return Ok(());
            }
        };

        let stat = file.inner_file().borrow().stat()?;

        ctx.objs
            .process
            .memory_borrow_mut()
            .write(statbuf_ptr, &stat)?;

        Ok(())
    }

    log_syscall!(fstatfs, /* rv */ std::ffi::c_int);
    pub fn fstatfs(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_fstatfs, ctx)
    }

    log_syscall!(newfstatat, /* rv */ std::ffi::c_int);
    pub fn newfstatat(ctx: &mut SyscallContext) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_newfstatat, ctx)
    }
}
