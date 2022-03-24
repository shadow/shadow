use nix::errno::Errno;

use crate::host::descriptor::{FileState, StateEventSource, StateListenerFilter};
use crate::host::syscall_types::{SyscallError, SyscallResult};
use crate::utility::byte_queue::ByteQueue;
use crate::utility::event_queue::{EventQueue, Handle};

pub struct SharedBuf {
    queue: ByteQueue,
    max_len: usize,
    state: FileState,
    num_readers: u16,
    num_writers: u16,
    event_source: StateEventSource,
}

impl SharedBuf {
    pub fn new(max_len: usize) -> Self {
        assert_ne!(max_len, 0);
        Self {
            queue: ByteQueue::new(4096),
            max_len,
            state: FileState::WRITABLE,
            num_readers: 0,
            num_writers: 0,
            event_source: StateEventSource::new(),
        }
    }

    pub fn has_data(&self) -> bool {
        self.queue.has_chunks()
    }

    pub fn max_len(&self) -> usize {
        self.max_len
    }

    pub fn space_available(&self) -> usize {
        self.max_len - usize::try_from(self.queue.num_bytes()).unwrap()
    }

    pub fn add_reader(&mut self, event_queue: &mut EventQueue) {
        self.num_readers += 1;
        self.refresh_state(event_queue);
    }

    pub fn remove_reader(&mut self, event_queue: &mut EventQueue) {
        self.num_readers -= 1;
        self.refresh_state(event_queue);
    }

    pub fn num_readers(&self) -> u16 {
        self.num_readers
    }

    pub fn add_writer(&mut self, event_queue: &mut EventQueue) {
        self.num_writers += 1;
        self.refresh_state(event_queue);
    }

    pub fn remove_writer(&mut self, event_queue: &mut EventQueue) {
        self.num_writers -= 1;
        self.refresh_state(event_queue);
    }

    pub fn num_writers(&self) -> u16 {
        self.num_writers
    }

    pub fn read<W: std::io::Write>(
        &mut self,
        bytes: W,
        event_queue: &mut EventQueue,
    ) -> SyscallResult {
        let (num, _chunk_type) = self.queue.pop(bytes)?;
        self.refresh_state(event_queue);

        Ok(num.into())
    }

    pub fn write_stream<R: std::io::Read>(
        &mut self,
        bytes: R,
        len: usize,
        event_queue: &mut EventQueue,
    ) -> SyscallResult {
        if len == 0 {
            return Ok(0.into());
        }

        if self.space_available() == 0 {
            return Err(Errno::EAGAIN.into());
        }

        let written = self
            .queue
            .push_stream(bytes.take(self.space_available().try_into().unwrap()))?;
        self.refresh_state(event_queue);

        Ok(written.into())
    }

    pub fn write_packet<R: std::io::Read>(
        &mut self,
        mut bytes: R,
        len: usize,
        event_queue: &mut EventQueue,
    ) -> Result<(), SyscallError> {
        if len > self.max_len() {
            // the socket could never send this packet, even if the buffer was empty
            return Err(Errno::EMSGSIZE.into());
        }

        if len > self.space_available() {
            return Err(Errno::EAGAIN.into());
        }

        self.queue.push_packet(bytes.by_ref(), len)?;
        self.refresh_state(event_queue);

        Ok(())
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
        let readable = self.has_data() || self.num_writers() == 0;
        let writable = self.space_available() > 0 || self.num_readers() == 0;

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
