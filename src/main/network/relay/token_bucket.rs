use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::simulation_time::SimulationTime;

use crate::core::worker::Worker;

#[derive(Debug)]
pub struct TokenBucket {
    capacity: u64,
    balance: u64,
    refill_increment: u64,
    refill_interval: SimulationTime,
    last_refill: EmulatedTime,
}

impl TokenBucket {
    /// Creates a new token bucket rate limiter with an initial balance set to
    /// `capacity`. The capacity enables burstiness, while the long term rate is
    /// defined by `refill_increment` tokens being periodically added to the
    /// bucket every `refill_interval` duration. Returns None if any of the args
    /// are non-positive.
    pub fn new(
        capacity: u64,
        refill_increment: u64,
        refill_interval: SimulationTime,
    ) -> Option<TokenBucket> {
        // Since we start at full capacity, starting with a last refill time of
        // 0 is inconsequential.
        TokenBucket::new_inner(
            capacity,
            refill_increment,
            refill_interval,
            EmulatedTime::SIMULATION_START,
        )
    }

    /// Implements the functionality of `new()` allowing the caller to set the
    /// last refill time. Useful for testing.
    fn new_inner(
        capacity: u64,
        refill_increment: u64,
        refill_interval: SimulationTime,
        last_refill: EmulatedTime,
    ) -> Option<TokenBucket> {
        if capacity > 0 && refill_increment > 0 && !refill_interval.is_zero() {
            log::trace!(
                "Initializing token bucket with capacity {capacity}, will refill {refill_increment} tokens every {refill_interval:?}"
            );
            Some(TokenBucket {
                capacity,
                balance: capacity,
                refill_increment,
                refill_interval,
                last_refill,
            })
        } else {
            None
        }
    }

    /// Remove `decrement` tokens from the bucket if and only if the bucket
    /// contains at least `decrement` tokens. Returns the updated token balance
    /// on success, or the duration until the next refill event after which we
    /// would have enough tokens to allow the decrement to conform on error
    /// (returned durations always align with this `TokenBucket`'s discrete
    /// refill interval boundaries). Passing a 0 `decrement` always succeeds.
    pub fn comforming_remove(&mut self, decrement: u64) -> Result<u64, SimulationTime> {
        let now = Worker::current_time().unwrap();
        self.conforming_remove_inner(decrement, &now)
    }

    /// Implements the functionality of `comforming_remove()` without calling into the
    /// `Worker` module. Useful for testing.
    fn conforming_remove_inner(
        &mut self,
        decrement: u64,
        now: &EmulatedTime,
    ) -> Result<u64, SimulationTime> {
        let next_refill_span = self.lazy_refill(now);
        self.balance = self
            .balance
            .checked_sub(decrement)
            .ok_or_else(|| self.compute_conforming_duration(decrement, next_refill_span))?;
        Ok(self.balance)
    }

    /// Computes the duration required to refill enough tokens such that our
    /// balance can be decremented by the given `decrement`. Returned durations
    /// always align with this `TokenBucket`'s discrete refill interval
    /// boundaries, as configured by its refill interval. `next_refill_span` is
    /// the duration until the next refill, which may be less than a full refill
    /// interval.
    fn compute_conforming_duration(
        &self,
        decrement: u64,
        next_refill_span: SimulationTime,
    ) -> SimulationTime {
        let required_token_increment = decrement.saturating_sub(self.balance);

        let num_required_refills = {
            // Same as `required_token_increment.div_ceil(self.refill_increment);`
            let num_refills = required_token_increment / self.refill_increment;
            let remainder = required_token_increment % self.refill_increment;
            if remainder > 0 {
                num_refills + 1
            } else {
                num_refills
            }
        };

        match num_required_refills {
            0 => SimulationTime::ZERO,
            1 => next_refill_span,
            _ => next_refill_span.saturating_add(
                self.refill_interval
                    .saturating_mul(num_required_refills.checked_sub(1).unwrap()),
            ),
        }
    }

    /// Simulates a fixed refill schedule following the bucket's configured
    /// refill interval. This function will lazily apply refills that may have
    /// occurred in the past but were not applied yet because the token bucket
    /// was not in use. No refills will occur if called multiple times within
    /// the same refill interval. Returns the duration to the next refill event.
    fn lazy_refill(&mut self, now: &EmulatedTime) -> SimulationTime {
        // Use saturating to tolerate small clock skew between worker threads
        // that might cause `now` to appear earlier than `last_refill`.
        let mut span = now.saturating_duration_since(&self.last_refill);

        if span >= self.refill_interval {
            // Apply refills for the scheduled refill events that have passed.
            let num_refills = span
                .as_nanos()
                .checked_div(self.refill_interval.as_nanos())
                .unwrap();
            let num_tokens = self
                .refill_increment
                .saturating_mul(num_refills.try_into().unwrap());
            debug_assert!(num_tokens > 0);

            self.balance = self
                .balance
                .saturating_add(num_tokens)
                .clamp(0, self.capacity);

            // Update to the most recent refill event time.
            let inc = self
                .refill_interval
                .saturating_mul(num_refills.try_into().unwrap());
            self.last_refill = self.last_refill.saturating_add(inc);

            span = now.saturating_duration_since(&self.last_refill);
        }

        debug_assert!(span < self.refill_interval);
        self.refill_interval - span
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::network::tests::mock_time_millis;

    #[test]
    fn test_new_invalid_args() {
        let now = mock_time_millis(1000);
        assert!(TokenBucket::new_inner(0, 1, SimulationTime::from_nanos(1), now).is_none());
        assert!(TokenBucket::new_inner(1, 0, SimulationTime::from_nanos(1), now).is_none());
        assert!(TokenBucket::new_inner(1, 1, SimulationTime::ZERO, now).is_none());
    }

    #[test]
    fn test_new_valid_args() {
        let now = mock_time_millis(1000);
        assert!(TokenBucket::new_inner(1, 1, SimulationTime::from_nanos(1), now).is_some());
        assert!(TokenBucket::new_inner(1, 1, SimulationTime::from_millis(1), now).is_some());
        assert!(TokenBucket::new_inner(1, 1, SimulationTime::from_secs(1), now).is_some());

        let tb = TokenBucket::new_inner(54321, 12345, SimulationTime::from_secs(1), now).unwrap();
        assert_eq!(tb.capacity, 54321);
        assert_eq!(tb.refill_increment, 12345);
        assert_eq!(tb.refill_interval, SimulationTime::from_secs(1));
    }

    #[test]
    fn test_refill_after_one_interval() {
        let interval = SimulationTime::from_millis(10);
        let capacity = 100;
        let increment = 10;
        let now = mock_time_millis(1000);

        let mut tb = TokenBucket::new_inner(capacity, increment, interval, now).unwrap();
        assert_eq!(tb.balance, capacity);

        // Remove all tokens
        assert!(tb.conforming_remove_inner(capacity, &now).is_ok());
        assert_eq!(tb.balance, 0);

        for i in 1..=(capacity / increment) {
            // One more interval of time passes
            let later = now + interval.saturating_mul(i);
            // Should cause an increment to the balance
            let result = tb.conforming_remove_inner(0, &later);
            assert!(result.is_ok());
            assert_eq!(result.unwrap(), tb.balance);
            assert_eq!(tb.balance, increment.saturating_mul(i));
        }
    }

    #[test]
    fn test_refill_after_multiple_intervals() {
        let now = mock_time_millis(1000);
        let mut tb = TokenBucket::new_inner(100, 10, SimulationTime::from_millis(10), now).unwrap();

        // Remove all tokens
        assert!(tb.conforming_remove_inner(100, &now).is_ok());
        assert_eq!(tb.balance, 0);

        // 5 Refill intervals have passed
        let later = now + SimulationTime::from_millis(50);

        let result = tb.conforming_remove_inner(0, &later);
        assert!(result.is_ok());
        assert_eq!(result.unwrap(), tb.balance);
        assert_eq!(tb.balance, 50);
    }

    #[test]
    fn test_capacity_limit() {
        let now = mock_time_millis(1000);
        let mut tb = TokenBucket::new_inner(100, 10, SimulationTime::from_millis(10), now).unwrap();

        // Remove all tokens
        assert!(tb.conforming_remove_inner(100, &now).is_ok());
        assert_eq!(tb.balance, 0);

        // Far into the future
        let later = now + SimulationTime::from_secs(60);

        // Should not exceed capacity
        let result = tb.conforming_remove_inner(0, &later);
        assert!(result.is_ok());
        assert_eq!(result.unwrap(), tb.balance);
        assert_eq!(tb.balance, 100);
    }

    #[test]
    fn test_remove_error() {
        let now = mock_time_millis(1000);
        let mut tb =
            TokenBucket::new_inner(100, 10, SimulationTime::from_millis(125), now).unwrap();

        // Clear the bucket
        let result = tb.conforming_remove_inner(100, &now);
        assert!(result.is_ok());
        assert_eq!(result.unwrap(), 0);

        // This many tokens are not available
        let result = tb.conforming_remove_inner(50, &now);
        assert!(result.is_err());

        // Refilling 10 tokens every 125 millis will require 5 refills
        let dur_until_conforming = SimulationTime::from_millis(125 * 5);
        assert_eq!(result.unwrap_err(), dur_until_conforming);

        // Moving time forward is still an error
        let inc = 10;
        let now = mock_time_millis(1000 + inc);
        let result = tb.conforming_remove_inner(50, &now);
        assert!(result.is_err());

        // We still need 5 refills, but we are 10 millis closer until it conforms
        let dur_until_conforming = SimulationTime::from_millis(125 * 5 - inc);
        assert_eq!(result.unwrap_err(), dur_until_conforming);
    }
}
