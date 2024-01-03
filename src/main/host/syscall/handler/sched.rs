use linux_api::errno::Errno;
use linux_api::posix_types::kernel_pid_t;
use linux_api::rseq::rseq;
use log::warn;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;
use syscall_logger::log_syscall;

use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::types::{ForeignArrayPtr, SyscallError};
use crate::host::thread::ThreadId;

// We always report that the thread is running on CPU 0, Node 0
const CURRENT_CPU: u32 = 0;

const RSEQ_FLAG_UNREGISTER: i32 = 1;

impl SyscallHandler {
    #[log_syscall(/* rv */ i32, /* pid */ kernel_pid_t, /* cpusetsize */ usize, /* mask */ *const std::ffi::c_void)]
    pub fn sched_getaffinity(
        ctx: &mut SyscallContext,
        tid: kernel_pid_t,
        cpusetsize: usize,
        // sched_getaffinity(2):
        // > The underlying system calls (which represent CPU masks as bit masks
        // > of type unsigned long *) impose no restriction on the size of the CPU
        // > mask
        mask_ptr: ForeignPtr<std::ffi::c_ulong>,
    ) -> Result<std::ffi::c_int, SyscallError> {
        let mask_ptr = mask_ptr.cast::<u8>();
        let mask_ptr = ForeignArrayPtr::new(mask_ptr, cpusetsize);

        let tid = ThreadId::try_from(tid).or(Err(Errno::ESRCH))?;
        if !ctx.objs.host.has_thread(tid) && kernel_pid_t::from(tid) != 0 {
            return Err(Errno::ESRCH.into());
        }

        // Shadow doesn't have users, so no need to check for permissions

        if cpusetsize == 0 {
            return Err(Errno::EINVAL.into());
        }

        let mut mem = ctx.objs.process.memory_borrow_mut();
        let mut mask = mem.memory_ref_mut(mask_ptr)?;

        // this assumes little endian
        let bytes_written = 1;
        mask[0] = 1;

        mask.flush()?;

        Ok(bytes_written)
    }

    #[log_syscall(/* rv */ i32, /* pid */ kernel_pid_t, /* cpusetsize */ usize, /* mask */ *const std::ffi::c_void)]
    pub fn sched_setaffinity(
        ctx: &mut SyscallContext,
        tid: kernel_pid_t,
        cpusetsize: usize,
        // sched_getaffinity(2):
        // > The underlying system calls (which represent CPU masks as bit masks
        // > of type unsigned long *) impose no restriction on the size of the CPU
        // > mask
        mask_ptr: ForeignPtr<std::ffi::c_ulong>,
    ) -> Result<std::ffi::c_int, SyscallError> {
        let mask_ptr = mask_ptr.cast::<u8>();
        let mask_ptr = ForeignArrayPtr::new(mask_ptr, cpusetsize);

        let tid = ThreadId::try_from(tid).or(Err(Errno::ESRCH))?;
        if !ctx.objs.host.has_thread(tid) && kernel_pid_t::from(tid) != 0 {
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

    #[log_syscall(/* rv */ i32, /* rseq */ *const std::ffi::c_void, /* rseq_len */ u32, /* flags */ i32, /* sig */ u32)]
    pub fn rseq(
        ctx: &mut SyscallContext,
        rseq_ptr: ForeignPtr<linux_api::rseq::rseq>,
        rseq_len: u32,
        flags: std::ffi::c_int,
        sig: u32,
    ) -> Result<std::ffi::c_int, SyscallError> {
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
        rseq_ptr: ForeignPtr<linux_api::rseq::rseq>,
        flags: i32,
        _sig: u32,
    ) -> Result<std::ffi::c_int, SyscallError> {
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
        let mut rseq = mem.memory_ref_mut(ForeignArrayPtr::new(rseq_ptr, 1))?;

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
        rseq[0].cpu_id = CURRENT_CPU;
        rseq[0].cpu_id_start = CURRENT_CPU;
        rseq.flush()?;

        Ok(0)
    }
}
