use core::result::Result;
use linux_syscall::syscall;
use linux_syscall::Result as LinuxSyscallResult;
use num_enum::{IntoPrimitive, TryFromPrimitive};

use crate::bindings;
use crate::errno::Errno;

// Clocks
#[derive(Debug, Copy, Clone, IntoPrimitive, TryFromPrimitive)]
#[repr(i32)]
#[allow(non_camel_case_types)]
pub enum ClockId {
    REALTIME = bindings::CLOCK_REALTIME as bindings::__kernel_clockid_t,
    MONOTONIC = bindings::CLOCK_MONOTONIC as bindings::__kernel_clockid_t,
    PROCESS_CPUTIME_ID = bindings::CLOCK_PROCESS_CPUTIME_ID as bindings::__kernel_clockid_t,
    THREAD_CPUTIME_ID = bindings::CLOCK_THREAD_CPUTIME_ID as bindings::__kernel_clockid_t,
    MONOTONIC_RAW = bindings::CLOCK_MONOTONIC_RAW as bindings::__kernel_clockid_t,
    REALTIME_COARSE = bindings::CLOCK_REALTIME_COARSE as bindings::__kernel_clockid_t,
    MONOTONIC_COARSE = bindings::CLOCK_MONOTONIC_COARSE as bindings::__kernel_clockid_t,
    BOOTTIME = bindings::CLOCK_BOOTTIME as bindings::__kernel_clockid_t,
    REALTIME_ALARM = bindings::CLOCK_REALTIME_ALARM as bindings::__kernel_clockid_t,
    BOOTTIME_ALARM = bindings::CLOCK_BOOTTIME_ALARM as bindings::__kernel_clockid_t,
    SGI_CYCLE = bindings::CLOCK_SGI_CYCLE as bindings::__kernel_clockid_t,
    TAI = bindings::CLOCK_TAI as bindings::__kernel_clockid_t,
}

pub fn clock_gettime(clockid: ClockId) -> Result<bindings::timespec, Errno> {
    let mut t: bindings::timespec = unsafe { core::mem::zeroed() };
    unsafe {
        syscall!(
            linux_syscall::SYS_clock_gettime,
            bindings::__kernel_clockid_t::from(clockid),
            &mut t
        )
    }
    .check()
    .map_err(Errno::from)?;
    Ok(t)
}

mod export {
    use super::*;

    #[no_mangle]
    pub unsafe extern "C" fn linux_clock_gettime(
        clockid: i32,
        res: *mut bindings::timespec,
    ) -> i64 {
        let Ok(clockid) = clockid.try_into() else {
            return Errno::EINVAL.to_negated_i64()
        };
        let t = match clock_gettime(clockid) {
            Ok(t) => t,
            Err(e) => return e.to_negated_i64(),
        };
        unsafe { res.write(t) }
        0
    }
}
