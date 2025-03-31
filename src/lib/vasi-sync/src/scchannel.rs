use core::fmt::Display;
use core::mem::MaybeUninit;

use vasi::VirtualAddressSpaceIndependent;

use crate::sync::{self, AtomicU32, UnsafeCell};

#[derive(Debug, Copy, Clone, Eq, PartialEq, VirtualAddressSpaceIndependent)]
#[repr(u8)]
enum ChannelContentsState {
    Empty,
    Writing,
    Ready,
    Reading,
}

impl From<u8> for ChannelContentsState {
    fn from(value: u8) -> Self {
        const EMPTY: u8 = ChannelContentsState::Empty as u8;
        const WRITING: u8 = ChannelContentsState::Writing as u8;
        const READY: u8 = ChannelContentsState::Ready as u8;
        const READING: u8 = ChannelContentsState::Reading as u8;
        match value {
            EMPTY => ChannelContentsState::Empty,
            WRITING => ChannelContentsState::Writing,
            READY => ChannelContentsState::Ready,
            READING => ChannelContentsState::Reading,
            _ => panic!("Bad value {value}"),
        }
    }
}

// bit flags in ChannelState
const WRITER_CLOSED: u32 = 0x1 << 9;
const HAS_SLEEPER: u32 = 0x1 << 10;

#[repr(C)]
#[derive(Debug, Eq, PartialEq, Copy, Clone, VirtualAddressSpaceIndependent)]
struct ChannelState {
    writer_closed: bool,
    has_sleeper: bool,
    contents_state: ChannelContentsState,
}

impl From<u32> for ChannelState {
    fn from(value: u32) -> Self {
        let has_sleeper = (value & HAS_SLEEPER) != 0;
        let writer_closed = (value & WRITER_CLOSED) != 0;
        let contents_state = ((value & 0xff) as u8).into();
        ChannelState {
            has_sleeper,
            writer_closed,
            contents_state,
        }
    }
}

impl From<ChannelState> for u32 {
    fn from(value: ChannelState) -> Self {
        let has_sleeper = if value.has_sleeper { HAS_SLEEPER } else { 0 };
        let writer_closed = if value.writer_closed {
            WRITER_CLOSED
        } else {
            0
        };
        writer_closed | has_sleeper | (value.contents_state as u32)
    }
}

#[cfg_attr(not(loom), derive(VirtualAddressSpaceIndependent))]
#[repr(transparent)]
struct AtomicChannelState(AtomicU32);
impl AtomicChannelState {
    pub fn new() -> Self {
        Self(AtomicU32::new(
            ChannelState {
                has_sleeper: false,
                writer_closed: false,
                contents_state: ChannelContentsState::Empty,
            }
            .into(),
        ))
    }

    /// Typed interface to `AtomicU32::fetch_update`
    fn fetch_update<F>(
        &self,
        set_order: sync::atomic::Ordering,
        fetch_order: sync::atomic::Ordering,
        mut f: F,
    ) -> Result<ChannelState, ChannelState>
    where
        F: FnMut(ChannelState) -> Option<ChannelState>,
    {
        self.0
            .fetch_update(set_order, fetch_order, |x| {
                let res = f(ChannelState::from(x));
                res.map(u32::from)
            })
            .map(ChannelState::from)
            .map_err(ChannelState::from)
    }

    /// Typed interface to `AtomicU32::load`
    fn load(&self, order: sync::atomic::Ordering) -> ChannelState {
        ChannelState::from(self.0.load(order))
    }

    /// Typed interface to `AtomicU32::compare_exchange`
    fn compare_exchange(
        &self,
        current: ChannelState,
        new: ChannelState,
        success: sync::atomic::Ordering,
        failure: sync::atomic::Ordering,
    ) -> Result<ChannelState, ChannelState> {
        self.0
            .compare_exchange(current.into(), new.into(), success, failure)
            .map(ChannelState::from)
            .map_err(ChannelState::from)
    }
}

#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum SelfContainedChannelError {
    WriterIsClosed,
}

impl Display for SelfContainedChannelError {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            SelfContainedChannelError::WriterIsClosed => write!(f, "WriterIsClosed"),
        }
    }
}

/// A fairly minimal channel implementation that implements the
/// [`vasi::VirtualAddressSpaceIndependent`] trait.
///
/// Breaking the documented API contracts may result both in immediate panics,
/// and panics in subsequent operations on the channel. To avoid this, the user
/// must:
/// * Never allow parallel `send` or `receive` operations. This is a
///   single-producer, single-consumer channel.
/// * Never call `send` when there is already a message pending.
///
/// Loosely inspired by the channel implementation examples in "Rust Atomics and
/// Locks" by Mara Box (O'Reilly). Copyright 2023 Mara Box, 978-1-098-11944-7.
/// (From the preface: "You may use all example code offered with this book for
/// any purpose").
///
/// TODO: Several candidate optimizations have been evaluated and discarded, but
/// are left in the commit history for posterity along with their corresponding
/// microbenchmark results.
///
/// One that might be worth revisiting is to remove the internal "Reading" and
/// "Writing" states and either make the interfaces `unsafe` (since it becomes
/// the caller's responsibility to avoid parallel reads or writes), or add
/// checked creation of `!Sync` Reader and Writer objects. This optimization
/// appeared to have a 22% benefit in the "ping pong" microbenchmark on a large
/// simulation machine, but only a 3.5% benefit in the "ping pong pinned"
/// microbenchmark; the latter is expected to be more representative of real
/// large simulation runs (i.e. pinning should be enabled).
#[cfg_attr(not(loom), derive(VirtualAddressSpaceIndependent))]
#[repr(C)]
pub struct SelfContainedChannel<T> {
    message: UnsafeCell<MaybeUninit<T>>,
    state: AtomicChannelState,
}

impl<T> SelfContainedChannel<T> {
    pub fn new() -> Self {
        Self {
            message: UnsafeCell::new(MaybeUninit::uninit()),
            state: AtomicChannelState::new(),
        }
    }

    /// Sends `message` through the channel.
    ///
    /// Panics if the channel already has an unreceived message.
    pub fn send(&self, message: T) {
        self.state
            .fetch_update(
                sync::atomic::Ordering::Acquire,
                sync::atomic::Ordering::Relaxed,
                |mut state| {
                    assert_eq!(state.contents_state, ChannelContentsState::Empty);
                    state.contents_state = ChannelContentsState::Writing;
                    Some(state)
                },
            )
            .unwrap();
        unsafe { self.message.get_mut().deref().as_mut_ptr().write(message) };
        let prev = self
            .state
            .fetch_update(
                sync::atomic::Ordering::Release,
                sync::atomic::Ordering::Relaxed,
                |mut state| {
                    assert_eq!(state.contents_state, ChannelContentsState::Writing);
                    state.contents_state = ChannelContentsState::Ready;
                    Some(state)
                },
            )
            .unwrap();
        if prev.has_sleeper {
            sync::futex_wake_one(&self.state.0).unwrap();
        }
    }

    /// Blocks until either the channel contains a message, or the writer has
    /// closed the channel.
    ///
    /// Returns `Ok(T)` if a message was received, or
    /// `Err(SelfContainedMutexError::WriterIsClosed)` if the writer is closed.
    ///
    /// Panics if another thread is already trying to receive on this channel.
    pub fn receive(&self) -> Result<T, SelfContainedChannelError> {
        let mut state = self.state.load(sync::atomic::Ordering::Relaxed);
        loop {
            if state.contents_state == ChannelContentsState::Ready {
                break;
            }
            if state.writer_closed {
                return Err(SelfContainedChannelError::WriterIsClosed);
            }
            assert!(
                state.contents_state == ChannelContentsState::Empty
                    || state.contents_state == ChannelContentsState::Writing
            );
            assert!(!state.has_sleeper);
            let mut sleeper_state = state;
            sleeper_state.has_sleeper = true;
            match self.state.compare_exchange(
                state,
                sleeper_state,
                sync::atomic::Ordering::Relaxed,
                sync::atomic::Ordering::Relaxed,
            ) {
                Ok(_) => (),
                Err(s) => {
                    // Something changed; re-evaluate.
                    state = s;
                    continue;
                }
            };
            let expected = sleeper_state.into();
            match sync::futex_wait(&self.state.0, expected, None) {
                Ok(_) | Err(rustix::io::Errno::INTR) | Err(rustix::io::Errno::AGAIN) => {
                    // Something changed; clear the sleeper bit and try again.
                    let mut updated_state = self
                        .state
                        .fetch_update(
                            sync::atomic::Ordering::Relaxed,
                            sync::atomic::Ordering::Relaxed,
                            |mut state| {
                                state.has_sleeper = false;
                                Some(state)
                            },
                        )
                        .unwrap();
                    updated_state.has_sleeper = false;
                    state = updated_state;
                    continue;
                }
                Err(e) => panic!("Unexpected futex error {:?}", e),
            };
        }
        self.state
            .fetch_update(
                sync::atomic::Ordering::Acquire,
                sync::atomic::Ordering::Relaxed,
                |mut state| {
                    assert_eq!(state.contents_state, ChannelContentsState::Ready);
                    state.contents_state = ChannelContentsState::Reading;
                    Some(state)
                },
            )
            .unwrap();
        let val = unsafe { self.message.get_mut().deref().assume_init_read() };
        self.state
            .fetch_update(
                sync::atomic::Ordering::Release,
                sync::atomic::Ordering::Relaxed,
                |mut state| {
                    assert_eq!(state.contents_state, ChannelContentsState::Reading);
                    state.contents_state = ChannelContentsState::Empty;
                    Some(state)
                },
            )
            .unwrap();
        Ok(val)
    }

    /// Closes the "write" end of the channel. This will cause any current
    /// and subsequent `receive` operations to fail once the channel is empty.
    ///
    /// This method *can* be called in parallel with other methods on the
    /// channel, making it suitable to be called from a separate watchdog thread
    /// that detects that the writing thread (or process) has died.
    pub fn close_writer(&self) {
        let prev = self
            .state
            .fetch_update(
                sync::atomic::Ordering::Relaxed,
                sync::atomic::Ordering::Relaxed,
                |mut state| {
                    state.writer_closed = true;
                    Some(state)
                },
            )
            .unwrap();
        if prev.has_sleeper {
            sync::futex_wake_one(&self.state.0).unwrap();
        }
    }

    /// Whether the write-end of the channel has been closed (via `close_writer`).
    pub fn writer_is_closed(&self) -> bool {
        self.state
            .load(sync::atomic::Ordering::Relaxed)
            .writer_closed
    }
}

unsafe impl<T> Send for SelfContainedChannel<T> where T: Send {}
unsafe impl<T> Sync for SelfContainedChannel<T> where T: Send {}

impl<T> Drop for SelfContainedChannel<T> {
    fn drop(&mut self) {
        // Conservatively use Acquire-ordering here to synchronize with the
        // Release-ordered store in `send`.
        //
        // This shouldn't be strictly necessary - for us to have a `&mut` reference, some
        // external synchronization must have already happened over the whole channel. Indeed
        // the original Box implementation uses get_mut here, which doesn't have an atomic
        // operation at all.
        let state = self.state.load(sync::atomic::Ordering::Acquire);
        if state.contents_state == ChannelContentsState::Ready {
            unsafe { self.message.get_mut().deref().assume_init_drop() }
        }
    }
}

impl<T> Default for SelfContainedChannel<T> {
    fn default() -> Self {
        Self::new()
    }
}
