use std::time::Duration;

use shadow_shim_helper_rs::{emulated_time::EmulatedTime, simulation_time::SimulationTime};

/// Accounts for time executing code on the native CPU, calculating a
/// corresponding delay for when the simulated CPU should be allowed to run
/// next.
pub struct Cpu {
    simulated_frequency: u64,
    native_frequency: u64,
    threshold: Option<SimulationTime>,
    precision: Option<SimulationTime>,
    now: EmulatedTime,
    time_cpu_available: EmulatedTime,
}

impl Cpu {
    /// `threshold`: if None, never report a delay. Otherwise only report a
    /// delay after it is more than this threshold.
    ///
    /// `precision`: if provided, round individual native delays to this
    /// granularity (rounding up at midpoint). Panics if this is `Some(0)`.
    pub fn new(
        simulated_frequency: u64,
        native_frequency: u64,
        threshold: Option<SimulationTime>,
        precision: Option<SimulationTime>,
    ) -> Self {
        if let Some(precision) = precision {
            assert!(precision > SimulationTime::ZERO)
        }

        Self {
            simulated_frequency,
            native_frequency,
            threshold,
            precision,
            now: EmulatedTime::MIN,
            time_cpu_available: EmulatedTime::MIN,
        }
    }

    /// Configure the current time.
    pub fn update_time(&mut self, now: EmulatedTime) {
        self.now = now;
    }

    /// Account for `native_delay` spent natively executing code.
    pub fn add_delay(&mut self, native_delay: Duration) {
        // first normalize the physical CPU to the virtual CPU. We use u128 here
        // to guarantee no overflow when multiplying two u64's.
        let cycles = native_delay
            .as_nanos()
            .checked_mul(self.native_frequency as u128)
            .unwrap();
        let simulated_delay_nanos = cycles / (self.simulated_frequency as u128);
        // Theoretically possible to overflow (and then panic) here, but only
        // for a delay of > ~500 years.
        let mut adjusted_delay =
            SimulationTime::from_nanos(simulated_delay_nanos.try_into().unwrap());

        // round the adjusted delay to the nearest precision if needed
        if let Some(precision) = self.precision {
            let remainder = adjusted_delay % precision;

            // first round down (this is also the first step to rounding up)
            adjusted_delay -= remainder;

            // now check if we should round up
            let half_precision = precision / 2;
            if remainder >= half_precision {
                // we should have rounded up, so adjust up by one interval
                adjusted_delay += precision;
            }
        }

        self.time_cpu_available += adjusted_delay;
    }

    /// Calculate the simulated delay until this CPU is ready to run again.
    pub fn delay(&self) -> SimulationTime {
        let Some(threshold) = self.threshold else {
            return SimulationTime::ZERO;
        };
        let Some(built_up_delay) = self.time_cpu_available.checked_duration_since(&self.now) else {
            return SimulationTime::ZERO;
        };
        if built_up_delay > threshold {
            built_up_delay
        } else {
            SimulationTime::ZERO
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const MHZ: u64 = 1_000_000;

    #[test]
    fn no_threshold_never_delays() {
        let mut cpu = Cpu::new(1000 * MHZ, 1000 * MHZ, None, None);
        assert_eq!(cpu.delay(), SimulationTime::ZERO);

        cpu.add_delay(Duration::from_secs(1));
        assert_eq!(cpu.delay(), SimulationTime::ZERO);
    }

    #[test]
    fn basic_delay() {
        let mut cpu = Cpu::new(
            1000 * MHZ,
            1000 * MHZ,
            Some(SimulationTime::NANOSECOND),
            None,
        );
        assert_eq!(cpu.delay(), SimulationTime::ZERO);

        // Set our start time.
        cpu.update_time(EmulatedTime::UNIX_EPOCH);

        // Simulate having spent 1 native second.
        cpu.add_delay(Duration::from_secs(1));

        // With this configuration, simulated delay should be 1:1 with native time spent.
        assert_eq!(cpu.delay(), SimulationTime::SECOND);

        // Moving time forward should reduce the delay by that amount.
        cpu.update_time(EmulatedTime::UNIX_EPOCH + SimulationTime::from_millis(100));
        assert_eq!(cpu.delay(), SimulationTime::from_millis(900));

        // Moving time forward to exactly the end of the delay should result in zero delay
        cpu.update_time(EmulatedTime::UNIX_EPOCH + SimulationTime::from_secs(1));
        assert_eq!(cpu.delay(), SimulationTime::ZERO);

        // Moving time past the end of the delay should still result in zero delay
        cpu.update_time(EmulatedTime::UNIX_EPOCH + SimulationTime::from_secs(2));
        assert_eq!(cpu.delay(), SimulationTime::ZERO);
    }

    #[test]
    fn no_overflow() {
        // Use 1 THz processor
        let mut cpu = Cpu::new(
            1_000_000 * MHZ,
            1_000_000 * MHZ,
            Some(SimulationTime::NANOSECOND),
            None,
        );

        // Simulate having spent a native hour
        cpu.add_delay(Duration::from_secs(3600));

        assert_eq!(cpu.delay(), SimulationTime::from_secs(3600));
    }

    #[test]
    fn faster_native() {
        let mut cpu = Cpu::new(
            1000 * MHZ,
            1100 * MHZ,
            Some(SimulationTime::NANOSECOND),
            None,
        );
        assert_eq!(cpu.delay(), SimulationTime::ZERO);

        // Since the simulated CPU is slower, it takes longer to execute.
        cpu.add_delay(Duration::from_millis(1000));
        assert_eq!(cpu.delay(), SimulationTime::from_millis(1100));
    }

    #[test]
    fn faster_simulated() {
        let mut cpu = Cpu::new(
            1100 * MHZ,
            1000 * MHZ,
            Some(SimulationTime::NANOSECOND),
            None,
        );
        assert_eq!(cpu.delay(), SimulationTime::ZERO);

        // Since the simulated CPU is faster, it takes less time to execute.
        cpu.add_delay(Duration::from_millis(1100));
        assert_eq!(cpu.delay(), SimulationTime::from_millis(1000));
    }

    #[test]
    fn thresholded() {
        let threshold = SimulationTime::from_millis(100);
        let mut cpu = Cpu::new(1000 * MHZ, 1000 * MHZ, Some(threshold), None);
        assert_eq!(cpu.delay(), SimulationTime::ZERO);

        // Simulate having spent 1 ms.
        cpu.add_delay(Duration::from_millis(1));

        // Since this is below the threshold, delay should still be 0.
        assert_eq!(cpu.delay(), SimulationTime::ZERO);

        // Spend another 100 ms.
        cpu.add_delay(Duration::from_millis(100));

        // Now that we're past the threshold, should see the full 101 ms we've spent.
        assert_eq!(cpu.delay(), SimulationTime::from_millis(101));
    }

    #[test]
    fn round_lt_half_precision() {
        let precision = SimulationTime::from_millis(100);
        let mut cpu = Cpu::new(
            1000 * MHZ,
            1000 * MHZ,
            Some(SimulationTime::NANOSECOND),
            Some(precision),
        );
        cpu.add_delay(Duration::from_millis(149));
        assert_eq!(cpu.delay(), SimulationTime::from_millis(100));
    }

    #[test]
    fn round_half_precision() {
        let precision = SimulationTime::from_millis(100);
        let mut cpu = Cpu::new(
            1000 * MHZ,
            1000 * MHZ,
            Some(SimulationTime::NANOSECOND),
            Some(precision),
        );
        cpu.add_delay(Duration::from_millis(150));
        assert_eq!(cpu.delay(), SimulationTime::from_millis(200));
    }

    #[test]
    fn round_gt_half_precision() {
        let precision = SimulationTime::from_millis(100);
        let mut cpu = Cpu::new(
            1000 * MHZ,
            1000 * MHZ,
            Some(SimulationTime::NANOSECOND),
            Some(precision),
        );
        cpu.add_delay(Duration::from_millis(151));
        assert_eq!(cpu.delay(), SimulationTime::from_millis(200));
    }
}
