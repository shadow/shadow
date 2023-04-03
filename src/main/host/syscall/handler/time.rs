use log::*;
use nix::errno::Errno;
use shadow_shim_helper_rs::simulation_time::SimulationTime;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;
use syscall_logger::log_syscall;

use crate::core::worker::Worker;
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall_types::SyscallResult;
use crate::host::timer::Timer;

fn itimerval_from_timer(timer: &Timer) -> libc::itimerval {
    libc::itimerval {
        it_interval: timer.expire_interval().try_into().unwrap(),
        it_value: timer
            .remaining_time()
            .unwrap_or(SimulationTime::ZERO)
            .try_into()
            .unwrap(),
    }
}

impl SyscallHandler {
    #[log_syscall(/* rv */ libc::c_int, /* which */ libc::c_int, /*curr_value*/ *const libc::c_void)]
    pub fn getitimer(
        ctx: &mut SyscallContext,
        which: libc::c_int,
        curr_value_ptr: ForeignPtr<libc::itimerval>,
    ) -> SyscallResult {
        if which != libc::ITIMER_REAL {
            error!("Timer type {} unsupported", which);
            return Err(Errno::ENOSYS.into());
        }

        let itimerval = itimerval_from_timer(&ctx.objs.process.realtime_timer_borrow());
        ctx.objs
            .process
            .memory_borrow_mut()
            .write(curr_value_ptr, &itimerval)?;

        Ok(0.into())
    }

    #[log_syscall(/* rv */ libc::c_int, /* which */ libc::c_int, /* new_value */ *const libc::c_void, /* old_value */ *const libc::c_void)]
    pub fn setitimer(
        ctx: &mut SyscallContext,
        which: libc::c_int,
        new_value_ptr: ForeignPtr<libc::itimerval>,
        old_value_ptr: ForeignPtr<libc::itimerval>,
    ) -> SyscallResult {
        if which != libc::ITIMER_REAL {
            error!("Timer type {} unsupported", which);
            return Err(Errno::ENOSYS.into());
        }

        if !old_value_ptr.is_null() {
            let itimerval = itimerval_from_timer(&ctx.objs.process.realtime_timer_borrow());
            ctx.objs
                .process
                .memory_borrow_mut()
                .write(old_value_ptr, &itimerval)?;
        }

        let new_value = ctx.objs.process.memory_borrow().read(new_value_ptr)?;
        let new_value_value =
            SimulationTime::try_from(new_value.it_value).map_err(|_| Errno::EINVAL)?;
        let new_value_interval =
            SimulationTime::try_from(new_value.it_interval).map_err(|_| Errno::EINVAL)?;

        if new_value_value == SimulationTime::ZERO {
            ctx.objs.process.realtime_timer_borrow_mut().disarm();
        } else {
            ctx.objs.process.realtime_timer_borrow_mut().arm(
                ctx.objs.host,
                Worker::current_time().unwrap() + new_value_value,
                new_value_interval,
            );
        }

        Ok(0.into())
    }
}
