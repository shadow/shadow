/*!
Values for working with a simulated duration. Use `EmulatedTime` to represent an instant in time.

In Rust, use `EmulatedTime` to represent an instant in time, or
`SimulationTime` to represent a time interval. `SimulationTime` is meant to
replace [`SimulationTime`] from the C APIs.

This module contains some identically-named constants defined as C macros in
`main/core/support/definitions.h`.
*/

use std::time::Duration;

use vasi::VirtualAddressSpaceIndependent;

use super::emulated_time;

#[derive(
    Copy, Clone, Eq, PartialEq, Debug, PartialOrd, Ord, Hash, VirtualAddressSpaceIndependent,
)]
#[repr(C)]
pub struct SimulationTime(CSimulationTime);

/// The same as the type alias in the 'main/cshadow.rs' bindings.
pub type CSimulationTime = u64;

impl SimulationTime {
    /// Maximum value. Currently equivalent to SIMTIME_MAX to avoid surprises
    /// when interoperating with C, but could use Duration::MAX when the C types
    /// go away.
    pub const MAX: SimulationTime = SimulationTime(SIMTIME_MAX);
    pub const ZERO: SimulationTime = SimulationTime(0);
    pub const SECOND: SimulationTime = SimulationTime(SIMTIME_ONE_SECOND);
    pub const MILLISECOND: SimulationTime = SimulationTime(SIMTIME_ONE_MILLISECOND);
    pub const MICROSECOND: SimulationTime = SimulationTime(SIMTIME_ONE_MICROSECOND);
    pub const NANOSECOND: SimulationTime = SimulationTime(SIMTIME_ONE_NANOSECOND);

    pub fn from_c_simtime(val: CSimulationTime) -> Option<Self> {
        if val == SIMTIME_INVALID {
            return None;
        }

        if val > SIMTIME_MAX {
            return None;
        }

        Some(Self(val / SIMTIME_ONE_NANOSECOND))
    }

    pub fn to_c_simtime(val: Option<Self>) -> CSimulationTime {
        if let Some(val) = val {
            val.0
        } else {
            SIMTIME_INVALID
        }
    }

    /// Convert a [`Duration`] to a [`SimulationTime`]. This function exists as a `const`
    /// alternative to `SimulationTime::try_from(duration)`. May panic if the duration is too large.
    pub const fn from_duration(val: std::time::Duration) -> Self {
        if SIMTIME_ONE_NANOSECOND != 1 {
            unreachable!();
        }

        let val = val.as_nanos();
        if val > SIMTIME_MAX as u128 {
            panic!("Duration is larger than SIMTIME_MAX");
        }

        Self(val as u64)
    }

    pub fn is_zero(&self) -> bool {
        self.0 == 0
    }

    pub fn is_positive(&self) -> bool {
        self.0 > 0
    }

    pub fn as_secs(&self) -> u64 {
        self.0 / SIMTIME_ONE_SECOND
    }

    pub fn as_millis(&self) -> u64 {
        self.0 / SIMTIME_ONE_MILLISECOND
    }

    pub fn as_micros(&self) -> u64 {
        self.0 / SIMTIME_ONE_MICROSECOND
    }

    pub fn as_nanos(&self) -> u128 {
        (self.0 / SIMTIME_ONE_NANOSECOND).into()
    }

    pub fn as_nanos_f64(&self) -> f64 {
        self.as_nanos() as f64
    }

    pub fn checked_add(self, other: Self) -> Option<Self> {
        match self.0.checked_add(other.0) {
            Some(sum) => SimulationTime::from_c_simtime(sum),
            None => None,
        }
    }

    pub fn checked_sub(self, other: Self) -> Option<Self> {
        match self.0.checked_sub(other.0) {
            Some(difference) => SimulationTime::from_c_simtime(difference),
            None => None,
        }
    }

    pub fn checked_mul(self, other: u64) -> Option<Self> {
        match self.0.checked_mul(other) {
            Some(product) => SimulationTime::from_c_simtime(product),
            None => None,
        }
    }

    pub fn checked_div(self, other: u64) -> Option<Self> {
        match self.0.checked_div(other) {
            Some(quotient) => SimulationTime::from_c_simtime(quotient),
            None => None,
        }
    }

    pub fn checked_rem(self, other: Self) -> Option<Self> {
        match self.0.checked_rem(other.0) {
            Some(rem) => SimulationTime::from_c_simtime(rem),
            None => None,
        }
    }

    pub fn saturating_add(self, other: Self) -> Self {
        let sum = self.0.checked_add(other.0).unwrap_or(SIMTIME_MAX);
        SimulationTime::from_c_simtime(sum).unwrap()
    }

    pub fn saturating_sub(self, other: Self) -> Self {
        let difference = self.0.checked_sub(other.0).unwrap_or(SIMTIME_MIN);
        SimulationTime::from_c_simtime(difference).unwrap()
    }

    pub fn saturating_mul(self, other: u64) -> Self {
        let product = self.0.checked_mul(other).unwrap_or(SIMTIME_MAX);
        SimulationTime::from_c_simtime(product).unwrap()
    }

    pub fn try_from_secs(s: u64) -> Option<Self> {
        Self::SECOND.checked_mul(s)
    }

    pub fn from_secs(s: u64) -> Self {
        Self::try_from_secs(s).unwrap()
    }

    pub fn try_from_millis(s: u64) -> Option<Self> {
        Self::MILLISECOND.checked_mul(s)
    }

    pub fn from_millis(s: u64) -> Self {
        Self::try_from_millis(s).unwrap()
    }

    pub fn try_from_micros(s: u64) -> Option<Self> {
        Self::MICROSECOND.checked_mul(s)
    }

    pub fn from_micros(s: u64) -> Self {
        Self::try_from_micros(s).unwrap()
    }

    pub fn try_from_nanos(s: u64) -> Option<Self> {
        Self::NANOSECOND.checked_mul(s)
    }

    pub fn from_nanos(s: u64) -> Self {
        Self::try_from_nanos(s).unwrap()
    }

    pub fn subsec_millis(&self) -> u32 {
        (self.as_millis() % 1_000).try_into().unwrap()
    }

    pub fn subsec_micros(&self) -> u32 {
        (self.as_micros() % 1_000_000).try_into().unwrap()
    }

    pub fn subsec_nanos(&self) -> u32 {
        (self.as_nanos() % 1_000_000_000).try_into().unwrap()
    }
}

impl std::ops::Add<SimulationTime> for SimulationTime {
    type Output = SimulationTime;

    fn add(self, other: Self) -> Self::Output {
        self.checked_add(other).unwrap()
    }
}

impl std::ops::AddAssign<SimulationTime> for SimulationTime {
    fn add_assign(&mut self, rhs: SimulationTime) {
        *self = *self + rhs;
    }
}

impl std::ops::Sub<SimulationTime> for SimulationTime {
    type Output = SimulationTime;

    fn sub(self, other: Self) -> Self::Output {
        self.checked_sub(other).unwrap()
    }
}

impl std::ops::SubAssign<SimulationTime> for SimulationTime {
    fn sub_assign(&mut self, rhs: SimulationTime) {
        *self = *self - rhs;
    }
}

impl std::ops::Mul<u32> for SimulationTime {
    type Output = SimulationTime;

    fn mul(self, other: u32) -> Self::Output {
        self.checked_mul(other.into()).unwrap()
    }
}

impl std::ops::MulAssign<u32> for SimulationTime {
    fn mul_assign(&mut self, rhs: u32) {
        *self = self.checked_mul(rhs.into()).unwrap();
    }
}

impl std::ops::Div<u32> for SimulationTime {
    type Output = SimulationTime;

    fn div(self, other: u32) -> Self::Output {
        self.checked_div(other.into()).unwrap()
    }
}

impl std::ops::DivAssign<u32> for SimulationTime {
    fn div_assign(&mut self, rhs: u32) {
        *self = self.checked_div(rhs.into()).unwrap();
    }
}

impl std::ops::Rem<SimulationTime> for SimulationTime {
    type Output = SimulationTime;

    fn rem(self, other: SimulationTime) -> Self::Output {
        self.checked_rem(other).unwrap()
    }
}

impl std::convert::TryFrom<std::time::Duration> for SimulationTime {
    type Error = ();

    fn try_from(val: std::time::Duration) -> Result<Self, Self::Error> {
        debug_assert_eq!(SIMTIME_ONE_NANOSECOND, 1);
        let val = val.as_nanos();
        if val > SIMTIME_MAX.into() {
            Err(())
        } else {
            Ok(Self(val.try_into().unwrap()))
        }
    }
}

impl std::convert::From<SimulationTime> for std::time::Duration {
    fn from(val: SimulationTime) -> std::time::Duration {
        debug_assert_eq!(SIMTIME_ONE_NANOSECOND, 1);
        Duration::from_nanos(val.0)
    }
}

impl std::convert::From<SimulationTime> for CSimulationTime {
    fn from(val: SimulationTime) -> CSimulationTime {
        val.0
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
        Self::try_from(secs + nanos)
    }
}

impl std::convert::TryFrom<SimulationTime> for libc::timespec {
    type Error = ();

    fn try_from(value: SimulationTime) -> Result<Self, Self::Error> {
        let value = Duration::from(value);
        let tv_sec = value.as_secs().try_into().map_err(|_| ())?;
        let tv_nsec = value.subsec_nanos().try_into().map_err(|_| ())?;
        Ok(libc::timespec { tv_sec, tv_nsec })
    }
}

impl std::convert::TryFrom<linux_api::time::timespec> for SimulationTime {
    type Error = ();

    fn try_from(value: linux_api::time::timespec) -> Result<Self, Self::Error> {
        if value.tv_sec < 0 || value.tv_nsec < 0 || value.tv_nsec > 999_999_999 {
            return Err(());
        }
        let secs = Duration::from_secs(value.tv_sec.try_into().unwrap());
        let nanos = Duration::from_nanos(value.tv_nsec.try_into().unwrap());
        Self::try_from(secs + nanos)
    }
}

impl std::convert::TryFrom<SimulationTime> for linux_api::time::timespec {
    type Error = ();

    fn try_from(value: SimulationTime) -> Result<Self, Self::Error> {
        let value = Duration::from(value);
        let tv_sec = value.as_secs().try_into().map_err(|_| ())?;
        let tv_nsec = value.subsec_nanos().try_into().map_err(|_| ())?;
        Ok(linux_api::time::timespec { tv_sec, tv_nsec })
    }
}

impl std::convert::TryFrom<libc::timeval> for SimulationTime {
    type Error = ();

    fn try_from(value: libc::timeval) -> Result<Self, Self::Error> {
        if value.tv_sec < 0 || value.tv_usec < 0 || value.tv_usec > 999_999 {
            return Err(());
        }
        let secs = Duration::from_secs(value.tv_sec.try_into().unwrap());
        let micros = Duration::from_micros(value.tv_usec.try_into().unwrap());
        Self::try_from(secs + micros)
    }
}

impl std::convert::TryFrom<SimulationTime> for libc::timeval {
    type Error = ();

    fn try_from(value: SimulationTime) -> Result<Self, Self::Error> {
        let value = Duration::from(value);
        let tv_sec = value.as_secs().try_into().map_err(|_| ())?;
        let tv_usec = value.subsec_micros().try_into().map_err(|_| ())?;
        Ok(libc::timeval { tv_sec, tv_usec })
    }
}

impl std::convert::TryFrom<linux_api::time::timeval> for SimulationTime {
    type Error = ();

    fn try_from(value: linux_api::time::timeval) -> Result<Self, Self::Error> {
        if value.tv_sec < 0 || value.tv_usec < 0 || value.tv_usec > 999_999 {
            return Err(());
        }
        let secs = Duration::from_secs(value.tv_sec.try_into().unwrap());
        let micros = Duration::from_micros(value.tv_usec.try_into().unwrap());
        Self::try_from(secs + micros)
    }
}

impl std::convert::TryFrom<SimulationTime> for linux_api::time::timeval {
    type Error = ();

    fn try_from(value: SimulationTime) -> Result<Self, Self::Error> {
        let value = Duration::from(value);
        let tv_sec = value.as_secs().try_into().map_err(|_| ())?;
        let tv_usec = value.subsec_micros().try_into().map_err(|_| ())?;
        Ok(linux_api::time::timeval { tv_sec, tv_usec })
    }
}

impl tcp::util::time::Duration for SimulationTime {
    const MAX: Self = Self::MAX;
    const NANOSECOND: Self = Self::NANOSECOND;
    const MICROSECOND: Self = Self::MICROSECOND;
    const MILLISECOND: Self = Self::MILLISECOND;
    const SECOND: Self = Self::SECOND;
    const ZERO: Self = Self::ZERO;

    #[inline]
    fn as_micros(&self) -> u128 {
        self.as_micros().into()
    }

    #[inline]
    fn as_millis(&self) -> u128 {
        self.as_millis().into()
    }

    #[inline]
    fn as_nanos(&self) -> u128 {
        self.as_nanos()
    }

    #[inline]
    fn as_secs(&self) -> u64 {
        self.as_secs()
    }

    #[inline]
    fn checked_add(self, rhs: Self) -> Option<Self> {
        self.checked_add(rhs)
    }

    #[inline]
    fn checked_div(self, rhs: u32) -> Option<Self> {
        self.checked_div(rhs.into())
    }

    #[inline]
    fn checked_mul(self, rhs: u32) -> Option<Self> {
        self.checked_mul(rhs.into())
    }

    #[inline]
    fn checked_sub(self, rhs: Self) -> Option<Self> {
        self.checked_sub(rhs)
    }

    #[inline]
    fn from_micros(micros: u64) -> Self {
        Self::from_micros(micros)
    }

    #[inline]
    fn from_millis(millis: u64) -> Self {
        Self::from_millis(millis)
    }

    #[inline]
    fn from_nanos(nanos: u64) -> Self {
        Self::from_nanos(nanos)
    }

    #[inline]
    fn from_secs(secs: u64) -> Self {
        Self::from_secs(secs)
    }

    #[inline]
    fn is_zero(&self) -> bool {
        self.is_zero()
    }

    #[inline]
    fn saturating_add(self, rhs: Self) -> Self {
        self.saturating_add(rhs)
    }

    #[inline]
    fn saturating_mul(self, rhs: u32) -> Self {
        self.saturating_mul(rhs.into())
    }

    #[inline]
    fn saturating_sub(self, rhs: Self) -> Self {
        self.saturating_sub(rhs)
    }

    #[inline]
    fn subsec_micros(&self) -> u32 {
        self.subsec_micros()
    }

    #[inline]
    fn subsec_millis(&self) -> u32 {
        self.subsec_millis()
    }

    #[inline]
    fn subsec_nanos(&self) -> u32 {
        self.subsec_nanos()
    }
}

/// Invalid simulation time.
pub const SIMTIME_INVALID: CSimulationTime = u64::MAX;

/// Maximum and minimum valid values.
//
// cbindgen refuses to do the arithmetic here, so we we pre-compute,
// and validate in the static assertion below.
pub const SIMTIME_MAX: CSimulationTime = 17500059273709551614u64;
const _: () =
    assert!(SIMTIME_MAX == emulated_time::EMUTIME_MAX - emulated_time::EMUTIME_SIMULATION_START);

pub const SIMTIME_MIN: CSimulationTime = 0u64;

/// Represents one nanosecond in simulation time.
pub const SIMTIME_ONE_NANOSECOND: CSimulationTime = 1u64;

/// Represents one microsecond in simulation time.
pub const SIMTIME_ONE_MICROSECOND: CSimulationTime = 1000u64;

/// Represents one millisecond in simulation time.
pub const SIMTIME_ONE_MILLISECOND: CSimulationTime = 1000000u64;

/// Represents one second in simulation time.
pub const SIMTIME_ONE_SECOND: CSimulationTime = 1000000000u64;

/// Represents one minute in simulation time.
pub const SIMTIME_ONE_MINUTE: CSimulationTime = 60000000000u64;

/// Represents one hour in simulation time.
pub const SIMTIME_ONE_HOUR: CSimulationTime = 3600000000000u64;

pub mod export {
    use super::*;
    use crate::notnull::*;

    #[no_mangle]
    pub extern "C" fn simtime_from_timeval(val: libc::timeval) -> CSimulationTime {
        SimulationTime::to_c_simtime(SimulationTime::try_from(val).ok())
    }

    #[no_mangle]
    pub extern "C" fn simtime_from_timespec(val: libc::timespec) -> CSimulationTime {
        SimulationTime::to_c_simtime(SimulationTime::try_from(val).ok())
    }

    /// # Safety
    ///
    /// Pointer args must be safe to write to.
    #[must_use]
    #[no_mangle]
    pub unsafe extern "C" fn simtime_to_timeval(
        val: CSimulationTime,
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
        unsafe { std::ptr::write(notnull_mut(out), tv) };
        true
    }

    /// # Safety
    ///
    /// Pointer args must be safe to write to.
    #[must_use]
    #[no_mangle]
    pub unsafe extern "C" fn simtime_to_timespec(
        val: CSimulationTime,
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
        unsafe { std::ptr::write(out, ts) };
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_from_csimtime() {
        let sim_time = 5 * SIMTIME_ONE_MINUTE + 7 * SIMTIME_ONE_MILLISECOND;
        let rust_time = SimulationTime::from_c_simtime(sim_time).unwrap();

        assert_eq!(Duration::from(rust_time).as_secs(), 5 * 60);
        assert_eq!(Duration::from(rust_time).as_millis(), 5 * 60 * 1_000 + 7);

        assert_eq!(
            SimulationTime::from_c_simtime(SIMTIME_MAX).unwrap(),
            SimulationTime::try_from(Duration::from_nanos(SIMTIME_MAX / SIMTIME_ONE_NANOSECOND))
                .unwrap()
        );
        assert_eq!(SimulationTime::from_c_simtime(SIMTIME_MAX + 1), None);
    }

    #[test]
    fn test_to_csimtime() {
        let rust_time = SimulationTime::from_secs(5 * 60) + SimulationTime::from_millis(7);
        let sim_time = 5 * SIMTIME_ONE_MINUTE + 7 * SIMTIME_ONE_MILLISECOND;

        assert_eq!(SimulationTime::to_c_simtime(Some(rust_time)), sim_time);
        assert_eq!(SimulationTime::to_c_simtime(None), SIMTIME_INVALID);
        assert_eq!(
            SimulationTime::to_c_simtime(Some(SimulationTime::MAX)),
            SIMTIME_MAX
        );
    }

    #[test]
    fn test_from_timeval() {
        use libc::timeval;

        assert_eq!(
            SimulationTime::try_from(timeval {
                tv_sec: 0,
                tv_usec: 0
            }),
            Ok(SimulationTime::ZERO)
        );
        assert_eq!(
            SimulationTime::try_from(timeval {
                tv_sec: 1,
                tv_usec: 2
            }),
            Ok(
                SimulationTime::try_from(Duration::from_secs(1) + Duration::from_micros(2))
                    .unwrap()
            )
        );
        assert_eq!(
            SimulationTime::try_from(timeval {
                tv_sec: SimulationTime::MAX.as_secs().try_into().unwrap(),
                tv_usec: SimulationTime::MAX.subsec_micros().into(),
            }),
            Ok(SimulationTime::from_micros(SimulationTime::MAX.as_micros()))
        );

        // Out of range
        assert_eq!(
            SimulationTime::try_from(timeval {
                tv_sec: libc::time_t::MAX,
                tv_usec: 999_999
            }),
            Err(())
        );

        assert_eq!(
            SimulationTime::try_from(timeval {
                tv_sec: 0,
                tv_usec: 1_000_000
            }),
            Err(())
        );
        assert_eq!(
            SimulationTime::try_from(timeval {
                tv_sec: 0,
                tv_usec: -1
            }),
            Err(())
        );
        assert_eq!(
            SimulationTime::try_from(timeval {
                tv_sec: -1,
                tv_usec: 0
            }),
            Err(())
        );
        assert_eq!(
            SimulationTime::try_from(timeval {
                tv_sec: -1,
                tv_usec: -1
            }),
            Err(())
        );
    }

    #[test]
    fn test_c_from_timeval() {
        use export::simtime_from_timeval;
        use libc::timeval;

        assert_eq!(
            simtime_from_timeval(timeval {
                tv_sec: 0,
                tv_usec: 0,
            }),
            0
        );
        assert_eq!(
            simtime_from_timeval(timeval {
                tv_sec: 1,
                tv_usec: 2,
            }),
            SIMTIME_ONE_SECOND + 2 * SIMTIME_ONE_MICROSECOND
        );

        // Out of range
        assert_eq!(
            simtime_from_timeval(timeval {
                tv_sec: libc::time_t::MAX,
                tv_usec: 999_999,
            }),
            SIMTIME_INVALID
        );

        assert_eq!(
            simtime_from_timeval(timeval {
                tv_sec: 0,
                tv_usec: 1_000_000,
            }),
            SIMTIME_INVALID
        );
        assert_eq!(
            simtime_from_timeval(timeval {
                tv_sec: 0,
                tv_usec: -1,
            }),
            SIMTIME_INVALID
        );
        assert_eq!(
            simtime_from_timeval(timeval {
                tv_sec: -1,
                tv_usec: 0,
            }),
            SIMTIME_INVALID
        );
        assert_eq!(
            simtime_from_timeval(timeval {
                tv_sec: -1,
                tv_usec: -1,
            }),
            SIMTIME_INVALID
        );
    }

    #[test]
    fn test_to_timeval() {
        use libc::timeval;

        assert_eq!(
            timeval::try_from(SimulationTime::ZERO),
            Ok(timeval {
                tv_sec: 0,
                tv_usec: 0
            })
        );
        assert_eq!(
            timeval::try_from(
                SimulationTime::try_from(Duration::from_secs(1) + Duration::from_micros(2))
                    .unwrap()
            ),
            Ok(timeval {
                tv_sec: 1,
                tv_usec: 2
            })
        );
        assert_eq!(
            timeval::try_from(SimulationTime::MAX),
            Ok(timeval {
                tv_sec: SimulationTime::MAX.as_secs().try_into().unwrap(),
                tv_usec: SimulationTime::MAX.subsec_micros().try_into().unwrap(),
            })
        );
    }

    #[test]
    fn test_c_to_timeval() {
        use export::simtime_to_timeval;
        use libc::timeval;

        let mut tv = unsafe { std::mem::zeroed() };

        assert!(unsafe { simtime_to_timeval(0, &mut tv) });
        assert_eq!(
            tv,
            timeval {
                tv_sec: 0,
                tv_usec: 0
            }
        );

        assert!(unsafe {
            simtime_to_timeval(SIMTIME_ONE_SECOND + 2 * SIMTIME_ONE_MICROSECOND, &mut tv)
        });
        assert_eq!(
            tv,
            timeval {
                tv_sec: 1,
                tv_usec: 2
            }
        );

        {
            assert!(unsafe { simtime_to_timeval(SIMTIME_MAX, &mut tv) });
            let d = Duration::from_nanos(SIMTIME_MAX / SIMTIME_ONE_NANOSECOND);
            assert_eq!(
                tv,
                timeval {
                    tv_sec: d.as_secs().try_into().unwrap(),
                    tv_usec: d.subsec_micros().try_into().unwrap()
                }
            );
        }
    }

    #[test]
    fn test_from_timespec() {
        use libc::timespec;

        assert_eq!(
            SimulationTime::try_from(timespec {
                tv_sec: 0,
                tv_nsec: 0
            }),
            Ok(SimulationTime::ZERO)
        );
        assert_eq!(
            SimulationTime::try_from(timespec {
                tv_sec: 1,
                tv_nsec: 2
            }),
            Ok(SimulationTime::try_from(Duration::from_secs(1) + Duration::from_nanos(2)).unwrap())
        );
        assert_eq!(
            SimulationTime::try_from(timespec {
                tv_sec: (SIMTIME_MAX / SIMTIME_ONE_SECOND).try_into().unwrap(),
                tv_nsec: 0,
            }),
            Ok(
                SimulationTime::try_from(Duration::from_secs(SIMTIME_MAX / SIMTIME_ONE_SECOND))
                    .unwrap()
            )
        );

        // The C SimulatedTime type is too small to represent this value.
        // The Rust SimulationTime *could* represent it if we widen it.
        assert_eq!(
            SimulationTime::try_from(timespec {
                tv_sec: libc::time_t::MAX,
                tv_nsec: 999_999_999
            }),
            Err(())
        );

        assert_eq!(
            SimulationTime::try_from(timespec {
                tv_sec: 0,
                tv_nsec: 1_000_000_000
            }),
            Err(())
        );
        assert_eq!(
            SimulationTime::try_from(timespec {
                tv_sec: 0,
                tv_nsec: -1
            }),
            Err(())
        );
        assert_eq!(
            SimulationTime::try_from(timespec {
                tv_sec: -1,
                tv_nsec: 0
            }),
            Err(())
        );
        assert_eq!(
            SimulationTime::try_from(timespec {
                tv_sec: -1,
                tv_nsec: -1
            }),
            Err(())
        );
    }

    #[test]
    fn test_c_from_timespec() {
        use export::simtime_from_timespec;
        use libc::timespec;

        assert_eq!(
            simtime_from_timespec(timespec {
                tv_sec: 0,
                tv_nsec: 0,
            }),
            0
        );
        assert_eq!(
            simtime_from_timespec(timespec {
                tv_sec: 1,
                tv_nsec: 2,
            }),
            SIMTIME_ONE_SECOND + 2 * SIMTIME_ONE_NANOSECOND
        );

        // The C SimulatedTime type is too small to represent this value.
        assert_eq!(
            simtime_from_timespec(timespec {
                tv_sec: libc::time_t::MAX,
                tv_nsec: 999_999_999,
            }),
            SIMTIME_INVALID
        );

        assert_eq!(
            simtime_from_timespec(timespec {
                tv_sec: 0,
                tv_nsec: 1_000_000_000,
            }),
            SIMTIME_INVALID
        );
        assert_eq!(
            simtime_from_timespec(timespec {
                tv_sec: 0,
                tv_nsec: -1,
            }),
            SIMTIME_INVALID
        );
        assert_eq!(
            simtime_from_timespec(timespec {
                tv_sec: -1,
                tv_nsec: 0,
            }),
            SIMTIME_INVALID
        );
        assert_eq!(
            simtime_from_timespec(timespec {
                tv_sec: -1,
                tv_nsec: -1,
            }),
            SIMTIME_INVALID
        );
    }

    #[test]
    fn test_to_timespec() {
        use libc::timespec;

        assert_eq!(
            timespec::try_from(SimulationTime::ZERO),
            Ok(timespec {
                tv_sec: 0,
                tv_nsec: 0
            })
        );
        assert_eq!(
            timespec::try_from(SimulationTime::from_secs(1) + SimulationTime::from_nanos(2)),
            Ok(timespec {
                tv_sec: 1,
                tv_nsec: 2
            })
        );

        assert_eq!(
            timespec::try_from(SimulationTime::MAX),
            Ok(timespec {
                tv_sec: SimulationTime::MAX.as_secs().try_into().unwrap(),
                tv_nsec: SimulationTime::MAX.subsec_nanos().into(),
            })
        );
    }

    #[test]
    fn test_c_to_timespec() {
        use export::simtime_to_timespec;
        use libc::timespec;

        let mut ts = unsafe { std::mem::zeroed() };

        assert!(unsafe { simtime_to_timespec(0, &mut ts) });
        assert_eq!(
            ts,
            timespec {
                tv_sec: 0,
                tv_nsec: 0
            }
        );

        assert!(unsafe {
            simtime_to_timespec(SIMTIME_ONE_SECOND + 2 * SIMTIME_ONE_NANOSECOND, &mut ts)
        });
        assert_eq!(
            ts,
            timespec {
                tv_sec: 1,
                tv_nsec: 2
            }
        );

        {
            assert!(unsafe { simtime_to_timespec(SIMTIME_MAX, &mut ts) });
            let d = Duration::from_nanos(SIMTIME_MAX / SIMTIME_ONE_NANOSECOND);
            assert_eq!(
                ts,
                timespec {
                    tv_sec: d.as_secs().try_into().unwrap(),
                    tv_nsec: d.subsec_nanos().try_into().unwrap()
                }
            );
        }
    }
}
