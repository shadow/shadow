use std::mem::MaybeUninit;

use bitflags::Flags;
use linux_api::errno::Errno;
use linux_api::posix_types::kernel_pid_t;
use linux_api::rseq::{rseq, rseq_flags};
use linux_api::sched::{SCHED_RESET_ON_FORK, Sched, sched_attr};
use log::warn;
use shadow_shim_helper_rs::explicit_drop::{ExplicitDrop, ExplicitDropper};
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::type_formatting::SyscallNonDeterministicArg;
use crate::host::syscall::types::ForeignArrayPtr;
use crate::host::thread::{Thread, ThreadId};

// We always report that the thread is running on CPU 0, Node 0
const CURRENT_CPU: u32 = 0;

/// Run `f` on the thread targeted by the scheduler syscalls.
///
/// Linux treats a tid of 0 as the current thread for these calls. For any other tid, borrow the
/// emulated thread through a cloned `RootedRc` and explicitly drop that clone before returning.
fn with_sched_target_thread<T>(
    ctx: &SyscallContext,
    tid: kernel_pid_t,
    f: impl FnOnce(&Thread) -> T,
) -> Result<T, Errno> {
    let current_tid = kernel_pid_t::from(ctx.objs.thread.id());
    if tid == 0 || tid == current_tid {
        return Ok(f(ctx.objs.thread));
    }

    let target_tid = ThreadId::try_from(tid).or(Err(Errno::ESRCH))?;
    let Some(thread_rc) = ctx.objs.host.thread_cloned_rc(target_tid) else {
        return Err(Errno::ESRCH);
    };
    let thread_rc = ExplicitDropper::new(thread_rc, |value| {
        value.explicit_drop(ctx.objs.host.root())
    });
    let thread = thread_rc.borrow(ctx.objs.host.root());
    Ok(f(&thread))
}

/// Parse a raw Linux scheduler policy and return the supported base policy together with the
/// `SCHED_RESET_ON_FORK` flag.
fn parse_sched_policy(policy: std::ffi::c_int) -> Option<(Sched, bool)> {
    let reset_on_fork = (policy & SCHED_RESET_ON_FORK) != 0;
    let policy = Sched::try_from(policy & !SCHED_RESET_ON_FORK).ok()?;

    // Shadow only tracks the common policies handled by `sched_getparam` and friends.
    match policy {
        Sched::SCHED_NORMAL
        | Sched::SCHED_FIFO
        | Sched::SCHED_RR
        | Sched::SCHED_BATCH
        | Sched::SCHED_IDLE => Some((policy, reset_on_fork)),
        _ => None,
    }
}

/// Validate the priority rules for the scheduler policies Shadow tracks.
///
/// Non-realtime policies use a fixed priority of 0, while Linux accepts priorities 1 through 99
/// for `SCHED_FIFO` and `SCHED_RR`.
fn validate_sched_attrs(policy: Sched, priority: std::ffi::c_int) -> Result<(), Errno> {
    match policy {
        Sched::SCHED_NORMAL | Sched::SCHED_BATCH | Sched::SCHED_IDLE if priority == 0 => Ok(()),
        Sched::SCHED_FIFO | Sched::SCHED_RR if (1..=99).contains(&priority) => Ok(()),
        _ => Err(Errno::EINVAL),
    }
}

impl SyscallHandler {
    log_syscall!(
        sched_getaffinity,
        /* rv */ i32,
        // Non-deterministic due to https://github.com/shadow/shadow/issues/3626
        /* pid */
        SyscallNonDeterministicArg<kernel_pid_t>,
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
        sched_getparam,
        /* rv */ i32,
        /* pid */ kernel_pid_t,
        /* param */ *const linux_api::sched::sched_attr,
    );
    pub fn sched_getparam(
        ctx: &mut SyscallContext,
        tid: kernel_pid_t,
        param_ptr: ForeignPtr<sched_attr>,
    ) -> Result<(), Errno> {
        warn_once_then_debug!(
            "sched_getparam() only returns tracked scheduler state; Shadow does not emulate Linux scheduling behavior"
        );

        let priority = with_sched_target_thread(ctx, tid, |thread| thread.sched_priority())?;
        ctx.objs
            .process
            .memory_borrow_mut()
            .write(param_ptr.cast::<std::ffi::c_int>(), &priority)?;

        Ok(())
    }

    log_syscall!(
        sched_getscheduler,
        /* rv */ i32,
        /* pid */ kernel_pid_t,
    );
    pub fn sched_getscheduler(
        ctx: &mut SyscallContext,
        tid: kernel_pid_t,
    ) -> Result<std::ffi::c_int, Errno> {
        warn_once_then_debug!(
            "sched_getscheduler() only returns tracked scheduler state; Shadow does not emulate Linux scheduling behavior"
        );

        with_sched_target_thread(ctx, tid, |thread| {
            let mut policy = i32::from(thread.sched_policy());
            if thread.sched_reset_on_fork() {
                policy |= SCHED_RESET_ON_FORK;
            }
            policy
        })
    }

    log_syscall!(
        sched_setparam,
        /* rv */ i32,
        /* pid */ kernel_pid_t,
        /* param */ *const linux_api::sched::sched_attr,
    );
    pub fn sched_setparam(
        ctx: &mut SyscallContext,
        tid: kernel_pid_t,
        param_ptr: ForeignPtr<sched_attr>,
    ) -> Result<(), Errno> {
        warn_once_then_debug!(
            "sched_setparam() only updates tracked scheduler state; Shadow does not emulate Linux scheduling behavior"
        );

        let new_priority = ctx
            .objs
            .process
            .memory_borrow()
            .read(param_ptr.cast::<std::ffi::c_int>())?;
        let (policy, reset_on_fork) = with_sched_target_thread(ctx, tid, |thread| {
            (thread.sched_policy(), thread.sched_reset_on_fork())
        })?;

        validate_sched_attrs(policy, new_priority)?;

        with_sched_target_thread(ctx, tid, |thread| {
            thread.set_sched_attrs(policy, reset_on_fork, new_priority);
        })?;

        Ok(())
    }

    log_syscall!(
        sched_setscheduler,
        /* rv */ i32,
        /* pid */ kernel_pid_t,
        /* policy */ std::ffi::c_int,
        /* param */ *const linux_api::sched::sched_attr,
    );
    pub fn sched_setscheduler(
        ctx: &mut SyscallContext,
        tid: kernel_pid_t,
        policy: std::ffi::c_int,
        param_ptr: ForeignPtr<sched_attr>,
    ) -> Result<(), Errno> {
        warn_once_then_debug!(
            "sched_setscheduler() only updates tracked scheduler state; Shadow does not emulate Linux scheduling behavior"
        );

        let new_priority = ctx
            .objs
            .process
            .memory_borrow()
            .read(param_ptr.cast::<std::ffi::c_int>())?;
        let Some((policy, reset_on_fork)) = parse_sched_policy(policy) else {
            return Err(Errno::EINVAL);
        };
        validate_sched_attrs(policy, new_priority)?;

        with_sched_target_thread(ctx, tid, |thread| {
            thread.set_sched_attrs(policy, reset_on_fork, new_priority);
        })?;

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

        let flags = rseq_flags::from_bits_retain(flags);
        let unknown_flags = flags.unknown_bits();

        if unknown_flags != 0 {
            warn!("Unrecognized rseq flags: 0x{unknown_flags:x} in {flags:?}");
            return Err(Errno::EINVAL);
        }
        if flags.contains(rseq_flags::RSEQ_FLAG_UNREGISTER) {
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
