use std::error::Error;
use std::fmt::Display;
use std::mem::MaybeUninit;

use vasi::VirtualAddressSpaceIndependent;

use crate::sync::{self, AtomicU32, UnsafeCell};

#[derive(Debug, Copy, Clone, Eq, PartialEq)]
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
const WRITER_DIED: u32 = 0x1 << 9;
const HAS_SLEEPER: u32 = 0x1 << 10;

#[derive(Debug, Eq, PartialEq, Copy, Clone)]
struct ChannelState {
    writer_died: bool,
    has_sleeper: bool,
    contents_state: ChannelContentsState,
}

impl From<u32> for ChannelState {
    fn from(value: u32) -> Self {
        let has_sleeper = (value & HAS_SLEEPER) != 0;
        let writer_died = (value & WRITER_DIED) != 0;
        let contents_state = ((value & 0xff) as u8).into();
        ChannelState {
            has_sleeper,
            writer_died,
            contents_state,
        }
    }
}

impl From<ChannelState> for u32 {
    fn from(value: ChannelState) -> Self {
        let has_sleeper = if value.has_sleeper { HAS_SLEEPER } else { 0 };
        let writer_died = if value.writer_died { WRITER_DIED } else { 0 };
        writer_died | has_sleeper | (value.contents_state as u32)
    }
}

struct AtomicChannelState(AtomicU32);
impl AtomicChannelState {
    pub fn new() -> Self {
        Self(AtomicU32::new(
            ChannelState {
                has_sleeper: false,
                writer_died: false,
                contents_state: ChannelContentsState::Empty,
            }
            .into(),
        ))
    }

    /// Change the inner state from `from` to `to`. Panics if the previous state
    /// doesn't match `from`.
    fn transition_contents_state(
        &self,
        from: ChannelContentsState,
        to: ChannelContentsState,
        order: sync::atomic::Ordering,
    ) -> ChannelState {
        let prev: ChannelState = if to as u32 > from as u32 {
            let inc = to as u32 - from as u32;
            self.0.fetch_add(inc, order).into()
        } else {
            let dec = from as u32 - to as u32;
            self.0.fetch_sub(dec, order).into()
        };
        assert_eq!(prev.contents_state, from);
        prev
    }

    /// Atomically change `has_sleeper` from false to true. Panics if it was already true.
    fn set_sleeper(&self, order: sync::atomic::Ordering) -> ChannelState {
        let prev = ChannelState::from(self.0.fetch_add(HAS_SLEEPER, order));
        assert!(!prev.has_sleeper);
        prev
    }

    /// Atomically change `has_sleeper` from true to false. Panics if it was already false.
    fn clear_sleeper(&self, order: sync::atomic::Ordering) -> ChannelState {
        let prev = ChannelState::from(self.0.fetch_sub(HAS_SLEEPER, order));
        assert!(prev.has_sleeper);
        prev
    }

    /// Atomically change `writer_died` from false to true. Panics if it was already true.
    fn set_writer_dead(&self, order: sync::atomic::Ordering) -> ChannelState {
        let prev = ChannelState::from(self.0.fetch_add(WRITER_DIED, order));
        assert!(!prev.writer_died);
        prev
    }
}

#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum SelfContainedChannelError {
    WriterIsDead,
}

impl Display for SelfContainedChannelError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            SelfContainedChannelError::WriterIsDead => write!(f, "WriterIsDead"),
        }
    }
}

impl Error for SelfContainedChannelError {
    fn source(&self) -> Option<&(dyn Error + 'static)> {
        None
    }
}

/// A fairly minimal channel implementation that implements the
/// [`vasi::VirtualAddressSpaceIndependent`] trait.
///
/// Only supports a single message at a time, and panics on unexpected state
/// transitions (e.g. attempting to send when there is already a pending message
/// in the channel).
///
/// Additionally, no attempt is made to ensure consistency after an operation
/// that panics.  e.g. if one side of a channel makes an API usage error that
/// causes a panic, the other side of the channel is likely to panic or
/// deadlock.
///
/// Loosely inspired by the channel implementation examples in "Rust Atomics and
/// Locks" by Mara Box (O'Reilly). Copyright 2023 Mara Box, 978-1-098-11944-7.
/// (From the preface: "You may use all example code offered with this book for
/// any purpose").
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
        self.state.transition_contents_state(
            ChannelContentsState::Empty,
            ChannelContentsState::Writing,
            sync::atomic::Ordering::Acquire,
        );
        unsafe { self.message.get_mut().deref().write(message) };
        let prev = self.state.transition_contents_state(
            ChannelContentsState::Writing,
            ChannelContentsState::Ready,
            sync::atomic::Ordering::Release,
        );
        if prev.has_sleeper {
            sync::futex_wake(&self.state.0).unwrap();
        }
    }

    /// Blocks until either the channel contains a message, or the writer has died.
    ///
    /// Returns `Ok(T)` if a message was received, or
    /// `Err(SelfContainedMutexError::WriterIsDead)` if the writer is dead.
    ///
    /// Panics if another thread is already trying to receive on this channel.
    pub fn receive(&self) -> Result<T, SelfContainedChannelError> {
        loop {
            let state = ChannelState::from(self.state.0.load(sync::atomic::Ordering::Relaxed));
            if state.contents_state == ChannelContentsState::Ready {
                break;
            }
            if state.writer_died {
                return Err(SelfContainedChannelError::WriterIsDead);
            }
            assert!(
                state.contents_state == ChannelContentsState::Empty
                    || state.contents_state == ChannelContentsState::Writing
            );
            let pre_sleeper_state = self.state.set_sleeper(sync::atomic::Ordering::Relaxed);
            if pre_sleeper_state != state {
                // Something changed; clear the sleeper bit and try again.
                self.state.clear_sleeper(sync::atomic::Ordering::Relaxed);
                continue;
            }
            let mut sleeper_state = pre_sleeper_state;
            sleeper_state.has_sleeper = true;
            let expected = sleeper_state.into();
            match sync::futex_wait(&self.state.0, expected) {
                Ok(_) | Err(nix::errno::Errno::EINTR) | Err(nix::errno::Errno::EAGAIN) => {
                    // Something changed; clear the sleeper bit and try again.
                    self.state.clear_sleeper(sync::atomic::Ordering::Relaxed);
                    continue;
                }
                Err(e) => panic!("Unexpected futex error {:?}", e),
            };
        }
        self.state.transition_contents_state(
            ChannelContentsState::Ready,
            ChannelContentsState::Reading,
            sync::atomic::Ordering::Acquire,
        );
        let val = unsafe { self.message.get_mut().deref().assume_init_read() };
        self.state.transition_contents_state(
            ChannelContentsState::Reading,
            ChannelContentsState::Empty,
            sync::atomic::Ordering::Release,
        );
        Ok(val)
    }

    /// Marks the writer as dead, causing `receive` to not block, and to return
    /// if it's already blocked. This is generally useful in conjunction with some type
    /// of "watchdog" that detects that the writer has died, such as
    /// `main::utility::ChildPidWatcher`.
    ///
    /// Panics if called more than once.
    pub fn set_writer_dead(&self) {
        let prev = self.state.set_writer_dead(sync::atomic::Ordering::Relaxed);
        if prev.has_sleeper {
            sync::futex_wake(&self.state.0).unwrap();
        }
    }
}

unsafe impl<T> Send for SelfContainedChannel<T> where T: Send {}
unsafe impl<T> Sync for SelfContainedChannel<T> where T: Send {}

// SAFETY: SelfContainedChannel is VirtualAddressSpaceIndependent as long as T is.
unsafe impl<T> VirtualAddressSpaceIndependent for SelfContainedChannel<T> where
    T: VirtualAddressSpaceIndependent
{
}

impl<T> Drop for SelfContainedChannel<T> {
    fn drop(&mut self) {
        // Conservatively use Acquire-ordering here to synchronize with the
        // Release-ordered store in `send`.
        //
        // This shouldn't be strictly necessary - for us to have a `&mut` reference, some
        // external synchronization must have already happened over the whole channel. Indeed
        // the original Box implementation uses get_mut here, which doesn't have an atomic
        // operation at all.
        let state = ChannelState::from(self.state.0.load(sync::atomic::Ordering::Acquire));
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
