use std::io::{Read, Write};

use linux_api::errno::Errno;
use linux_api::ioctls::IoctlRequest;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::cshadow as c;
use crate::host::descriptor::listener::{StateEventSource, StateListenHandle, StateListenerFilter};
use crate::host::descriptor::{FileMode, FileSignals, FileState, FileStatus};
use crate::host::memory_manager::MemoryManager;
use crate::host::syscall::io::{IoVec, IoVecReader, IoVecWriter};
use crate::host::syscall::types::{SyscallError, SyscallResult};
use crate::utility::callback_queue::CallbackQueue;
use crate::utility::HostTreePointer;

pub struct EventFd {
    counter: u64,
    is_semaphore_mode: bool,
    event_source: StateEventSource,
    state: FileState,
    status: FileStatus,
    // should only be used by `OpenFile` to make sure there is only ever one `OpenFile` instance for
    // this file
    has_open_file: bool,
}

impl EventFd {
    pub fn new(init_value: u64, is_semaphore_mode: bool, status: FileStatus) -> Self {
        Self {
            counter: init_value,
            is_semaphore_mode,
            event_source: StateEventSource::new(),
            state: FileState::ACTIVE | FileState::WRITABLE,
            status,
            has_open_file: false,
        }
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
        false
    }

    pub fn set_has_open_file(&mut self, val: bool) {
        self.has_open_file = val;
    }

    pub fn close(&mut self, cb_queue: &mut CallbackQueue) -> Result<(), SyscallError> {
        // set the closed flag and remove the active, readable, and writable flags
        self.update_state(
            FileState::CLOSED | FileState::ACTIVE | FileState::READABLE | FileState::WRITABLE,
            FileState::CLOSED,
            FileSignals::empty(),
            cb_queue,
        );

        Ok(())
    }

    pub fn readv(
        &mut self,
        iovs: &[IoVec],
        offset: Option<libc::off_t>,
        _flags: libc::c_int,
        mem: &mut MemoryManager,
        cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        // eventfds don't support seeking
        if offset.is_some() {
            return Err(Errno::ESPIPE.into());
        }

        // eventfd(2): "Each successful read(2) returns an 8-byte integer"
        const NUM_BYTES: usize = 8;

        let len: libc::size_t = iovs.iter().map(|x| x.len).sum();

        // this check doesn't guarentee that we can write all bytes since the stream length is only
        // a hint
        if len < NUM_BYTES {
            log::trace!(
                "Reading from eventfd requires a buffer of at least {} bytes",
                NUM_BYTES
            );
            return Err(Errno::EINVAL.into());
        }

        if self.counter == 0 {
            log::trace!("Eventfd counter is 0 and cannot be read right now");
            return Err(Errno::EWOULDBLOCK.into());
        }

        let mut writer = IoVecWriter::new(iovs, mem);

        // behavior defined in `man 2 eventfd`
        if self.is_semaphore_mode {
            const ONE: [u8; NUM_BYTES] = 1u64.to_ne_bytes();
            writer.write_all(&ONE)?;
            self.counter -= 1;
        } else {
            let to_write: [u8; NUM_BYTES] = self.counter.to_ne_bytes();
            writer.write_all(&to_write)?;
            self.counter = 0;
        }

        self.refresh_state(FileSignals::empty(), cb_queue);

        Ok(NUM_BYTES.try_into().unwrap())
    }

    pub fn writev(
        &mut self,
        iovs: &[IoVec],
        offset: Option<libc::off_t>,
        _flags: libc::c_int,
        mem: &mut MemoryManager,
        cb_queue: &mut CallbackQueue,
    ) -> Result<libc::ssize_t, SyscallError> {
        // eventfds don't support seeking
        if offset.is_some() {
            return Err(Errno::ESPIPE.into());
        }

        // eventfd(2): "A write(2) call adds the 8-byte integer value supplied in its buffer to the
        // counter"
        const NUM_BYTES: usize = 8;

        let len: libc::size_t = iovs.iter().map(|x| x.len).sum();

        // this check doesn't guarentee that we can read all bytes since the stream length is only
        // a hint
        if len < NUM_BYTES {
            log::trace!(
                "Writing to eventfd requires a buffer with at least {} bytes",
                NUM_BYTES
            );
            return Err(Errno::EINVAL.into());
        }

        if iovs.len() > 1 {
            // Linux doesn't seem to let you write to an eventfd with multiple iovecs
            return Err(Errno::EINVAL.into());
        }

        let mut reader = IoVecReader::new(iovs, mem);

        let mut read_buf = [0u8; NUM_BYTES];
        reader.read_exact(&mut read_buf)?;
        let value: u64 = u64::from_ne_bytes(read_buf);

        if value == u64::MAX {
            log::trace!("We do not allow writing the max counter value");
            return Err(Errno::EINVAL.into());
        }

        const MAX_ALLOWED: u64 = u64::MAX - 1;
        if value > MAX_ALLOWED - self.counter {
            log::trace!("The write value does not currently fit into the counter");
            return Err(Errno::EWOULDBLOCK.into());
        }

        self.counter += value;
        let signals = if self.counter > 0 {
            FileSignals::READ_BUFFER_GREW
        } else {
            FileSignals::empty()
        };
        self.refresh_state(signals, cb_queue);

        Ok(NUM_BYTES.try_into().unwrap())
    }

    pub fn ioctl(
        &mut self,
        request: IoctlRequest,
        _arg_ptr: ForeignPtr<()>,
        _memory_manager: &mut MemoryManager,
    ) -> SyscallResult {
        log::warn!("We do not yet handle ioctl request {request:?} on eventfds");
        Err(Errno::EINVAL.into())
    }

    pub fn stat(&self) -> Result<linux_api::stat::stat, SyscallError> {
        warn_once_then_debug!("We do not yet handle stat calls on eventfds");
        Err(Errno::EINVAL.into())
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

    pub fn add_legacy_listener(&mut self, ptr: HostTreePointer<c::StatusListener>) {
        self.event_source.add_legacy_listener(ptr);
    }

    pub fn remove_legacy_listener(&mut self, ptr: *mut c::StatusListener) {
        self.event_source.remove_legacy_listener(ptr);
    }

    pub fn state(&self) -> FileState {
        self.state
    }

    fn refresh_state(&mut self, signals: FileSignals, cb_queue: &mut CallbackQueue) {
        if self.state.contains(FileState::CLOSED) {
            return;
        }

        let mut readable_writable = FileState::empty();

        // set the descriptor as readable if we have a non-zero counter
        readable_writable.set(FileState::READABLE, self.counter > 0);
        // set the descriptor as writable if we can write a value of at least 1
        readable_writable.set(FileState::WRITABLE, self.counter < u64::MAX - 1);

        self.update_state(
            FileState::READABLE | FileState::WRITABLE,
            readable_writable,
            signals,
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

        // remove the masked flags, then copy the masked flags
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

        // if nothing changed
        if states_changed.is_empty() && signals.is_empty() {
            return;
        }

        self.event_source
            .notify_listeners(self.state, states_changed, signals, cb_queue);
    }
}
