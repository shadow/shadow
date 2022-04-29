/*!
Deals with instances of time in a Shadow simulation.
*/

use crate::core::support::simulation_time;
use crate::core::support::simulation_time::SimulationTime;
use crate::cshadow as c;
use std::time::Duration;

/// An instant in time (analagous to std::time::Instant) in the Shadow
/// simulation.
// Internally represented as Duration since the Unix Epoch.
#[derive(Copy, Clone, Eq, PartialEq, Debug, PartialOrd, Ord)]
pub struct EmulatedTime(Duration);

/// The  Unix epoch (00:00:00 UTC on 1 January 1970)
pub const UNIX_EPOCH: EmulatedTime = EmulatedTime(Duration::from_secs(0));

// Duplicated from the EMULATED_TIME_OFFSET macro in definitions.h.
pub(super) const SIMULATION_START_SEC: u64 = 946684800;
pub const EMUTIME_INVALID: c::EmulatedTime = u64::MAX;
pub const EMUTIME_MAX: c::EmulatedTime = u64::MAX - 1;
pub const EMUTIME_MIN: c::EmulatedTime = 0;

/// The start time of the simulation - 00:00:00 UTC on 1 January, 2000.
pub const SIMULATION_START: EmulatedTime = EmulatedTime(Duration::from_secs(SIMULATION_START_SEC));

impl EmulatedTime {
    /// Get the instance corresponding to `val` SimulationTime units since the Unix Epoch.
    pub fn from_c_emutime(val: c::EmulatedTime) -> Option<Self> {
        if val == EMUTIME_INVALID {
            None
        } else {
            Some(Self(Duration::from_nanos(
                val * simulation_time::SIMTIME_ONE_NANOSECOND,
            )))
        }
    }

    /// Convert to number of SimulationTime units since the Unix Epoch.
    pub fn to_c_emutime(val: Option<Self>) -> c::EmulatedTime {
        match val {
            Some(val) => SimulationTime::to_c_simtime(Some(SimulationTime::from(
                val.duration_since(&UNIX_EPOCH),
            ))),
            None => EMUTIME_INVALID,
        }
    }

    /// Get the instant corresponding to `val` time units since the simulation began.
    pub fn from_abs_simtime(val: SimulationTime) -> Self {
        SIMULATION_START + *val
    }

    /// Convert to the SimulationTime since the simulation began.
    pub fn to_abs_simtime(self) -> SimulationTime {
        SimulationTime::from(self.duration_since(&SIMULATION_START))
    }

    /// Returns the duration since `earlier`, or panics if `earlier` is after `self`.
    pub fn duration_since(&self, earlier: &EmulatedTime) -> Duration {
        self.checked_duration_since(earlier).unwrap()
    }

    /// Returns the duration since `earlier`, or `None` if `earlier` is after `self`.
    pub fn checked_duration_since(&self, earlier: &EmulatedTime) -> Option<Duration> {
        self.0.checked_sub(earlier.0)
    }
}

impl std::ops::Add<Duration> for EmulatedTime {
    type Output = EmulatedTime;

    fn add(self, dur: Duration) -> Self {
        Self(self.0 + dur)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_from_emu_time() {
        let emu_time =
            5 * simulation_time::SIMTIME_ONE_MINUTE + 7 * simulation_time::SIMTIME_ONE_MILLISECOND;
        let rust_time = EmulatedTime::from_c_emutime(emu_time).unwrap();

        assert_eq!(rust_time.duration_since(&UNIX_EPOCH).as_secs(), 5 * 60);
        assert_eq!(
            rust_time.duration_since(&UNIX_EPOCH).as_millis(),
            5 * 60 * 1_000 + 7
        );
    }

    #[test]
    fn test_to_emu_time() {
        let rust_time = UNIX_EPOCH
            + 5 * std::time::Duration::from_secs(60)
            + 7 * std::time::Duration::from_micros(1000);
        let sim_time =
            5 * simulation_time::SIMTIME_ONE_MINUTE + 7 * simulation_time::SIMTIME_ONE_MILLISECOND;

        assert_eq!(EmulatedTime::to_c_emutime(Some(rust_time)), sim_time);
        assert_eq!(
            EmulatedTime::to_c_emutime(None),
            simulation_time::SIMTIME_INVALID
        );
    }

    #[test]
    fn test_from_abs_simtime() {
        assert_eq!(
            EmulatedTime::from_abs_simtime(SimulationTime::from(Duration::from_secs(0))),
            SIMULATION_START
        );

        assert_eq!(
            EmulatedTime::from_abs_simtime(SimulationTime::from(Duration::from_secs(1))),
            SIMULATION_START + Duration::from_secs(1)
        );
    }

    #[test]
    fn test_to_abs_simtime() {
        assert_eq!(
            SIMULATION_START.to_abs_simtime(),
            SimulationTime::from(Duration::from_secs(0))
        );

        assert_eq!(
            (SIMULATION_START + Duration::from_secs(1)).to_abs_simtime(),
            SimulationTime::from(Duration::from_secs(1))
        );
    }
}
