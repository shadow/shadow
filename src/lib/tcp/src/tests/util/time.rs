//! Time-related types for our unit tests. The types represent simulated times.

// this is only used in doc comments.
#[allow(unused)]
use crate::Dependencies;

pub use std::time::Duration;

/// Like [`std::time::Instant`], but the time is arbitrary. Since you cannot create arbitrary
/// `std::time::Instant` values (only values derived from [`std::time::Instant::now()`]), this type
/// is needed so that we can emulate time in our tests.
// time is internally represented in nanoseconds
#[derive(Copy, Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Instant(u64);

impl Instant {
    /// This has no correspondance with the Unix epoch. It's just an arbitrary value for the minimum
    /// instant.
    pub const EPOCH: Self = Self(0);

    pub const fn duration_since(&self, earlier: Instant) -> Duration {
        // the rust docs for `std::time::Instant` say `duration_since` is currently saturating, but
        // may panic in the future; let's just panic to avoid bugs
        match self.0.checked_sub(earlier.0) {
            Some(x) => Duration::from_nanos(x),
            None => panic!("Argument `earlier` is later than `self`"),
        }
    }

    pub const fn saturating_duration_since(&self, earlier: Instant) -> Duration {
        Duration::from_nanos(self.0.saturating_sub(earlier.0))
    }

    pub const fn checked_duration_since(&self, earlier: Instant) -> Option<Duration> {
        match self.0.checked_sub(earlier.0) {
            Some(x) => Some(Duration::from_nanos(x)),
            None => None,
        }
    }

    pub const fn checked_add(&self, duration: Duration) -> Option<Instant> {
        let duration_ns = if (duration.as_nanos() as u64) as u128 == duration.as_nanos() {
            duration.as_nanos() as u64
        } else {
            return None;
        };
        match self.0.checked_add(duration_ns) {
            Some(x) => Some(Instant(x)),
            None => None,
        }
    }

    pub const fn checked_sub(&self, duration: Duration) -> Option<Instant> {
        let duration_ns = if (duration.as_nanos() as u64) as u128 == duration.as_nanos() {
            duration.as_nanos() as u64
        } else {
            return None;
        };
        match self.0.checked_sub(duration_ns) {
            Some(x) => Some(Instant(x)),
            None => None,
        }
    }
}

impl std::ops::Add<Duration> for Instant {
    type Output = Self;

    fn add(self, rhs: Duration) -> Self::Output {
        self.checked_add(rhs).unwrap()
    }
}

impl std::ops::AddAssign<Duration> for Instant {
    fn add_assign(&mut self, rhs: Duration) {
        *self = self.checked_add(rhs).unwrap();
    }
}

impl std::ops::Sub<Duration> for Instant {
    type Output = Self;

    fn sub(self, rhs: Duration) -> Self::Output {
        self.checked_sub(rhs).unwrap()
    }
}

impl std::ops::Sub<Instant> for Instant {
    type Output = Duration;

    fn sub(self, rhs: Instant) -> Self::Output {
        // the rust docs for `std::time::Instant` say `sub` is currently saturating, but may panic
        // in the future; let's just panic to avoid bugs
        self.checked_duration_since(rhs).unwrap()
    }
}

impl std::ops::SubAssign<Duration> for Instant {
    fn sub_assign(&mut self, rhs: Duration) {
        *self = self.checked_sub(rhs).unwrap();
    }
}

impl crate::util::time::Instant for Instant {
    type Duration = Duration;

    #[inline]
    fn duration_since(&self, earlier: Self) -> Self::Duration {
        self.duration_since(earlier)
    }

    #[inline]
    fn saturating_duration_since(&self, earlier: Self) -> Self::Duration {
        self.saturating_duration_since(earlier)
    }

    #[inline]
    fn checked_duration_since(&self, earlier: Self) -> Option<Self::Duration> {
        self.checked_duration_since(earlier)
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
