/// Helper for formatting times.
#[derive(Debug, Eq, PartialEq)]
pub struct TimeParts {
    pub hours: u32,
    pub mins: u32,
    pub secs: u64,
    pub nanos: u64,
}

impl TimeParts {
    pub fn from_nanos(total_nanos: u128) -> Self {
        // Total number of integer seconds.
        let whole_secs = u64::try_from(total_nanos / 1_000_000_000).unwrap();
        // Total number of integer minutes.
        let whole_mins = u32::try_from(whole_secs / 60).unwrap();
        // Total number of integer hours, which is also the hours part.
        let whole_hours = whole_mins / 60;

        // Integer minutes, after whole hours are subtracted out.
        let mins_part = whole_mins - whole_hours * 60;
        // Integers secs, after integer minutes are subtracted out.
        let secs_part = whole_secs - u64::from(whole_mins) * 60;
        // Nanos, after integer secs are subtracted out.
        let nanos_part =
            u64::try_from(total_nanos - u128::from(whole_secs) * 1_000_000_000).unwrap();

        Self {
            hours: whole_hours,
            mins: mins_part,
            secs: secs_part,
            nanos: nanos_part,
        }
    }

    /// Format as HH:MM:SS.
    pub fn fmt_hr_min_sec(&self) -> TimePartsFmtHrMinSec<'_> {
        TimePartsFmtHrMinSec { time: self }
    }

    /// Format as HH:MM:SS.mmm.
    pub fn fmt_hr_min_sec_milli(&self) -> TimePartsFmtHrMinSecMilli<'_> {
        TimePartsFmtHrMinSecMilli { time: self }
    }

    /// Format as HH:MM:SS.nnnnnnnnn.
    pub fn fmt_hr_min_sec_nano(&self) -> TimePartsFmtHrMinSecNano<'_> {
        TimePartsFmtHrMinSecNano { time: self }
    }
}

pub struct TimePartsFmtHrMinSec<'a> {
    time: &'a TimeParts,
}

impl core::fmt::Display for TimePartsFmtHrMinSec<'_> {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        write!(
            f,
            "{:02}:{:02}:{:02}",
            self.time.hours, self.time.mins, self.time.secs
        )
    }
}

pub struct TimePartsFmtHrMinSecMilli<'a> {
    time: &'a TimeParts,
}

impl core::fmt::Display for TimePartsFmtHrMinSecMilli<'_> {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        write!(
            f,
            "{:02}:{:02}:{:02}.{:03}",
            self.time.hours,
            self.time.mins,
            self.time.secs,
            self.time.nanos / 1_000_000
        )
    }
}

pub struct TimePartsFmtHrMinSecNano<'a> {
    time: &'a TimeParts,
}

impl core::fmt::Display for TimePartsFmtHrMinSecNano<'_> {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        write!(
            f,
            "{:02}:{:02}:{:02}.{:09}",
            self.time.hours, self.time.mins, self.time.secs, self.time.nanos,
        )
    }
}

#[cfg(test)]
#[test]
fn test_time_parts() {
    use std::time::Duration;
    assert_eq!(
        TimeParts::from_nanos(
            (Duration::from_nanos(1) + Duration::from_secs(3600 + 60 + 1)).as_nanos()
        ),
        TimeParts {
            hours: 1,
            mins: 1,
            secs: 1,
            nanos: 1
        }
    );
}
