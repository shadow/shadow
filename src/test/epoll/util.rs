use std::sync::{Arc, Condvar, Mutex};
use std::time::Duration;

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
    pre_syscall_latch: Latch,
}

impl EpollWaiter {
    pub fn new(epoll_fd: i32, timeout: Duration, pre_syscall_latch: Latch) -> Self {
        Self {
            epoll_fd,
            timeout_ms: timeout.as_millis().try_into().unwrap(),
            pre_syscall_latch,
        }
    }

    pub fn wait(&self) -> WaiterResult {
        let mut events = Vec::new();
        events.resize(10, epoll::EpollEvent::empty());

        let t0 = std::time::Instant::now();
        self.pre_syscall_latch.trip();
        let res = epoll::epoll_wait(self.epoll_fd, &mut events, self.timeout_ms);
        let t1 = std::time::Instant::now();

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
