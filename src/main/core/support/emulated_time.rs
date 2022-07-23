/*!
Deals with instances of time in a Shadow simulation.
*/

use crate::core::support::simulation_time;
use crate::core::support::simulation_time::SimulationTime;
use crate::cshadow as c;

/// An instant in time (analagous to std::time::Instant) in the Shadow
/// simulation.
// Internally represented as Duration since the Unix Epoch.
#[derive(Copy, Clone, Eq, PartialEq, Debug, PartialOrd, Ord)]
pub struct EmulatedTime(c::EmulatedTime);

// Duplicated from the EMULATED_TIME_OFFSET macro in definitions.h.
pub(super) const SIMULATION_START_SEC: u64 = 946684800;
pub const EMUTIME_INVALID: c::EmulatedTime = u64::MAX;
pub const EMUTIME_MAX: c::EmulatedTime = u64::MAX - 1;
pub const EMUTIME_MIN: c::EmulatedTime = 0;

impl EmulatedTime {
    /// The start time of the simulation - 00:00:00 UTC on 1 January, 2000.
    pub const SIMULATION_START: Self =
        Self(SIMULATION_START_SEC * simulation_time::SIMTIME_ONE_SECOND);
    /// The  Unix epoch (00:00:00 UTC on 1 January 1970)
    pub const UNIX_EPOCH: Self = Self(0);

    pub const MAX: Self = Self(EMUTIME_MAX);

    /// Get the instance corresponding to `val` SimulationTime units since the Unix Epoch.
    pub fn from_c_emutime(val: c::EmulatedTime) -> Option<Self> {
        if val == EMUTIME_INVALID {
            None
        } else if val > EMUTIME_MAX {
            None
        } else {
            Some(Self(val))
        }
    }

    /// Convert to number of SimulationTime units since the Unix Epoch.
    pub fn to_c_emutime(val: Option<Self>) -> c::EmulatedTime {
        match val {
            Some(v) => v.0,
            None => EMUTIME_INVALID,
        }
    }

    /// Get the instant corresponding to `val` time units since the simulation began.
    pub fn from_abs_simtime(val: SimulationTime) -> Self {
        Self::SIMULATION_START + val
    }

    /// Convert to the SimulationTime since the simulation began.
    pub fn to_abs_simtime(self) -> SimulationTime {
        SimulationTime::from(self.duration_since(&Self::SIMULATION_START))
    }

    /// Returns the duration since `earlier`, or panics if `earlier` is after `self`, or
    /// if the difference can't be represented as SimulationTime.
    pub fn duration_since(&self, earlier: &EmulatedTime) -> SimulationTime {
        self.checked_duration_since(earlier).unwrap()
    }

    /// Returns the duration since `earlier`, or `None` if `earlier` is after `self`.
    pub fn checked_duration_since(&self, earlier: &EmulatedTime) -> Option<SimulationTime> {
        let d = self.0.checked_sub(earlier.0)?;
        SimulationTime::from_c_simtime(d)
    }

    /// Returns the duration since `earlier`, or 0 if `earlier` is after `self`.
    pub fn saturating_duration_since(&self, earlier: &EmulatedTime) -> SimulationTime {
        self.checked_duration_since(earlier)
            .unwrap_or(SimulationTime::ZERO)
    }

    pub fn checked_add(&self, other: SimulationTime) -> Option<EmulatedTime> {
        EmulatedTime::from_c_emutime(self.0.checked_add(c::SimulationTime::from(other))?)
    }

    pub fn checked_sub(&self, other: SimulationTime) -> Option<EmulatedTime> {
        EmulatedTime::from_c_emutime(self.0.checked_sub(c::SimulationTime::from(other))?)
    }
}

impl std::ops::Add<SimulationTime> for EmulatedTime {
    type Output = EmulatedTime;

    fn add(self, other: SimulationTime) -> Self {
        self.checked_add(other).unwrap()
    }
}

impl std::ops::Sub<SimulationTime> for EmulatedTime {
    type Output = EmulatedTime;

    fn sub(self, other: SimulationTime) -> Self {
        self.checked_sub(other).unwrap()
    }
}

impl std::ops::Sub<EmulatedTime> for EmulatedTime {
    type Output = SimulationTime;

    fn sub(self, other: EmulatedTime) -> Self::Output {
        self.duration_since(&other)
    }
}

pub mod export {
    use super::*;

    #[no_mangle]
    pub unsafe extern "C" fn emutime_add_simtime(
        lhs: c::EmulatedTime,
        rhs: c::SimulationTime,
    ) -> c::EmulatedTime {
        let lhs = if let Some(e) = EmulatedTime::from_c_emutime(lhs) {
            e
        } else {
            return EmulatedTime::to_c_emutime(None);
        };
        let rhs = if let Some(e) = SimulationTime::from_c_simtime(rhs) {
            e
        } else {
            return EmulatedTime::to_c_emutime(None);
        };
        let sum = lhs.checked_add(rhs);
        EmulatedTime::to_c_emutime(sum)
    }

    #[no_mangle]
    pub unsafe extern "C" fn emutime_sub_emutime(
        lhs: c::EmulatedTime,
        rhs: c::EmulatedTime,
    ) -> c::SimulationTime {
        let lhs = if let Some(e) = EmulatedTime::from_c_emutime(lhs) {
            e
        } else {
            return EmulatedTime::to_c_emutime(None);
        };
        let rhs = if let Some(e) = EmulatedTime::from_c_emutime(rhs) {
            e
        } else {
            return EmulatedTime::to_c_emutime(None);
        };
        let diff = lhs.checked_duration_since(&rhs);
        SimulationTime::to_c_simtime(diff)
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

        assert_eq!(
            rust_time
                .duration_since(&EmulatedTime::UNIX_EPOCH)
                .as_secs(),
            5 * 60
        );
        assert_eq!(
            rust_time
                .duration_since(&EmulatedTime::UNIX_EPOCH)
                .as_millis(),
            5 * 60 * 1_000 + 7
        );
    }

    #[test]
    fn test_to_emu_time() {
        let rust_time = EmulatedTime::UNIX_EPOCH
            + SimulationTime::SECOND * 60 * 5
            + SimulationTime::MILLISECOND * 7;
        let sim_time =
            5 * simulation_time::SIMTIME_ONE_MINUTE + 7 * simulation_time::SIMTIME_ONE_MILLISECOND;

        assert_eq!(EmulatedTime::to_c_emutime(Some(rust_time)), sim_time);
        assert_eq!(EmulatedTime::to_c_emutime(None), EMUTIME_INVALID);
    }

    #[test]
    fn test_from_abs_simtime() {
        assert_eq!(
            EmulatedTime::from_abs_simtime(SimulationTime::ZERO),
            EmulatedTime::SIMULATION_START
        );

        assert_eq!(
            EmulatedTime::from_abs_simtime(SimulationTime::SECOND),
            EmulatedTime::SIMULATION_START + SimulationTime::SECOND
        );
    }

    #[test]
    fn test_to_abs_simtime() {
        assert_eq!(
            EmulatedTime::SIMULATION_START.to_abs_simtime(),
            SimulationTime::ZERO
        );

        assert_eq!(
            (EmulatedTime::SIMULATION_START + SimulationTime::SECOND).to_abs_simtime(),
            SimulationTime::SECOND
        );
    }
}
