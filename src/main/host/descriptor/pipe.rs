use atomic_refcell::AtomicRefCell;
use nix::errno::Errno;
use std::sync::Arc;

use crate::cshadow as c;
use crate::host::descriptor::shared_buf::{
    BufferHandle, BufferState, ReaderHandle, SharedBuf, WriterHandle,
};
use crate::host::descriptor::{
    FileMode, FileState, FileStatus, StateEventSource, StateListenerFilter,
};
use crate::host::memory_manager::MemoryManager;
use crate::host::syscall_types::{PluginPtr, SyscallError, SyscallResult};
use crate::utility::callback_queue::{CallbackQueue, Handle};
use crate::utility::stream_len::StreamLen;
use crate::utility::HostTreePointer;

pub struct Pipe {
    buffer: Option<Arc<AtomicRefCell<SharedBuf>>>,
    event_source: StateEventSource,
    state: FileState,
    mode: FileMode,
    status: FileStatus,
    write_mode: WriteMode,
    buffer_event_handle: Option<BufferHandle>,
    reader_handle: Option<ReaderHandle>,
    writer_handle: Option<WriterHandle>,
    // should only be used by `OpenFile` to make sure there is only ever one `OpenFile` instance for
    // this file
    has_open_file: bool,
}

impl Pipe {
    /// Create a new [`Pipe`]. The new pipe must be initialized using [`Pipe::connect_to_buffer`]
    /// before any of its methods are called.
    pub fn new(mode: FileMode, status: FileStatus) -> Self {
        Self {
            buffer: None,
            event_source: StateEventSource::new(),
            state: FileState::ACTIVE,
            mode,
            status,
            write_mode: WriteMode::Stream,
            buffer_event_handle: None,
            reader_handle: None,
            writer_handle: None,
            has_open_file: false,
        }
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

    pub fn has_open_file(&self) -> bool {
        self.has_open_file
    }

    pub fn supports_sa_restart(&self) -> bool {
        true
    }

    pub fn set_has_open_file(&mut self, val: bool) {
        self.has_open_file = val;
    }

    pub fn max_size(&self) -> usize {
        self.buffer.as_ref().unwrap().borrow().max_len()
    }

    pub fn close(&mut self, cb_queue: &mut CallbackQueue) -> Result<(), SyscallError> {
        if self.state.contains(FileState::CLOSED) {
            log::warn!("Attempting to close an already-closed pipe");
        }

        // drop the event listener handle so that we stop receiving new events
        if let Some(h) = self.buffer_event_handle.take() {
            h.stop_listening()
        }

        // if acting as a writer, inform the buffer that there is one fewer writers
        if let Some(writer_handle) = self.writer_handle.take() {
            self.buffer
                .as_ref()
                .unwrap()
                .borrow_mut()
                .remove_writer(writer_handle, cb_queue);
        }

        // if acting as a reader, inform the buffer that there is one fewer readers
        if let Some(reader_handle) = self.reader_handle.take() {
            self.buffer
                .as_ref()
                .unwrap()
                .borrow_mut()
                .remove_reader(reader_handle, cb_queue);
        }

        // no need to hold on to the buffer anymore
        self.buffer = None;

        // set the closed flag and remove the active, readable, and writable flags
        self.copy_state(
            FileState::CLOSED | FileState::ACTIVE | FileState::READABLE | FileState::WRITABLE,
            FileState::CLOSED,
            cb_queue,
        );

        Ok(())
    }

    pub fn read<W>(
        &mut self,
        mut bytes: W,
        offset: libc::off_t,
        cb_queue: &mut CallbackQueue,
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

        let (num_copied, _num_removed_from_buf) = self
            .buffer
            .as_ref()
            .unwrap()
            .borrow_mut()
            .read(&mut bytes, cb_queue)?;

        // the read would block if all:
        //  1. we could not read any bytes
        //  2. we were asked to read >0 bytes
        //  3. there are open descriptors that refer to the write end of the pipe
        if num_copied == 0
            && bytes.stream_len_bp()? != 0
            && self.buffer.as_ref().unwrap().borrow().num_writers() > 0
        {
            Err(Errno::EWOULDBLOCK.into())
        } else {
            Ok(num_copied.into())
        }
    }

    pub fn write<R>(
        &mut self,
        mut bytes: R,
        offset: libc::off_t,
        cb_queue: &mut CallbackQueue,
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

        let mut buffer = self.buffer.as_ref().unwrap().borrow_mut();

        if buffer.num_readers() == 0 {
            return Err(nix::errno::Errno::EPIPE.into());
        }

        if self.write_mode == WriteMode::Packet && !self.status.contains(FileStatus::DIRECT) {
            // switch to stream mode immediately, regardless of whether the buffer is empty or not
            self.write_mode = WriteMode::Stream;
        } else if self.write_mode == WriteMode::Stream && self.status.contains(FileStatus::DIRECT) {
            // in linux, it seems that pipes only switch to packet mode when a new page is added to
            // the buffer, so we simulate that behaviour for when the first page is added (when the
            // buffer is empty)
            if !buffer.has_data() {
                self.write_mode = WriteMode::Packet;
            }
        }

        let len = bytes.stream_len_bp()? as usize;

        match self.write_mode {
            WriteMode::Stream => Ok(buffer.write_stream(bytes.by_ref(), len, cb_queue)?.into()),
            WriteMode::Packet => {
                let mut num_written = 0;

                loop {
                    // the number of remaining bytes to write
                    let bytes_remaining = len - num_written;

                    // if there are no more bytes to write (pipes don't support 0-length packets)
                    if bytes_remaining == 0 {
                        break Ok(num_written.into());
                    }

                    // split the packet up into PIPE_BUF-sized packets
                    let bytes_to_write = std::cmp::min(bytes_remaining, libc::PIPE_BUF);

                    if let Err(e) = buffer.write_packet(bytes.by_ref(), bytes_to_write, cb_queue) {
                        // if we've already written bytes, return those instead of an error
                        if num_written > 0 {
                            break Ok(num_written.into());
                        }
                        break Err(e);
                    }

                    num_written += bytes_to_write;
                }
            }
        }
    }

    pub fn ioctl(
        &mut self,
        request: u64,
        _arg_ptr: PluginPtr,
        _memory_manager: &mut MemoryManager,
    ) -> SyscallResult {
        log::warn!("We do not yet handle ioctl request {} on pipes", request);
        Err(Errno::EINVAL.into())
    }

    pub fn connect_to_buffer(
        arc: &Arc<AtomicRefCell<Self>>,
        buffer: Arc<AtomicRefCell<SharedBuf>>,
        cb_queue: &mut CallbackQueue,
    ) {
        let weak = Arc::downgrade(arc);
        let pipe = &mut *arc.borrow_mut();

        pipe.buffer = Some(buffer);

        if pipe.mode.contains(FileMode::WRITE) {
            pipe.writer_handle = Some(
                pipe.buffer
                    .as_ref()
                    .unwrap()
                    .borrow_mut()
                    .add_writer(cb_queue),
            );
        }

        if pipe.mode.contains(FileMode::READ) {
            pipe.reader_handle = Some(
                pipe.buffer
                    .as_ref()
                    .unwrap()
                    .borrow_mut()
                    .add_reader(cb_queue),
            );
        }

        // buffer state changes that we want to receive events for
        let mut monitoring = BufferState::empty();

        // if the file is open for reading, watch for the buffer to become readable or have no
        // writers
        if pipe.mode.contains(FileMode::READ) {
            monitoring.insert(BufferState::READABLE);
            monitoring.insert(BufferState::NO_WRITERS);
        }

        // if the file is open for writing, watch for the buffer to become writable or have no
        // readers
        if pipe.mode.contains(FileMode::WRITE) {
            monitoring.insert(BufferState::WRITABLE);
            monitoring.insert(BufferState::NO_READERS);
        }

        let handle = pipe.buffer.as_ref().unwrap().borrow_mut().add_listener(
            monitoring,
            move |buffer_state, cb_queue| {
                // if the file hasn't been dropped
                if let Some(pipe) = weak.upgrade() {
                    let mut pipe = pipe.borrow_mut();

                    // update the pipe file's state to align with the buffer's current state
                    pipe.align_state_to_buffer(buffer_state, cb_queue);
                }
            },
        );

        pipe.buffer_event_handle = Some(handle);

        // update the pipe file's initial state to align with the buffer's current state
        let buffer_state = pipe.buffer.as_ref().unwrap().borrow().state();
        pipe.align_state_to_buffer(buffer_state, cb_queue);
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

    /// Align the pipe's state to the buffer state. For example if the buffer is both `READABLE` and
    /// `WRITABLE`, and the pipe is only open in `READ` mode, the pipe's `READABLE` state will be
    /// set and the `WRITABLE` state will be unchanged.
    fn align_state_to_buffer(&mut self, buffer_state: BufferState, cb_queue: &mut CallbackQueue) {
        let mut mask = FileState::empty();
        let mut file_state = FileState::empty();

        // if the pipe is already closed, do nothing
        if self.state.contains(FileState::CLOSED) {
            return;
        }

        // only update the readable state if the file is open for reading
        if self.mode.contains(FileMode::READ) {
            mask.insert(FileState::READABLE);
            // file is readable if the buffer is readable or there are no writers
            if buffer_state.intersects(BufferState::READABLE | BufferState::NO_WRITERS) {
                file_state.insert(FileState::READABLE);
            }
        }

        // only update the writable state if the file is open for writing
        if self.mode.contains(FileMode::WRITE) {
            mask.insert(FileState::WRITABLE);
            // file is writable if the buffer is writable or there are no readers
            if buffer_state.intersects(BufferState::WRITABLE | BufferState::NO_READERS) {
                file_state.insert(FileState::WRITABLE);
            }
        }

        // update the file's state
        self.copy_state(mask, file_state, cb_queue);
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

#[derive(Debug, PartialEq, Eq)]
enum WriteMode {
    Stream,
    Packet,
}
