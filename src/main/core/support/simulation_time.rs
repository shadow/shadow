/*!
Values for working with time in simulation units.

In Rust we should use standard time types (for example from [`std::time`]), but
when communicating with C we should convert to [`c::SimulationTime`]. This
module contains some identically-named constants defined as C macros in
`main/core/support/definitions.h`.
*/

use crate::cshadow as c;

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
