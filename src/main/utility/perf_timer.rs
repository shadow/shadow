use std::sync::atomic::{compiler_fence, Ordering};
use std::time::{Duration, Instant};

/// Intended as a drop-in-replacement for glib's GTimer.
pub struct PerfTimer {
    start_time: Option<Instant>,
    elapsed: Duration,
}

impl PerfTimer {
    /// Create a timer, and start it.
    pub fn new_started() -> Self {
        Self {
            start_time: Some(Instant::now()),
            elapsed: Duration::new(0, 0),
        }
    }

    /// Create a timer, but don't start it.
    pub fn new_stopped() -> Self {
        Self {
            start_time: None,
            elapsed: Duration::new(0, 0),
        }
    }

    /// Start the timer, which must not already be running.
    pub fn start(&mut self) {
        compiler_fence(Ordering::SeqCst);
        debug_assert!(self.start_time.is_none());
        self.start_time = Some(Instant::now());
        compiler_fence(Ordering::SeqCst);
    }

    /// Stop the timer, which must already be running.
    pub fn stop(&mut self) {
        compiler_fence(Ordering::SeqCst);
        debug_assert!(self.start_time.is_some());
        if let Some(t) = self.start_time.take() {
            self.elapsed += Instant::now().duration_since(t)
        }
        compiler_fence(Ordering::SeqCst);
    }

    /// Total time elapsed while the timer has been running.
    pub fn elapsed(&self) -> Duration {
        let mut e = self.elapsed;
        if let Some(t) = self.start_time.as_ref() {
            e += Instant::now().duration_since(*t)
        }
        e
    }
}

impl Default for PerfTimer {
    fn default() -> Self {
        Self::new_started()
    }
}
