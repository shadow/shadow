//! An event/listener framework to allow listeners to subscribe to event sources. To prevent
//! recursive events (events which trigger new events) from leading to two listeners attempting to
//! mutate the same state simultaneously, an event queue is used to defer new events until the
//! current event has finished running.

use std::collections::VecDeque;
use std::num::Wrapping;
use std::sync::{Arc, Weak};

use atomic_refcell::AtomicRefCell;

/// A queue of events (functions/closures) which when run can add their own events to the queue.
/// This allows events to be deferred and run later.
#[allow(clippy::type_complexity)]
pub struct CallbackQueue(VecDeque<Box<dyn FnOnce(&mut Self)>>);

impl CallbackQueue {
    /// Create an empty event queue.
    pub fn new() -> Self {
        Self(VecDeque::new())
    }

    pub fn len(&self) -> usize {
        self.0.len()
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Add an event to the queue.
    pub fn add(&mut self, f: impl FnOnce(&mut Self) + 'static) {
        self.0.push_back(Box::new(f));
    }

    /// Process all of the events in the queue (and any new events that are generated).
    pub fn run(&mut self) {
        // loop until there are no more events
        let mut count = 0;
        while let Some(f) = self.0.pop_front() {
            // run the event and allow it to add new events
            (f)(self);

            count += 1;
            if count == 10_000 {
                log::trace!("Possible infinite loop of event callbacks.");
            } else if count == 10_000_000 {
                log::warn!("Very likely an infinite loop of event callbacks.");
            }
        }
    }

    /// A convenience function to create a [CallbackQueue], allow the caller to add events, and
    /// process them all before returning.
    fn queue_and_run<F, U>(f: F) -> U
    where
        F: FnOnce(&mut Self) -> U,
    {
        let mut cb_queue = Self::new();
        let rv = (f)(&mut cb_queue);
        cb_queue.run();
        rv
    }

    /// A convenience function to create a [CallbackQueue], allow the caller to add events, and
    /// process them all before returning.
    ///
    /// This also has the side-effect of ensuring that a global thread-local queue is configured for
    /// C code using
    /// [`with_global_cb_queue`](crate::utility::legacy_callback_queue::with_global_cb_queue). We do
    /// this for convenience and to help prevent bugs where we forget to call
    /// `with_global_cb_queue`. Ideally we'd like to remove this side-effect as we remove more C
    /// code from Shadow.
    ///
    /// TODO: Once we have removed C file objects, remove this function and make
    /// `Self::queue_and_run` public.
    pub fn queue_and_run_with_legacy<F, U>(f: F) -> U
    where
        F: FnOnce(&mut Self) -> U,
    {
        crate::utility::legacy_callback_queue::with_global_cb_queue(|| Self::queue_and_run(f))
    }
}

impl Default for CallbackQueue {
    fn default() -> Self {
        Self::new()
    }
}

impl Drop for CallbackQueue {
    fn drop(&mut self) {
        // don't show the following warning message if panicking
        if std::thread::panicking() {
            return;
        }

        if !self.is_empty() {
            // panic in debug builds since the backtrace will be helpful for debugging
            debug_panic!("Dropping EventQueue while it still has events pending.");
        }
    }
}

#[derive(Clone, Copy, PartialEq, PartialOrd)]
struct HandleId(u32);

#[must_use = "Stops listening when the handle is dropped"]
/// A handle is used to stop listening for events. The listener will receive events until the handle
/// is dropped, or [`stop_listening()`](Self::stop_listening) is called.
pub struct Handle<T> {
    id: HandleId,
    source: Weak<AtomicRefCell<EventSourceInner<T>>>,
}

impl<T> Handle<T> {
    fn new(id: HandleId, source: Weak<AtomicRefCell<EventSourceInner<T>>>) -> Self {
        Self { id, source }
    }

    /// Stop listening for new events. Equivalent to dropping the handle.
    pub fn stop_listening(self) {}
}

impl<T> Drop for Handle<T> {
    fn drop(&mut self) {
        if let Some(x) = self.source.upgrade() {
            x.borrow_mut().remove_listener(self.id);
        }
    }
}

/// Emits events to subscribed listeners.
pub struct EventSource<T> {
    inner: Arc<AtomicRefCell<EventSourceInner<T>>>,
}

impl<T: Clone + Copy + 'static> EventSource<T> {
    pub fn new() -> Self {
        Self {
            inner: Arc::new(AtomicRefCell::new(EventSourceInner::new())),
        }
    }

    /// Add a listener.
    pub fn add_listener(
        &mut self,
        notify_fn: impl Fn(T, &mut CallbackQueue) + Send + Sync + 'static,
    ) -> Handle<T> {
        let inner_ref = Arc::downgrade(&Arc::clone(&self.inner));
        self.inner.borrow_mut().add_listener(inner_ref, notify_fn)
    }

    /// Notify all listeners.
    pub fn notify_listeners(&mut self, message: T, cb_queue: &mut CallbackQueue) {
        for (_, l) in &self.inner.borrow().listeners {
            let l_clone = l.clone();
            cb_queue.add(move |cb_queue| (l_clone)(message, cb_queue));
        }
    }
}

impl<T: Clone + Copy + 'static> Default for EventSource<T> {
    fn default() -> Self {
        Self::new()
    }
}

type Listener<T> = Arc<dyn Fn(T, &mut CallbackQueue) + Send + Sync>;

struct EventSourceInner<T> {
    listeners: Vec<(HandleId, Listener<T>)>,
    next_id: Wrapping<u32>,
}

impl<T> EventSourceInner<T> {
    pub fn new() -> Self {
        Self {
            listeners: Vec::new(),
            next_id: Wrapping(0),
        }
    }

    fn get_unused_id(&mut self) -> HandleId {
        // it's very unlikely that there will be collisions, but we loop anyways since we
        // don't care about worst-case performance here
        loop {
            let id = HandleId(self.next_id.0);
            self.next_id += Wrapping(1);

            if !self.listeners.iter().any(|x| x.0 == id) {
                break id;
            }
        }
    }

    pub fn add_listener(
        &mut self,
        inner: std::sync::Weak<AtomicRefCell<Self>>,
        notify_fn: impl Fn(T, &mut CallbackQueue) + Send + Sync + 'static,
    ) -> Handle<T> {
        let handle_id = self.get_unused_id();

        self.listeners.push((handle_id, Arc::new(notify_fn)));

        Handle::new(handle_id, inner)
    }

    pub fn remove_listener(&mut self, id: HandleId) {
        self.listeners
            .remove(self.listeners.iter().position(|x| x.0 == id).unwrap());
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_eventqueue() {
        let counter = Arc::new(AtomicRefCell::new(0u32));
        let counter_clone = Arc::clone(&counter);

        let mut source = EventSource::new();

        let handle = source.add_listener(move |inc, _| {
            *counter_clone.borrow_mut() += inc;
        });

        CallbackQueue::queue_and_run(|queue| source.notify_listeners(1, queue));
        CallbackQueue::queue_and_run(|queue| source.notify_listeners(3, queue));

        handle.stop_listening();

        CallbackQueue::queue_and_run(|queue| source.notify_listeners(5, queue));
        CallbackQueue::queue_and_run(|queue| source.notify_listeners(7, queue));

        assert_eq!(*counter.borrow(), 4);
    }
}
