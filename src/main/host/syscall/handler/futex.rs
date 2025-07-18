use linux_api::errno::Errno;
use linux_api::futex::{FUTEX_BITSET_MATCH_ANY, FutexOpFlags};
use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::simulation_time::SimulationTime;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::core::worker::Worker;
use crate::cshadow as c;
use crate::host::futex_table::FutexRef;
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::type_formatting::SyscallNonDeterministicArg;
use crate::host::syscall::types::SyscallError;

impl SyscallHandler {
    log_syscall!(
        futex,
        /* rv */ std::ffi::c_int,
        /* uaddr */ *const u32,
        /* op */ std::ffi::c_int,
        /* val */ u32,
        /* utime */ *const std::ffi::c_void,
        /* uaddr2 */ *const u32,
        /* val3 */ SyscallNonDeterministicArg<u32>,
    );
    pub fn futex(
        ctx: &mut SyscallContext,
        uaddr: ForeignPtr<u32>,
        op: std::ffi::c_int,
        val: u32,
        utime: ForeignPtr<linux_api::time::kernel_timespec>,
        _uaddr2: ForeignPtr<u32>,
        val3: u32,
    ) -> Result<std::ffi::c_int, SyscallError> {
        // TODO: currently only supports uaddr from the same virtual address space (i.e., process)
        // Support across different address spaces requires us to compute a unique id from the
        // hardware address (i.e., page table and offset). This is needed, e.g., when using
        // futexes across process boundaries.

        let op = FutexOpFlags::from_bits_retain(op);

        const POSSIBLE_OPTIONS: FutexOpFlags =
            FutexOpFlags::FUTEX_PRIVATE_FLAG.union(FutexOpFlags::FUTEX_CLOCK_REALTIME);
        let options = op.intersection(POSSIBLE_OPTIONS);
        let operation = op.difference(POSSIBLE_OPTIONS);

        log::trace!(
            "futex called with addr={uaddr:p} op={op:?} (operation={operation:?} and options={options:?}) and val={val}",
        );

        match operation {
            FutexOpFlags::FUTEX_WAIT => {
                log::trace!("Handling FUTEX_WAIT operation {operation:?}");
                return Self::futex_wait_helper(ctx, uaddr, val, utime, TimeoutType::Relative);
            }
            FutexOpFlags::FUTEX_WAKE => {
                log::trace!("Handling FUTEX_WAKE operation {operation:?}");
                // TODO: Should we do better than a cast here? Maybe should add a test for this,
                // and/or warn if it overflows?
                return Ok(Self::futex_wake_helper(ctx, uaddr.cast::<()>(), val) as i32);
            }
            FutexOpFlags::FUTEX_WAIT_BITSET => {
                log::trace!("Handling FUTEX_WAIT_BITSET operation {operation:?} bitset {val3:b}");
                if val3 == FUTEX_BITSET_MATCH_ANY {
                    return Self::futex_wait_helper(ctx, uaddr, val, utime, TimeoutType::Absolute);
                }
                // Other bitsets not yet handled.
            }
            FutexOpFlags::FUTEX_WAKE_BITSET => {
                log::trace!("Handling FUTEX_WAKE_BITSET operation {operation:?} bitset {val3:b}");
                if val3 == FUTEX_BITSET_MATCH_ANY {
                    // TODO: Should we do better than a cast here? Maybe should add a test for this,
                    // and/or warn if it overflows?
                    return Ok(Self::futex_wake_helper(ctx, uaddr.cast::<()>(), val) as i32);
                }
                // Other bitsets not yet handled.
            }
            _ => {}
        }

        log::warn!("Unhandled futex operation {operation:?}");
        Err(Errno::ENOSYS.into())
    }

    fn futex_wake_helper(ctx: &mut SyscallContext, ptr: ForeignPtr<()>, num_wakeups: u32) -> u32 {
        // convert the virtual ptr to a physical ptr that can uniquely identify the futex
        let ptr = ctx.objs.process.physical_address(ptr);

        // lookup the futex in the futex table
        let table = ctx.objs.host.futextable_borrow();
        let futex = table.get(ptr);

        let Some(futex) = futex else {
            log::trace!("No futex found at futex addr {ptr:p}");
            return 0;
        };

        log::trace!("Found futex {:p} at futex addr {ptr:p}", futex.ptr());

        if num_wakeups == 0 {
            return 0;
        }

        log::trace!("Futex trying to perform {num_wakeups} wakeups");
        let num_woken = futex.wake(num_wakeups);
        log::trace!("Futex was able to perform {num_woken}/{num_wakeups} wakeups");

        num_woken
    }

    fn futex_wait_helper(
        ctx: &mut SyscallContext,
        ptr: ForeignPtr<u32>,
        expected_val: u32,
        timeout: ForeignPtr<linux_api::time::kernel_timespec>,
        timeout_type: TimeoutType,
    ) -> Result<i32, SyscallError> {
        let mem = ctx.objs.process.memory_borrow();

        // This is a new wait operation on the futex for this thread.
        // Check if a timeout was given in the syscall args.
        let timeout = if timeout.is_null() {
            None
        } else {
            let tspec = mem.read(timeout)?;
            let sim_time = SimulationTime::try_from(tspec).map_err(|_| Errno::EINVAL)?;
            Some(sim_time)
        };

        // Normally, the load/compare is done atomically. Since Shadow does not run multiple
        // threads from the same plugin at the same time, we do not use atomic ops.
        // `man 2 futex`: blocking via a futex is an atomic compare-and-block operation
        let futex_val = mem.read(ptr)?;

        log::trace!("Futex value is {futex_val}, expected value is {expected_val}");
        if !ctx.handler.is_blocked() && futex_val != expected_val {
            log::trace!("Futex values don't match, try again later");
            return Err(Errno::EAGAIN.into());
        }

        // convert the virtual ptr to a physical ptr that can uniquely identify the futex
        let ptr = ctx.objs.process.physical_address(ptr.cast::<()>());

        // check if we already have a futex
        let mut table = ctx.objs.host.futextable_borrow_mut();
        let futex = table.get(ptr);

        if ctx.handler.is_blocked() {
            let futex = futex.expect("syscall was blocked, but there wasn't an existing futex");

            let result;

            // we already blocked on wait, so this is either a timeout or wakeup
            if timeout.is_some() && ctx.handler.did_listen_timeout_expire() {
                // timeout while waiting for a wakeup
                log::trace!("Futex {ptr:p} timeout out while waiting");
                result = Err(Errno::ETIMEDOUT);
            } else if ctx.objs.thread.unblocked_signal_pending(
                ctx.objs.process,
                &ctx.objs.host.shim_shmem_lock_borrow().unwrap(),
            ) {
                log::trace!("Futex {ptr:p} has been interrupted by a signal");
                result = Err(Errno::EINTR);
            } else {
                // proper wakeup from another thread
                log::trace!("Futex {ptr:p} has been woken up");
                result = Ok(0);
            }

            // dynamically clean up the futex if needed
            if futex.listener_count() == 0 {
                log::trace!("Dynamically freed a futex object for futex addr {ptr:p}");
                table.remove(ptr).expect("futex disappeared");
            }

            return result.map_err(Into::into);
        }

        // we'll need to block; dynamically create a futex if one does not yet exist
        let futex = match futex {
            Some(x) => x.clone(),
            None => {
                log::trace!("Dynamically created a new futex object for futex addr {ptr:p}");

                let futex = unsafe { c::futex_new(ptr) };
                assert!(!futex.is_null());
                let futex = unsafe { FutexRef::new(futex) };

                table
                    .add(futex.clone())
                    .expect("new futex is already in table");

                futex
            }
        };

        // now we need to block until another thread does a wake on the futex
        log::trace!(
            "Futex blocking for wakeup {} timeout",
            if timeout.is_some() { "with" } else { "without" },
        );
        let mut rv = SyscallError::new_blocked_on_futex(futex, /* restartable= */ true);
        if let Some(timeout) = timeout {
            let now = Worker::current_time().unwrap();
            let timeout = match timeout_type {
                TimeoutType::Relative => now + timeout,
                TimeoutType::Absolute => EmulatedTime::UNIX_EPOCH + timeout,
            };

            // handle if the timeout has already expired
            let timeout = std::cmp::max(timeout, now);

            rv.blocked_condition().unwrap().set_timeout(Some(timeout));
        }

        Err(rv)
    }

    log_syscall!(
        get_robust_list,
        /* rv */ std::ffi::c_int,
        /* pid */ std::ffi::c_int,
        /* head_ptr */ *const std::ffi::c_void,
        /* len_ptr */ *const libc::size_t,
    );
    pub fn get_robust_list(
        _ctx: &mut SyscallContext,
        _pid: std::ffi::c_int,
        _head_ptr: ForeignPtr<ForeignPtr<linux_api::futex::robust_list_head>>,
        _len_ptr: ForeignPtr<libc::size_t>,
    ) -> Result<(), Errno> {
        warn_once_then_debug!("get_robust_list was called but we don't yet support it");
        Err(Errno::ENOSYS)
    }

    log_syscall!(
        set_robust_list,
        /* rv */ std::ffi::c_int,
        /* head */ *const std::ffi::c_void,
        /* len */ libc::size_t,
    );
    pub fn set_robust_list(
        _ctx: &mut SyscallContext,
        _head: ForeignPtr<linux_api::futex::robust_list_head>,
        _len: libc::size_t,
    ) -> Result<(), Errno> {
        warn_once_then_debug!("set_robust_list was called but we don't yet support it");
        Err(Errno::ENOSYS)
    }
}

/// The type of futex-wait timeout.
enum TimeoutType {
    /// Timeout is relative to current time.
    Relative,
    /// Timeout is absolute.
    Absolute,
}
