use crate::host::context::ThreadContext;
use crate::host::syscall::handler::SyscallHandler;
use crate::host::syscall_types::{PluginPtr, SysCallArgs, SyscallResult, TypedPluginPtr};
use rand::RngCore;

use log::*;
use nix::errno::Errno;

use syscall_logger::log_syscall;

impl SyscallHandler {
    #[log_syscall(/* rv */ libc::ssize_t, /* buf */ *const libc::c_void, /* count */ libc::size_t,
                  /* flags */ libc::c_uint)]
    pub fn getrandom(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let buf_ptr: PluginPtr = args.get(0).into(); // char*
        let count: libc::size_t = args.get(1).into();

        // We ignore the flags arg, because we use the same random source for both
        // random and urandom, and it never blocks anyway.

        trace!("Trying to read {} random bytes.", count);

        // Get a native-process mem buffer where we can copy the random bytes.
        let dst_ptr = TypedPluginPtr::new::<u8>(buf_ptr, count);
        let mut memory = ctx.process.memory_mut();
        let mut mem_ref = match memory.memory_ref_mut_uninit(dst_ptr) {
            Ok(m) => m,
            Err(e) => {
                warn!("Failed to get memory ref: {:?}", e);
                return Err(Errno::EFAULT.into());
            }
        };

        // Get random bytes using host rng to maintain determinism.
        let mut rng = ctx.host.random_mut();
        rng.fill_bytes(&mut mem_ref);

        // We must flush the memory reference to write it back.
        match mem_ref.flush() {
            Ok(()) => Ok(libc::ssize_t::try_from(count).unwrap().into()),
            Err(e) => {
                warn!("Failed to flush writes: {:?}", e);
                Err(Errno::EFAULT.into())
            }
        }
    }
}
