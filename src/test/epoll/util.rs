use std::sync::{Arc, Condvar, Mutex};
use std::time::{Duration, Instant};
use std::{fs, thread};

use nix::sys::epoll::{self, EpollFlags};
use nix::unistd;

/// When we expect an event to be ready quickly, use a long timeout that
/// shouldn't normally trigger but provides an upper bound on error.
#[allow(dead_code)]
pub const LONG_DUR: Duration = Duration::from_secs(10);

/// When we expect that the timeout will be reached, use a shorter one to
/// prevent the test from consuming too much runtime.
pub const SHORT_DUR: Duration = Duration::from_millis(200);

/// A one-shot synchronization gate that blocks threads until it is tripped.
/// Once tripped, it stays open permanently.
#[derive(Debug, Clone)]
pub struct Latch {
    state: Arc<(Mutex<bool>, Condvar)>,
}

impl Latch {
    /// Creates a new, closed Latch.
    pub fn new() -> Self {
        Self {
            state: Arc::new((Mutex::new(false), Condvar::new())),
        }
    }

    /// Blocks the current thread until the latch is tripped. If the latch is
    /// already open, it returns immediately.
    pub fn wait(&self) {
        let (lock, cvar) = self.state.as_ref();
        let mut is_open = lock.lock().unwrap();

        // The loop should handle spurious wakeups safely.
        while !*is_open {
            is_open = cvar.wait(is_open).unwrap();
        }
    }

    /// Permanently opens the latch and wakes up all waiting threads.
    pub fn trip(&self) {
        let (lock, cvar) = self.state.as_ref();
        let mut is_open = lock.lock().unwrap();

        if !*is_open {
            *is_open = true;
            cvar.notify_all();
        }
    }

    fn tripped(&self) -> bool {
        let (lock, _) = self.state.as_ref();
        let is_open = lock.lock().unwrap();
        *is_open
    }
}

#[derive(Debug)]
pub struct WaiterResult {
    pub duration: Duration,
    pub epoll_res: nix::Result<usize>,
    pub events: Vec<epoll::EpollEvent>,
}

pub struct EpollWaiter {
    epoll_fd: i32,
    timeout_ms: isize,
    syscall_latch: Latch,
}

impl EpollWaiter {
    pub fn new(epoll_fd: i32, timeout: Duration, syscall_latch: Latch) -> Self {
        Self {
            epoll_fd,
            timeout_ms: timeout.as_millis().try_into().unwrap(),
            syscall_latch,
        }
    }

    pub fn wait(&self) -> WaiterResult {
        let mut events = Vec::new();
        events.resize(10, epoll::EpollEvent::empty());

        // Set up a thread to trip the latch when we're blocked in epoll_wait().
        trip_when_blocked(self.syscall_latch.clone());

        let t0 = Instant::now();
        let res = epoll::epoll_wait(self.epoll_fd, &mut events, self.timeout_ms);
        let t1 = Instant::now();

        // We are now certain that the syscall has been made. We trip the latch
        // just in case the background thread failed to do so yet.
        self.syscall_latch.trip();

        events.resize(res.unwrap_or(0), epoll::EpollEvent::empty());

        WaiterResult {
            duration: t1.duration_since(t0),
            epoll_res: res,
            events,
        }
    }

    #[allow(dead_code)]
    pub fn wait_then_read(&self) -> WaiterResult {
        let result = self.wait();

        for ev in &result.events {
            let fd = ev.data() as i32;
            // we don't care if the read is successful or not (another thread may have already read)
            let _ = unistd::read(fd, &mut [0]);
        }

        result
    }
}

/// Create epoll_wait state that can be run inside a thread, with linked sync
/// primitives that give the main thread some semblence of control.
pub fn init(epoll_fd: i32, timeout: Duration) -> (EpollWaiter, Latch) {
    let latch = Latch::new();
    (EpollWaiter::new(epoll_fd, timeout, latch.clone()), latch)
}

pub fn readable_zero() -> epoll::EpollEvent {
    epoll::EpollEvent::new(EpollFlags::EPOLLIN, 0)
}

fn trip_when_blocked(latch: Latch) {
    // Get the calling thread id.
    let raw_tid = rustix::thread::gettid().as_raw_nonzero().get();

    // Spawn a background thread to trip when the current thread blocks.
    thread::spawn(move || {
        if test_utils::running_in_shadow() {
            // When running in Shadow, sleeping any amount of time should
            // advance the clock enough forward that the parent will have made
            // the syscall and be in a blocked state.
            thread::sleep(Duration::from_millis(1));
            latch.trip();
            return;
        }

        // Running in Linux: use procfs to check the parent thread status.
        let stat_path = format!("/proc/self/task/{}/stat", raw_tid);

        while !latch.tripped() {
            // Give our parent a chance to run in case we are CPU constrained.
            thread::yield_now();

            if let Ok(stat) = fs::read_to_string(&stat_path) {
                let parts: Vec<&str> = stat.split_whitespace().collect();
                if parts.len() > 2 && parts[2] == "S" {
                    // The parent is in the sleeping state ('S').
                    latch.trip();
                    return;
                } else {
                    // Avoid busy wait.
                    thread::sleep(Duration::from_millis(1));
                    continue;
                }
            } else {
                // Cannot get procfs data so we use a probabilistic workaraound.
                // There is still a race here. If we trip the latch to signal to
                // another thread that our parent is inside a syscall, the other
                // thread might wake up and run _before_ our parent is actually
                // blocked. For now we just delay the trip() call a bit to bias
                // the race toward the desired outcome.
                //
                // TODO: can we solve this by using ptrace to manually trace the
                // parent and trip the latch only when we can guarantee that it
                // has made the syscall and is blocked?
                thread::sleep(Duration::from_millis(10));
                latch.trip();
                return;
            }
        }
    });
}
