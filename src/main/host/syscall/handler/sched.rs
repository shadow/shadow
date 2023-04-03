use log::warn;
use nix::errno::Errno;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;
use syscall_logger::log_syscall;

use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall_types::{ForeignArrayPtr, SyscallError};
use crate::host::thread::ThreadId;
use crate::utility::pod::Pod;

// We always report that the thread is running on CPU 0, Node 0
const CURRENT_CPU: u32 = 0;

const RSEQ_FLAG_UNREGISTER: i32 = 1;

#[repr(C, align(32))]
#[derive(Debug, Copy, Clone)]
pub struct rseq {
    cpu_id_start: i32,
    cpu_id: i32,
    // Actually a pointer, but guaranteed to be 64 bits even on 32 bit platforms
    rseq_cs: u64,
    flags: u32,
}

unsafe impl Pod for rseq {}

impl SyscallHandler {
    #[log_syscall(/* rv */ i32, /* pid */ libc::pid_t, /* cpusetsize */ libc::size_t, /* mask */ *const libc::c_void)]
    pub fn sched_getaffinity(
        ctx: &mut SyscallContext,
        tid: libc::pid_t,
        cpusetsize: libc::size_t,
        mask_ptr: ForeignPtr<libc::c_ulong>,
    ) -> Result<libc::c_int, SyscallError> {
        let mask_ptr = mask_ptr.cast::<u8>();
        let mask_ptr = ForeignArrayPtr::new(mask_ptr, cpusetsize);

        let tid = ThreadId::try_from(tid).or(Err(Errno::ESRCH))?;
        if !ctx.objs.host.has_thread(tid) && libc::pid_t::from(tid) != 0 {
            return Err(Errno::ESRCH.into());
        }

        // Shadow doesn't have users, so no need to check for permissions

        if cpusetsize == 0 {
            return Err(Errno::EINVAL.into());
        }

        let mut mem = ctx.objs.process.memory_borrow_mut();
        let mut mask = mem.memory_ref_mut(mask_ptr)?;

        // this assumes little endian
        mask.fill(0);
        mask[0] = 1;

        mask.flush()?;

        Ok(0)
    }

    #[log_syscall(/* rv */ i32, /* pid */ libc::pid_t, /* cpusetsize */ libc::size_t, /* mask */ *const libc::c_void)]
    pub fn sched_setaffinity(
        ctx: &mut SyscallContext,
        tid: libc::pid_t,
        cpusetsize: libc::size_t,
        mask_ptr: ForeignPtr<libc::c_ulong>,
    ) -> Result<libc::c_int, SyscallError> {
        let mask_ptr = mask_ptr.cast::<u8>();
        let mask_ptr = ForeignArrayPtr::new(mask_ptr, cpusetsize);

        let tid = ThreadId::try_from(tid).or(Err(Errno::ESRCH))?;
        if !ctx.objs.host.has_thread(tid) && libc::pid_t::from(tid) != 0 {
            return Err(Errno::ESRCH.into());
        };

        // Shadow doesn't have users, so no need to check for permissions

        if cpusetsize == 0 {
            return Err(Errno::EINVAL.into());
        }

        let mem = ctx.objs.process.memory_borrow_mut();
        let mask = mem.memory_ref(mask_ptr)?;

        // this assumes little endian
        if mask[0] & 0x01 == 0 {
            return Err(Errno::EINVAL.into());
        }

        Ok(0)
    }

    #[log_syscall(/* rv */ i32)]
    pub fn sched_yield(_ctx: &mut SyscallContext) -> Result<libc::c_int, SyscallError> {
        // Do nothing. We already yield and reschedule after some number of
        // unblocked syscalls.
        Ok(0)
    }

    #[log_syscall(/* rv */ i32, /* rseq */ *const libc::c_void, /* rseq_len */ u32, /* flags */ i32, /* sig */ u32)]
    pub fn rseq(
        ctx: &mut SyscallContext,
        rseq_ptr: ForeignPtr<rseq>,
        rseq_len: u32,
        flags: libc::c_int,
        sig: u32,
    ) -> Result<libc::c_int, SyscallError> {
        let rseq_ptr = ForeignArrayPtr::new(rseq_ptr, 1);
        let rseq_len = usize::try_from(rseq_len).unwrap();
        if rseq_len != std::mem::size_of::<rseq>() {
            // Probably worth a warning; decent chance that the bug is in Shadow
            // rather than the calling code.
            warn!(
                "rseq_len {} instead of expected {}",
                rseq_len,
                std::mem::size_of::<rseq>()
            );
            return Err(Errno::EINVAL.into());
        }
        Self::rseq_impl(ctx, rseq_ptr, flags, sig)
    }

    fn rseq_impl(
        ctx: &mut SyscallContext,
        rseq_ptr: ForeignArrayPtr<rseq>,
        flags: i32,
        _sig: u32,
    ) -> Result<libc::c_int, SyscallError> {
        if flags & (!RSEQ_FLAG_UNREGISTER) != 0 {
            warn!("Unrecognized rseq flags: {}", flags);
            return Err(Errno::EINVAL.into());
        }
        if flags & RSEQ_FLAG_UNREGISTER != 0 {
            // TODO:
            // * Validate that an rseq was previously registered
            // * Validate that `sig` matches registration
            // * Set the cpu_id of the previously registerd rseq to the uninitialized
            //   state.
            return Ok(0);
        }
        let mut mem = ctx.objs.process.memory_borrow_mut();
        let mut rseq = mem.memory_ref_mut(rseq_ptr)?;

        // rseq is mostly unimplemented, but also mostly unneeded in Shadow.
        // We'd only need to implement the "real" functionality if we ever implement
        // true preemption, in which case we'd need to do something if we ever pre-empted
        // while the user code was in a restartable sequence. As it is, Shadow only
        // reschedules threads at system calls, and system calls are disallowed inside
        // restartable sequences.
        //
        // TODO: One place where Shadow might need to implement rseq recovery is
        // if a hardware-based signal is delivered in the middle of an
        // interruptible sequence.  e.g. the code in the rseq accesses an
        // invalid address, raising SIGSEGV, but then catching it and recovering
        // in a handler.
        // https://github.com/shadow/shadow/issues/2139

        // For now we just update to reflect that the thread is running on CPU 0.
        rseq[0].cpu_id = CURRENT_CPU as i32;
        rseq[0].cpu_id_start = CURRENT_CPU as i32;
        rseq.flush()?;

        Ok(0)
    }
}
