use atomic_refcell::AtomicRefCell;
use nix::errno::Errno;
use std::sync::Arc;

use crate::cshadow as c;
use crate::host::descriptor::{
    FileMode, FileState, FileStatus, StateEventSource, StateListenerFilter,
};
use crate::host::syscall_types::SyscallResult;
use crate::utility::byte_queue::ByteQueue;
use crate::utility::event_queue::{EventQueue, Handle};
use crate::utility::stream_len::StreamLen;

pub struct PipeFile {
    buffer: Arc<AtomicRefCell<SharedBuf>>,
    event_source: StateEventSource,
    state: FileState,
    mode: FileMode,
    status: FileStatus,
    // we only store this so that the handle is dropped when we are
    _buffer_event_handle: Option<Handle<(FileState, FileState)>>,
}

impl PipeFile {
    pub fn new(buffer: Arc<AtomicRefCell<SharedBuf>>, mode: FileMode, status: FileStatus) -> Self {
        let mut rv = Self {
            buffer,
            event_source: StateEventSource::new(),
            state: FileState::ACTIVE,
            mode,
            status,
            _buffer_event_handle: None,
        };

        rv.state
            .insert(rv.filter_state(rv.buffer.borrow_mut().state()));

        rv
    }

    pub fn get_status(&self) -> FileStatus {
        self.status
    }

    pub fn set_status(&mut self, status: FileStatus) {
        self.status = status;
    }

    pub fn mode(&self) -> FileMode {
        self.mode
    }

    pub fn max_size(&self) -> usize {
        self.buffer.borrow().max_len()
    }

    pub fn close(&mut self, event_queue: &mut EventQueue) -> SyscallResult {
        // set the closed flag and remove the active flag
        self.copy_state(
            FileState::CLOSED | FileState::ACTIVE,
            FileState::CLOSED,
            event_queue,
        );
        Ok(0.into())
    }

    pub fn read<W>(
        &mut self,
        bytes: W,
        offset: libc::off_t,
        event_queue: &mut EventQueue,
    ) -> SyscallResult
    where
        W: std::io::Write + std::io::Seek,
    {
        // pipes don't support seeking
        if offset != 0 {
            return Err(nix::errno::Errno::ESPIPE.into());
        }

        // if the file is not open for reading, return EBADF
        if !self.mode.contains(FileMode::READ) {
            return Err(nix::errno::Errno::EBADF.into());
        }

        let mut bytes = bytes;
        let num_read = self.buffer.borrow_mut().read(&mut bytes, event_queue)?;

        // the read would block if we could not write any bytes, but were asked to
        if usize::from(num_read) == 0 && bytes.stream_len_bp()? != 0 {
            Err(Errno::EWOULDBLOCK.into())
        } else {
            Ok(num_read.into())
        }
    }

    pub fn write<R>(
        &mut self,
        bytes: R,
        offset: libc::off_t,
        event_queue: &mut EventQueue,
    ) -> SyscallResult
    where
        R: std::io::Read + std::io::Seek,
    {
        // pipes don't support seeking
        if offset != 0 {
            return Err(nix::errno::Errno::ESPIPE.into());
        }

        // if the file is not open for writing, return EBADF
        if !self.mode.contains(FileMode::WRITE) {
            return Err(nix::errno::Errno::EBADF.into());
        }

        let mut bytes = bytes;
        let num_written = self
            .buffer
            .borrow_mut()
            .write(bytes.by_ref(), event_queue)?;

        // the write would block if we could not write any bytes, but were asked to
        if usize::from(num_written) == 0 && bytes.stream_len_bp()? != 0 {
            Err(Errno::EWOULDBLOCK.into())
        } else {
            Ok(num_written.into())
        }
    }

    pub fn enable_notifications(arc: &Arc<AtomicRefCell<Self>>) {
        let weak = Arc::downgrade(arc);
        let pipe = &mut *arc.borrow_mut();

        // remove any state flags that aren't relevant to us
        let monitoring = pipe.filter_state(FileState::READABLE | FileState::WRITABLE);

        let handle = pipe.buffer.borrow_mut().add_listener(
            monitoring,
            StateListenerFilter::Always,
            move |state, _changed, event_queue| {
                // if the file hasn't been dropped
                if let Some(pipe) = weak.upgrade() {
                    pipe.borrow_mut().copy_state(monitoring, state, event_queue)
                }
            },
        );

        pipe._buffer_event_handle = Some(handle);
    }

    pub fn add_listener(
        &mut self,
        monitoring: FileState,
        filter: StateListenerFilter,
        notify_fn: impl Fn(FileState, FileState, &mut EventQueue) + Send + Sync + 'static,
    ) -> Handle<(FileState, FileState)> {
        self.event_source
            .add_listener(monitoring, filter, notify_fn)
    }

    pub fn add_legacy_listener(&mut self, ptr: *mut c::StatusListener) {
        self.event_source.add_legacy_listener(ptr);
    }

    pub fn remove_legacy_listener(&mut self, ptr: *mut c::StatusListener) {
        self.event_source.remove_legacy_listener(ptr);
    }

    pub fn state(&self) -> FileState {
        self.state
    }

    fn filter_state(&self, mut state: FileState) -> FileState {
        // if not open for reading, remove the readable flag
        if !self.mode.contains(FileMode::READ) {
            state.remove(FileState::READABLE);
        }

        // if not open for writing, remove the writable flag
        if !self.mode.contains(FileMode::WRITE) {
            state.remove(FileState::WRITABLE);
        }

        state
    }

    fn copy_state(&mut self, mask: FileState, state: FileState, event_queue: &mut EventQueue) {
        let old_state = self.state;

        // remove any flags that aren't relevant
        let state = self.filter_state(state);

        // remove the masked flags, then copy the masked flags
        self.state.remove(mask);
        self.state.insert(state & mask);

        self.handle_state_change(old_state, event_queue);
    }

    fn handle_state_change(&mut self, old_state: FileState, event_queue: &mut EventQueue) {
        let states_changed = self.state ^ old_state;

        // if nothing changed
        if states_changed.is_empty() {
            return;
        }

        self.event_source
            .notify_listeners(self.state, states_changed, event_queue);
    }
}

pub struct SharedBuf {
    queue: ByteQueue,
    max_len: usize,
    state: FileState,
    event_source: StateEventSource,
}

impl SharedBuf {
    pub fn new() -> Self {
        Self {
            queue: ByteQueue::new(8192),
            max_len: c::CONFIG_PIPE_BUFFER_SIZE as usize,
            state: FileState::WRITABLE,
            event_source: StateEventSource::new(),
        }
    }

    pub fn is_empty(&self) -> bool {
        self.queue.len() == 0
    }

    pub fn max_len(&self) -> usize {
        self.max_len
    }

    pub fn space_available(&self) -> usize {
        self.max_len - self.queue.len()
    }

    pub fn read<W: std::io::Write>(
        &mut self,
        bytes: W,
        event_queue: &mut EventQueue,
    ) -> SyscallResult {
        let num = self.queue.pop(bytes)?;

        // readable if not empty
        self.adjust_state(FileState::READABLE, !self.is_empty(), event_queue);

        // writable if space is available
        self.adjust_state(FileState::WRITABLE, self.space_available() > 0, event_queue);

        Ok(num.into())
    }

    pub fn write<R: std::io::Read>(
        &mut self,
        bytes: R,
        event_queue: &mut EventQueue,
    ) -> SyscallResult {
        let written = self.queue.push(bytes.take(self.space_available() as u64))?;

        self.adjust_state(FileState::READABLE, !self.is_empty(), event_queue);
        self.adjust_state(FileState::WRITABLE, self.space_available() > 0, event_queue);

        Ok(written.into())
    }

    pub fn add_listener(
        &mut self,
        monitoring: FileState,
        filter: StateListenerFilter,
        notify_fn: impl Fn(FileState, FileState, &mut EventQueue) + Send + Sync + 'static,
    ) -> Handle<(FileState, FileState)> {
        self.event_source
            .add_listener(monitoring, filter, notify_fn)
    }

    pub fn state(&self) -> FileState {
        self.state
    }

    fn adjust_state(&mut self, state: FileState, do_set_bits: bool, event_queue: &mut EventQueue) {
        let old_state = self.state;

        // add or remove the flags
        self.state.set(state, do_set_bits);

        self.handle_state_change(old_state, event_queue);
    }

    fn handle_state_change(&mut self, old_state: FileState, event_queue: &mut EventQueue) {
        let states_changed = self.state ^ old_state;

        // if nothing changed
        if states_changed.is_empty() {
            return;
        }

        self.event_source
            .notify_listeners(self.state, states_changed, event_queue);
    }
}
