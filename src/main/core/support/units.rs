/*!
Types for parsing/deserializing unit values.

```
let time = Time::from_str("10 min").unwrap();
assert_eq!(time, Time::new(10, TimePrefix::Sec));

assert_eq!(
    time.convert(TimePrefix::Sec).unwrap(),
    Time::from_str("600 sec").unwrap()
);
```
*/

use std::convert::TryInto;
use std::fmt::{self, Debug, Display};
use std::str::FromStr;

use once_cell::sync::Lazy;
use regex::Regex;
use serde::de::{Deserialize, Deserializer, Visitor};
use serde::ser::{Serialize, Serializer};

/// The prefix of a unit value denoting magnitude. Common prefixes are
/// SI prefixes (nano, micro, milli, etc).
pub trait Prefix: Clone + Copy + Default + PartialEq + FromStr + Display + Debug {
    /// The magnitude of this prefix relative to other prefixes of this type.
    fn relative_magnitude(&self) -> u128;

    /// An integer conversion factor.
    fn conversion_factor(&self, to: Self) -> Result<u128, String> {
        let from_mag = self.relative_magnitude();
        let to_mag = to.relative_magnitude();
        if from_mag % to_mag != 0 {
            return Err("Conversion would lose precision".to_string());
        }
        Ok(from_mag / to_mag)
    }

    /// A floating point conversion factor.
    fn conversion_factor_lossy(&self, to: Self) -> f64 {
        let from_mag = self.relative_magnitude();
        let to_mag = to.relative_magnitude();
        from_mag as f64 / to_mag as f64
    }
}

/// Common SI prefixes (including base-2 prefixes since they're similar).
#[derive(Debug, Copy, Clone, PartialEq)]
pub enum SiPrefix {
    Nano,
    Micro,
    Milli,
    Base,
    Kilo,
    Kibi,
    Mega,
    Mebi,
    Giga,
    Gibi,
    Tera,
    Tebi,
}

impl Default for SiPrefix {
    fn default() -> Self {
        Self::Base
    }
}

impl FromStr for SiPrefix {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "n" => Ok(Self::Nano),
            "u" | "μ" => Ok(Self::Micro),
            "m" => Ok(Self::Milli),
            "K" => Ok(Self::Kilo),
            "Ki" => Ok(Self::Kibi),
            "M" => Ok(Self::Mega),
            "Mi" => Ok(Self::Mebi),
            "G" => Ok(Self::Giga),
            "Gi" => Ok(Self::Gibi),
            "T" => Ok(Self::Tera),
            "Ti" => Ok(Self::Tebi),
            _ => Err("Unit prefix was not one of (n|u|μ|m|K|Ki|M|Mi|G|Gi|T|Ti)".to_string()),
        }
    }
}

impl fmt::Display for SiPrefix {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Nano => write!(f, "n"),
            Self::Micro => write!(f, "μ"),
            Self::Milli => write!(f, "m"),
            Self::Kilo => write!(f, "K"),
            Self::Kibi => write!(f, "Ki"),
            Self::Mega => write!(f, "M"),
            Self::Mebi => write!(f, "Mi"),
            Self::Giga => write!(f, "G"),
            Self::Gibi => write!(f, "Gi"),
            Self::Tera => write!(f, "T"),
            Self::Tebi => write!(f, "Ti"),
            Self::Base => Ok(()),
        }
    }
}

impl Prefix for SiPrefix {
    fn relative_magnitude(&self) -> u128 {
        const TEN: u128 = 10;
        const TWO: u128 = 2;
        const BASE: u128 = TEN.pow(9);
        match self {
            Self::Nano => BASE / TEN.pow(9),
            Self::Micro => BASE / TEN.pow(6),
            Self::Milli => BASE / TEN.pow(3),
            Self::Base => BASE,
            Self::Kilo => BASE * TEN.pow(3),
            Self::Kibi => BASE * TWO.pow(10),
            Self::Mega => BASE * TEN.pow(6),
            Self::Mebi => BASE * TWO.pow(20),
            Self::Giga => BASE * TEN.pow(9),
            Self::Gibi => BASE * TWO.pow(30),
            Self::Tera => BASE * TEN.pow(12),
            Self::Tebi => BASE * TWO.pow(40),
        }
    }
}

/// Common SI prefixes larger than the base unit (including base-2 prefixes
/// since they're similar).
#[derive(Debug, Copy, Clone, PartialEq)]
pub enum SiPrefixUpper {
    Base,
    Kilo,
    Kibi,
    Mega,
    Mebi,
    Giga,
    Gibi,
    Tera,
    Tebi,
}

impl Default for SiPrefixUpper {
    fn default() -> Self {
        Self::Base
    }
}

impl FromStr for SiPrefixUpper {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "K" => Ok(Self::Kilo),
            "Ki" => Ok(Self::Kibi),
            "M" => Ok(Self::Mega),
            "Mi" => Ok(Self::Mebi),
            "G" => Ok(Self::Giga),
            "Gi" => Ok(Self::Gibi),
            "T" => Ok(Self::Tera),
            "Ti" => Ok(Self::Tebi),
            _ => Err("Unit prefix was not one of (K|Ki|M|Mi|G|Gi|T|Ti)".to_string()),
        }
    }
}

impl fmt::Display for SiPrefixUpper {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Kilo => write!(f, "K"),
            Self::Kibi => write!(f, "Ki"),
            Self::Mega => write!(f, "M"),
            Self::Mebi => write!(f, "Mi"),
            Self::Giga => write!(f, "G"),
            Self::Gibi => write!(f, "Gi"),
            Self::Tera => write!(f, "T"),
            Self::Tebi => write!(f, "Ti"),
            Self::Base => Ok(()),
        }
    }
}

impl Prefix for SiPrefixUpper {
    fn relative_magnitude(&self) -> u128 {
        const TEN: u128 = 10;
        const TWO: u128 = 2;
        match self {
            Self::Base => 1,
            Self::Kilo => TEN.pow(3),
            Self::Kibi => TWO.pow(10),
            Self::Mega => TEN.pow(6),
            Self::Mebi => TWO.pow(20),
            Self::Giga => TEN.pow(9),
            Self::Gibi => TWO.pow(30),
            Self::Tera => TEN.pow(12),
            Self::Tebi => TWO.pow(40),
        }
    }
}

/// Time units, which we pretend are prefixes for implementation simplicity. These
/// contain both the prefix ("n", "u", "m") and the suffix ("sec", "min", "hr")
/// and should be used with the [`Time`] unit.
#[derive(Debug, Copy, Clone, PartialEq)]
pub enum TimePrefix {
    Nano,
    Micro,
    Milli,
    Sec,
    Min,
    Hour,
}

impl Default for TimePrefix {
    fn default() -> Self {
        Self::Sec
    }
}

impl FromStr for TimePrefix {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "ns" => Ok(Self::Nano),
            "us" | "μs" => Ok(Self::Micro),
            "ms" => Ok(Self::Milli),
            "s" | "sec" => Ok(Self::Sec),
            "min" => Ok(Self::Min),
            "hour" => Ok(Self::Hour),
            _ => Err("Unit prefix was not one of (ns|us|μs|ms|s|sec|min|hour)".to_string()),
        }
    }
}

impl fmt::Display for TimePrefix {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Nano => write!(f, "ns"),
            Self::Micro => write!(f, "μs"),
            Self::Milli => write!(f, "ms"),
            Self::Sec => write!(f, "s"),
            Self::Min => write!(f, "min"),
            Self::Hour => write!(f, "hour"),
        }
    }
}

impl Prefix for TimePrefix {
    fn relative_magnitude(&self) -> u128 {
        const TEN: u128 = 10;
        const BASE: u128 = TEN.pow(9);
        match self {
            Self::Nano => BASE / TEN.pow(9),
            Self::Micro => BASE / TEN.pow(6),
            Self::Milli => BASE / TEN.pow(3),
            Self::Sec => BASE,
            Self::Min => BASE * 60,
            Self::Hour => BASE * 60 * 60,
        }
    }
}

/// Time units larger than the base unit, which we pretend are prefixes for
/// implementation simplicity. These really contain the unit suffix ("sec",
/// "min", "hr") and should be used with the [`Time`] unit.
#[derive(Debug, Copy, Clone, PartialEq)]
pub enum TimePrefixUpper {
    Sec,
    Min,
    Hour,
}

impl Default for TimePrefixUpper {
    fn default() -> Self {
        Self::Sec
    }
}

impl FromStr for TimePrefixUpper {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "s" | "sec" => Ok(Self::Sec),
            "min" => Ok(Self::Min),
            "hour" => Ok(Self::Hour),
            _ => Err("Unit prefix was not one of (s|sec|min|hour)".to_string()),
        }
    }
}

impl fmt::Display for TimePrefixUpper {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Sec => write!(f, "s"),
            Self::Min => write!(f, "min"),
            Self::Hour => write!(f, "hour"),
        }
    }
}

impl Prefix for TimePrefixUpper {
    fn relative_magnitude(&self) -> u128 {
        match self {
            Self::Sec => 1,
            Self::Min => 60,
            Self::Hour => 60 * 60,
        }
    }
}

macro_rules! visit_fn {
    ($fn_name:ident, $type:ty) => {
        fn $fn_name<E>(self, x: $type) -> Result<Self::Value, E>
        where
            E: serde::de::Error,
        {
            Ok(Self::Value::new(
                x.try_into().map_err(serde::de::Error::custom)?,
                T::default(),
            ))
        }
    };
}

macro_rules! unit_impl {
    ($name:ident, $type:ident, $suffix:tt) => {
        impl<T: Prefix> $name<T> {
            pub fn new(value: $type, prefix: T) -> Self {
                Self { value, prefix }
            }
        }

        impl<T: Prefix> Default for $name<T> {
            fn default() -> Self {
                Self::new(0, T::default())
            }
        }

        impl<T: Prefix> Unit for $name<T> {
            type U = $type;
            type T = T;

            fn value(&self) -> Self::U {
                self.value
            }

            fn prefix(&self) -> Self::T {
                self.prefix
            }

            fn suffix() -> &'static str {
                $suffix
            }

            fn convert(&self, prefix: Self::T) -> Result<Self, String> {
                let factor = self.prefix.conversion_factor(prefix)?;
                let factor = factor.try_into().unwrap();
                Ok(Self::new(
                    Self::U::checked_mul(self.value, factor).ok_or(format!(
                        "The resulting value is outside of the bounds [{}, {}]",
                        Self::U::MIN,
                        Self::U::MAX,
                    ))?,
                    prefix,
                ))
            }

            fn convert_lossy(&self, prefix: Self::T) -> Self {
                let factor = self.prefix.conversion_factor_lossy(prefix);
                Self::new(
                    (self.value as f64 * factor).round() as Self::U,
                    prefix,
                )
            }
        }

        impl<T: Prefix> Display for $name<T> {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                write!(f, "{} {}{}", self.value(), self.prefix(), Self::suffix())
            }
        }

        impl<T: Prefix> FromStr for $name<T>
        where
            <T as FromStr>::Err: std::fmt::Debug + std::fmt::Display,
        {
            type Err = Box<dyn std::error::Error>;

            fn from_str(s: &str) -> Result<Self, Self::Err> {
                static RE: Lazy<Regex> = Lazy::new(|| Regex::new(r"^([+-]?[0-9\.]*)\s*(.*)$").unwrap());

                let captures = RE.captures(s).ok_or("Unable to identify value and unit")?;
                let (value, unit) = (
                    captures.get(1).unwrap().as_str(),
                    captures.get(2).unwrap().as_str(),
                );

                let prefix = unit
                    .trim()
                    .strip_suffix($name::<T>::suffix())
                    .or(Some(unit))
                    .unwrap();

                let prefix = match prefix {
                    "" => T::default(),
                    _ => T::from_str(prefix).map_err(|x| x.to_string())?,
                };

                Ok($name::new(
                    value.parse()?,
                    prefix,
                ))
            }
        }

        impl<'de, T: Prefix> Deserialize<'de> for $name<T>
        where
            <T as FromStr>::Err: std::fmt::Debug + std::fmt::Display,
        {
            fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
            where
                D: Deserializer<'de>,
            {
                struct ValueVisitor<T> {
                    marker: std::marker::PhantomData<T>,
                }

                impl<'de, T: Prefix> Visitor<'de> for ValueVisitor<T>
                where
                    <T as FromStr>::Err: std::fmt::Debug + std::fmt::Display,
                {
                    type Value = $name<T>;

                    fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
                        formatter.write_str(stringify!(struct $name<T>))
                    }

                    fn visit_str<E>(self, s: &str) -> Result<Self::Value, E>
                    where
                        E: serde::de::Error,
                    {
                        Self::Value::from_str(s).map_err(serde::de::Error::custom)
                    }

                    visit_fn!(visit_u64, u64);
                    visit_fn!(visit_u32, u32);
                    visit_fn!(visit_u8, u8);
                    visit_fn!(visit_i64, i64);
                    visit_fn!(visit_i32, i32);
                    visit_fn!(visit_i8, i8);
                }

                deserializer.deserialize_any(ValueVisitor {
                    marker: std::marker::PhantomData,
                })
            }
        }

        impl<T: Prefix> Serialize for $name<T> {
            fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
            where
                S: Serializer,
            {
                serializer.serialize_str(&self.to_string())
            }
        }
    };
}

/// A unit containing a value (ex: an integer), a prefix (ex: an enum), and
/// allowed constant suffix strings.
pub trait Unit: Sized {
    type U;
    type T: Prefix;

    /// The value of the unit in the size of its current prefix.
    fn value(&self) -> Self::U;

    /// The current prefix.
    fn prefix(&self) -> Self::T;

    // this can be nicer once we have &str support for constant generics
    fn suffix() -> &'static str;

    /// Convert value to a different prefix, but return an error if the conversion
    /// cannot be done without possibly losing precision.
    fn convert(&self, prefix: Self::T) -> Result<Self, String>
    where
        Self: Sized;

    /// Convert value to a different prefix, even if it loses precision.
    fn convert_lossy(&self, prefix: Self::T) -> Self
    where
        Self: Sized;
}

/// An amount of time. Should only use the time prefix types ([`TimePrefix`] and
/// [`TimePrefixUpper`]) with this type.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Time<T: Prefix> {
    value: u64,
    prefix: T,
}

// Since our time prefix types ([TimePrefix] and [TimePrefixUpper]) aren't
// really prefixes, time units don't have a suffix.
unit_impl!(Time, u64, "");

/// A number of bytes.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Bytes<T: Prefix> {
    pub value: u64,
    pub prefix: T,
}

unit_impl!(Bytes, u64, "B");

/// A throughput in bits-per-second.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct BitsPerSec<T: Prefix> {
    pub value: u64,
    pub prefix: T,
}

unit_impl!(BitsPerSec, u64, "bps");

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_string() {
        assert_eq!(
            Time::from_str("10").unwrap(),
            Time::new(10, TimePrefix::Sec)
        );
        assert_eq!(
            Time::from_str("10 s").unwrap(),
            Time::new(10, TimePrefix::Sec)
        );
        assert_eq!(
            Time::from_str("10s").unwrap(),
            Time::new(10, TimePrefix::Sec)
        );
        assert_eq!(
            Time::from_str("10   s").unwrap(),
            Time::new(10, TimePrefix::Sec)
        );
        assert_eq!(
            Time::from_str("10sec").unwrap(),
            Time::new(10, TimePrefix::Sec)
        );
        assert_eq!(
            Time::from_str("10  min").unwrap(),
            Time::new(10, TimePrefix::Min)
        );
        assert_eq!(
            Time::from_str("10 ms").unwrap(),
            Time::new(10, TimePrefix::Milli)
        );
        assert_eq!(
            Time::from_str("10 μs").unwrap(),
            Time::new(10, TimePrefix::Micro)
        );

        assert!(Time::<TimePrefix>::from_str("-10 ms").is_err());
        assert!(Time::<TimePrefix>::from_str("abc 10 ms").is_err());
        assert!(Time::<TimePrefix>::from_str("10.5 ms").is_err());
        assert!(Time::<TimePrefix>::from_str("10 abc").is_err());
        assert!(Time::<TimePrefixUpper>::from_str("10 ms").is_err());

        assert_eq!(
            Bytes::from_str("10").unwrap(),
            Bytes::new(10, SiPrefixUpper::Base)
        );
        assert_eq!(
            Bytes::from_str("10 B").unwrap(),
            Bytes::new(10, SiPrefixUpper::Base)
        );
        assert_eq!(
            Bytes::from_str("10B").unwrap(),
            Bytes::new(10, SiPrefixUpper::Base)
        );
        assert_eq!(
            Bytes::from_str("10   B").unwrap(),
            Bytes::new(10, SiPrefixUpper::Base)
        );
        assert_eq!(
            Bytes::from_str("10  KB").unwrap(),
            Bytes::new(10, SiPrefixUpper::Kilo)
        );
        assert_eq!(
            Bytes::from_str("10 KiB").unwrap(),
            Bytes::new(10, SiPrefixUpper::Kibi)
        );
        assert_eq!(
            Bytes::from_str("10 MB").unwrap(),
            Bytes::new(10, SiPrefixUpper::Mega)
        );

        assert!(Bytes::<SiPrefixUpper>::from_str("-10 KB").is_err());
        assert!(Bytes::<SiPrefixUpper>::from_str("abc 10 KB").is_err());
        assert!(Bytes::<SiPrefixUpper>::from_str("10.5 KB").is_err());
        assert!(Bytes::<SiPrefixUpper>::from_str("10 abc").is_err());
        assert!(Bytes::<SiPrefixUpper>::from_str("10 mB").is_err());

        assert_eq!(
            BitsPerSec::from_str("10").unwrap(),
            BitsPerSec::new(10, SiPrefixUpper::Base)
        );
        assert_eq!(
            BitsPerSec::from_str("10 bps").unwrap(),
            BitsPerSec::new(10, SiPrefixUpper::Base)
        );
        assert_eq!(
            BitsPerSec::from_str("10bps").unwrap(),
            BitsPerSec::new(10, SiPrefixUpper::Base)
        );
        assert_eq!(
            BitsPerSec::from_str("10   bps").unwrap(),
            BitsPerSec::new(10, SiPrefixUpper::Base)
        );
        assert_eq!(
            BitsPerSec::from_str("10  Kbps").unwrap(),
            BitsPerSec::new(10, SiPrefixUpper::Kilo)
        );
        assert_eq!(
            BitsPerSec::from_str("10 Kibps").unwrap(),
            BitsPerSec::new(10, SiPrefixUpper::Kibi)
        );
        assert_eq!(
            BitsPerSec::from_str("10 Mbps").unwrap(),
            BitsPerSec::new(10, SiPrefixUpper::Mega)
        );

        assert!(BitsPerSec::<SiPrefixUpper>::from_str("-10 Kbps").is_err());
        assert!(BitsPerSec::<SiPrefixUpper>::from_str("abc 10 Kbps").is_err());
        assert!(BitsPerSec::<SiPrefixUpper>::from_str("10.5 Kbps").is_err());
        assert!(BitsPerSec::<SiPrefixUpper>::from_str("10 abc").is_err());
        assert!(BitsPerSec::<SiPrefixUpper>::from_str("10 mbps").is_err());
    }

    #[test]
    fn test_conversion() {
        let time = Time::from_str("70 min").unwrap();

        assert_eq!(
            time.convert(TimePrefix::Sec).unwrap(),
            Time::from_str("4200 sec").unwrap()
        );
        assert!(time.convert(TimePrefix::Hour).is_err());

        assert_eq!(
            time.convert_lossy(TimePrefix::Sec),
            Time::from_str("4200 sec").unwrap()
        );
        assert_eq!(
            time.convert_lossy(TimePrefix::Hour),
            Time::from_str("1 hour").unwrap()
        );

        let bw = BitsPerSec::from_str("1024 Kbps").unwrap();

        assert_eq!(
            bw.convert(SiPrefixUpper::Base).unwrap(),
            BitsPerSec::from_str("1024000 bps").unwrap()
        );
        assert!(bw.convert(SiPrefixUpper::Kibi).is_err());

        assert_eq!(
            bw.convert_lossy(SiPrefixUpper::Base),
            BitsPerSec::from_str("1024000 bps").unwrap()
        );
        assert_eq!(
            bw.convert_lossy(SiPrefixUpper::Kibi),
            BitsPerSec::from_str("1000 Kibps").unwrap()
        );
    }
}

mod export {
    use super::*;
    use log::*;

    /// Parses a string as bits-per-second. Returns '-1' on error.
    #[no_mangle]
    pub extern "C" fn parse_bandwidth(s: *const libc::c_char) -> i64 {
        assert!(!s.is_null());

        let s = match unsafe { std::ffi::CStr::from_ptr(s) }.to_str() {
            Ok(s) => s,
            Err(e) => {
                warn!("{}", e.to_string());
                return -1;
            }
        };

        let value = match BitsPerSec::from_str(s) {
            Ok(x) => x.convert(SiPrefixUpper::Base).unwrap().value(),
            Err(e) => {
                warn!("{}", e.to_string());
                return -1;
            }
        };

        let rv: i64 = match value.try_into() {
            Ok(x) => x,
            Err(e) => {
                warn!("{}", e.to_string());
                return -1;
            }
        };

        rv
    }
}
