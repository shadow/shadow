use std::sync::{Arc, Condvar, Mutex};

/// A latch counter.
///
/// If a counter is cloned, it will inherit the counter's state for the current generation. For
/// example if a counter is cloned after it has already counted down, then the new counter will also
/// be treated as if it had already counted down in the current generation. If a counter is cloned
/// before it has counted down, then the new counter will also need to count down in the current
/// generation.
#[derive(Debug)]
pub struct LatchCounter {
    inner: Arc<LatchInner>,
    /// An ID for this counter's count-down round.
    generation: usize,
}

/// A latch waiter.
///
/// If a waiter is cloned, it will inherit the waiter's state for the current generation. For
/// example if a waiter is cloned after it has already waited, then the new waiter will also be
/// treated as if it had already waited in the current generation. If a waiter is cloned before it
/// has waited, then the new waiter will also need to wait in the current generation.
#[derive(Debug)]
pub struct LatchWaiter {
    inner: Arc<LatchInner>,
    /// An ID for this waiter's count-down round.
    generation: usize,
}

#[derive(Debug)]
struct LatchInner {
    lock: Mutex<LatchState>,
    cond: Condvar,
}

#[derive(Debug)]
struct LatchState {
    /// The current latch "round".
    generation: usize,
    /// Number of counters remaining.
    counters: usize,
    /// Number of waiters remaining.
    waiters: usize,
    /// Total number of counters.
    total_counters: usize,
    /// Total number of waiters.
    total_waiters: usize,
}

/// Build a latch counter and waiter. The counter and waiter can be cloned to create new counters
/// and waiters.
pub fn build_count_down_latch() -> (LatchCounter, LatchWaiter) {
    let inner = Arc::new(LatchInner {
        lock: Mutex::new(LatchState {
            generation: 0,
            counters: 1,
            waiters: 1,
            total_counters: 1,
            total_waiters: 1,
        }),
        cond: Condvar::new(),
    });

    let counter = LatchCounter {
        inner: Arc::clone(&inner),
        generation: 0,
    };

    let waiter = LatchWaiter {
        inner,
        generation: 0,
    };

    (counter, waiter)
}

impl LatchState {
    pub fn advance_generation(&mut self) {
        debug_assert_eq!(self.counters, 0);
        debug_assert_eq!(self.waiters, 0);
        self.counters = self.total_counters;
        self.waiters = self.total_waiters;
        self.generation = self.generation.wrapping_add(1);
    }
}

impl LatchCounter {
    /// Decrement the latch count and wake the waiters if the count reaches 0. This must not be
    /// called more than once per generation (must not be called again until all of the waiters have
    /// returned from their [`LatchWaiter::wait()`] calls), otherwise it will panic.
    pub fn count_down(&mut self) {
        let counters;
        {
            let mut lock = self.inner.lock.lock().unwrap();

            if self.generation != lock.generation {
                let latch_gen = lock.generation;
                std::mem::drop(lock);
                panic!(
                    "Counter generation does not match latch generation ({} != {})",
                    self.generation, latch_gen
                );
            }

            lock.counters = lock.counters.checked_sub(1).unwrap();
            counters = lock.counters;
        }

        // if this is the last counter, notify the waiters
        if counters == 0 {
            self.inner.cond.notify_all();
        }

        self.generation = self.generation.wrapping_add(1);
    }
}

impl LatchWaiter {
    /// Wait for the latch count to reach 0. If the latch count has already reached 0 for the
    /// current genration, this will return immediately.
    pub fn wait(&mut self) {
        {
            let lock = self.inner.lock.lock().unwrap();

            let mut lock = self
                .inner
                .cond
                // wait until we're in the active generation and all counters have counted down
                .wait_while(lock, |x| self.generation != x.generation || x.counters > 0)
                .unwrap();

            lock.waiters = lock.waiters.checked_sub(1).unwrap();

            // if this is the last waiter (and we already know that there are no more counters), start
            // the next generation
            if lock.waiters == 0 {
                lock.advance_generation();
            }
        }

        self.generation = self.generation.wrapping_add(1);
    }
}

impl Clone for LatchCounter {
    fn clone(&self) -> Self {
        let mut lock = self.inner.lock.lock().unwrap();
        lock.total_counters = lock.total_counters.checked_add(1).unwrap();

        // if we haven't already counted down during the current generation
        if self.generation == lock.generation {
            lock.counters = lock.counters.checked_add(1).unwrap();
        }

        LatchCounter {
            inner: Arc::clone(&self.inner),
            generation: self.generation,
        }
    }
}

impl Clone for LatchWaiter {
    fn clone(&self) -> Self {
        let mut lock = self.inner.lock.lock().unwrap();
        lock.total_waiters = lock.total_waiters.checked_add(1).unwrap();

        // if we haven't already waited during the current generation
        if self.generation == lock.generation {
            lock.waiters = lock.waiters.checked_add(1).unwrap();
        }

        LatchWaiter {
            inner: Arc::clone(&self.inner),
            generation: self.generation,
        }
    }
}

impl std::ops::Drop for LatchCounter {
    fn drop(&mut self) {
        let mut lock = self.inner.lock.lock().unwrap();
        lock.total_counters = lock.total_counters.checked_sub(1).unwrap();

        // if we haven't already counted down during the current generation
        if self.generation == lock.generation {
            lock.counters = lock.counters.checked_sub(1).unwrap();
        }

        // if this is the last counter, notify the waiters
        if lock.counters == 0 {
            self.inner.cond.notify_all();
        }
    }
}

impl std::ops::Drop for LatchWaiter {
    fn drop(&mut self) {
        let mut lock = self.inner.lock.lock().unwrap();
        lock.total_waiters = lock.total_waiters.checked_sub(1).unwrap();

        // if we haven't already waited during the current generation
        if self.generation == lock.generation {
            lock.waiters = lock.waiters.checked_sub(1).unwrap();
        }

        // if this is the last waiter and there are no more counters, start the next generation
        if lock.waiters == 0 && lock.counters == 0 {
            lock.advance_generation();
        }
    }
}

#[cfg(test)]
mod tests {
    use std::time::Duration;

    use atomic_refcell::AtomicRefCell;
    use rand::{Rng, SeedableRng};

    use super::*;

    #[test]
    fn test_clone() {
        let (mut counter, mut waiter) = build_count_down_latch();
        let (mut counter_clone, mut waiter_clone) = (counter.clone(), waiter.clone());

        counter.count_down();
        counter_clone.count_down();
        waiter.wait();
        waiter_clone.wait();
    }

    #[test]
    fn test_clone_before_countdown() {
        let (mut counter, mut waiter) = build_count_down_latch();

        // the cloned counter will also need to count down for the current generation
        let mut counter_clone = counter.clone();
        counter.count_down();
        counter_clone.count_down();
        waiter.wait();

        counter.count_down();
        counter_clone.count_down();
        waiter.wait();

        let (mut counter, mut waiter) = build_count_down_latch();

        // the cloned waiter will also need to wait for the current generation
        let mut waiter_clone = waiter.clone();
        counter.count_down();
        waiter.wait();
        waiter_clone.wait();

        counter.count_down();
        waiter.wait();
        waiter_clone.wait();
    }

    #[test]
    fn test_clone_after_countdown() {
        let (mut counter, mut waiter) = build_count_down_latch();

        counter.count_down();
        // the cloned counter will also be considered "counted down" for the current generation
        let mut counter_clone = counter.clone();
        // if the cloned counter did count down here, it would panic
        waiter.wait();

        counter.count_down();
        counter_clone.count_down();
        waiter.wait();

        let (mut counter, mut waiter) = build_count_down_latch();
        let mut waiter_clone = waiter.clone();

        counter.count_down();
        waiter.wait();
        // the cloned waiter will also be considered "waited" for the current generation
        let mut waiter_clone_2 = waiter.clone();
        // if the cloned waiter did wait here, it would be waiting for the next generation
        waiter_clone.wait();

        counter.count_down();
        waiter.wait();
        waiter_clone.wait();
        waiter_clone_2.wait();
    }

    #[test]
    #[should_panic]
    fn test_double_count() {
        let (mut counter, mut _waiter) = build_count_down_latch();
        counter.count_down();
        counter.count_down();
    }

    #[test]
    fn test_single_thread() {
        let (mut counter, mut waiter) = build_count_down_latch();

        counter.count_down();
        waiter.wait();
        counter.count_down();
        waiter.wait();
        counter.count_down();
        waiter.wait();

        let mut waiter_clone = waiter.clone();

        counter.count_down();
        waiter.wait();
        waiter_clone.wait();

        counter.count_down();
        waiter.wait();
        waiter_clone.wait();
    }

    #[test]
    fn test_multi_thread() {
        let (mut exclusive_counter, mut exclusive_waiter) = build_count_down_latch();
        let (mut shared_counter, mut shared_waiter) = build_count_down_latch();
        let repeat = 30;

        let lock = Arc::new(AtomicRefCell::new(()));
        let lock_clone = Arc::clone(&lock);

        // The goal of this test is to make sure that the new threads alternate with the main thread
        // to access the atomic refcell. The new threads each hold on to a shared borrow of the
        // atomic refcell for ~5 ms, then the main thread gets an exclusive borrow for ~5 ms,
        // repeating. If these time slices ever overlap, then either a shared or exclusive borrow
        // will cause a panic and the test will fail. Randomness is added to the sleeps to vary the
        // order in which threads wait and count down, to try to cover more edge cases.

        let thread_fn = move |seed| {
            let mut rng = rand::rngs::StdRng::seed_from_u64(seed);

            for _ in 0..repeat {
                // wait for the main thread to be done with its exclusive borrow
                std::thread::sleep(Duration::from_millis(5));
                exclusive_waiter.wait();
                {
                    // a shared borrow for a duration in the range of 0-10 ms
                    let _x = lock_clone.borrow();
                    std::thread::sleep(Duration::from_millis(rng.random_range(0..10)));
                }
                shared_counter.count_down();
            }
        };

        // start 5 threads
        let handles: Vec<_> = (0..5)
            .map(|seed| {
                let mut f = thread_fn.clone();
                std::thread::spawn(move || f(seed))
            })
            .collect();
        std::mem::drop(thread_fn);

        let mut rng = rand::rngs::StdRng::seed_from_u64(100);

        for _ in 0..repeat {
            {
                // an exclusive borrow for a duration in the range of 0-10 ms
                let _x = lock.borrow_mut();
                std::thread::sleep(Duration::from_millis(rng.random_range(0..10)));
            }
            exclusive_counter.count_down();
            // wait for the other threads to be done with their shared borrow
            std::thread::sleep(Duration::from_millis(5));
            shared_waiter.wait();
        }

        for h in handles {
            h.join().unwrap();
        }
    }
}
