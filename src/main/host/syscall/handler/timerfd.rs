use std::sync::Arc;

use atomic_refcell::AtomicRefCell;
use linux_api::errno::Errno;
use linux_api::fcntl::DescriptorFlags;
use linux_api::time::{itimerspec, ClockId};
use nix::sys::timerfd::{TimerFlags, TimerSetTimeFlags};
use shadow_shim_helper_rs::{
    emulated_time::EmulatedTime, simulation_time::SimulationTime, syscall_types::ForeignPtr,
};

use crate::core::worker::Worker;
use crate::host::descriptor::descriptor_table::DescriptorHandle;
use crate::host::descriptor::{
    timerfd::TimerFd, CompatFile, Descriptor, File, FileStatus, OpenFile,
};
use crate::host::{
    syscall::handler::{SyscallContext, SyscallHandler},
    syscall::types::SyscallError,
};
use crate::utility::callback_queue::CallbackQueue;

impl SyscallHandler {
    log_syscall!(
        timerfd_create,
        /* rv */ std::ffi::c_int,
        /* clockid */ linux_api::time::ClockId,
        /* flags */ std::ffi::c_int,
    );
    pub fn timerfd_create(
        ctx: &mut SyscallContext,
        clockid: std::ffi::c_int,
        flags: std::ffi::c_int,
    ) -> Result<DescriptorHandle, SyscallError> {
        let Ok(clockid) = ClockId::try_from(clockid) else {
            log::debug!("Invalid clockid: {clockid}");
            return Err(Errno::EINVAL.into());
        };

        // Continue only if we support the clockid.
        check_clockid(clockid)?;

        let Some(flags) = TimerFlags::from_bits(flags) else {
            log::debug!("Invalid timerfd_create flags: {flags}");
            return Err(Errno::EINVAL.into());
        };

        let mut file_flags = FileStatus::empty();
        let mut desc_flags = DescriptorFlags::empty();

        if flags.contains(TimerFlags::TFD_NONBLOCK) {
            file_flags.insert(FileStatus::NONBLOCK);
        }

        if flags.contains(TimerFlags::TFD_CLOEXEC) {
            desc_flags.insert(DescriptorFlags::FD_CLOEXEC);
        }

        let file = TimerFd::new(file_flags);
        let mut desc = Descriptor::new(CompatFile::New(OpenFile::new(File::TimerFd(file))));
        desc.set_flags(desc_flags);

        let fd = ctx
            .objs
            .thread
            .descriptor_table_borrow_mut(ctx.objs.host)
            .register_descriptor(desc)
            .or(Err(Errno::ENFILE))?;

        log::trace!("timerfd_create() returning fd {fd}");

        Ok(fd)
    }

    log_syscall!(
        timerfd_gettime,
        /* rv */ std::ffi::c_int,
        /* fd */ std::ffi::c_int,
        /*curr_value*/ *const std::ffi::c_void,
    );
    pub fn timerfd_gettime(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
        curr_value_ptr: ForeignPtr<linux_api::time::itimerspec>,
    ) -> Result<(), SyscallError> {
        // Get the TimerFd object.
        let file = get_cloned_file(ctx, fd)?;
        let File::TimerFd(ref timerfd) = file else {
            return Err(Errno::EINVAL.into());
        };

        Self::timerfd_gettime_helper(ctx, timerfd, curr_value_ptr)?;

        Ok(())
    }

    fn timerfd_gettime_helper(
        ctx: &mut SyscallContext,
        timerfd: &Arc<AtomicRefCell<TimerFd>>,
        value_ptr: ForeignPtr<linux_api::time::itimerspec>,
    ) -> Result<(), Errno> {
        // Lookup the timer state.
        let (remaining, interval) = {
            let borrowed = timerfd.borrow();

            // We return a zero duration if the timer is disarmed.
            let remaining = borrowed
                .get_timer_remaining()
                .unwrap_or(SimulationTime::ZERO);

            // We return a zero duration if the timer is not configured with an interval, which
            // indicates that it is non-periodic (i.e., set to expire only once).
            let interval = borrowed
                .get_timer_interval()
                .unwrap_or(SimulationTime::ZERO);

            (remaining, interval)
        };

        // Set up the result values.
        let result = itimerspec {
            it_value: remaining.try_into().unwrap(),
            it_interval: interval.try_into().unwrap(),
        };

        // Write the result to the plugin.
        ctx.objs
            .process
            .memory_borrow_mut()
            .write(value_ptr, &result)?;

        Ok(())
    }

    log_syscall!(
        timerfd_settime,
        /* rv */ std::ffi::c_int,
        /* fd */ std::ffi::c_int,
        /* flags */ std::ffi::c_int,
        /* new_value */ *const std::ffi::c_void,
        /* old_value */ *const std::ffi::c_void,
    );
    pub fn timerfd_settime(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
        flags: std::ffi::c_int,
        new_value_ptr: ForeignPtr<linux_api::time::itimerspec>,
        old_value_ptr: ForeignPtr<linux_api::time::itimerspec>,
    ) -> Result<(), SyscallError> {
        let Some(flags) = TimerSetTimeFlags::from_bits(flags) else {
            log::debug!("Invalid timerfd_settime flags: {flags}");
            return Err(Errno::EINVAL.into());
        };

        // Get the TimerFd object.
        let file = get_cloned_file(ctx, fd)?;
        let File::TimerFd(ref timerfd) = file else {
            return Err(Errno::EINVAL.into());
        };

        // Read in the new value from the plugin.
        let new_value = ctx.objs.process.memory_borrow().read(new_value_ptr)?;

        // Verify a valid range for new_time nanosecond vals.
        let value = SimulationTime::try_from(new_value.it_value).or(Err(Errno::EINVAL))?;
        let interval = SimulationTime::try_from(new_value.it_interval).or(Err(Errno::EINVAL))?;

        // First, write out the old time if requested.
        if !old_value_ptr.is_null() {
            // Old value is always relative, even if TFD_TIMER_ABSTIME is set.
            Self::timerfd_gettime_helper(ctx, timerfd, old_value_ptr)?;
        }

        // Now we can adjust the timer with the new_value.
        if value.is_zero() {
            // A value of 0 disarms the timer; it_interval is ignored.
            CallbackQueue::queue_and_run(|cb_queue| {
                timerfd.borrow_mut().disarm_timer(cb_queue);
            });
            log::trace!("TimerFd {fd} disarmed");
        } else {
            // Need to arm the timer, value may be absolute or relative.
            let now = Worker::current_time().unwrap();

            let expire_time = {
                let base = match flags.contains(TimerSetTimeFlags::TFD_TIMER_ABSTIME) {
                    true => EmulatedTime::UNIX_EPOCH,
                    false => now,
                };
                // The man page does not specify what happens if the configured time is in the past.
                // On Linux, the result is an immediate timer expiration.
                EmulatedTime::max(base + value, now)
            };

            CallbackQueue::queue_and_run(|cb_queue| {
                timerfd.borrow_mut().arm_timer(
                    ctx.objs.host,
                    expire_time,
                    interval.is_positive().then_some(interval),
                    cb_queue,
                );
            });

            log::trace!(
                "TimerFd {fd} armed to expire at {expire_time:?} (in {:?})",
                expire_time.duration_since(&now)
            );
        }

        Ok(())
    }
}

/// Checks the clockid; returns `Ok(())` if the clockid is `CLOCK_REALTIME` or
/// `CLOCK_MONOTONIC`, or the appropriate errno if the clockid is unknown or
/// unsupported.
fn check_clockid(clockid: ClockId) -> Result<(), Errno> {
    if clockid == ClockId::CLOCK_MONOTONIC || clockid == ClockId::CLOCK_REALTIME {
        return Ok(());
    }

    warn_once_then_debug!("Unsupported clockid {clockid:?}");
    Err(Errno::EINVAL)
}

fn get_cloned_file(ctx: &mut SyscallContext, fd: std::ffi::c_int) -> Result<File, Errno> {
    // get the descriptor, or return error if it doesn't exist
    let desc_table = ctx.objs.thread.descriptor_table_borrow(ctx.objs.host);
    let desc = SyscallHandler::get_descriptor(&desc_table, fd)?;

    // Our TimerFd is a New Rust type, if we get a Legacy C type it must not be a TimerFd.
    let CompatFile::New(file) = desc.file() else {
        return Err(Errno::EINVAL);
    };

    Ok(file.inner_file().clone())
}
