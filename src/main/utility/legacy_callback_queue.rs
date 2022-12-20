use std::cell::RefCell;
use std::ops::DerefMut;

use crate::cshadow as c;

thread_local! {
    static C_CALLBACK_QUEUE: RefCell<Option<LegacyCallbackQueue>> = RefCell::new(None);
}

/// Similar to [`CallbackQueue`](crate::utility::callback_queue::CallbackQueue), but with some
/// tweaks to work with C code.
struct LegacyCallbackQueue(std::collections::VecDeque<Box<dyn FnOnce()>>);

impl LegacyCallbackQueue {
    pub fn new() -> Self {
        Self(std::collections::VecDeque::new())
    }

    pub fn len(&self) -> usize {
        self.0.len()
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    pub fn add(&mut self, f: impl FnOnce() + 'static) {
        self.0.push_back(Box::new(f));
    }

    /// The `Option` must be `Some`.
    pub fn run(queue: &RefCell<Option<Self>>) {
        // loop until there are no more events
        let mut count = 0;

        loop {
            // the mutable borrow is short-lived so that new callbacks can be added to the queue
            // while running `f`
            let Some(f) = queue.borrow_mut().as_mut().unwrap().0.pop_front() else {
                break;
            };

            // run the event and allow it to add new events
            (f)();

            count += 1;
            if count == 200 {
                log::trace!("Possible infinite loop of event callbacks.");
            } else if count == 10_000 {
                log::warn!("Very likely an infinite loop of event callbacks.");
            }
        }
    }
}

impl std::ops::Drop for LegacyCallbackQueue {
    fn drop(&mut self) {
        // don't show the following warning message if panicking
        if std::thread::panicking() {
            return;
        }

        if !self.is_empty() {
            // panic in debug builds since the backtrace will be helpful for debugging
            debug_panic!("Dropping LegacyEventQueue while it still has events pending.");
        }
    }
}

/// Helper function to initialize and run a global thread-local callback queue. This is a hack so
/// that C [`LegacyFile`](crate::cshadow::LegacyFile)s can queue listener callbacks using
/// `add_to_global_cb_queue`. This is primarily for [`TCP`](crate::cshadow::TCP) objects, and should
/// not be used with Rust file objects.
///
/// Just like
/// [`CallbackQueue::queue_and_run`](crate::utility::callback_queue::CallbackQueue::queue_and_run),
/// the closure should make any borrows of the file object, rather than making any borrows outside
/// of the closure.
pub fn with_global_cb_queue<T>(f: impl FnOnce() -> T) -> T {
    C_CALLBACK_QUEUE.with(|cb_queue| {
        if cb_queue.borrow().is_some() {
            // we seem to be in a nested `with_global_cb_queue()` call, so just run the closure with
            // the existing queue
            return f();
        }

        // set the global queue
        assert!(cb_queue
            .borrow_mut()
            .replace(LegacyCallbackQueue::new())
            .is_none());

        let rv = f();

        // run and drop the global queue
        LegacyCallbackQueue::run(cb_queue);
        assert!(cb_queue.borrow_mut().take().is_some());

        rv
    })
}

mod export {
    use super::*;

    /// Returns `true` if successfully added to the queue, or `false` if there was no queue.
    #[no_mangle]
    pub unsafe extern "C" fn add_to_global_cb_queue(
        listener: *mut c::StatusListener,
        status: c::Status,
        changed: c::Status,
    ) -> bool {
        C_CALLBACK_QUEUE.with(|cb_queue| {
            let mut cb_queue = cb_queue.borrow_mut();

            if let Some(cb_queue) = cb_queue.deref_mut() {
                unsafe { c::statuslistener_ref(listener) };
                cb_queue.add(move || {
                    unsafe { c::statuslistener_onStatusChanged(listener, status, changed) };
                    unsafe { c::statuslistener_unref(listener) };
                });
                true
            } else {
                false
            }
        })
    }
}
