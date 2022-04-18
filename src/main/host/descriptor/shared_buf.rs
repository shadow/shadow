use nix::errno::Errno;

use crate::host::syscall_types::{SyscallError, SyscallResult};
use crate::utility::byte_queue::ByteQueue;
use crate::utility::event_queue::{EventQueue, EventSource, Handle};

pub struct SharedBuf {
    queue: ByteQueue,
    max_len: usize,
    state: BufferState,
    num_readers: u16,
    num_writers: u16,
    event_source: EventSource<(BufferState, BufferState)>,
}

impl SharedBuf {
    pub fn new(max_len: usize) -> Self {
        assert_ne!(max_len, 0);
        Self {
            queue: ByteQueue::new(4096),
            max_len,
            state: BufferState::WRITABLE | BufferState::NO_READERS | BufferState::NO_WRITERS,
            num_readers: 0,
            num_writers: 0,
            event_source: EventSource::new(),
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
        monitoring: BufferState,
        notify_fn: impl Fn(BufferState, &mut EventQueue) + Send + Sync + 'static,
    ) -> BufferHandle {
        self.event_source
            .add_listener(move |(state, changed), event_queue| {
                // true if any of the bits we're monitoring have changed
                let flipped = monitoring.intersects(changed);

                if !flipped {
                    return;
                }

                (notify_fn)(state, event_queue)
            })
    }

    pub fn state(&self) -> BufferState {
        self.state
    }

    fn refresh_state(&mut self, event_queue: &mut EventQueue) {
        let state_mask = BufferState::READABLE
            | BufferState::WRITABLE
            | BufferState::NO_READERS
            | BufferState::NO_WRITERS;

        let mut new_state = BufferState::empty();

        new_state.set(BufferState::READABLE, self.has_data());
        new_state.set(BufferState::WRITABLE, self.space_available() > 0);
        new_state.set(BufferState::NO_READERS, self.num_readers() == 0);
        new_state.set(BufferState::NO_WRITERS, self.num_writers() == 0);

        self.copy_state(state_mask, new_state, event_queue);
    }

    fn copy_state(&mut self, mask: BufferState, state: BufferState, event_queue: &mut EventQueue) {
        let old_state = self.state;

        // remove the masked flags, then copy the masked flags
        self.state.remove(mask);
        self.state.insert(state & mask);

        self.handle_state_change(old_state, event_queue);
    }

    fn handle_state_change(&mut self, old_state: BufferState, event_queue: &mut EventQueue) {
        let states_changed = self.state ^ old_state;

        // if nothing changed
        if states_changed.is_empty() {
            return;
        }

        self.event_source
            .notify_listeners((self.state, states_changed), event_queue);
    }
}

bitflags::bitflags! {
    #[derive(Default)]
    pub struct BufferState: u8 {
        /// There is data waiting in the buffer.
        const READABLE = 0b00000001;
        /// There is available buffer space.
        const WRITABLE = 0b00000010;
        /// The buffer has no readers.
        const NO_READERS = 0b00000100;
        /// The buffer has no writers.
        const NO_WRITERS = 0b00001000;
    }
}

pub type BufferHandle = Handle<(BufferState, BufferState)>;
