use bytemuck::TransparentWrapper;
use core::result::Result;
use linux_syscall::syscall;
use linux_syscall::Result as LinuxSyscallResult;
use num_enum::{IntoPrimitive, TryFromPrimitive};

use crate::bindings;
use crate::const_conversions;
use crate::errno::Errno;

pub use bindings::linux___kernel_clockid_t;

// Clocks
#[derive(Debug, Copy, Clone, Eq, PartialEq, IntoPrimitive, TryFromPrimitive)]
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

pub use bindings::linux_timespec;

#[derive(Debug, Copy, Clone, Eq, PartialEq)]
#[repr(transparent)]
pub struct TimeSpec(linux_timespec);
unsafe impl TransparentWrapper<linux_timespec> for TimeSpec {}

pub fn clock_gettime_raw(clockid: linux___kernel_clockid_t) -> Result<TimeSpec, Errno> {
    let mut t: TimeSpec = unsafe { core::mem::zeroed() };
    let kernel_t: &mut linux_timespec = TimeSpec::peel_mut(&mut t);
    unsafe { syscall!(linux_syscall::SYS_clock_gettime, clockid, kernel_t) }
        .check()
        .map_err(Errno::from)?;
    Ok(t)
}

pub fn clock_gettime(clockid: ClockId) -> Result<TimeSpec, Errno> {
    clock_gettime_raw(clockid.into())
}

mod export {
    use super::*;

    #[no_mangle]
    pub unsafe extern "C" fn linux_clock_gettime(clockid: i32, res: *mut linux_timespec) -> i64 {
        let t = match clock_gettime_raw(clockid) {
            Ok(t) => t,
            Err(e) => return e.to_negated_i64(),
        };
        assert!(!res.is_null());
        unsafe { res.write(TimeSpec::peel(t)) }
        0
    }
}
