use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
#[cfg(debug_assertions)]
use std::sync::Mutex;

/// Used to unpark a thread, but which hasn't been assigned a specific thread yet.
#[derive(Debug, Clone)]
pub struct ThreadUnparkerUnassigned {
    ready_flag: Arc<AtomicBool>,
    /// The ID of the thread which is allowed to park.
    #[cfg(debug_assertions)]
    shared_thread_id: Arc<Mutex<Option<std::thread::ThreadId>>>,
}

/// Used to unpark a thread.
#[derive(Debug, Clone)]
pub struct ThreadUnparker {
    thread: std::thread::Thread,
    ready_flag: Arc<AtomicBool>,
    /// The ID of the thread which is allowed to park.
    #[cfg(debug_assertions)]
    shared_thread_id: Arc<Mutex<Option<std::thread::ThreadId>>>,
}

/// Used to park a thread. The `ThreadParker` is derived from a `ThreadUnparker` or
/// `ThreadUnparkerUnassigned`, and must only be used on the thread which the unparker was assigned
/// to. If the `ThreadUnparker` was assigned to thread A, then `ThreadParker::park()` must only be
/// called from thread A.
#[derive(Debug, Clone)]
pub struct ThreadParker {
    ready_flag: Arc<AtomicBool>,
    /// The ID of the thread which is allowed to park.
    #[cfg(debug_assertions)]
    shared_thread_id: Arc<Mutex<Option<std::thread::ThreadId>>>,
}

impl ThreadUnparkerUnassigned {
    pub fn new() -> Self {
        Self {
            ready_flag: Arc::new(AtomicBool::new(false)),
            // there is no assigned thread yet
            #[cfg(debug_assertions)]
            shared_thread_id: Arc::new(Mutex::new(None)),
        }
    }

    /// Assign this to a thread that will be unparked.
    #[must_use]
    pub fn assign(self, thread: std::thread::Thread) -> ThreadUnparker {
        ThreadUnparker::new(
            self.ready_flag,
            thread,
            #[cfg(debug_assertions)]
            self.shared_thread_id,
        )
    }

    /// Get a new [`ThreadParker`]. The `ThreadParker` must only be used from the thread which we
    /// will later assign ourselves to using `assign()`. This is useful if you want to pass a
    /// `ThreadParker` to a new thread before you have a handle to that thread.
    pub fn parker(&self) -> ThreadParker {
        ThreadParker::new(
            Arc::clone(&self.ready_flag),
            #[cfg(debug_assertions)]
            Arc::clone(&self.shared_thread_id),
        )
    }
}

impl Default for ThreadUnparkerUnassigned {
    fn default() -> Self {
        Self::new()
    }
}

impl ThreadUnparker {
    fn new(
        ready_flag: Arc<AtomicBool>,
        thread: std::thread::Thread,
        #[cfg(debug_assertions)] shared_thread_id: Arc<Mutex<Option<std::thread::ThreadId>>>,
    ) -> Self {
        // set the value of `shared_thread_id`, or if it was already set, verify that it's the
        // correct value
        #[cfg(debug_assertions)]
        {
            let mut shared_thread_id = shared_thread_id.lock().unwrap();

            // it's valid to park before the unparker has been assigned to a thread
            // (`shared_thread_id` would be `Some` in this case), so if it was already set we should
            // check that it is the correct thread
            let shared_thread_id = shared_thread_id.get_or_insert_with(|| thread.id());

            assert_eq!(
                *shared_thread_id,
                thread.id(),
                "An earlier `ThreadParker::park()` was called from the wrong thread"
            );
        }

        Self {
            ready_flag,
            thread,
            #[cfg(debug_assertions)]
            shared_thread_id,
        }
    }

    /// Unpark the assigned thread.
    pub fn unpark(&self) {
        // TODO: Rust does not guarantee any synchronization between the thread that parks and the
        // thread that unparks:
        //
        // https://doc.rust-lang.org/nightly/std/thread/fn.park.html#park-and-unpark
        //
        // > Notice that being unblocked does not imply any synchronization with someone that
        // > unparked this thread, it could also be spurious. For example, it would be a valid, but
        // > inefficient, implementation to make both park and unpark return immediately without doing
        // > anything.
        //
        // There is no guarentee that the change to `ready_flag` will be visible after the parked
        // thread resumes, which may lead to a deadlock. In practice, rust currently does use an
        // atomic with release-acquire ordering, but this might be relaxed in the future.
        // https://github.com/rust-lang/rust/blob/21b246587c2687935bd6004ffa5dcc4f4dd6600d/library/std/src/sys_common/thread_parker/futex.rs#L21-L27
        self.ready_flag.store(true, Ordering::Release);
        self.thread.unpark();
    }

    /// Get a new [`ThreadParker`] for the assigned thread.
    pub fn parker(&self) -> ThreadParker {
        ThreadParker::new(
            Arc::clone(&self.ready_flag),
            #[cfg(debug_assertions)]
            Arc::clone(&self.shared_thread_id),
        )
    }
}

impl ThreadParker {
    fn new(
        ready_flag: Arc<AtomicBool>,
        #[cfg(debug_assertions)] shared_thread_id: Arc<Mutex<Option<std::thread::ThreadId>>>,
    ) -> Self {
        Self {
            ready_flag,
            #[cfg(debug_assertions)]
            shared_thread_id,
        }
    }

    /// Park the current thread until [`ThreadUnparker::unpark()`] is called. You must only call
    /// `park()` from the thread which the corresponding `ThreadUnparker` is assigned, otherwise a
    /// deadlock may occur. In debug builds, this should panic instead of deadlock.
    pub fn park(&self) {
        while self
            .ready_flag
            .compare_exchange(true, false, Ordering::Acquire, Ordering::Relaxed)
            .is_err()
        {
            // verify that we're parking from the proper thread (only in debug builds since this is
            // slow)
            #[cfg(debug_assertions)]
            {
                let mut shared_thread_id = self.shared_thread_id.lock().unwrap();

                // it's valid to park before the unparker has been assigned to a thread
                // (`shared_thread_id` would be `None` in this case), so we should set the thread ID
                // here and let the unparker panic instead if this is the wrong thread
                let shared_thread_id =
                    shared_thread_id.get_or_insert_with(|| std::thread::current().id());

                assert_eq!(
                    *shared_thread_id,
                    std::thread::current().id(),
                    "`ThreadParker::park()` was called from the wrong thread"
                );
            }

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
