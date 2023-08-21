use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::Arc;

use nix::errno::Errno;

/// A simple reusable latch. Multiple waiters can wait for the latch to open. After opening the
/// latch with [`open()`](Self::open), you must not open the latch again until all waiters have
/// waited with [`wait()`](LatchWaiter::wait) on the latch. In other words, you must not call
/// `open()` multiple times without making sure that all waiters have successfully returned from
/// `wait()` each time. This typically requires some other synchronization to make sure that the
/// waiters have waited. If the latch and its waiters aren't kept in sync, the waiters will usually
/// panic, but in some cases may behave incorrectly[^note].
///
/// [^note]: Since this latch uses a 32-bit wrapping integer to track the positions of the latch and
/// its waiters, calling `open()` `u32::MAX + 1` times without allowing the waiters to wait will
/// behave as if you did not call `open()` at all.
///
/// The latch uses release-acquire ordering, so any changes made before an `open()` should be
/// visible in other threads after a `wait()` returns.
#[derive(Debug)]
pub struct Latch {
    /// The generation of the latch.
    latch_gen: Arc<AtomicU32>,
}

/// A waiter that waits for the latch to open. A waiter for a latch can be created with
/// [`waiter()`](Latch::waiter). Cloning a waiter will create a new waiter with the same
/// state/generation as the existing waiter.
#[derive(Debug, Clone)]
pub struct LatchWaiter {
    /// The generation of this waiter.
    gen: u32,
    /// The read-only generation of the latch.
    latch_gen: Arc<AtomicU32>,
    /// Should we sched_yield in a spinloop indefinitely rather than futex-wait?
    spin_yield: bool,
}

impl Latch {
    /// Create a new latch.
    pub fn new() -> Self {
        Self {
            latch_gen: Arc::new(AtomicU32::new(0)),
        }
    }

    /// Get a new waiter for this latch. The new waiter will have the same generation as the latch,
    /// meaning that a single [`wait()`](LatchWaiter::wait) will block the waiter until the next
    /// latch [`open()`](Self::open).
    ///
    /// If `spin_yield` is `true`, the waiter will `sched_yield` in a spinloop indefinitely. If
    /// `spin_yield` is `false`, the waiter will futex-wait. Setting to `true` may improve
    /// performance in some workloads.
    pub fn waiter(&mut self, spin_yield: bool) -> LatchWaiter {
        LatchWaiter {
            // we're the only one who can mutate the atomic,
            // so there's no race condition here
            gen: self.latch_gen.load(Ordering::Relaxed),
            latch_gen: Arc::clone(&self.latch_gen),
            spin_yield,
        }
    }

    /// Open the latch.
    pub fn open(&mut self) {
        // the addition is wrapping
        let _prev = self.latch_gen.fetch_add(1, Ordering::Release);

        // This is safe since `AtomicU32` "has the same in-memory representation as the underlying
        // integer type, u32": https://doc.rust-lang.org/std/sync/atomic/struct.AtomicU32.html.
        //
        // TODO: Consider using `as_mut_ptr` here once it's stabilized.
        // https://doc.rust-lang.org/std/sync/atomic/struct.AtomicU32.html#method.as_mut_ptr
        static_assertions::assert_eq_size!(AtomicU32, u32);
        static_assertions::assert_eq_align!(AtomicU32, u32);

        let futex_word: &AtomicU32 = self.latch_gen.as_ref();

        let rv = unsafe {
            libc::syscall(
                libc::SYS_futex,
                futex_word.as_ptr(),
                libc::FUTEX_WAKE | libc::FUTEX_PRIVATE_FLAG,
                // the man page says to use INT_MAX, even though this is a uint32_t
                i32::MAX,
                std::ptr::null::<libc::timespec>(),
                std::ptr::null_mut::<u32>(),
                0u32,
            )
        };
        assert!(rv >= 0);
    }
}

impl Default for Latch {
    fn default() -> Self {
        Self::new()
    }
}

impl LatchWaiter {
    /// Wait for the latch to open.
    pub fn wait(&mut self) {
        loop {
            let latch_gen = self.latch_gen.load(Ordering::Acquire);

            match latch_gen.wrapping_sub(self.gen) {
                // the latch has been opened and we can advance to the next generation
                1 => break,
                // the latch has not been opened and we're at the same generation
                0 => {}
                // the latch has been opened multiple times and we haven't been kept in sync
                _ => panic!("Latch has been opened multiple times without us waiting"),
            }

            if !self.spin_yield {
                let futex_word: &AtomicU32 = self.latch_gen.as_ref();

                let rv = Errno::result(unsafe {
                    libc::syscall(
                        libc::SYS_futex,
                        futex_word.as_ptr(),
                        libc::FUTEX_WAIT | libc::FUTEX_PRIVATE_FLAG,
                        latch_gen,
                        std::ptr::null::<libc::timespec>(),
                        std::ptr::null_mut::<u32>(),
                        0u32,
                    )
                });
                assert!(
                    rv.is_ok() || rv == Err(Errno::EAGAIN) || rv == Err(Errno::EINTR),
                    "FUTEX_WAIT failed with {rv:?}"
                );
            } else {
                // we don't know if a pause instruction is beneficial or not here, but it doesn't
                // seem to hurt performance
                // https://www.intel.com/content/www/us/en/docs/cpp-compiler/developer-guide-reference/2021-9/pause-intrinsic.html
                std::hint::spin_loop();
                std::thread::yield_now();
            }
        }

        self.gen = self.gen.wrapping_add(1);
    }

    pub fn enable_spinning(&mut self, value: bool) {
        self.spin_yield = value;
    }
}

#[cfg(test)]
mod tests {
    use std::thread::sleep;
    use std::time::{Duration, Instant};

    use atomic_refcell::AtomicRefCell;

    use super::*;

    #[test]
    fn test_simple() {
        let mut latch = Latch::new();
        let mut waiter = latch.waiter(false);

        latch.open();
        waiter.wait();
        latch.open();
        waiter.wait();
        latch.open();
        waiter.wait();
    }

    #[test]
    #[should_panic]
    fn test_multiple_open() {
        let mut latch = Latch::new();
        let mut waiter = latch.waiter(false);

        latch.open();
        waiter.wait();
        latch.open();
        latch.open();

        // this should panic
        waiter.wait();
    }

    #[test]
    fn test_blocking() {
        let mut latch = Latch::new();
        let mut waiter = latch.waiter(false);

        let t = std::thread::spawn(move || {
            let start = Instant::now();
            waiter.wait();
            start.elapsed()
        });

        let sleep_duration = Duration::from_millis(200);
        sleep(sleep_duration);
        latch.open();

        let wait_duration = t.join().unwrap();

        let threshold = Duration::from_millis(40);
        assert!(wait_duration > sleep_duration - threshold);
        assert!(wait_duration < sleep_duration + threshold);
    }

    #[test]
    fn test_clone() {
        let mut latch = Latch::new();
        let mut waiter = latch.waiter(false);

        latch.open();
        waiter.wait();
        latch.open();
        waiter.wait();

        // new waiter should have the same generation
        let mut waiter_2 = waiter.clone();

        latch.open();
        waiter.wait();
        waiter_2.wait();
    }

    #[test]
    fn test_ping_pong() {
        let mut latch_1 = Latch::new();
        let mut latch_2 = Latch::new();
        let mut waiter_1 = latch_1.waiter(true);
        let mut waiter_2 = latch_2.waiter(false);

        let counter = Arc::new(AtomicRefCell::new(0));
        let counter_clone = Arc::clone(&counter);

        fn latch_loop(
            latch: &mut Latch,
            waiter: &mut LatchWaiter,
            counter: &Arc<AtomicRefCell<usize>>,
            iterations: usize,
        ) {
            for _ in 0..iterations {
                waiter.wait();
                *counter.borrow_mut() += 1;
                latch.open();
            }
        }

        let t = std::thread::spawn(move || {
            latch_loop(&mut latch_2, &mut waiter_1, &counter_clone, 100);
        });

        latch_1.open();
        latch_loop(&mut latch_1, &mut waiter_2, &counter, 100);

        t.join().unwrap();

        assert_eq!(*counter.borrow(), 200);
    }
}
