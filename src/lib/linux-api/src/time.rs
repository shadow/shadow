use core::result::Result;
use linux_syscall::syscall;
use linux_syscall::Result as LinuxSyscallResult;
use num_enum::{IntoPrimitive, TryFromPrimitive};

use crate::bindings;
use crate::const_conversions;
use crate::errno::Errno;

pub use bindings::linux___kernel_clockid_t;

/// Clocks
#[derive(Debug, Copy, Clone, Eq, PartialEq, IntoPrimitive, TryFromPrimitive)]
// clock_gettime syscall takes clockid_t, which is i32:
// ```
// kernel/time/posix-timers.c:SYSCALL_DEFINE2(clock_gettime, const clockid_t, which_clock,
// include/linux/types.h:typedef __kernel_clockid_t        clockid_t;
// include/uapi/asm-generic/posix_types.h:typedef int              __kernel_clockid_t;
// ```
#[repr(i32)]
#[allow(non_camel_case_types)]
pub enum ClockId {
    CLOCK_REALTIME = const_conversions::i32_from_u32(bindings::LINUX_CLOCK_REALTIME),
    CLOCK_MONOTONIC = const_conversions::i32_from_u32(bindings::LINUX_CLOCK_MONOTONIC),
    CLOCK_PROCESS_CPUTIME_ID =
        const_conversions::i32_from_u32(bindings::LINUX_CLOCK_PROCESS_CPUTIME_ID),
    CLOCK_THREAD_CPUTIME_ID =
        const_conversions::i32_from_u32(bindings::LINUX_CLOCK_THREAD_CPUTIME_ID),
    CLOCK_MONOTONIC_RAW = const_conversions::i32_from_u32(bindings::LINUX_CLOCK_MONOTONIC_RAW),
    CLOCK_REALTIME_COARSE = const_conversions::i32_from_u32(bindings::LINUX_CLOCK_REALTIME_COARSE),
    CLOCK_MONOTONIC_COARSE =
        const_conversions::i32_from_u32(bindings::LINUX_CLOCK_MONOTONIC_COARSE),
    CLOCK_BOOTTIME = const_conversions::i32_from_u32(bindings::LINUX_CLOCK_BOOTTIME),
    CLOCK_REALTIME_ALARM = const_conversions::i32_from_u32(bindings::LINUX_CLOCK_REALTIME_ALARM),
    CLOCK_BOOTTIME_ALARM = const_conversions::i32_from_u32(bindings::LINUX_CLOCK_BOOTTIME_ALARM),
    CLOCK_SGI_CYCLE = const_conversions::i32_from_u32(bindings::LINUX_CLOCK_SGI_CYCLE),
    CLOCK_TAI = const_conversions::i32_from_u32(bindings::LINUX_CLOCK_TAI),
}

bitflags::bitflags! {
    /// Valid flags passed to `clock_nanosleep(2)`.
    #[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
    pub struct ClockNanosleepFlags: i32 {
        const TIMER_ABSTIME = const_conversions::i32_from_u32(bindings::LINUX_TIMER_ABSTIME);
    }
}

/// Interval timers
#[derive(Debug, Copy, Clone, Eq, PartialEq, IntoPrimitive, TryFromPrimitive)]
// getitimer takes `int`:
// ```
// kernel/time/itimer.c:SYSCALL_DEFINE2(getitimer, int, which, struct __kernel_old_itimerval __user *, value)
// ```
#[repr(i32)]
#[allow(non_camel_case_types)]
pub enum ITimerId {
    ITIMER_REAL = const_conversions::i32_from_u32(bindings::LINUX_ITIMER_REAL),
    ITIMER_VIRTUAL = const_conversions::i32_from_u32(bindings::LINUX_ITIMER_VIRTUAL),
    ITIMER_PROF = const_conversions::i32_from_u32(bindings::LINUX_ITIMER_PROF),
}

pub use bindings::linux_timespec;
#[allow(non_camel_case_types)]
pub type timespec = linux_timespec;
unsafe impl shadow_pod::Pod for timespec {}

pub use bindings::linux_timeval;
#[allow(non_camel_case_types)]
pub type timeval = linux_timeval;

pub use bindings::linux___kernel_old_timeval;
#[allow(non_camel_case_types)]
pub type old_timeval = linux___kernel_old_timeval;

pub fn clock_gettime_raw(clockid: linux___kernel_clockid_t) -> Result<timespec, Errno> {
    let mut t = shadow_pod::zeroed();
    unsafe { syscall!(linux_syscall::SYS_clock_gettime, clockid, &mut t) }
        .check()
        .map_err(Errno::from)?;
    Ok(t)
}

pub fn clock_gettime(clockid: ClockId) -> Result<timespec, Errno> {
    clock_gettime_raw(clockid.into())
}

pub use bindings::linux_itimerspec;
#[allow(non_camel_case_types)]
pub type itimerspec = linux_itimerspec;
unsafe impl shadow_pod::Pod for itimerspec {}

pub use bindings::linux_itimerval;
#[allow(non_camel_case_types)]
pub type itimerval = linux_itimerval;
unsafe impl shadow_pod::Pod for itimerval {}

mod export {
    use super::*;

    #[no_mangle]
    pub unsafe extern "C-unwind" fn linux_clock_gettime(
        clockid: i32,
        res: *mut linux_timespec,
    ) -> i64 {
        let t = match clock_gettime_raw(clockid) {
            Ok(t) => t,
            Err(e) => return e.to_negated_i64(),
        };
        assert!(!res.is_null());
        unsafe { res.write(t) }
        0
    }
}
