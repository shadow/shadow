//! A buffer for files that need to share a buffer with other files. Example use-cases are pipes and
//! unix sockets. This buffer supports notifying files when readers or writers are added or removed.

use linux_api::errno::Errno;

use crate::utility::byte_queue::ByteQueue;
use crate::utility::callback_queue::{CallbackQueue, EventSource, Handle};

pub struct SharedBuf {
    queue: ByteQueue,
    max_len: usize,
    state: BufferState,
    num_readers: u16,
    num_writers: u16,
    event_source: EventSource<(BufferState, BufferState, BufferSignals)>,
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
        self.max_len - self.queue.num_bytes()
    }

    /// Register as a reader. The [`ReaderHandle`] must be returned to the buffer later with
    /// [`remove_reader()`](Self::remove_reader).
    pub fn add_reader(&mut self, cb_queue: &mut CallbackQueue) -> ReaderHandle {
        self.num_readers += 1;
        self.refresh_state(BufferSignals::empty(), cb_queue);
        ReaderHandle {}
    }

    pub fn remove_reader(&mut self, handle: ReaderHandle, cb_queue: &mut CallbackQueue) {
        self.num_readers -= 1;
        // don't run the handle's drop impl
        std::mem::forget(handle);
        self.refresh_state(BufferSignals::empty(), cb_queue);
    }

    pub fn num_readers(&self) -> u16 {
        self.num_readers
    }

    /// Register as a writer. The [`WriterHandle`] must be returned to the buffer later with
    /// [`remove_writer()`](Self::remove_writer).
    pub fn add_writer(&mut self, cb_queue: &mut CallbackQueue) -> WriterHandle {
        self.num_writers += 1;
        self.refresh_state(BufferSignals::empty(), cb_queue);
        WriterHandle {}
    }

    pub fn remove_writer(&mut self, handle: WriterHandle, cb_queue: &mut CallbackQueue) {
        self.num_writers -= 1;
        // don't run the handle's drop impl
        std::mem::forget(handle);
        self.refresh_state(BufferSignals::empty(), cb_queue);
    }

    pub fn num_writers(&self) -> u16 {
        self.num_writers
    }

    pub fn read<W: std::io::Write>(
        &mut self,
        bytes: W,
        cb_queue: &mut CallbackQueue,
    ) -> Result<(usize, usize), std::io::Error> {
        let (num_copied, num_removed_from_buf) = match self.queue.pop(bytes)? {
            Some((num_copied, num_removed_from_buf, _chunk_type)) => {
                (num_copied, num_removed_from_buf)
            }
            None => (0, 0),
        };
        self.refresh_state(BufferSignals::empty(), cb_queue);

        Ok((num_copied, num_removed_from_buf))
    }

    pub fn write_stream<R: std::io::Read>(
        &mut self,
        bytes: R,
        len: usize,
        cb_queue: &mut CallbackQueue,
    ) -> Result<usize, std::io::Error> {
        if len == 0 {
            return Ok(0);
        }

        if self.space_available() == 0 {
            return Err(Errno::EAGAIN.into());
        }

        let written = self
            .queue
            .push_stream(bytes.take(self.space_available().try_into().unwrap()))?;

        let signals = if written > 0 {
            BufferSignals::BUFFER_GREW
        } else {
            BufferSignals::empty()
        };
        self.refresh_state(signals, cb_queue);

        Ok(written)
    }

    pub fn write_packet<R: std::io::Read>(
        &mut self,
        mut bytes: R,
        len: usize,
        cb_queue: &mut CallbackQueue,
    ) -> Result<(), std::io::Error> {
        if len > self.max_len() {
            // the socket could never send this packet, even if the buffer was empty
            return Err(Errno::EMSGSIZE.into());
        }

        if len > self.space_available() {
            return Err(Errno::EAGAIN.into());
        }

        self.queue.push_packet(bytes.by_ref(), len)?;

        self.refresh_state(BufferSignals::BUFFER_GREW, cb_queue);

        Ok(())
    }

    pub fn add_listener(
        &mut self,
        monitoring_state: BufferState,
        monitoring_signals: BufferSignals,
        notify_fn: impl Fn(BufferState, BufferSignals, &mut CallbackQueue) + Send + Sync + 'static,
    ) -> BufferHandle {
        self.event_source
            .add_listener(move |(state, changed, signals), cb_queue| {
                // true if any of the bits we're monitoring have changed
                let flipped = monitoring_state.intersects(changed);

                // filter the signals to only the ones we're monitoring
                let signals = signals.intersection(monitoring_signals);

                if !flipped && signals.is_empty() {
                    return;
                }

                (notify_fn)(state, signals, cb_queue)
            })
    }

    pub fn state(&self) -> BufferState {
        self.state
    }

    fn refresh_state(&mut self, signals: BufferSignals, cb_queue: &mut CallbackQueue) {
        let state_mask = BufferState::READABLE
            | BufferState::WRITABLE
            | BufferState::NO_READERS
            | BufferState::NO_WRITERS;

        let mut new_state = BufferState::empty();

        new_state.set(BufferState::READABLE, self.has_data());
        new_state.set(BufferState::WRITABLE, self.space_available() > 0);
        new_state.set(BufferState::NO_READERS, self.num_readers() == 0);
        new_state.set(BufferState::NO_WRITERS, self.num_writers() == 0);

        self.update_state(state_mask, new_state, signals, cb_queue);
    }

    fn update_state(
        &mut self,
        mask: BufferState,
        state: BufferState,
        signals: BufferSignals,
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
        old_state: BufferState,
        signals: BufferSignals,
        cb_queue: &mut CallbackQueue,
    ) {
        let states_changed = self.state ^ old_state;

        // if nothing changed
        if states_changed.is_empty() && signals.is_empty() {
            return;
        }

        self.event_source
            .notify_listeners((self.state, states_changed, signals), cb_queue);
    }
}

impl Drop for SharedBuf {
    fn drop(&mut self) {
        // don't show the following warning message if panicking
        if std::thread::panicking() {
            return;
        }

        // listeners waiting for `NO_READERS` or `NO_WRITERS` status changes will never be notified
        if self.num_readers != 0 || self.num_writers != 0 {
            // panic in debug builds since the backtrace will be helpful for debugging
            debug_panic!(
                "Dropping SharedBuf while it still has {} readers and {} writers.",
                self.num_readers,
                self.num_writers,
            );
        }
    }
}

bitflags::bitflags! {
    #[derive(Default, Copy, Clone, Debug)]
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

bitflags::bitflags! {
    #[derive(Default, Copy, Clone, Debug)]
    pub struct BufferSignals: u8 {
        /// The buffer now has additional data available to read.
        const BUFFER_GREW = 1 << 0;
    }
}

pub type BufferHandle = Handle<(BufferState, BufferState, BufferSignals)>;

/// A handle that signifies that the owner is acting as a reader for the buffer. The handle must be
/// returned to the buffer later with [`SharedBuf::remove_reader()`].
///
/// Handles aren't linked to specific buffers, so make sure to only return the handle to the same
/// buffer which you acquired the handle from.
// do not implement copy or clone
pub struct ReaderHandle;

/// See [`ReaderHandle`].
// do not implement copy or clone
pub struct WriterHandle;

impl Drop for ReaderHandle {
    fn drop(&mut self) {
        // don't show the following warning message if panicking
        if std::thread::panicking() {
            return;
        }

        // panic in debug builds since the backtrace will be helpful for debugging
        debug_panic!(
            "Dropping ReaderHandle without returning it to SharedBuf. \
             This likely indicates a bug in Shadow."
        );
    }
}

impl Drop for WriterHandle {
    fn drop(&mut self) {
        // don't show the following warning message if panicking
        if std::thread::panicking() {
            return;
        }

        // panic in debug builds since the backtrace will be helpful for debugging
        debug_panic!(
            "Dropping WriterHandle without returning it to SharedBuf. \
             This likely indicates a bug in Shadow."
        );
    }
}
