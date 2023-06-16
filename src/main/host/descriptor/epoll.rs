use std::sync::Arc;

use atomic_refcell::AtomicRefCell;
use linux_api::ioctls::IoctlRequest;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::cshadow as c;
use crate::host::descriptor::{
    File, FileMode, FileState, FileStatus, StateEventSource, StateListenerFilter, SyscallResult,
};
use crate::host::memory_manager::MemoryManager;
use crate::host::syscall::io::IoVec;
use crate::host::syscall_types::SyscallError;
use crate::utility::callback_queue::{CallbackQueue, Handle};
use crate::utility::{HostTreePointer, ObjectCounter};

pub struct Epoll {
    event_source: StateEventSource,
    status: FileStatus,
    state: FileState,
    // should only be used by `OpenFile` to make sure there is only ever one `OpenFile` instance for
    // this file
    has_open_file: bool,
    _counter: ObjectCounter,
}

impl Epoll {
    pub fn new() -> Arc<AtomicRefCell<Self>> {
        let mut socket = Self {
            event_source: StateEventSource::new(),
            status: FileStatus::empty(),
            state: FileState::ACTIVE,
            has_open_file: false,
            _counter: ObjectCounter::new("Epoll"),
        };

        CallbackQueue::queue_and_run(|cb_queue| socket.refresh_readable_writable(cb_queue));

        Arc::new(AtomicRefCell::new(socket))
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
        todo!();
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
        todo!();
    }

    pub fn writev(
        &mut self,
        _iovs: &[IoVec],
        _offset: Option<libc::off_t>,
        _flags: libc::c_int,
        _mem: &mut MemoryManager,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        todo!();
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
        _op: std::ffi::c_int,
        _fd: std::ffi::c_int,
        _target: File,
        _event: libc::epoll_event,
        _cb_queue: &mut CallbackQueue,
    ) -> Result<std::ffi::c_int, SyscallError> {
        todo!();
    }

    pub fn num_events_ready(&self) -> usize {
        todo!();
    }

    pub fn get_events(
        &self,
        _events_ptr: ForeignPtr<libc::epoll_event>,
        _max_events: u32,
        _mem: &mut MemoryManager,
    ) -> Result<std::ffi::c_int, SyscallError> {
        todo!();
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

    pub fn add_legacy_listener(&mut self, ptr: HostTreePointer<c::StatusListener>) {
        self.event_source.add_legacy_listener(ptr);
    }

    pub fn remove_legacy_listener(&mut self, ptr: *mut c::StatusListener) {
        self.event_source.remove_legacy_listener(ptr);
    }

    pub fn state(&self) -> FileState {
        self.state
    }

    fn refresh_readable_writable(&mut self, _cb_queue: &mut CallbackQueue) {
        todo!();
    }

    fn copy_state(&mut self, mask: FileState, state: FileState, cb_queue: &mut CallbackQueue) {
        let old_state = self.state;

        // remove the masked flags, then copy the masked flags
        self.state.remove(mask);
        self.state.insert(state & mask);

        self.handle_state_change(old_state, cb_queue);
    }

    fn handle_state_change(&mut self, old_state: FileState, cb_queue: &mut CallbackQueue) {
        let states_changed = self.state ^ old_state;

        // if nothing changed
        if states_changed.is_empty() {
            return;
        }

        self.event_source
            .notify_listeners(self.state, states_changed, cb_queue);
    }
}
