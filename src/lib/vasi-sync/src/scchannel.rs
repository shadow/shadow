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
/// Breaking the documented API contracts may result both in immediate panics,
/// and panics in subsequent operations on the channel. To avoid this, the user
/// must:
/// * Never allow parallel `send` or `receive` operations. This is a single-producer,
/// single-consumer channel.
/// * Never call `send` when there is already a message pending.
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
        let mut state = self.state.load(sync::atomic::Ordering::Relaxed);
        loop {
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
            match sync::futex_wait(&self.state.0, expected) {
                Ok(_) | Err(nix::errno::Errno::EINTR) | Err(nix::errno::Errno::EAGAIN) => {
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

    /// Marks the writer as dead, causing `receive` to not block, and to return
    /// if it's already blocked. This is generally useful in conjunction with some type
    /// of "watchdog" that detects that the writer has died, such as
    /// `main::utility::ChildPidWatcher`.
    pub fn set_writer_dead(&self) {
        let prev = self
            .state
            .fetch_update(
                sync::atomic::Ordering::Relaxed,
                sync::atomic::Ordering::Relaxed,
                |mut state| {
                    state.writer_died = true;
                    Some(state)
                },
            )
            .unwrap();
        if prev.has_sleeper {
            sync::futex_wake(&self.state.0).unwrap();
        }
    }
}

unsafe impl<T> Send for SelfContainedChannel<T> where T: Send {}
unsafe impl<T> Sync for SelfContainedChannel<T> where T: Send {}

// TODO: Use the VirtualAddressSpaceIndependent Derive macro when it supports
// trait bounds.
//
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
