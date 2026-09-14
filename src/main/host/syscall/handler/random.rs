use linux_api::errno::Errno;
use log::*;
use rand::Rng;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::types::ForeignArrayPtr;

impl SyscallHandler {
    log_syscall!(
        getrandom,
        /* rv */ isize,
        /* buf */ *const std::ffi::c_void,
        /* count */ usize,
        /* flags */ std::ffi::c_uint,
    );
    pub fn getrandom(
        ctx: &mut SyscallContext,
        buf_ptr: ForeignPtr<u8>,
        count: usize,
        _flags: std::ffi::c_uint,
    ) -> Result<isize, Errno> {
        // We ignore the flags arg, because we use the same random source for both
        // random and urandom, and it never blocks anyway.

        trace!("Trying to read {count} random bytes.");

        // Get random bytes using host rng to maintain determinism.
        let rnd_bytes = {
            let mut v = vec![0u8; count];
            let mut rng = ctx.objs.host.random_mut();
            rng.fill_bytes(&mut v);
            v
        };

        // Copy to managed process.
        ctx.objs
            .process
            .memory_borrow_mut()
            .copy_to_ptr(ForeignArrayPtr::new(buf_ptr, count), &rnd_bytes)?;

        Ok(isize::try_from(count).unwrap())
    }
}
