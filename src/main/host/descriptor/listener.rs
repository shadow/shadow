use crate::core::worker;
use crate::cshadow as c;
use crate::host::descriptor::{FileSignals, FileState};
use crate::utility::callback_queue::{CallbackQueue, EventSource, Handle};
use crate::utility::HostTreePointer;

#[derive(Clone, Copy, Debug)]
pub enum StateListenerFilter {
    Never,
    OffToOn,
    OnToOff,
    Always,
}

/// A wrapper for a `*mut c::StatusListener` that increments its ref count when created,
/// and decrements when dropped.
struct LegacyListener(HostTreePointer<c::StatusListener>);

impl LegacyListener {
    fn new(ptr: HostTreePointer<c::StatusListener>) -> Self {
        assert!(!unsafe { ptr.ptr().is_null() });
        unsafe { c::statuslistener_ref(ptr.ptr()) };
        Self(ptr)
    }
}

impl std::ops::Deref for LegacyListener {
    type Target = HostTreePointer<c::StatusListener>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl Drop for LegacyListener {
    fn drop(&mut self) {
        unsafe { c::statuslistener_unref(self.0.ptr()) };
    }
}

/// [Handles](Handle) for [event source](StateEventSource) listeners.
pub type StateListenHandle = Handle<(FileState, FileState, FileSignals)>;

/// Stores event listener handles so that `c::StatusListener` objects can subscribe to events.
struct LegacyListenerHelper {
    // We expect only a small number of listeners at a time, which means that performance is
    // generally better and memory usage is lower with a `Vec` than a `HashMap`. The `usize` is the
    // pointer of the [`c::StatusListener`] that corresponds to this [`Handle`].
    handles: Vec<(usize, StateListenHandle)>,
}

impl LegacyListenerHelper {
    fn new() -> Self {
        Self {
            handles: Vec::new(),
        }
    }

    fn add_listener(
        &mut self,
        ptr: HostTreePointer<c::StatusListener>,
        event_source: &mut EventSource<(FileState, FileState, FileSignals)>,
    ) {
        assert!(!unsafe { ptr.ptr() }.is_null());

        // if it's already listening, don't add a second time
        if self
            .handles
            .iter()
            .any(|x| x.0 == (unsafe { ptr.ptr() } as usize))
        {
            return;
        }

        // this will ref the pointer and unref it when the closure is dropped
        let ptr_wrapper = LegacyListener::new(ptr);

        let handle =
            event_source.add_listener(move |(state, changed, _signals), _cb_queue| unsafe {
                c::statuslistener_onStatusChanged(ptr_wrapper.ptr(), state, changed)
            });

        // use a usize as the key so we don't accidentally deref the pointer
        self.handles.push((unsafe { ptr.ptr() } as usize, handle));
    }

    fn remove_listener(&mut self, ptr: *mut c::StatusListener) {
        assert!(!ptr.is_null());

        // find the position and remove it
        if let Some(x) = self.handles.iter().position(|x| x.0 == (ptr as usize)) {
            // drop the handle
            let _ = self.handles.remove(x);
        }
    }
}

/// A specified event source that passes a state and the changed bits to the function, but only if
/// the monitored bits have changed and if the change the filter is satisfied.
pub struct StateEventSource {
    inner: EventSource<(FileState, FileState, FileSignals)>,
    legacy_helper: LegacyListenerHelper,
}

impl StateEventSource {
    pub fn new() -> Self {
        Self {
            inner: EventSource::new(),
            legacy_helper: LegacyListenerHelper::new(),
        }
    }

    /// Add a listener. The filter applies only to state changes, not signals. Only signals that are
    /// monitored will be passed to the listener.
    pub fn add_listener(
        &mut self,
        monitoring_state: FileState,
        monitoring_signals: FileSignals,
        filter: StateListenerFilter,
        notify_fn: impl Fn(FileState, FileState, FileSignals, &mut CallbackQueue)
            + Send
            + Sync
            + 'static,
    ) -> StateListenHandle {
        self.inner
            .add_listener(move |(state, changed, signals), cb_queue| {
                // true if any of the bits we're monitoring have changed
                let flipped = monitoring_state.intersects(changed);

                // true if any of the bits we're monitoring are set
                let on = monitoring_state.intersects(state);

                let notify = match filter {
                    // at least one monitored bit is on, and at least one has changed
                    StateListenerFilter::OffToOn => flipped && on,
                    // all monitored bits are off, and at least one has changed
                    StateListenerFilter::OnToOff => flipped && !on,
                    // at least one monitored bit has changed
                    StateListenerFilter::Always => flipped,
                    StateListenerFilter::Never => false,
                };

                // filter the signals to only the ones we're monitoring
                let signals = signals.intersection(monitoring_signals);

                // also want to notify if a monitored signal was emitted
                let notify = notify || !signals.is_empty();

                if !notify {
                    return;
                }

                (notify_fn)(state, changed, signals, cb_queue)
            })
    }

    pub fn add_legacy_listener(&mut self, ptr: HostTreePointer<c::StatusListener>) {
        self.legacy_helper.add_listener(ptr, &mut self.inner);
    }

    pub fn remove_legacy_listener(&mut self, ptr: *mut c::StatusListener) {
        self.legacy_helper.remove_listener(ptr);
    }

    pub fn notify_listeners(
        &mut self,
        state: FileState,
        changed: FileState,
        signals: FileSignals,
        cb_queue: &mut CallbackQueue,
    ) {
        self.inner
            .notify_listeners((state, changed, signals), cb_queue)
    }
}

impl Default for StateEventSource {
    fn default() -> Self {
        Self::new()
    }
}

mod export {
    use super::*;

    use shadow_shim_helper_rs::rootedcell::refcell::RootedRefCell;

    use crate::utility::legacy_callback_queue::RootedRefCell_StateEventSource;

    #[no_mangle]
    pub extern "C-unwind" fn eventsource_new() -> *mut RootedRefCell_StateEventSource {
        let event_source = worker::Worker::with_active_host(|host| {
            Box::new(RootedRefCell::new(host.root(), StateEventSource::new()))
        })
        .unwrap();
        Box::into_raw(event_source)
    }

    #[no_mangle]
    pub extern "C-unwind" fn eventsource_free(event_source: *mut RootedRefCell_StateEventSource) {
        assert!(!event_source.is_null());
        drop(unsafe { Box::from_raw(event_source) });
    }

    #[no_mangle]
    pub extern "C-unwind" fn eventsource_addLegacyListener(
        event_source: *const RootedRefCell_StateEventSource,
        listener: *mut c::StatusListener,
    ) {
        let event_source = unsafe { event_source.as_ref() }.unwrap();
        worker::Worker::with_active_host(|host| {
            let mut event_source = event_source.borrow_mut(host.root());

            event_source.add_legacy_listener(HostTreePointer::new(listener));
        })
        .unwrap();
    }

    #[no_mangle]
    pub extern "C-unwind" fn eventsource_removeLegacyListener(
        event_source: *const RootedRefCell_StateEventSource,
        listener: *mut c::StatusListener,
    ) {
        let event_source = unsafe { event_source.as_ref() }.unwrap();
        worker::Worker::with_active_host(|host| {
            let mut event_source = event_source.borrow_mut(host.root());

            event_source.remove_legacy_listener(listener);
        })
        .unwrap();
    }
}
