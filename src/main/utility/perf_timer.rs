use std::time::{Duration, Instant};

/// Intended as a drop-in-replacement for glib's GTimer.
pub struct PerfTimer {
    start_time: Option<Instant>,
    elapsed: Duration,
}

impl PerfTimer {
    /// Create timer, which starts running.
    pub fn new() -> Self {
        Self {
            start_time: Some(Instant::now()),
            elapsed: Duration::new(0, 0),
        }
    }

    /// Start the timer, which must not already be running.
    pub fn start(&mut self) {
        debug_assert!(self.start_time.is_none());
        self.start_time = Some(Instant::now());
    }

    /// Stop the timer, which must already be running.
    pub fn stop(&mut self) {
        debug_assert!(self.start_time.is_some());
        if let Some(t) = self.start_time.take() {
            self.elapsed += Instant::now().duration_since(t)
        }
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
