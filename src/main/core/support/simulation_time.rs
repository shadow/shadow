/*!
Values for working with time in simulation units.

In Rust we should use standard time types (for example from [`std::time`]), but
when communicating with C we should convert to [`c::SimulationTime`]. This
module contains some identically-named constants defined as C macros in
`main/core/support/definitions.h`.
*/

use crate::cshadow as c;
use std::convert::TryFrom;

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

/// Invalid simulation time.
pub const SIMTIME_INVALID: c::SimulationTime = u64::MAX;

/// Maximum and minimum valid values.
pub const SIMTIME_MAX: c::SimulationTime = u64::MAX - 1;
pub const SIMTIME_MIN: c::SimulationTime = 0;

/// Represents one nanosecond in simulation time.
pub const SIMTIME_ONE_NANOSECOND: c::SimulationTime = 1;

/// Represents one microsecond in simulation time.
pub const SIMTIME_ONE_MICROSECOND: c::SimulationTime = 1000;

/// Represents one millisecond in simulation time.
pub const SIMTIME_ONE_MILLISECOND: c::SimulationTime = 1000000;

/// Represents one second in simulation time.
pub const SIMTIME_ONE_SECOND: c::SimulationTime = 1000000000;

/// Represents one minute in simulation time.
pub const SIMTIME_ONE_MINUTE: c::SimulationTime = 60000000000;

/// Represents one hour in simulation time.
pub const SIMTIME_ONE_HOUR: c::SimulationTime = 3600000000000;

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
