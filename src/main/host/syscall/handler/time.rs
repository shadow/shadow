use crate::core::support::simulation_time::SimulationTime;
use crate::core::worker::Worker;
use crate::host::context::ThreadContext;
use crate::host::syscall::handler::SyscallHandler;
use crate::host::syscall_types::SyscallResult;
use crate::host::syscall_types::{SysCallArgs, TypedPluginPtr};
use crate::host::timer::Timer;

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
    pub fn getitimer(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let which = libc::c_int::from(args.get(0));
        let curr_value_ptr = TypedPluginPtr::new::<libc::itimerval>(args.get(1).into(), 1);

        if which != libc::ITIMER_REAL {
            error!("Timer type {} unsupported", which);
            return Err(Errno::ENOSYS.into());
        }

        let itimerval = itimerval_from_timer(ctx.process.realtime_timer());
        ctx.process
            .memory_mut()
            .copy_to_ptr(curr_value_ptr, &[itimerval])?;

        Ok(0.into())
    }

    #[log_syscall(/* rv */ libc::c_int, /* which */ libc::c_int, /* new_value */ *const libc::c_void, /* old_value */ *const libc::c_void)]
    pub fn setitimer(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let which = libc::c_int::from(args.get(0));
        let new_value_ptr = TypedPluginPtr::new::<libc::itimerval>(args.get(1).into(), 1);
        let old_value_ptr = TypedPluginPtr::new::<libc::itimerval>(args.get(2).into(), 1);

        if which != libc::ITIMER_REAL {
            error!("Timer type {} unsupported", which);
            return Err(Errno::ENOSYS.into());
        }

        if !old_value_ptr.is_null() {
            let itimerval = itimerval_from_timer(ctx.process.realtime_timer());
            ctx.process
                .memory_mut()
                .copy_to_ptr(old_value_ptr, &[itimerval])?;
        }

        let new_value = ctx.process.memory().read_vals::<_, 1>(new_value_ptr)?[0];
        let new_value_value =
            SimulationTime::try_from(new_value.it_value).map_err(|_| Errno::EINVAL)?;
        let new_value_interval =
            SimulationTime::try_from(new_value.it_interval).map_err(|_| Errno::EINVAL)?;

        if new_value_value == SimulationTime::ZERO {
            ctx.process.realtime_timer_mut().disarm();
        } else {
            ctx.process.realtime_timer_mut().arm(
                ctx.host,
                Worker::current_time().unwrap() + new_value_value,
                new_value_interval,
            );
        }

        Ok(0.into())
    }
}
