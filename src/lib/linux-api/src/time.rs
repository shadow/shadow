use linux_syscall::syscall;
use linux_syscall::Result as LinuxSyscallResult;
use linux_syscall::Result64;
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

pub use bindings::linux___kernel_timespec;
#[allow(non_camel_case_types)]
pub type kernel_timespec = linux___kernel_timespec;
unsafe impl shadow_pod::Pod for kernel_timespec {}

pub use bindings::linux_timeval;
#[allow(non_camel_case_types)]
pub type timeval = linux_timeval;
unsafe impl shadow_pod::Pod for timeval {}

pub use bindings::linux___kernel_old_timeval;
#[allow(non_camel_case_types)]
pub type kernel_old_timeval = linux___kernel_old_timeval;
unsafe impl shadow_pod::Pod for kernel_old_timeval {}

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

pub use bindings::linux___kernel_old_itimerval;
#[allow(non_camel_case_types)]
pub type kernel_old_itimerval = linux___kernel_old_itimerval;
unsafe impl shadow_pod::Pod for kernel_old_itimerval {}

/// Raw `alarm` syscall. Permits u64 arg and return value for generality with
/// the general syscall ABI, but note that the `alarm` syscall definition itself
/// uses u32.
pub fn alarm_raw(secs: u64) -> Result<u64, Errno> {
    unsafe { syscall!(linux_syscall::SYS_alarm, secs) }
        .try_u64()
        .map_err(Errno::from)
}

/// Make an `alarm` syscall.
pub fn alarm(secs: u32) -> Result<u32, Errno> {
    let res = alarm_raw(secs.into())?;
    // The syscall defines the return type as u32, so it *should* always be
    // convertible to u32.
    Ok(res.try_into().unwrap())
}

/// Make a `getitimer` syscall.
///
/// # Safety
///
/// `curr_value` must be safe for the kernel to write to.
//
// Kernel decl:
// ```
// kernel/time/itimer.c:SYSCALL_DEFINE2(getitimer, int, which, struct __kernel_old_itimerval __user *, value)
// ```
pub unsafe fn getitimer_raw(
    which: core::ffi::c_int,
    curr_value: *mut kernel_old_itimerval,
) -> Result<(), Errno> {
    unsafe { syscall!(linux_syscall::SYS_getitimer, which, curr_value) }
        .check()
        .map_err(Errno::from)
}

/// Make a `getitimer` syscall.
pub fn getitimer(which: ITimerId, curr_value: &mut kernel_old_itimerval) -> Result<(), Errno> {
    unsafe { getitimer_raw(which.into(), curr_value) }
}

/// Make a `setitimer` syscall.
///
/// # Safety
///
/// `old_value` must be safe for the kernel to write to, or NULL.
///
/// An invalid or inaccessible `new_value` *isn't* a safety violation, but may
/// cause the syscall to fail e.g. with `EFAULT`.
pub unsafe fn setitimer_raw(
    which: core::ffi::c_int,
    new_value: *const kernel_old_itimerval,
    old_value: *mut kernel_old_itimerval,
) -> Result<(), Errno> {
    unsafe { syscall!(linux_syscall::SYS_setitimer, which, new_value, old_value) }
        .check()
        .map_err(Errno::from)
}

/// Make a `setitimer` syscall.
pub fn setitimer(
    which: ITimerId,
    new_value: &kernel_old_itimerval,
    old_value: Option<&mut kernel_old_itimerval>,
) -> Result<(), Errno> {
    let old_value = old_value
        .map(core::ptr::from_mut)
        .unwrap_or(core::ptr::null_mut());
    unsafe { setitimer_raw(which.into(), new_value, old_value) }
}

mod export {
    use super::*;

    #[unsafe(no_mangle)]
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
