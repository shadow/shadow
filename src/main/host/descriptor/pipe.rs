use atomic_refcell::AtomicRefCell;
use nix::errno::Errno;
use std::convert::{TryFrom, TryInto};
use std::sync::Arc;

use crate::cshadow as c;
use crate::host::descriptor::{
    File, FileMode, FileState, FileStatus, ReadSeek, StateEventSource, StateListenerFilter,
};
use crate::host::syscall_types::SyscallResult;
use crate::utility::byte_queue::ByteQueue;
use crate::utility::event_queue::{EventQueue, Handle};
use crate::utility::stream_len::StreamLen;

pub struct PipeFile {
    buffer: Option<Arc<AtomicRefCell<SharedBuf>>>,
    event_source: StateEventSource,
    state: FileState,
    mode: FileMode,
    status: FileStatus,
    write_mode: WriteMode,
    // we only store this so that the handle is dropped when we are
    buffer_event_handle: Option<Handle<(FileState, FileState)>>,
}

impl PipeFile {
    /// Create a new [`PipeFile`]. The new pipe must be initialized using
    /// [`PipeFile::connect_to_buffer`] before any of its methods are called.
    pub fn new(mode: FileMode, status: FileStatus) -> Self {
        Self {
            buffer: None,
            event_source: StateEventSource::new(),
            state: FileState::ACTIVE,
            mode,
            status,
            write_mode: WriteMode::Stream,
            buffer_event_handle: None,
        }
    }

    pub fn max_size(&self) -> usize {
        self.buffer.as_ref().unwrap().borrow().max_len()
    }

    pub fn connect_to_buffer(
        arc: &Arc<AtomicRefCell<Self>>,
        buffer: Arc<AtomicRefCell<SharedBuf>>,
        event_queue: &mut EventQueue,
    ) {
        let weak = Arc::downgrade(arc);
        let pipe = &mut *arc.borrow_mut();

        pipe.buffer = Some(buffer);

        if pipe.mode.contains(FileMode::WRITE) {
            pipe.buffer
                .as_ref()
                .unwrap()
                .borrow_mut()
                .add_writer(event_queue);
        }

        // remove any state flags that aren't relevant to us
        let monitoring = pipe.filter_state(FileState::READABLE | FileState::WRITABLE);

        let handle = pipe.buffer.as_ref().unwrap().borrow_mut().add_listener(
            monitoring,
            StateListenerFilter::Always,
            move |state, _changed, event_queue| {
                // if the file hasn't been dropped
                if let Some(pipe) = weak.upgrade() {
                    let mut pipe = pipe.borrow_mut();

                    // if the pipe is already closed, do nothing
                    if pipe.state.contains(FileState::CLOSED) {
                        return;
                    }

                    pipe.copy_state(monitoring, state, event_queue);
                }
            },
        );

        pipe.buffer_event_handle = Some(handle);

        // update the pipe file's initial state based on the buffer's state
        let buffer_state = pipe.buffer.as_ref().unwrap().borrow_mut().state();
        pipe.state.insert(pipe.filter_state(buffer_state));
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

impl File for PipeFile {
    fn get_status(&self) -> FileStatus {
        self.status
    }

    fn set_status(&mut self, status: FileStatus) {
        self.status = status;
    }

    fn mode(&self) -> FileMode {
        self.mode
    }

    fn close(&mut self, event_queue: &mut EventQueue) -> SyscallResult {
        // drop the event listener handle so that we stop receiving new events
        self.buffer_event_handle.take().unwrap().stop_listening();

        // if open for writing, inform the buffer that there is one fewer writers
        if self.mode.contains(FileMode::WRITE) {
            self.buffer
                .as_ref()
                .unwrap()
                .borrow_mut()
                .remove_writer(event_queue);
        }

        // no need to hold on to the buffer anymore
        self.buffer = None;

        // set the closed flag and remove the active, readable, and writable flags
        self.copy_state(
            FileState::CLOSED | FileState::ACTIVE | FileState::READABLE | FileState::WRITABLE,
            FileState::CLOSED,
            event_queue,
        );

        Ok(0.into())
    }

    fn read(
        &mut self,
        bytes: &mut dyn super::WriteSeek,
        offset: libc::off_t,
        event_queue: &mut EventQueue,
    ) -> SyscallResult {
        // pipes don't support seeking
        if offset != 0 {
            return Err(nix::errno::Errno::ESPIPE.into());
        }

        // if the file is not open for reading, return EBADF
        if !self.mode.contains(FileMode::READ) {
            return Err(nix::errno::Errno::EBADF.into());
        }

        let num_read = self
            .buffer
            .as_ref()
            .unwrap()
            .borrow_mut()
            .read(bytes.as_write(), event_queue)?;

        // the read would block if all:
        //  1. we could not read any bytes
        //  2. we were asked to read >0 bytes
        //  3. there are open descriptors that refer to the write end of the pipe
        if usize::from(num_read) == 0
            && bytes.as_seek().stream_len_bp()? != 0
            && self.buffer.as_ref().unwrap().borrow().num_writers > 0
        {
            Err(Errno::EWOULDBLOCK.into())
        } else {
            Ok(num_read.into())
        }
    }

    fn write(
        &mut self,
        bytes: &mut dyn super::ReadSeek,
        offset: libc::off_t,
        event_queue: &mut EventQueue,
    ) -> SyscallResult {
        // pipes don't support seeking
        if offset != 0 {
            return Err(nix::errno::Errno::ESPIPE.into());
        }

        // if the file is not open for writing, return EBADF
        if !self.mode.contains(FileMode::WRITE) {
            return Err(nix::errno::Errno::EBADF.into());
        }

        let mut buffer = self.buffer.as_ref().unwrap().borrow_mut();

        if self.write_mode == WriteMode::Packet && !self.status.contains(FileStatus::DIRECT) {
            // switch to stream mode immediately, regardless of whether the buffer is empty or not
            self.write_mode = WriteMode::Stream;
        } else if self.write_mode == WriteMode::Stream && self.status.contains(FileStatus::DIRECT) {
            // in linux, it seems that pipes only switch to packet mode when a new page is added to
            // the buffer, so we simulate that behaviour for when the first page is added (when the
            // buffer is empty)
            if buffer.is_empty() {
                self.write_mode = WriteMode::Packet;
            }
        }

        let num_written = match self.write_mode {
            WriteMode::Packet => buffer.write_packet(bytes, event_queue)?,
            WriteMode::Stream => buffer.write_stream(bytes.as_read(), event_queue)?,
        };

        // the write would block if we could not write any bytes, but were asked to
        if usize::from(num_written) == 0 && bytes.stream_len_bp()? != 0 {
            Err(Errno::EWOULDBLOCK.into())
        } else {
            Ok(num_written.into())
        }
    }

    fn add_listener(
        &mut self,
        monitoring: FileState,
        filter: StateListenerFilter,
        notify_fn: Box<dyn Fn(FileState, FileState, &mut EventQueue) + Send + Sync + 'static>,
    ) -> Handle<(FileState, FileState)> {
        self.event_source
            .add_listener(monitoring, filter, notify_fn)
    }

    fn add_legacy_listener(&mut self, ptr: *mut c::StatusListener) {
        self.event_source.add_legacy_listener(ptr);
    }

    fn remove_legacy_listener(&mut self, ptr: *mut c::StatusListener) {
        self.event_source.remove_legacy_listener(ptr);
    }

    fn state(&self) -> FileState {
        self.state
    }

    fn as_any(&self) -> &dyn std::any::Any {
        self
    }

    fn as_any_mut(&mut self) -> &mut dyn std::any::Any {
        self
    }
}

impl std::fmt::Debug for PipeFile {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "Pipe (state: {:?}, status: {:?})",
            self.state, self.status
        )
    }
}

#[derive(Debug, PartialEq, Eq)]
enum WriteMode {
    Stream,
    Packet,
}

pub struct SharedBuf {
    queue: ByteQueue,
    max_len: usize,
    state: FileState,
    num_writers: u16,
    event_source: StateEventSource,
}

impl SharedBuf {
    pub fn new() -> Self {
        Self {
            queue: ByteQueue::new(8192),
            max_len: c::CONFIG_PIPE_BUFFER_SIZE as usize,
            state: FileState::WRITABLE,
            num_writers: 0,
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
        self.max_len - usize::try_from(self.queue.len()).unwrap()
    }

    pub fn add_writer(&mut self, event_queue: &mut EventQueue) {
        self.num_writers += 1;
        self.refresh_state(event_queue);
    }

    pub fn remove_writer(&mut self, event_queue: &mut EventQueue) {
        self.num_writers -= 1;
        self.refresh_state(event_queue);
    }

    pub fn read(
        &mut self,
        bytes: &mut dyn std::io::Write,
        event_queue: &mut EventQueue,
    ) -> SyscallResult {
        let (num, _chunk_type) = self.queue.pop(bytes)?;
        self.refresh_state(event_queue);

        Ok(num.into())
    }

    pub fn write_stream(
        &mut self,
        bytes: &mut dyn std::io::Read,
        event_queue: &mut EventQueue,
    ) -> SyscallResult {
        let written = self
            .queue
            .push_stream(bytes, self.space_available().try_into().unwrap())?;
        self.refresh_state(event_queue);

        Ok(written.into())
    }

    pub fn write_packet(
        &mut self,
        bytes: &mut dyn ReadSeek,
        event_queue: &mut EventQueue,
    ) -> SyscallResult {
        let size = bytes.stream_len_bp().unwrap();

        if size > self.space_available().try_into().unwrap() {
            return Err(Errno::EAGAIN.into());
        }

        let mut bytes_remaining = size;

        // pipes don't support 0-length packets
        while bytes_remaining > 0 {
            // split the packet up into PIPE_BUF-sized packets
            let bytes_to_write = std::cmp::min(bytes_remaining, libc::PIPE_BUF as u64);
            self.queue
                .push_packet(bytes.as_read(), bytes_to_write.try_into().unwrap())?;
            bytes_remaining -= bytes_to_write;
        }

        self.refresh_state(event_queue);

        Ok(size.into())
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

    fn refresh_state(&mut self, event_queue: &mut EventQueue) {
        let readable = !self.is_empty() || self.num_writers == 0;
        let writable = self.space_available() > 0;

        self.adjust_state(FileState::READABLE, readable, event_queue);
        self.adjust_state(FileState::WRITABLE, writable, event_queue);
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
