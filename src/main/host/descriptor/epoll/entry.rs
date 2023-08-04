use linux_api::epoll::EpollEvents;

use crate::host::descriptor::{FileState, StateListenerFilter};
use crate::utility::callback_queue::Handle;

/// Used to track the status of a file we are monitoring for events. Any complicated logic for
/// deciding when a file has events that epoll should report should be specified in this object's
/// implementation.
pub(super) struct Entry {
    // Priority value among other ready entries.
    priority: Option<u64>,
    // The events of interest registered by the managed process.
    interest: EpollEvents,
    // The data registared by the managed process, to be returned upon event notification.
    data: u64,
    // The handle to the currently registered file status listener.
    listener_handle: Option<Handle<(FileState, FileState)>>,
}

impl Entry {
    pub(super) fn new(interest: EpollEvents, data: u64) -> Self {
        Self {
            priority: None,
            interest,
            data,
            listener_handle: None,
        }
    }

    pub(super) fn set_priority(&mut self, priority: Option<u64>) {
        self.priority = priority;
    }

    pub(super) fn get_priority(&self) -> Option<u64> {
        self.priority
    }

    pub(super) fn set_interested_events(&mut self, interest: EpollEvents, data: u64) {
        self.interest = interest;
        self.data = data;
    }

    pub(super) fn get_listener_state(&self) -> (FileState, StateListenerFilter) {
        // Return the file state changes that we want to be notified about.
        let listening = FileState::all();
        let filter = StateListenerFilter::Always;
        (listening, filter)
    }

    pub(super) fn set_listener_handle(&mut self, handle: Option<Handle<(FileState, FileState)>>) {
        self.listener_handle = handle;
    }

    pub(super) fn refresh_state(
        &mut self,
        _listening: FileState,
        _filter: StateListenerFilter,
        _state: FileState,
        _changed: FileState,
    ) {
        // This is or notification that the file state has changed.
        todo!()
    }

    pub(super) fn has_ready_events(&self) -> bool {
        todo!()
    }

    pub(super) fn collect_ready_events(&mut self) -> (EpollEvents, u64) {
        todo!()
    }
}
