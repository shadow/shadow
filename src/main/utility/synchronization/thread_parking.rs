use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

/// Used to unpark a thread, but which hasn't been assigned a specific thread yet.
#[derive(Debug, Clone)]
pub struct ThreadUnparkerUnassigned {
    ready_flag: Arc<AtomicBool>,
}

/// Used to unpark a thread.
#[derive(Debug, Clone)]
pub struct ThreadUnparker {
    thread: std::thread::Thread,
    ready_flag: Arc<AtomicBool>,
}

/// Used to park a thread. The `ThreadParker` is derived from a `ThreadUnparker` or
/// `ThreadUnparkerUnassigned`, and must only be used on the thread which the unparker was assigned
/// to. If the `ThreadUnparker` was assigned to thread A, then `ThreadParker::park()` must only be
/// called from thread A.
#[derive(Debug, Clone)]
pub struct ThreadParker {
    ready_flag: Arc<AtomicBool>,
}

impl ThreadUnparkerUnassigned {
    pub fn new() -> Self {
        Self {
            ready_flag: Arc::new(AtomicBool::new(false)),
        }
    }

    /// Assign this to a thread that will be unparked.
    #[must_use]
    pub fn assign(self, thread: std::thread::Thread) -> ThreadUnparker {
        ThreadUnparker::new(self.ready_flag, thread)
    }

    /// Get a new [`ThreadParker`]. The `ThreadParker` must only be used from the thread which we
    /// will later assign ourselves to using `assign()`. This is useful if you want to pass a
    /// `ThreadParker` to a new thread before you have a handle to that thread.
    pub fn parker(&self) -> ThreadParker {
        ThreadParker::new(Arc::clone(&self.ready_flag))
    }
}

impl ThreadUnparker {
    fn new(ready_flag: Arc<AtomicBool>, thread: std::thread::Thread) -> Self {
        Self { ready_flag, thread }
    }

    /// Unpark the assigned thread.
    pub fn unpark(&self) {
        // rust guarantees that everything that happens before the `unpark()` is visible on the
        // other thread after returning from `park()`, so the `ready_flag` should always be visible
        // as true after the other thread returns from `park()` (the store should not be reordered
        // after the `unpark()`):
        // https://github.com/rust-lang/rust/blob/21b246587c2687935bd6004ffa5dcc4f4dd6600d/library/std/src/sys_common/thread_parker/futex.rs#L21-L27
        self.ready_flag.store(true, Ordering::Release);
        self.thread.unpark();
    }

    /// Get a new [`ThreadParker`] for the assigned thread.
    pub fn parker(&self) -> ThreadParker {
        ThreadParker::new(Arc::clone(&self.ready_flag))
    }
}

impl ThreadParker {
    fn new(ready_flag: Arc<AtomicBool>) -> Self {
        Self { ready_flag }
    }

    /// Park the current thread until [`ThreadUnparker::unpark()`] is called. You must only call
    /// `park()` from the thread which the corresponding `ThreadUnparker` is assigned, otherwise a
    /// deadlock may occur.
    pub fn park(&self) {
        while self
            .ready_flag
            .compare_exchange(true, false, Ordering::Release, Ordering::Relaxed)
            .is_err()
        {
            // if unpark() was called before this park(), this park() will return immediately
            std::thread::park();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parking() {
        let unparker = ThreadUnparkerUnassigned::new();
        let parker = unparker.parker();

        let handle = std::thread::spawn(move || {
            parker.park();
        });

        let unparker = unparker.assign(handle.thread().clone());

        // there is no race condition here: if `unpark` happens first, `park` will return
        // immediately
        unparker.unpark();

        handle.join().unwrap();
    }
}
