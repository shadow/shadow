use std::error::Error;
use std::fmt::Display;
use std::mem::MaybeUninit;

use vasi::VirtualAddressSpaceIndependent;

use crate::sync::{self, AtomicBool, AtomicU32, UnsafeCell};

#[derive(Debug, Copy, Clone, Eq, PartialEq, VirtualAddressSpaceIndependent)]
#[repr(u8)]
enum ChannelContentsState {
    Empty,
    Ready,
}

impl From<u8> for ChannelContentsState {
    fn from(value: u8) -> Self {
        const EMPTY: u8 = ChannelContentsState::Empty as u8;
        const READY: u8 = ChannelContentsState::Ready as u8;
        match value {
            EMPTY => ChannelContentsState::Empty,
            READY => ChannelContentsState::Ready,
            _ => panic!("Bad value {value}"),
        }
    }
}

#[derive(Debug, Eq, PartialEq, Copy, Clone, VirtualAddressSpaceIndependent)]
struct ChannelState {
    contents_state: ChannelContentsState,
}

impl From<u32> for ChannelState {
    fn from(value: u32) -> Self {
        let contents_state = ((value & 0xff) as u8).into();
        ChannelState { contents_state }
    }
}

impl From<ChannelState> for u32 {
    fn from(value: ChannelState) -> Self {
        value.contents_state as u32
    }
}

#[cfg_attr(not(loom), derive(VirtualAddressSpaceIndependent))]
struct AtomicChannelState(AtomicU32);
impl AtomicChannelState {
    pub fn new() -> Self {
        Self(AtomicU32::new(
            ChannelState {
                contents_state: ChannelContentsState::Empty,
            }
            .into(),
        ))
    }

    /// Typed interface to `AtomicU32::load`
    fn load(&self, order: sync::atomic::Ordering) -> ChannelState {
        ChannelState::from(self.0.load(order))
    }

    /// Typed interface to `AtomicU32::store`
    fn store(&self, value: ChannelState, order: sync::atomic::Ordering) {
        self.0.store(value.into(), order)
    }
}

#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum SelfContainedChannelError {
    WriterIsClosed,
}

impl Display for SelfContainedChannelError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            SelfContainedChannelError::WriterIsClosed => write!(f, "WriterIsClosed"),
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
#[cfg_attr(not(loom), derive(VirtualAddressSpaceIndependent))]
pub struct SelfContainedChannel<T> {
    message: UnsafeCell<MaybeUninit<T>>,
    state: AtomicChannelState,
    has_sleeper: AtomicBool,
    writer_closed: AtomicBool,
}

impl<T> SelfContainedChannel<T> {
    pub fn new() -> Self {
        Self {
            message: UnsafeCell::new(MaybeUninit::uninit()),
            state: AtomicChannelState::new(),
            has_sleeper: AtomicBool::new(false),
            writer_closed: AtomicBool::new(false),
        }
    }

    /// Sends `message` through the channel.
    ///
    /// # Safety
    ///
    /// Channel must be empty, and there must not be a parallel call to `send`.
    /// 
    /// XXX: Must not be closed? (message will leak, but otherwise not unsound)
    pub unsafe fn send(&self, message: T) {
        unsafe { self.message.get_mut().deref().as_mut_ptr().write(message) };
        self.state
            .store(ChannelState{ contents_state: ChannelContentsState::Ready }, sync::atomic::Ordering::Relaxed);
        sync::atomic::fence(sync::atomic::Ordering::SeqCst);
        if self.has_sleeper.load(sync::atomic::Ordering::Relaxed) {
            sync::futex_wake(&self.state.0).unwrap();
        }
    }

    /// Blocks until either the channel contains a message, or the writer has
    /// closed the channel.
    ///
    /// Returns `Ok(T)` if a message was received, or
    /// `Err(SelfContainedMutexError::WriterIsClosed)` if the writer is closed.
    ///
    /// # Safety
    ///
    /// There must be no parallel call to `self.receive`.
    pub unsafe fn receive(&self) -> Result<T, SelfContainedChannelError> {
        let mut state = self.state.load(sync::atomic::Ordering::Relaxed);
        loop {
            if state.contents_state == ChannelContentsState::Ready {
                break;
            }
            assert!(state.contents_state == ChannelContentsState::Empty);

            sync::atomic::fence(sync::atomic::Ordering::SeqCst);

            self.has_sleeper
                .store(true, sync::atomic::Ordering::Relaxed);

            // Unclear whether this fence is strictly necessary, or whether the futex operation
            // already effectively has one. Our loom model of futex conservatively doesn't
            // have such a fence, so a fence is required here to ensure consistent ordering
            // with the other atomic operations above.
            sync::atomic::fence(sync::atomic::Ordering::SeqCst);

            let res = sync::futex_wait(&self.state.0, state.into());

            self.has_sleeper
                .store(false, sync::atomic::Ordering::Relaxed);

            match res {
                Ok(_) | Err(nix::errno::Errno::EINTR) | Err(nix::errno::Errno::EAGAIN) => {
                    // Something changed; try again.
                    state = self.state.load(sync::atomic::Ordering::Relaxed);
                    continue;
                }
                Err(e) => panic!("Unexpected futex error {:?}", e),
            };
        }
        // We use an Acquire fence here instead of making every load above
        // have Acquire ordering.
        sync::atomic::fence(sync::atomic::Ordering::Acquire);
        sync::atomic::fence(sync::atomic::Ordering::SeqCst);
        if self.writer_closed.load(sync::atomic::Ordering::Relaxed) {
            return Err(SelfContainedChannelError::WriterIsClosed);
        }
        let val = unsafe { self.message.get_mut().deref().assume_init_read() };
        self.state.store(ChannelState { contents_state: ChannelContentsState::Empty }, sync::atomic::Ordering::Release);
        Ok(val)
    }

    /// Closes the "write" end of the channel. This will cause any current
    /// and subsequent `receive` operations to fail once the channel is empty.
    ///
    /// This method *can* be called in parallel with other methods on the
    /// channel, making it suitable to be called from a separate watchdog thread
    /// that detects that the writing thread (or process) has died.
    pub fn close_writer(&self) {
        self.writer_closed
            .store(true, sync::atomic::Ordering::Relaxed);
        sync::atomic::fence(sync::atomic::Ordering::SeqCst);
        self.state
            .store(ChannelState{ contents_state: ChannelContentsState::Ready }, sync::atomic::Ordering::Relaxed);
        sync::atomic::fence(sync::atomic::Ordering::SeqCst);
        if self.has_sleeper.load(sync::atomic::Ordering::Relaxed) {
            sync::futex_wake(&self.state.0).unwrap();
        }
    }
}

unsafe impl<T> Send for SelfContainedChannel<T> where T: Send {}
unsafe impl<T> Sync for SelfContainedChannel<T> where T: Send {}

impl<T> Drop for SelfContainedChannel<T> {
    fn drop(&mut self) {
        sync::atomic::fence(sync::atomic::Ordering::SeqCst);
        let writer_closed= self.writer_closed.load(sync::atomic::Ordering::Relaxed);
        let state = self.state.load(sync::atomic::Ordering::Relaxed);
        if state.contents_state == ChannelContentsState::Ready && !writer_closed {
            unsafe { self.message.get_mut().deref().assume_init_drop() }
        }
    }
}

impl<T> Default for SelfContainedChannel<T> {
    fn default() -> Self {
        Self::new()
    }
}
