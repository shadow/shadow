//! Time-related types for our unit tests. The types represent simulated times.

// this is only used in doc comments.
#[allow(unused)]
use crate::Dependencies;

/// Like [`std::time::Instant`], but the time is arbitrary and is controlled by the
/// [`Dependencies`].
// time is internally represented in nanoseconds
#[derive(Copy, Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Instant(u64);

/// Like [`std::time::Duration`], but the time is arbitrary and is controlled by the
/// [`Dependencies`].
// time is internally represented in nanoseconds
#[derive(Copy, Clone, Debug, Default, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Duration(u64);

impl Instant {
    /// This has no correspondance with the Unix epoch. It's just an arbitrary value for the minimum
    /// instant.
    pub const EPOCH: Self = Self(0);

    pub const fn duration_since(&self, earlier: Instant) -> Duration {
        // the rust docs for `std::time::Instant` say `duration_since` is currently saturating, but
        // may panic in the future; let's just panic to avoid bugs
        match self.0.checked_sub(earlier.0) {
            Some(x) => Duration(x),
            None => panic!("Argument `earlier` is later than `self`"),
        }
    }

    pub const fn saturating_duration_since(&self, earlier: Instant) -> Duration {
        Duration(self.0.saturating_sub(earlier.0))
    }

    pub const fn checked_duration_since(&self, earlier: Instant) -> Option<Duration> {
        match self.0.checked_sub(earlier.0) {
            Some(x) => Some(Duration(x)),
            None => None,
        }
    }

    pub const fn checked_add(&self, duration: Duration) -> Option<Instant> {
        match self.0.checked_add(duration.0) {
            Some(x) => Some(Instant(x)),
            None => None,
        }
    }

    pub const fn checked_sub(&self, duration: Duration) -> Option<Instant> {
        match self.0.checked_sub(duration.0) {
            Some(x) => Some(Instant(x)),
            None => None,
        }
    }
}

impl Duration {
    pub const MAX: Self = Self(u64::MAX);
    pub const NANOSECOND: Self = Self(1);
    pub const MICROSECOND: Self = Self(Self::NANOSECOND.0 * 1000);
    pub const MILLISECOND: Self = Self(Self::MICROSECOND.0 * 1000);
    pub const SECOND: Self = Self(Self::MILLISECOND.0 * 1000);
    pub const ZERO: Self = Self(0);

    pub const fn as_micros(&self) -> u128 {
        (self.0 / Self::MICROSECOND.0) as u128
    }

    pub const fn as_millis(&self) -> u128 {
        (self.0 / Self::MILLISECOND.0) as u128
    }

    pub const fn as_nanos(&self) -> u128 {
        self.0 as u128
    }

    pub const fn as_secs(&self) -> u64 {
        self.0 / Self::SECOND.0
    }

    pub const fn checked_add(self, rhs: Duration) -> Option<Duration> {
        match self.0.checked_add(rhs.0) {
            Some(x) => Some(Duration(x)),
            None => None,
        }
    }

    pub const fn checked_div(self, rhs: u32) -> Option<Duration> {
        match self.0.checked_div(rhs as u64) {
            Some(x) => Some(Duration(x)),
            None => None,
        }
    }

    pub const fn checked_mul(self, rhs: u32) -> Option<Duration> {
        match self.0.checked_mul(rhs as u64) {
            Some(x) => Some(Duration(x)),
            None => None,
        }
    }

    pub const fn checked_sub(self, rhs: Duration) -> Option<Duration> {
        match self.0.checked_sub(rhs.0) {
            Some(x) => Some(Duration(x)),
            None => None,
        }
    }

    pub const fn from_micros(micros: u64) -> Duration {
        match Self::MICROSECOND.0.checked_mul(micros) {
            Some(x) => Duration(x),
            None => panic!("Integer overflow"),
        }
    }

    pub const fn from_millis(millis: u64) -> Duration {
        match Self::MILLISECOND.0.checked_mul(millis) {
            Some(x) => Duration(x),
            None => panic!("Integer overflow"),
        }
    }

    pub const fn from_nanos(nanos: u64) -> Duration {
        match Self::NANOSECOND.0.checked_mul(nanos) {
            Some(x) => Duration(x),
            None => panic!("Integer overflow"),
        }
    }

    pub const fn from_secs(secs: u64) -> Duration {
        match Self::SECOND.0.checked_mul(secs) {
            Some(x) => Duration(x),
            None => panic!("Integer overflow"),
        }
    }

    pub const fn is_zero(&self) -> bool {
        self.0 == 0
    }

    pub const fn saturating_add(self, rhs: Duration) -> Duration {
        Duration(self.0.saturating_add(rhs.0))
    }

    pub const fn saturating_mul(self, rhs: u32) -> Duration {
        Duration(self.0.saturating_mul(rhs as u64))
    }

    pub const fn saturating_sub(self, rhs: Duration) -> Duration {
        Duration(self.0.saturating_sub(rhs.0))
    }

    pub const fn subsec_micros(&self) -> u32 {
        static_assertions::const_assert!(Duration::MICROSECOND.0 < u32::MAX as u64);
        (self.0 % Self::MICROSECOND.0) as u32
    }

    pub const fn subsec_millis(&self) -> u32 {
        static_assertions::const_assert!(Duration::MILLISECOND.0 < u32::MAX as u64);
        (self.0 % Self::MILLISECOND.0) as u32
    }

    pub const fn subsec_nanos(&self) -> u32 {
        static_assertions::const_assert!(Duration::NANOSECOND.0 < u32::MAX as u64);
        (self.0 % Self::NANOSECOND.0) as u32
    }
}

impl std::ops::Add<Duration> for Duration {
    type Output = Self;

    fn add(self, rhs: Duration) -> Self::Output {
        self.checked_add(rhs).unwrap()
    }
}

impl std::ops::Add<Duration> for Instant {
    type Output = Self;

    fn add(self, rhs: Duration) -> Self::Output {
        self.checked_add(rhs).unwrap()
    }
}

impl std::ops::AddAssign<Duration> for Duration {
    fn add_assign(&mut self, rhs: Duration) {
        *self = self.checked_add(rhs).unwrap();
    }
}

impl std::ops::AddAssign<Duration> for Instant {
    fn add_assign(&mut self, rhs: Duration) {
        *self = self.checked_add(rhs).unwrap();
    }
}

impl std::ops::Sub<Duration> for Duration {
    type Output = Self;

    fn sub(self, rhs: Duration) -> Self::Output {
        self.checked_sub(rhs).unwrap()
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

impl std::ops::SubAssign<Duration> for Duration {
    fn sub_assign(&mut self, rhs: Duration) {
        *self = self.checked_sub(rhs).unwrap();
    }
}

impl std::ops::SubAssign<Duration> for Instant {
    fn sub_assign(&mut self, rhs: Duration) {
        *self = self.checked_sub(rhs).unwrap();
    }
}

impl std::ops::Mul<Duration> for u32 {
    type Output = Duration;

    fn mul(self, rhs: Duration) -> Self::Output {
        rhs.checked_mul(self).unwrap()
    }
}

impl std::ops::Mul<u32> for Duration {
    type Output = Duration;

    fn mul(self, rhs: u32) -> Self::Output {
        self.checked_mul(rhs).unwrap()
    }
}

impl std::ops::MulAssign<u32> for Duration {
    fn mul_assign(&mut self, rhs: u32) {
        *self = self.checked_mul(rhs).unwrap();
    }
}

impl std::ops::Div<u32> for Duration {
    type Output = Duration;

    fn div(self, rhs: u32) -> Self::Output {
        self.checked_div(rhs).unwrap()
    }
}

impl std::ops::DivAssign<u32> for Duration {
    fn div_assign(&mut self, rhs: u32) {
        *self = self.checked_div(rhs).unwrap();
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

impl crate::util::time::Duration for Duration {
    const MAX: Self = Self::MAX;
    const NANOSECOND: Self = Self::MAX;
    const MICROSECOND: Self = Self::MICROSECOND;
    const MILLISECOND: Self = Self::MILLISECOND;
    const SECOND: Self = Self::SECOND;
    const ZERO: Self = Self::ZERO;

    #[inline]
    fn as_micros(&self) -> u128 {
        self.as_micros()
    }

    #[inline]
    fn as_millis(&self) -> u128 {
        self.as_millis()
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
        self.checked_div(rhs)
    }

    #[inline]
    fn checked_mul(self, rhs: u32) -> Option<Self> {
        self.checked_mul(rhs)
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
        self.saturating_mul(rhs)
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
