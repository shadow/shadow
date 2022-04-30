/*!
Values for working with time in simulation units.

In Rust, use `EmulatedTime` to represent an instant in time, or
[`std::time::Duration`] to represent a time interval. Use `SimulationTime` only
when interacting with C APIs that use [`c::SimulationTime`].

This module contains some identically-named constants defined as C macros in
`main/core/support/definitions.h`.
*/

use super::emulated_time;
use std::time::Duration;

use log::error;

use crate::cshadow as c;

#[derive(Copy, Clone, Eq, PartialEq, Debug, PartialOrd, Ord)]
pub struct SimulationTime(std::time::Duration);

impl std::ops::Deref for SimulationTime {
    type Target = std::time::Duration;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl SimulationTime {
    pub fn from_c_simtime(val: u64) -> Option<Self> {
        if val == SIMTIME_INVALID {
            return None;
        }

        Some(Self::from(std::time::Duration::from_nanos(
            val * SIMTIME_ONE_NANOSECOND,
        )))
    }

    pub fn to_c_simtime(val: Option<Self>) -> c::SimulationTime {
        if let Some(val) = val {
            u64::try_from(val.as_nanos() / u128::from(SIMTIME_ONE_NANOSECOND)).unwrap()
        } else {
            SIMTIME_INVALID
        }
    }
}

impl std::convert::From<std::time::Duration> for SimulationTime {
    fn from(val: std::time::Duration) -> Self {
        Self(val)
    }
}

impl std::convert::TryFrom<libc::timespec> for SimulationTime {
    type Error = ();

    fn try_from(value: libc::timespec) -> Result<Self, Self::Error> {
        if value.tv_sec < 0 || value.tv_nsec < 0 || value.tv_nsec > 999_999_999 {
            return Err(());
        }
        let secs = Duration::from_secs(value.tv_sec.try_into().unwrap());
        let nanos = Duration::from_nanos(value.tv_nsec.try_into().unwrap());
        Ok(Self(secs + nanos))
    }
}

impl std::convert::TryFrom<SimulationTime> for libc::timespec {
    type Error = ();

    fn try_from(value: SimulationTime) -> Result<Self, Self::Error> {
        let tv_sec = value.as_secs().try_into().map_err(|_| ())?;
        let tv_nsec = value.subsec_nanos().try_into().map_err(|_| ())?;
        Ok(libc::timespec { tv_sec, tv_nsec })
    }
}

impl std::convert::TryFrom<libc::timeval> for SimulationTime {
    type Error = ();

    fn try_from(value: libc::timeval) -> Result<Self, Self::Error> {
        if value.tv_sec < 0 || value.tv_usec < 0 || value.tv_usec > 999_999 {
            return Err(());
        }
        let secs = Duration::from_secs(u64::try_from(value.tv_sec).unwrap());
        let micros = Duration::from_micros(u64::try_from(value.tv_usec).unwrap());
        Ok(Self(secs + micros))
    }
}

impl std::convert::TryFrom<SimulationTime> for libc::timeval {
    type Error = ();

    fn try_from(value: SimulationTime) -> Result<Self, Self::Error> {
        let tv_sec = value.as_secs().try_into().map_err(|_| ())?;
        let tv_usec = value.subsec_micros().try_into().map_err(|_| ())?;
        Ok(libc::timeval { tv_sec, tv_usec })
    }
}

/// Invalid simulation time.
/// cbindgen:ignore
pub const SIMTIME_INVALID: c::SimulationTime = u64::MAX;

/// Maximum and minimum valid values.
/// cbindgen:ignore
pub const SIMTIME_MAX: c::SimulationTime =
    emulated_time::EMUTIME_MAX - (emulated_time::SIMULATION_START_SEC * SIMTIME_ONE_SECOND);
/// cbindgen:ignore
pub const SIMTIME_MIN: c::SimulationTime = 0;

/// Represents one nanosecond in simulation time.
/// cbindgen:ignore
pub const SIMTIME_ONE_NANOSECOND: c::SimulationTime = 1;

/// Represents one microsecond in simulation time.
/// cbindgen:ignore
pub const SIMTIME_ONE_MICROSECOND: c::SimulationTime = 1000;

/// Represents one millisecond in simulation time.
/// cbindgen:ignore
pub const SIMTIME_ONE_MILLISECOND: c::SimulationTime = 1000000;

/// Represents one second in simulation time.
/// cbindgen:ignore
pub const SIMTIME_ONE_SECOND: c::SimulationTime = 1000000000;

/// Represents one minute in simulation time.
/// cbindgen:ignore
pub const SIMTIME_ONE_MINUTE: c::SimulationTime = 60000000000;

/// Represents one hour in simulation time.
/// cbindgen:ignore
pub const SIMTIME_ONE_HOUR: c::SimulationTime = 3600000000000;

pub mod export {
    use super::*;

    #[no_mangle]
    pub unsafe extern "C" fn simtime_from_timeval(val: libc::timeval) -> c::SimulationTime {
        SimulationTime::to_c_simtime(SimulationTime::try_from(val).ok())
    }

    #[no_mangle]
    pub unsafe extern "C" fn simtime_from_timespec(val: libc::timespec) -> c::SimulationTime {
        SimulationTime::to_c_simtime(SimulationTime::try_from(val).ok())
    }

    #[must_use]
    #[no_mangle]
    pub unsafe extern "C" fn simtime_to_timeval(
        val: c::SimulationTime,
        out: *mut libc::timeval,
    ) -> bool {
        let simtime: SimulationTime = if let Some(s) = SimulationTime::from_c_simtime(val) {
            s
        } else {
            return false;
        };
        let tv: libc::timeval = if let Ok(tv) = libc::timeval::try_from(simtime) {
            tv
        } else {
            return false;
        };
        *unsafe { out.as_mut() }.unwrap() = tv;
        true
    }

    #[must_use]
    #[no_mangle]
    pub unsafe extern "C" fn simtime_to_timespec(
        val: c::SimulationTime,
        out: *mut libc::timespec,
    ) -> bool {
        let simtime: SimulationTime = if let Some(s) = SimulationTime::from_c_simtime(val) {
            s
        } else {
            return false;
        };
        let ts: libc::timespec = if let Ok(ts) = libc::timespec::try_from(simtime) {
            ts
        } else {
            return false;
        };
        *unsafe { out.as_mut() }.unwrap() = ts;
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_from_sim_time() {
        let sim_time = 5 * SIMTIME_ONE_MINUTE + 7 * SIMTIME_ONE_MILLISECOND;
        let rust_time = SimulationTime::from_c_simtime(sim_time).unwrap();

        assert_eq!(rust_time.as_secs(), 5 * 60);
        assert_eq!(rust_time.as_millis(), 5 * 60 * 1_000 + 7);
    }

    #[test]
    fn test_to_sim_time() {
        let rust_time = SimulationTime::from(
            5 * std::time::Duration::from_secs(60) + 7 * std::time::Duration::from_micros(1000),
        );
        let sim_time = 5 * SIMTIME_ONE_MINUTE + 7 * SIMTIME_ONE_MILLISECOND;

        assert_eq!(SimulationTime::to_c_simtime(Some(rust_time)), sim_time);
        assert_eq!(SimulationTime::to_c_simtime(None), SIMTIME_INVALID);
    }
}
