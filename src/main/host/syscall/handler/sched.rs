use std::mem::MaybeUninit;

use linux_api::errno::Errno;
use linux_api::posix_types::kernel_pid_t;
use linux_api::rseq::rseq;
use log::warn;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::types::ForeignArrayPtr;
use crate::host::thread::ThreadId;

// We always report that the thread is running on CPU 0, Node 0
const CURRENT_CPU: u32 = 0;

const RSEQ_FLAG_UNREGISTER: i32 = 1;

impl SyscallHandler {
    log_syscall!(
        sched_getaffinity,
        /* rv */ i32,
        /* pid */ kernel_pid_t,
        /* cpusetsize */ usize,
        /* mask */ *const std::ffi::c_void,
    );
    pub fn sched_getaffinity(
        ctx: &mut SyscallContext,
        tid: kernel_pid_t,
        cpusetsize: usize,
        // sched_getaffinity(2):
        // > The underlying system calls (which represent CPU masks as bit masks
        // > of type unsigned long *) impose no restriction on the size of the CPU
        // > mask
        mask_ptr: ForeignPtr<std::ffi::c_ulong>,
    ) -> Result<std::ffi::c_int, Errno> {
        let mask_ptr = mask_ptr.cast::<u8>();
        let mask_ptr = ForeignArrayPtr::new(mask_ptr, cpusetsize);

        let tid = ThreadId::try_from(tid).or(Err(Errno::ESRCH))?;
        if !ctx.objs.host.has_thread(tid) && kernel_pid_t::from(tid) != 0 {
            return Err(Errno::ESRCH);
        }

        // Shadow doesn't have users, so no need to check for permissions

        if cpusetsize == 0 {
            return Err(Errno::EINVAL);
        }

        let mut mem = ctx.objs.process.memory_borrow_mut();
        let mut mask = mem.memory_ref_mut(mask_ptr)?;

        // this assumes little endian
        let bytes_written = 1;
        mask[0] = 1;

        mask.flush()?;

        Ok(bytes_written)
    }

    log_syscall!(
        sched_setaffinity,
        /* rv */ i32,
        /* pid */ kernel_pid_t,
        /* cpusetsize */ usize,
        /* mask */ *const std::ffi::c_void,
    );
    pub fn sched_setaffinity(
        ctx: &mut SyscallContext,
        tid: kernel_pid_t,
        cpusetsize: usize,
        // sched_getaffinity(2):
        // > The underlying system calls (which represent CPU masks as bit masks
        // > of type unsigned long *) impose no restriction on the size of the CPU
        // > mask
        mask_ptr: ForeignPtr<std::ffi::c_ulong>,
    ) -> Result<(), Errno> {
        let mask_ptr = mask_ptr.cast::<u8>();
        let mask_ptr = ForeignArrayPtr::new(mask_ptr, cpusetsize);

        let tid = ThreadId::try_from(tid).or(Err(Errno::ESRCH))?;
        if !ctx.objs.host.has_thread(tid) && kernel_pid_t::from(tid) != 0 {
            return Err(Errno::ESRCH);
        };

        // Shadow doesn't have users, so no need to check for permissions

        if cpusetsize == 0 {
            return Err(Errno::EINVAL);
        }

        let mem = ctx.objs.process.memory_borrow_mut();
        let mask = mem.memory_ref(mask_ptr)?;

        // this assumes little endian
        if mask[0] & 0x01 == 0 {
            return Err(Errno::EINVAL);
        }

        Ok(())
    }

    log_syscall!(
        rseq,
        /* rv */ i32,
        /* rseq */ *const std::ffi::c_void,
        /* rseq_len */ u32,
        /* flags */ i32,
        /* sig */ u32,
    );
    pub fn rseq(
        ctx: &mut SyscallContext,
        rseq_ptr: ForeignPtr<MaybeUninit<u8>>,
        rseq_len: u32,
        flags: std::ffi::c_int,
        _sig: u32,
    ) -> Result<(), Errno> {
        // we won't need more bytes than the size of the `rseq` struct
        let rseq_len = rseq_len.try_into().unwrap();
        let rseq_len = std::cmp::min(rseq_len, std::mem::size_of::<rseq>());

        if flags & (!RSEQ_FLAG_UNREGISTER) != 0 {
            warn!("Unrecognized rseq flags: {flags}");
            return Err(Errno::EINVAL);
        }
        if flags & RSEQ_FLAG_UNREGISTER != 0 {
            // TODO:
            // * Validate that an rseq was previously registered
            // * Validate that `sig` matches registration
            // * Set the cpu_id of the previously registerd rseq to the uninitialized
            //   state.
            return Ok(());
        }

        // The `rseq` struct is designed to grow as linux needs to add more features, so we can't
        // assume that the application making the rseq syscall is using the exact same struct as we
        // have available in the linux_api crate (the calling application's rseq struct may have
        // more or fewer fields). Furthermore, the rseq struct ends with a "flexible array member",
        // which means that the rseq struct cannot be `Copy` and therefore not `Pod`.
        //
        // Instead, we should treat the rseq struct as a bunch of bytes and write to individual
        // fields if possible without making assumptions about the size of the data.
        let mut mem = ctx.objs.process.memory_borrow_mut();
        let mut rseq_mem = mem.memory_ref_mut(ForeignArrayPtr::new(rseq_ptr, rseq_len))?;
        let rseq_bytes = &mut *rseq_mem;

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
        //
        // For now we just update to reflect that the thread is running on CPU 0.

        let Some((cpu_id, cpu_id_start)) = field_project!(rseq_bytes, rseq, (cpu_id, cpu_id_start))
        else {
            return Err(Errno::EINVAL);
        };

        cpu_id.write(CURRENT_CPU);
        cpu_id_start.write(CURRENT_CPU);

        rseq_mem.flush()?;

        Ok(())
    }
}
