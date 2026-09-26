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

#[derive(Debug, Clone)]
pub struct Notify {
    state: Arc<(Mutex<bool>, Condvar)>,
}

impl Notify {
    pub fn new() -> Self {
        Self {
            state: Arc::new((Mutex::new(false), Condvar::new())),
        }
    }

    /// Waits for a notify signal to be sent by another thread.
    pub fn wait(&self) {
        let (lock, cvar) = self.state.as_ref();
        let mut value = lock.lock().unwrap();
        while !*value {
            value = cvar.wait(value).unwrap();
        }
    }

    /// Sets the value to true and notifies the condition variable if the value
    /// changed from false to true.
    pub fn notify(&self) {
        let (lock, cvar) = self.state.as_ref();
        let mut value = lock.lock().unwrap();
        if !*value {
            *value = true;
            cvar.notify_one();
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
    notify: Notify,
}

impl EpollWaiter {
    pub fn new(epoll_fd: i32, timeout: Duration, notify: Notify) -> Self {
        Self {
            epoll_fd,
            timeout_ms: timeout.as_millis().try_into().unwrap(),
            notify,
        }
    }

    pub fn wait(&self) -> WaiterResult {
        let mut events = Vec::new();
        events.resize(1, epoll::EpollEvent::empty());

        let t0 = std::time::Instant::now();
        self.notify.notify();
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
pub fn init(epoll_fd: i32, timeout: Duration) -> (EpollWaiter, Notify) {
    let notify = Notify::new();
    (EpollWaiter::new(epoll_fd, timeout, notify.clone()), notify)
}

pub fn readable_zero() -> epoll::EpollEvent {
    epoll::EpollEvent::new(EpollFlags::EPOLLIN, 0)
}
