/*!
Deals with instances of time in a Shadow simulation.
*/

use std::sync::atomic::{AtomicU64, Ordering};

use vasi::VirtualAddressSpaceIndependent;

use crate::simulation_time::{self, CSimulationTime, SimulationTime};

/// An instant in time (analagous to std::time::Instant) in the Shadow
/// simulation.
// Internally represented as Duration since the Unix Epoch.
#[derive(
    Copy, Clone, Eq, PartialEq, Debug, PartialOrd, Ord, Hash, VirtualAddressSpaceIndependent,
)]
#[repr(C)]
pub struct EmulatedTime(CEmulatedTime);

/// Emulation time in nanoseconds. Allows for a consistent representation
/// of time throughput the simulator. Emulation time is the simulation time
/// plus the EMULATION_TIME_OFFSET. This type allows us to explicitly
/// distinguish each type of time in the code.
pub type CEmulatedTime = u64;

// Duplicated from the EMULATED_TIME_OFFSET macro in definitions.h.
pub const SIMULATION_START_SEC: u64 = 946684800u64;
pub const EMUTIME_INVALID: CEmulatedTime = u64::MAX;
pub const EMUTIME_MAX: CEmulatedTime = u64::MAX - 1;
pub const EMUTIME_MIN: CEmulatedTime = 0u64;

/// The number of nanoseconds from the epoch to January 1st, 2000 at 12:00am UTC.
/// This is used to emulate to applications that we are in a recent time.
// cbindgen won't do the constant propagation here. We use the static assertion below
// to ensure this definition is equal to the intended canonical definition.
pub const EMUTIME_SIMULATION_START: CEmulatedTime = 946684800u64 * 1_000_000_000u64;
const _: () =
    assert!(EMUTIME_SIMULATION_START == SIMULATION_START_SEC * simulation_time::SIMTIME_ONE_SECOND);

/// Duplicated as EmulatedTime::UNIX_EPOCH
pub const EMUTIME_UNIX_EPOCH: CEmulatedTime = 0u64;

impl EmulatedTime {
    /// The start time of the simulation - 00:00:00 UTC on 1 January, 2000.
    pub const SIMULATION_START: Self = Self(EMUTIME_SIMULATION_START);
    /// The  Unix epoch (00:00:00 UTC on 1 January 1970)
    pub const UNIX_EPOCH: Self = Self(0);

    pub const MAX: Self = Self(EMUTIME_MAX);
    pub const MIN: Self = Self(0);

    /// Get the instance corresponding to `val` SimulationTime units since the Unix Epoch.
    pub const fn from_c_emutime(val: CEmulatedTime) -> Option<Self> {
        if val == EMUTIME_INVALID || val > EMUTIME_MAX {
            None
        } else {
            Some(Self(val))
        }
    }

    /// Convert to number of SimulationTime units since the Unix Epoch.
    pub const fn to_c_emutime(val: Option<Self>) -> CEmulatedTime {
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
        self.duration_since(&Self::SIMULATION_START)
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

    pub fn checked_add(&self, duration: SimulationTime) -> Option<EmulatedTime> {
        EmulatedTime::from_c_emutime(self.0.checked_add(CSimulationTime::from(duration))?)
    }

    pub fn checked_sub(&self, duration: SimulationTime) -> Option<EmulatedTime> {
        EmulatedTime::from_c_emutime(self.0.checked_sub(CSimulationTime::from(duration))?)
    }

    pub fn saturating_add(&self, duration: SimulationTime) -> EmulatedTime {
        match self.checked_add(duration) {
            Some(later) => later,
            None => EmulatedTime::MAX,
        }
    }

    pub fn saturating_sub(&self, duration: SimulationTime) -> EmulatedTime {
        match self.checked_sub(duration) {
            Some(earlier) => earlier,
            None => EmulatedTime::SIMULATION_START,
        }
    }
}

impl std::ops::Add<SimulationTime> for EmulatedTime {
    type Output = EmulatedTime;

    fn add(self, other: SimulationTime) -> Self {
        self.checked_add(other).unwrap()
    }
}

impl std::ops::AddAssign<SimulationTime> for EmulatedTime {
    fn add_assign(&mut self, rhs: SimulationTime) {
        *self = *self + rhs;
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

impl std::ops::SubAssign<SimulationTime> for EmulatedTime {
    fn sub_assign(&mut self, rhs: SimulationTime) {
        *self = self.checked_sub(rhs).unwrap();
    }
}

impl tcp::util::time::Instant for EmulatedTime {
    type Duration = SimulationTime;

    #[inline]
    fn duration_since(&self, earlier: Self) -> Self::Duration {
        self.duration_since(&earlier)
    }

    #[inline]
    fn saturating_duration_since(&self, earlier: Self) -> Self::Duration {
        self.saturating_duration_since(&earlier)
    }

    #[inline]
    fn checked_duration_since(&self, earlier: Self) -> Option<Self::Duration> {
        self.checked_duration_since(&earlier)
    }

    #[inline]
    fn checked_add(&self, duration: Self::Duration) -> Option<Self> {
        self.checked_add(duration)
    }

    #[inline]
    fn checked_sub(&self, duration: Self::Duration) -> Option<Self> {
        self.checked_sub(duration)
    }
}

pub mod export {
    use super::*;

    #[no_mangle]
    pub extern "C-unwind" fn emutime_add_simtime(
        lhs: CEmulatedTime,
        rhs: CSimulationTime,
    ) -> CEmulatedTime {
        let Some(lhs) = EmulatedTime::from_c_emutime(lhs) else {
            return EmulatedTime::to_c_emutime(None);
        };
        let Some(rhs) = SimulationTime::from_c_simtime(rhs) else {
            return EmulatedTime::to_c_emutime(None);
        };
        let sum = lhs.checked_add(rhs);
        EmulatedTime::to_c_emutime(sum)
    }

    #[no_mangle]
    pub extern "C-unwind" fn emutime_sub_emutime(
        lhs: CEmulatedTime,
        rhs: CEmulatedTime,
    ) -> CSimulationTime {
        let Some(lhs) = EmulatedTime::from_c_emutime(lhs) else {
            return EmulatedTime::to_c_emutime(None);
        };
        let Some(rhs) = EmulatedTime::from_c_emutime(rhs) else {
            return EmulatedTime::to_c_emutime(None);
        };
        let diff = lhs.checked_duration_since(&rhs);
        SimulationTime::to_c_simtime(diff)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::simulation_time;

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

#[derive(VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct AtomicEmulatedTime(AtomicU64);

impl AtomicEmulatedTime {
    pub fn new(t: EmulatedTime) -> Self {
        Self(AtomicU64::new(t.0))
    }

    pub fn load(&self, order: Ordering) -> EmulatedTime {
        EmulatedTime(self.0.load(order))
    }

    pub fn store(&self, val: EmulatedTime, order: Ordering) {
        self.0.store(val.0, order)
    }
}
