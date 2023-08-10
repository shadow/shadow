//! Traits that provide an abstract interface for time-related operations, modelled after [`std::time`].
//!
//! ```
//! fn add_durations<T: tcp::util::time::Duration>(x1: T, x2: T) -> T {
//!     x1 + x2
//! }
//!
//! use std::time::Duration;
//! assert_eq!(
//!     add_durations(Duration::from_secs(1), Duration::from_millis(2)),
//!     Duration::from_secs(1) + Duration::from_millis(2),
//! );
//! ```

// this is only used in doc comments.
#[allow(unused)]
use crate::Dependencies;

use std::fmt::Debug;

/// A trait for time instants that follow the API of [`std::time::Instant`]. This is useful for code
/// that should work with not just the real time, but also simulated time.
pub trait Instant:
    'static
    + Sized
    + Copy
    + Clone
    + Debug
    + std::ops::Add<Self::Duration, Output = Self>
    + std::ops::AddAssign<Self::Duration>
    + std::ops::Sub<Self::Duration, Output = Self>
    + std::ops::Sub<Self, Output = Self::Duration>
    + std::ops::SubAssign<Self::Duration>
    + std::cmp::PartialOrd
    + std::cmp::Ord
    + std::cmp::PartialEq
    + std::cmp::Eq
    + std::hash::Hash
{
    type Duration: Duration;

    /// See [`std::time::Instant::duration_since`].
    fn duration_since(&self, earlier: Self) -> Self::Duration;
    /// See [`std::time::Instant::saturating_duration_since`].
    fn saturating_duration_since(&self, earlier: Self) -> Self::Duration;
    /// See [`std::time::Instant::checked_duration_since`].
    fn checked_duration_since(&self, earlier: Self) -> Option<Self::Duration>;
    /// See [`std::time::Instant::checked_add`].
    fn checked_add(&self, duration: Self::Duration) -> Option<Self>;
    /// See [`std::time::Instant::checked_sub`].
    fn checked_sub(&self, duration: Self::Duration) -> Option<Self>;
}

/// A trait for time durations that follow the API of [`std::time::Duration`]. This is useful for
/// code that should work with not just the real time, but also simulated time.
pub trait Duration:
    'static
    + Sized
    + Copy
    + Clone
    + Debug
    + std::ops::Add<Output = Self>
    + std::ops::AddAssign
    + std::ops::Sub<Output = Self>
    + std::ops::SubAssign
    + std::ops::Mul<u32>
    + std::ops::MulAssign<u32>
    + std::ops::Div<u32>
    + std::ops::DivAssign<u32>
    + std::cmp::PartialOrd
    + std::cmp::Ord
    + std::cmp::PartialEq
    + std::cmp::Eq
    + std::hash::Hash
// it would also be nice to include the bound `u32: std::ops::Mul<Self>`, but we can't do that
// without a where clause, and the where clause would need to be duplicated everywhere that we use
// the trait
{
    /// See [`std::time::Duration::MAX`].
    const MAX: Self;
    /// See [`std::time::Duration::NANOSECOND`].
    const NANOSECOND: Self;
    /// See [`std::time::Duration::MICROSECOND`].
    const MICROSECOND: Self;
    /// See [`std::time::Duration::MILLISECOND`].
    const MILLISECOND: Self;
    /// See [`std::time::Duration::SECOND`].
    const SECOND: Self;
    /// See [`std::time::Duration::ZERO`].
    const ZERO: Self;

    /// See [`std::time::Duration::as_micros`].
    fn as_micros(&self) -> u128;
    /// See [`std::time::Duration::as_millis`].
    fn as_millis(&self) -> u128;
    /// See [`std::time::Duration::as_nanos`].
    fn as_nanos(&self) -> u128;
    /// See [`std::time::Duration::as_secs`].
    fn as_secs(&self) -> u64;
    /// See [`std::time::Duration::checked_add`].
    fn checked_add(self, rhs: Self) -> Option<Self>;
    /// See [`std::time::Duration::checked_div`].
    fn checked_div(self, rhs: u32) -> Option<Self>;
    /// See [`std::time::Duration::checked_mul`].
    fn checked_mul(self, rhs: u32) -> Option<Self>;
    /// See [`std::time::Duration::checked_sub`].
    fn checked_sub(self, rhs: Self) -> Option<Self>;
    /// See [`std::time::Duration::from_micros`].
    fn from_micros(micros: u64) -> Self;
    /// See [`std::time::Duration::from_millis`].
    fn from_millis(millis: u64) -> Self;
    /// See [`std::time::Duration::from_nanos`].
    fn from_nanos(nanos: u64) -> Self;
    /// See [`std::time::Duration::from_secs`].
    fn from_secs(secs: u64) -> Self;
    /// See [`std::time::Duration::is_zero`].
    fn is_zero(&self) -> bool;
    /// See [`std::time::Duration::saturating_add`].
    fn saturating_add(self, rhs: Self) -> Self;
    /// See [`std::time::Duration::saturating_mul`].
    fn saturating_mul(self, rhs: u32) -> Self;
    /// See [`std::time::Duration::saturating_sub`].
    fn saturating_sub(self, rhs: Self) -> Self;
    /// See [`std::time::Duration::subsec_micros`].
    fn subsec_micros(&self) -> u32;
    /// See [`std::time::Duration::subsec_millis`].
    fn subsec_millis(&self) -> u32;
    /// See [`std::time::Duration::subsec_nanos`].
    fn subsec_nanos(&self) -> u32;
}

/// Calls into [`std::time::Instant`] methods of the same name.
impl Instant for std::time::Instant {
    type Duration = std::time::Duration;

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

/// Calls into [`std::time::Duration`] methods of the same name.
impl Duration for std::time::Duration {
    const MAX: Self = Self::MAX;
    // the std lib does contain these constants on nightly
    const NANOSECOND: Self = Self::from_nanos(1);
    const MICROSECOND: Self = Self::from_micros(1);
    const MILLISECOND: Self = Self::from_millis(1);
    const SECOND: Self = Self::from_secs(1);
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
