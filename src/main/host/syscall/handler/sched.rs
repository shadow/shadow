use crate::host::context::ThreadContext;
use crate::host::process::ProcessId;
use crate::host::syscall::handler::SyscallHandler;
use crate::host::syscall_types::{SysCallArgs, SyscallResult, TypedPluginPtr};
use crate::utility::pod::Pod;

use log::warn;
use nix::errno::Errno;
use syscall_logger::log_syscall;

// We always report that the thread is running on CPU 0, Node 0
const CURRENT_CPU: u32 = 0;

const RSEQ_FLAG_UNREGISTER: i32 = 1;

#[repr(C, align(32))]
#[derive(Debug, Copy, Clone)]
struct rseq {
    cpu_id_start: i32,
    cpu_id: i32,
    // Actually a pointer, but guaranteed to be 64 bits even on 32 bit platforms
    rseq_cs: u64,
    flags: u32,
}

unsafe impl Pod for rseq {}

impl SyscallHandler {
    #[log_syscall(/* rv */ i32, /* pid */ libc::pid_t, /* cpusetsize */ libc::size_t, /* mask */ *const libc::c_void)]
    pub fn sched_getaffinity(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let pid_t =
            libc::pid_t::try_from(unsafe { args.get(0).as_i64 }).map_err(|_| Errno::ESRCH)?;
        let cpusetsize = libc::size_t::try_from(unsafe { args.get(1).as_u64 }).unwrap();
        let mask_ptr = TypedPluginPtr::new::<u8>(unsafe { args.get(2).as_ptr }.into(), cpusetsize);

        let pid = ProcessId::try_from(pid_t).map_err(|_| Errno::ESRCH)?;
        if ctx.host.process(pid).is_none() && pid_t != 0 {
            return Err(Errno::ESRCH.into());
        };

        // Shadow doesn't have users, so no need to check for permissions

        if cpusetsize == 0 {
            return Err(Errno::EINVAL.into());
        }

        let mut mem = ctx.process.memory_mut();
        let mut mask = mem.memory_ref_mut(mask_ptr)?;

        // this assumes little endian
        mask.fill(0);
        mask[0] = 1;

        mask.flush()?;

        Ok(0.into())
    }

    #[log_syscall(/* rv */ i32, /* pid */ libc::pid_t, /* cpusetsize */ libc::size_t, /* mask */ *const libc::c_void)]
    pub fn sched_setaffinity(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let pid_t =
            libc::pid_t::try_from(unsafe { args.get(0).as_i64 }).map_err(|_| Errno::ESRCH)?;
        let cpusetsize = libc::size_t::try_from(unsafe { args.get(1).as_u64 }).unwrap();
        let mask_ptr = TypedPluginPtr::new::<u8>(unsafe { args.get(2).as_ptr }.into(), cpusetsize);

        let pid = ProcessId::try_from(pid_t).map_err(|_| Errno::ESRCH)?;
        if ctx.host.process(pid).is_none() && pid_t != 0 {
            return Err(Errno::ESRCH.into());
        };

        // Shadow doesn't have users, so no need to check for permissions

        if cpusetsize == 0 {
            return Err(Errno::EINVAL.into());
        }

        let mem = ctx.process.memory_mut();
        let mask = mem.memory_ref(mask_ptr)?;

        // this assumes little endian
        if mask[0] & 0x01 == 0 {
            return Err(Errno::EINVAL.into());
        }

        Ok(0.into())
    }

    #[log_syscall(/* rv */ i32)]
    pub fn sched_yield(&self, _ctx: &mut ThreadContext, _args: &SysCallArgs) -> SyscallResult {
        // Do nothing. We already yield and reschedule after some number of
        // unblocked syscalls.
        Ok(0.into())
    }

    #[log_syscall(/* rv */ i32, /* rseq */ *const libc::c_void, /* rseq_len */u32, /* flags */i32, /* sig */u32)]
    pub fn rseq(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let rseq_ptr = TypedPluginPtr::new::<rseq>(unsafe { args.get(0).as_ptr }.into(), 1);
        let rseq_len = usize::try_from(unsafe { args.get(1).as_u64 }).unwrap();
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
        let flags = i32::try_from(unsafe { args.get(2).as_i64 }).map_err(|_| Errno::EINVAL)?;
        let sig = u32::try_from(unsafe { args.get(3).as_u64 }).map_err(|_| Errno::EINVAL)?;
        self.rseq_impl(ctx, rseq_ptr, flags, sig)
    }

    fn rseq_impl(
        &self,
        ctx: &mut ThreadContext,
        rseq_ptr: TypedPluginPtr<rseq>,
        flags: i32,
        _sig: u32,
    ) -> SyscallResult {
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
            return Ok(0.into());
        }
        let mut mem = ctx.process.memory_mut();
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

        Ok(0.into())
    }
}
