use std::collections::hash_map::Entry as HashMapEntry;
use std::collections::{BinaryHeap, HashMap};
use std::sync::{Arc, Weak};

use atomic_refcell::AtomicRefCell;
use linux_api::epoll::{EpollCtlOp, EpollEvents};
use linux_api::errno::Errno;
use linux_api::ioctls::IoctlRequest;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::host::descriptor::listener::{StateEventSource, StateListenHandle, StateListenerFilter};
use crate::host::descriptor::{File, FileMode, FileSignals, FileState, FileStatus, SyscallResult};
use crate::host::memory_manager::MemoryManager;
use crate::host::syscall::io::IoVec;
use crate::host::syscall::types::SyscallError;
use crate::utility::callback_queue::CallbackQueue;
use crate::utility::{HostTreePointer, ObjectCounter};

use self::entry::Entry;
use self::key::{Key, PriorityKey};

use super::socket::inet::InetSocket;
use super::socket::Socket;

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
    // Because our ready set is a max heap, we initialize this counter to u64::MAX and count down as
    // we assign values so that entries whose events were last reported longest ago are prioritized.
    pri_counter: u64,
    // Stores entries for all descriptors we are currently monitoring for events.
    monitoring: HashMap<Key, Entry>,
    // Stores keys for entries with events that are ready to be reported.
    ready: BinaryHeap<PriorityKey>,
    _counter: ObjectCounter,
}

impl Epoll {
    pub fn new() -> Arc<AtomicRefCell<Self>> {
        let mut epoll = Self {
            event_source: StateEventSource::new(),
            status: FileStatus::empty(),
            state: FileState::ACTIVE,
            has_open_file: false,
            pri_counter: u64::MAX,
            monitoring: HashMap::new(),
            ready: BinaryHeap::new(),
            _counter: ObjectCounter::new("Epoll"),
        };

        CallbackQueue::queue_and_run(|cb_queue| epoll.refresh_state(cb_queue));

        Arc::new(AtomicRefCell::new(epoll))
    }

    pub fn status(&self) -> FileStatus {
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
        self.update_state(
            /* mask= */ FileState::all(),
            FileState::CLOSED,
            FileSignals::empty(),
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
        // After checking the epoll man pages and the Linux source for eventpoll.c, we don't think
        // epoll descriptors support any ioctl operations.
        warn_once_then_trace!("Epoll does not support any ioctl requests.");
        // From ioctl(2): ENOTTY The specified request does not apply to the kind of object that the
        // file descriptor fd references. Verified that epoll descriptors return this on Linux.
        Err(Errno::ENOTTY.into())
    }

    pub fn stat(&self) -> Result<linux_api::stat::stat, SyscallError> {
        warn_once_then_debug!("We do not yet handle stat calls on epoll fds");
        Err(Errno::EINVAL.into())
    }

    /// Executes an epoll control operation on the target file.
    ///
    /// We think this panics if `target_file` is an instance of this epoll object due to recursive
    /// mutable borrows (but it does not panic due to a check+panic).
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
        let state = target_file.borrow().state();
        let key = Key::new(target_fd, target_file);

        log::trace!("Epoll editing fd {target_fd} while in state {state:?}");

        match op {
            EpollCtlOp::EPOLL_CTL_ADD => {
                // Check if we're trying to add a file that's already been closed. Typically a file
                // that is referenced in the descriptor table should never be a closed file, but
                // Shadow's C TCP sockets do close themselves even if there are still file handles
                // (see `_tcp_endOfFileSignalled`), so we need to check this.
                //
                // TODO change this to an assertion when legacy tcp is removed.
                if state.contains(FileState::CLOSED) {
                    log::warn!("Attempted to add a closed file {target_fd} to epoll");
                    return Err(Errno::EBADF.into());
                }

                let mut entry = Entry::new(events, data, state);

                // TODO remove when legacy tcp is removed.
                if matches!(
                    key.file(),
                    File::Socket(Socket::Inet(InetSocket::LegacyTcp(_)))
                ) {
                    entry.set_legacy();
                }

                // From epoll_ctl(2): Returns EEXIST when "op was EPOLL_CTL_ADD, and the supplied
                // file descriptor fd is already registered with this epoll instance."
                match self.monitoring.entry(key.clone()) {
                    HashMapEntry::Occupied(_) => return Err(Errno::EEXIST.into()),
                    HashMapEntry::Vacant(x) => x.insert(entry),
                };
            }
            EpollCtlOp::EPOLL_CTL_MOD => {
                let entry = self.monitoring.get_mut(&key).ok_or(Errno::ENOENT)?;
                entry.modify(events, data, state);
            }
            EpollCtlOp::EPOLL_CTL_DEL => {
                // Stop monitoring this entry. Dropping the entry will cause it to stop listening
                // for status changes on its inner `File` event source object.
                let entry = self.monitoring.remove(&key).ok_or(Errno::ENOENT)?;

                // If it has a priority, then we also remove it from the ready set.
                if let Some(pri) = entry.priority() {
                    self.ready.retain(|e| e.priority() != pri)
                }
            }
        };

        self.refresh_ready(key.clone());
        self.refresh_listener(weak_self, key);
        self.refresh_state(cb_queue);

        Ok(())
    }

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
        self.event_source
            .add_listener(monitoring_state, monitoring_signals, filter, notify_fn)
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

    fn refresh_state(&mut self, cb_queue: &mut CallbackQueue) {
        let readable = self
            .has_ready_events()
            .then_some(FileState::READABLE)
            .unwrap_or_default();
        self.update_state(
            /* mask= */ FileState::READABLE,
            readable,
            FileSignals::empty(),
            cb_queue,
        );
    }

    fn update_state(
        &mut self,
        mask: FileState,
        state: FileState,
        signals: FileSignals,
        cb_queue: &mut CallbackQueue,
    ) {
        let old_state = self.state;

        // Remove the masked flags, then copy the masked flags.
        self.state.remove(mask);
        self.state.insert(state & mask);

        self.handle_state_change(old_state, signals, cb_queue);
    }

    fn handle_state_change(
        &mut self,
        old_state: FileState,
        signals: FileSignals,
        cb_queue: &mut CallbackQueue,
    ) {
        let states_changed = self.state ^ old_state;

        // If something changed, notify our listeners.
        if !states_changed.is_empty() || !signals.is_empty() {
            self.event_source
                .notify_listeners(self.state, states_changed, signals, cb_queue);
        }
    }

    fn refresh_listener(&mut self, weak_self: Weak<AtomicRefCell<Epoll>>, key: Key) {
        let Some(entry) = self.monitoring.get_mut(&key) else {
            return;
        };

        // Check what state and what signals we need to listen for this entry.
        // We always listen for closed so we know when to stop monitoring the entry.
        let listen_state = entry.get_listener_state().union(FileState::CLOSED);
        let listen_signals = entry.get_listener_signals();
        let filter = StateListenerFilter::Always;

        // Set up a callback so we get informed when the file changes.
        let file = key.file().clone();
        let handle = file.borrow_mut().add_listener(
            listen_state,
            listen_signals,
            filter,
            move |state, changed, signals, cb_queue| {
                if let Some(epoll) = weak_self.upgrade() {
                    epoll
                        .borrow_mut()
                        .notify_entry(&key, state, changed, signals, cb_queue);
                }
            },
        );
        entry.set_listener_handle(Some(handle));
    }

    /// The file listener callback for when a monitored entry file status changes.
    fn notify_entry(
        &mut self,
        key: &Key,
        state: FileState,
        changed: FileState,
        signals: FileSignals,
        cb_queue: &mut CallbackQueue,
    ) {
        // Notify entry of file state change if we're still monitoring it.
        match self.monitoring.get_mut(&key.clone()) {
            Some(entry) => entry.notify(state, changed, signals),
            None => return,
        };

        // Update our ready set, which removes the key if the file closed.
        self.refresh_ready(key.clone());

        // Also stop monitoring if the file was closed.
        if state.contains(FileState::CLOSED) {
            self.monitoring.remove(key);
        }

        // Update the readability of the epoll descriptor.
        self.refresh_state(cb_queue);
    }

    /// Ensures that the entry is in the ready set if it should be, or not if it shouldn't be.
    fn refresh_ready(&mut self, key: Key) {
        let Some(entry) = self.monitoring.get_mut(&key.clone()) else {
            return;
        };

        // The entry will not be ready if the file closed.
        if entry.has_ready_events() {
            if entry.priority().is_none() {
                // It's ready but not in the ready set yet.
                let pri = self.pri_counter;
                self.pri_counter -= 1;
                self.ready.push(PriorityKey::new(pri, key));
                entry.set_priority(Some(pri));
            }
        } else if let Some(pri) = entry.priority() {
            // It's not ready anymore but it's in the ready set, so remove it.
            self.ready.retain(|e| e.priority() != pri);
            entry.set_priority(None);
        }
    }

    pub fn has_ready_events(&self) -> bool {
        !self.ready.is_empty()
    }

    pub fn collect_ready_events(
        &mut self,
        cb_queue: &mut CallbackQueue,
        max_events: u32,
    ) -> Vec<(EpollEvents, u64)> {
        let mut events = vec![];
        let mut keep = vec![];

        while !self.ready.is_empty() && events.len() < max_events as usize {
            // Get the next ready entry.
            let pri_key = self.ready.pop().unwrap();
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
                self.pri_counter -= 1;
                let pri_key = PriorityKey::new(pri, key);

                // Use temp vec so we don't report the same entry twice in the same round.
                keep.push(pri_key);

                // The entry will be in the ready set, keep its priority consistent.
                entry.set_priority(Some(pri));
            }
        }

        // Add everything that is still ready back to the ready set.
        self.ready.extend(keep);

        // We've mutated the ready list; we may need to trigger callbacks.
        self.refresh_state(cb_queue);

        // The events to be returned to the managed process.
        events
    }
}
