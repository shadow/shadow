use crate::core::worker::Worker;
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall_types::{SyscallResult, TypedPluginPtr};
use crate::host::timer::Timer;
use shadow_shim_helper_rs::simulation_time::SimulationTime;
use shadow_shim_helper_rs::syscall_types::PluginPtr;

use log::*;
use nix::errno::Errno;

use syscall_logger::log_syscall;

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
        curr_value_ptr: PluginPtr,
    ) -> SyscallResult {
        let curr_value_ptr = TypedPluginPtr::new::<libc::itimerval>(curr_value_ptr, 1);

        if which != libc::ITIMER_REAL {
            error!("Timer type {} unsupported", which);
            return Err(Errno::ENOSYS.into());
        }

        let itimerval = itimerval_from_timer(&ctx.objs.process.realtime_timer_borrow());
        ctx.objs
            .process
            .memory_borrow_mut()
            .copy_to_ptr(curr_value_ptr, &[itimerval])?;

        Ok(0.into())
    }

    #[log_syscall(/* rv */ libc::c_int, /* which */ libc::c_int, /* new_value */ *const libc::c_void, /* old_value */ *const libc::c_void)]
    pub fn setitimer(
        ctx: &mut SyscallContext,
        which: libc::c_int,
        new_value_ptr: PluginPtr,
        old_value_ptr: PluginPtr,
    ) -> SyscallResult {
        let new_value_ptr = TypedPluginPtr::new::<libc::itimerval>(new_value_ptr, 1);
        let old_value_ptr = TypedPluginPtr::new::<libc::itimerval>(old_value_ptr, 1);

        if which != libc::ITIMER_REAL {
            error!("Timer type {} unsupported", which);
            return Err(Errno::ENOSYS.into());
        }

        if !old_value_ptr.is_null() {
            let itimerval = itimerval_from_timer(&ctx.objs.process.realtime_timer_borrow());
            ctx.objs
                .process
                .memory_borrow_mut()
                .copy_to_ptr(old_value_ptr, &[itimerval])?;
        }

        let new_value = ctx
            .objs
            .process
            .memory_borrow()
            .read_vals::<_, 1>(new_value_ptr)?[0];
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
