//! An event/listener framework to allow listeners to subscribe to event sources. To prevent
//! recursive events (events which trigger new events) from leading to two listeners attempting to
//! mutate the same state simultaneously, an event queue is used to defer new events until the
//! current event has finished running.

use atomic_refcell::AtomicRefCell;
use log::*;
use std::sync::{Arc, Weak};

/// A queue of events (functions/closures) which when run can add their own events to the queue.
/// This allows events to be deferred and run later.
pub struct EventQueue(std::collections::VecDeque<Box<dyn FnOnce(&mut Self)>>);

impl EventQueue {
    /// Create an empty event queue.
    pub fn new() -> Self {
        Self(std::collections::VecDeque::new())
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
            if count == 200 {
                trace!("Possible infinite loop of event callbacks.");
            } else if count == 10_000 {
                warn!("Very likely an infinite loop of event callbacks.");
            }
        }
    }

    /// A convenience function to create an EventQueue, allow the caller to add events,
    /// and process them all before returning.
    pub fn queue_and_run<F, U>(f: F) -> U
    where
        F: FnOnce(&mut Self) -> U,
    {
        let mut event_queue = Self::new();
        let rv = (f)(&mut event_queue);
        event_queue.run();
        rv
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
        notify_fn: impl Fn(T, &mut EventQueue) + Send + Sync + 'static,
    ) -> Handle<T> {
        let inner_ref = Arc::downgrade(&Arc::clone(&self.inner));
        self.inner.borrow_mut().add_listener(inner_ref, notify_fn)
    }

    /// Notify all listeners.
    pub fn notify_listeners(&mut self, message: T, event_queue: &mut EventQueue) {
        for (_, l) in &self.inner.borrow().listeners {
            let l_clone = l.clone();
            event_queue.add(move |event_queue| (l_clone)(message, event_queue));
        }
    }
}

struct EventSourceInner<T> {
    listeners: std::vec::Vec<(HandleId, Arc<Box<dyn Fn(T, &mut EventQueue) + Send + Sync>>)>,
    next_id: std::num::Wrapping<u32>,
}

impl<T> EventSourceInner<T> {
    pub fn new() -> Self {
        Self {
            listeners: std::vec::Vec::new(),
            next_id: std::num::Wrapping(0),
        }
    }

    fn get_unused_id(&mut self) -> HandleId {
        // it's very unlikely that there will be collisions, but we loop anyways since we
        // don't care about worst-case performance here
        let handle_id = loop {
            let id = HandleId(self.next_id.0);
            self.next_id += std::num::Wrapping(1);

            if !self.listeners.iter().any(|x| x.0 == id) {
                break id;
            }
        };
        handle_id
    }

    pub fn add_listener(
        &mut self,
        inner: std::sync::Weak<AtomicRefCell<Self>>,
        notify_fn: impl Fn(T, &mut EventQueue) + Send + Sync + 'static,
    ) -> Handle<T> {
        let handle_id = self.get_unused_id();

        self.listeners
            .push((handle_id, Arc::new(Box::new(notify_fn))));

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

        EventQueue::queue_and_run(|queue| source.notify_listeners(1, queue));
        EventQueue::queue_and_run(|queue| source.notify_listeners(3, queue));

        handle.stop_listening();

        EventQueue::queue_and_run(|queue| source.notify_listeners(5, queue));
        EventQueue::queue_and_run(|queue| source.notify_listeners(7, queue));

        assert_eq!(*counter.borrow(), 4);
    }
}
