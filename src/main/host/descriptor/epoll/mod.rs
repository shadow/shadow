use std::collections::{BTreeMap, BTreeSet};
use std::sync::{Arc, Weak};

use atomic_refcell::AtomicRefCell;
use linux_api::epoll::{EpollCtlOp, EpollEvents};
use linux_api::errno::Errno;
use linux_api::ioctls::IoctlRequest;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::host::descriptor::{
    File, FileMode, FileState, FileStatus, StateEventSource, StateListenerFilter, SyscallResult,
};
use crate::host::memory_manager::MemoryManager;
use crate::host::syscall::io::IoVec;
use crate::host::syscall_types::SyscallError;
use crate::utility::callback_queue::{CallbackQueue, Handle};
use crate::utility::{HostTreePointer, ObjectCounter};

use self::entry::Entry;
use self::key::{Key, PriorityKey};

// Private submodules to help us track the status of items we are monitoring.
mod entry;
mod key;

pub struct Epoll {
    event_source: StateEventSource,
    status: FileStatus,
    state: FileState,
    // Should only be used by `OpenFile` to make sure there is only ever one `OpenFile` instance for
    // this file.
    has_open_file: bool,
    // A counter for sorting entries, to guarantee fairness and determinism when reporting events.
    pri_counter: u64,
    // Stores entries for all descriptors we are currently monitoring for events.
    monitoring: BTreeMap<Key, Entry>,
    // Stores keys for entries with events that are ready to be reported.
    ready: BTreeSet<PriorityKey>,
    _counter: ObjectCounter,
}

impl Epoll {
    pub fn new() -> Arc<AtomicRefCell<Self>> {
        let mut epoll = Self {
            event_source: StateEventSource::new(),
            status: FileStatus::empty(),
            state: FileState::ACTIVE,
            has_open_file: false,
            pri_counter: 1,
            monitoring: BTreeMap::new(),
            ready: BTreeSet::new(),
            _counter: ObjectCounter::new("Epoll"),
        };

        CallbackQueue::queue_and_run(|cb_queue| epoll.refresh_readable(cb_queue));

        Arc::new(AtomicRefCell::new(epoll))
    }

    pub fn get_status(&self) -> FileStatus {
        self.status
    }

    pub fn set_status(&mut self, status: FileStatus) {
        self.status = status;
    }

    pub fn mode(&self) -> FileMode {
        FileMode::READ | FileMode::WRITE
    }

    pub fn has_open_file(&self) -> bool {
        self.has_open_file
    }

    pub fn supports_sa_restart(&self) -> bool {
        // Epoll always returns EINTR if interrupted by a signal handler regardless of the use of
        // the SA_RESTART flag. See signal(7).
        false
    }

    pub fn set_has_open_file(&mut self, val: bool) {
        self.has_open_file = val;
    }

    pub fn close(&mut self, cb_queue: &mut CallbackQueue) -> Result<(), SyscallError> {
        self.copy_state(
            /* mask= */ FileState::all(),
            FileState::CLOSED,
            cb_queue,
        );
        Ok(())
    }

    pub fn readv(
        &mut self,
        _iovs: &[IoVec],
        _offset: Option<libc::off_t>,
        _flags: libc::c_int,
        _mem: &mut MemoryManager,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        // EpollFDs don't support reading.
        Err(Errno::EINVAL.into())
    }

    pub fn writev(
        &mut self,
        _iovs: &[IoVec],
        _offset: Option<libc::off_t>,
        _flags: libc::c_int,
        _mem: &mut MemoryManager,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        // EpollFDs don't support writing.
        Err(Errno::EINVAL.into())
    }

    pub fn ioctl(
        &mut self,
        _request: IoctlRequest,
        _arg_ptr: ForeignPtr<()>,
        _mem: &mut MemoryManager,
    ) -> SyscallResult {
        todo!();
    }

    pub fn ctl(
        &mut self,
        op: EpollCtlOp,
        target_fd: i32,
        target_file: File,
        events: EpollEvents,
        data: u64,
        weak_self: Weak<AtomicRefCell<Epoll>>,
        cb_queue: &mut CallbackQueue,
    ) -> Result<(), SyscallError> {
        let key = Key::new(target_fd, target_file);

        let result = match op {
            EpollCtlOp::EPOLL_CTL_ADD => self.ctl_add(key, events, data, weak_self),
            EpollCtlOp::EPOLL_CTL_MOD => self.ctl_mod(key, events, data, weak_self),
            EpollCtlOp::EPOLL_CTL_DEL => self.ctl_del(key),
        };

        self.refresh_readable(cb_queue);

        result
    }

    fn ctl_add(
        &mut self,
        key: Key,
        events: EpollEvents,
        data: u64,
        weak_self: Weak<AtomicRefCell<Epoll>>,
    ) -> Result<(), SyscallError> {
        // Check if we're trying to add a file that's already been closed. Typically a file that is
        // referenced in the descriptor table should never be a closed file, but Shadow's C TCP
        // sockets do close themselves even if there are still file handles (see
        // `_tcp_endOfFileSignalled`), so we need to check this.
        let file_state = key.get_file_ref().borrow().state();
        if file_state.contains(FileState::CLOSED) {
            log::warn!("Attempted to add a closed file {} to epoll", key.get_fd());
            return Err(Errno::EBADF.into());
        }

        // From epoll_ctl(2): "op was EPOLL_CTL_ADD, and the supplied file descriptor fd is already
        // registered with this epoll instance."
        if self.monitoring.contains_key(&key) {
            return Err(Errno::EEXIST.into());
        }

        // Create a new epoll entry.
        let mut entry = Entry::new(events, data, file_state);

        // Set up a listener to notify us when the file status changes.
        Epoll::refresh_entry_listener(weak_self, key.clone(), &mut entry);

        // Track the entry.
        self.monitoring.insert(key, entry);

        Ok(())
    }

    fn ctl_mod(
        &mut self,
        key: Key,
        events: EpollEvents,
        data: u64,
        weak_self: Weak<AtomicRefCell<Epoll>>,
    ) -> Result<(), SyscallError> {
        // Change the settings for this entry.
        let entry = self.monitoring.get_mut(&key).ok_or(Errno::ENOENT)?;
        let file_state = key.get_file_ref().borrow().state();
        entry.reset(events, data, file_state);

        // Set a new listener that is consistent with the new events.
        Epoll::refresh_entry_listener(weak_self, key, entry);

        Ok(())
    }

    fn ctl_del(&mut self, key: Key) -> Result<(), SyscallError> {
        // Stop monitoring this entry. Dropping the entry will cause it to stop listening for
        // status changes on its inner `File` event source object.
        let entry = self.monitoring.remove(&key).ok_or(Errno::ENOENT)?;

        // If it has a priority, then we also remove it from the ready set.
        if let Some(pri) = entry.get_priority() {
            let pri_key = PriorityKey::new(pri, key);
            self.ready.remove(&pri_key);
        }

        Ok(())
    }

    pub fn add_listener(
        &mut self,
        monitoring: FileState,
        filter: StateListenerFilter,
        notify_fn: impl Fn(FileState, FileState, &mut CallbackQueue) + Send + Sync + 'static,
    ) -> Handle<(FileState, FileState)> {
        self.event_source
            .add_listener(monitoring, filter, notify_fn)
    }

    pub fn add_legacy_listener(&mut self, ptr: HostTreePointer<crate::cshadow::StatusListener>) {
        self.event_source.add_legacy_listener(ptr);
    }

    pub fn remove_legacy_listener(&mut self, ptr: *mut crate::cshadow::StatusListener) {
        self.event_source.remove_legacy_listener(ptr);
    }

    pub fn state(&self) -> FileState {
        self.state
    }

    fn refresh_readable(&mut self, cb_queue: &mut CallbackQueue) {
        let readable = self
            .has_ready_events()
            .then_some(FileState::READABLE)
            .unwrap_or_default();
        self.copy_state(/* mask= */ FileState::READABLE, readable, cb_queue);
    }

    fn copy_state(&mut self, mask: FileState, state: FileState, cb_queue: &mut CallbackQueue) {
        let old_state = self.state;

        // Remove the masked flags, then copy the masked flags.
        self.state.remove(mask);
        self.state.insert(state & mask);

        self.handle_state_change(old_state, cb_queue);
    }

    fn handle_state_change(&mut self, old_state: FileState, cb_queue: &mut CallbackQueue) {
        let states_changed = self.state ^ old_state;

        // If something changed, notify our listeners.
        if !states_changed.is_empty() {
            self.event_source
                .notify_listeners(self.state, states_changed, cb_queue);
        }
    }

    fn refresh_entry_listener(weak_self: Weak<AtomicRefCell<Epoll>>, key: Key, entry: &mut Entry) {
        // Check what state we need to listen for this entry.
        let (listen, filter) = entry.get_listener_state();

        let handle_opt = if listen.is_empty() {
            // We don't need a listener.
            None
        } else {
            // Set up a callback so we get informed when the file changes.
            let file = key.get_file_ref().clone();
            let handle =
                file.borrow_mut()
                    .add_listener(listen, filter, move |state, changed, cb_queue| {
                        if let Some(epoll) = weak_self.upgrade() {
                            epoll
                                .borrow_mut()
                                .refresh_entry(&key, state, changed, cb_queue);
                        }
                    });
            Some(handle)
        };

        // Sets a new listener while dropping any old one.
        entry.set_listener_handle(handle_opt);
    }

    /// The file listener callback for when a monitored entry file status changes.
    fn refresh_entry(
        &mut self,
        key: &Key,
        state: FileState,
        changed: FileState,
        cb_queue: &mut CallbackQueue,
    ) {
        let Some(entry) = self.monitoring.get_mut(key) else {
            // We stopped monitoring and can ignore the state change.
            return;
        };

        // Pass the new file state to the entry, which may change its ready status.
        entry.notify(state, changed);

        // Make sure it's in the ready set if it should be, or not if it shouldn't be.
        if entry.has_ready_events() {
            if entry.get_priority().is_none() {
                // It's ready but not in the ready set yet.
                let pri = self.pri_counter;
                self.pri_counter += 1;
                self.ready.insert(PriorityKey::new(pri, key.clone()));
                entry.set_priority(Some(pri));
            }
        } else {
            if let Some(pri) = entry.get_priority() {
                // It's not ready anymore but it's in the ready set.
                self.ready.remove(&PriorityKey::new(pri, key.clone()));
                entry.set_priority(None);
            }
        }

        // The entry update may change the epoll readability.
        self.refresh_readable(cb_queue);
    }

    pub fn has_ready_events(&self) -> bool {
        !self.ready.is_empty()
    }

    pub fn collect_ready_events(&mut self, max_events: u32) -> Vec<(EpollEvents, u64)> {
        let mut events = vec![];
        let mut keep = vec![];

        while self.ready.len() > 0 && events.len() < max_events as usize {
            // Get the next ready entry.
            let pri_key = self.ready.pop_first().unwrap();
            let key = Key::from(pri_key);
            let entry = self.monitoring.get_mut(&key).unwrap();

            // Just removed from the ready set, keep the priority consistent.
            entry.set_priority(None);

            // It was ready so it should have events.
            debug_assert!(entry.has_ready_events());

            // Store the events we should report to the managed process.
            events.push(entry.collect_ready_events().unwrap());

            // It might still be ready even after we report.
            if entry.has_ready_events() {
                // It's ready again. Assign a new priority to ensure fairness with other entries.
                let pri = self.pri_counter;
                self.pri_counter += 1;
                let pri_key = PriorityKey::new(pri, key);

                // Use temp vec so we don't report the same entry twice in the same round.
                keep.push(pri_key);

                // The entry will be in the ready set, keep its priority consistent.
                entry.set_priority(Some(pri));
            }
        }

        // Add everything that is still ready back to the ready set.
        while !keep.is_empty() {
            self.ready.insert(keep.pop().unwrap());
        }

        // The events to be returned to the managed process.
        events
    }
}
