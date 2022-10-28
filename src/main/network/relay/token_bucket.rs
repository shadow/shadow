use crate::core::worker::Worker;
use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::simulation_time::SimulationTime;

pub struct TokenBucket {
    capacity: u64,
    balance: u64,
    refill_size: u64,
    refill_interval: SimulationTime,
    last_refill: EmulatedTime,
}

impl TokenBucket {
    /// Creates a new token bucket rate limiter. The capacity enables
    /// burstiness, while the long term rate is defined by refill_size tokens
    /// being periodically added to the bucket every refill_interval duration.
    /// Returns None if any of the args are non-positive.
    pub fn new(
        capacity: u64,
        refill_size: u64,
        refill_interval: SimulationTime,
    ) -> Option<TokenBucket> {
        let now = Worker::current_time().unwrap();
        TokenBucket::new_inner(capacity, refill_size, refill_interval, now)
    }

    /// Implements the functionality of `new()` without calling into the
    /// `Worker` module. Useful for testing.
    fn new_inner(
        capacity: u64,
        refill_size: u64,
        refill_interval: SimulationTime,
        now: EmulatedTime,
    ) -> Option<TokenBucket> {
        if capacity > 0 && refill_size > 0 && !refill_interval.is_zero() {
            Some(TokenBucket {
                capacity,
                balance: capacity,
                refill_size,
                refill_interval,
                last_refill: now,
            })
        } else {
            None
        }
    }

    /// Remove size tokens from the bucket if and only if the bucket contains at
    /// least size tokens. Returns the updated token balance on success, and the
    /// duration until the next token refill on error. Passing a 0 size always
    /// succeeds.
    pub fn comforming_remove(&mut self, size: u64) -> Result<u64, SimulationTime> {
        let now = Worker::current_time().unwrap();
        self.conforming_remove_inner(size, &now)
    }

    /// Implements the functionality of `comforming_remove()` without calling into the
    /// `Worker` module. Useful for testing.
    fn conforming_remove_inner(
        &mut self,
        size: u64,
        now: &EmulatedTime,
    ) -> Result<u64, SimulationTime> {
        let next_refill_span = self.lazy_refill(now);
        self.balance = self.balance.checked_sub(size).ok_or(next_refill_span)?;
        Ok(self.balance)
    }

    /// Simulates a fixed refill schedule following the bucket's configured
    /// refill interval. This function will lazily apply refills that may have
    /// occurred in the past but were not applied yet because the token bucket
    /// was not in use. No refills will occur if called multiple times within
    /// the same refill interval. Returns the duration to the next refill event.
    fn lazy_refill(&mut self, now: &EmulatedTime) -> SimulationTime {
        let mut span = now.duration_since(&self.last_refill);

        if span >= self.refill_interval {
            // Apply refills for the scheduled refill events that have passed.
            let num_refills = span
                .as_nanos()
                .checked_div(self.refill_interval.as_nanos())
                .unwrap();
            let num_tokens = self
                .refill_size
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

            span = now.duration_since(&self.last_refill);
        }

        debug_assert!(span < self.refill_interval);
        self.refill_interval - span
    }
}

mod export {
    use super::*;

    #[no_mangle]
    pub extern "C" fn tokenbucket_new(
        capacity: u64,
        refill_size: u64,
        refill_interval_nanos: u64,
    ) -> *mut TokenBucket {
        Box::into_raw(Box::new(
            TokenBucket::new(
                capacity,
                refill_size,
                SimulationTime::from_nanos(refill_interval_nanos),
            )
            .unwrap(),
        ))
    }

    #[no_mangle]
    pub extern "C" fn tokenbucket_free(tokenbucket_ptr: *mut TokenBucket) {
        if tokenbucket_ptr.is_null() {
            return;
        }
        unsafe { Box::from_raw(tokenbucket_ptr) };
    }

    #[no_mangle]
    pub extern "C" fn tokenbucket_consume(
        tokenbucket_ptr: *mut TokenBucket,
        count: u64,
        remaining_tokens: *mut u64,
        nanos_until_refill: *mut u64,
    ) -> bool {
        let tokenbucket = unsafe { tokenbucket_ptr.as_mut() }.unwrap();
        let remaining_tokens = unsafe { remaining_tokens.as_mut() }.unwrap();
        let nanos_until_refill = unsafe { nanos_until_refill.as_mut() }.unwrap();

        match tokenbucket.comforming_remove(count) {
            Ok(remaining) => {
                *remaining_tokens = remaining;
                *nanos_until_refill = 0;
                true
            }
            Err(next_refill_duration) => {
                *remaining_tokens = 0;
                *nanos_until_refill = next_refill_duration.as_nanos().try_into().unwrap();
                false
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::network::tests::mock_time_millis;

    #[test]
    fn test_new_invalid_args() {
        let now = mock_time_millis(1000);
        assert!(TokenBucket::new_inner(0, 1, SimulationTime::from_nanos(1), now.clone()).is_none());
        assert!(TokenBucket::new_inner(1, 0, SimulationTime::from_nanos(1), now.clone()).is_none());
        assert!(TokenBucket::new_inner(1, 1, SimulationTime::ZERO, now).is_none());
    }
}
