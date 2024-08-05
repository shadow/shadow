use linux_api::errno::Errno;
use log::*;
use rand::RngCore;
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

        // Get a native-process mem buffer where we can copy the random bytes.
        let dst_ptr = ForeignArrayPtr::new(buf_ptr, count);
        let mut memory = ctx.objs.process.memory_borrow_mut();
        let mut mem_ref = match memory.memory_ref_mut_uninit(dst_ptr) {
            Ok(m) => m,
            Err(e) => {
                warn!("Failed to get memory ref: {e:?}");
                return Err(Errno::EFAULT);
            }
        };

        // Get random bytes using host rng to maintain determinism.
        let mut rng = ctx.objs.host.random_mut();
        rng.fill_bytes(&mut mem_ref);

        // We must flush the memory reference to write it back.
        match mem_ref.flush() {
            Ok(()) => Ok(isize::try_from(count).unwrap()),
            Err(e) => {
                warn!("Failed to flush writes: {e:?}");
                Err(Errno::EFAULT)
            }
        }
    }
}
