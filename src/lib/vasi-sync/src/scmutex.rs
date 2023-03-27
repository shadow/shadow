use std::{marker::PhantomData, ops::Deref, pin::Pin};

use rkyv::{Archive, Serialize};
use vasi::VirtualAddressSpaceIndependent;

use crate::sync;

#[cfg_attr(not(loom), derive(VirtualAddressSpaceIndependent))]
#[repr(transparent)]
struct AtomicFutexWord(sync::atomic::AtomicU32);

impl AtomicFutexWord {
    pub fn new(val: FutexWord) -> Self {
        Self(sync::atomic::AtomicU32::new(val.into()))
    }

    pub fn inc_sleepers_and_fetch(&self, ord: sync::atomic::Ordering) -> FutexWord {
        // The number of sleepers is stored in the low bits of the futex word,
        // so we can increment the whole word.
        let prev = FutexWord::from(self.0.fetch_add(1, ord));

        // We'll panic here if we've overflowed she "sleepers" half of the word,
        // leaving the lock in a bad state. Since UNLOCKED is 0, this will never
        // cause a spurious unlock, but still-live threads using the lock
        // will likely panic or deadlock.
        FutexWord {
            lock_state: prev.lock_state,
            num_sleepers: prev.num_sleepers.checked_add(1).unwrap(),
        }
    }

    pub fn dec_sleepers_and_fetch(&self, ord: sync::atomic::Ordering) -> FutexWord {
        // The number of sleepers is stored in the low bits of the futex word,
        // so we can decrement the whole word.

        // Ideally we'd just use an atomic op on the "sleepers" part of the
        // larger word, but that sort of aliasing breaks loom's analysis.
        let prev = FutexWord::from(self.0.fetch_sub(1, ord));

        // We'll panic here if we've underflowed the "sleepers" half of the word,
        // leaving the lock in a bad state. This shouldn't be possible assuming
        // SelfContainedMutex itself isn't buggy.
        FutexWord {
            lock_state: prev.lock_state,
            num_sleepers: prev.num_sleepers.checked_sub(1).unwrap(),
        }
    }

    pub fn unlock_and_fetch(&self, ord: sync::atomic::Ordering) -> FutexWord {
        // We avoid having to synchronize the number of sleepers by using fetch_sub
        // instead of a compare and swap.
        debug_assert_eq!(UNLOCKED, 0);
        let prev = FutexWord::from(self.0.fetch_sub(
            u32::from(FutexWord {
                lock_state: LOCKED,
                num_sleepers: 0,
            }),
            ord,
        ));
        assert_eq!(prev.lock_state, LOCKED);
        FutexWord {
            lock_state: UNLOCKED,
            num_sleepers: prev.num_sleepers,
        }
    }

    pub fn disconnect(&self, ord: sync::atomic::Ordering) {
        // We avoid having to synchronize the number of sleepers by using fetch_add
        // instead of a compare and swap.
        //
        // We'll panic here if we've somehow underflowed the word. This
        // shouldn't be possible assuming SelfContainedMutex itself isn't buggy.
        let to_add = LOCKED_DISCONNECTED.checked_sub(LOCKED).unwrap();
        let prev = FutexWord::from(self.0.fetch_add(
            u32::from(FutexWord {
                lock_state: to_add,
                num_sleepers: 0,
            }),
            ord,
        ));
        assert_eq!(prev.lock_state, LOCKED);
    }

    pub fn load(&self, ord: sync::atomic::Ordering) -> FutexWord {
        self.0.load(ord).into()
    }

    pub fn compare_exchange(
        &self,
        current: FutexWord,
        new: FutexWord,
        success: sync::atomic::Ordering,
        failure: sync::atomic::Ordering,
    ) -> Result<FutexWord, FutexWord> {
        let raw_res = self
            .0
            .compare_exchange(current.into(), new.into(), success, failure);
        raw_res.map(FutexWord::from).map_err(FutexWord::from)
    }
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
struct FutexWord {
    lock_state: u16,
    num_sleepers: u16,
}

impl From<u32> for FutexWord {
    fn from(val: u32) -> Self {
        Self {
            lock_state: (val >> 16).try_into().unwrap(),
            num_sleepers: (val & 0xff_ff).try_into().unwrap(),
        }
    }
}

impl From<FutexWord> for u32 {
    fn from(val: FutexWord) -> Self {
        ((val.lock_state as u32) << 16) | (val.num_sleepers as u32)
    }
}

/// Simple mutex that is suitable for use in shared memory:
///
/// * It has a fixed layout (repr(C))
/// * It's self-contained; e.g. isn't boxed and doesn't refer
///   to global lock-state in this process's address space.
/// * Works across processes (e.g. doesn't use FUTEX_PRIVATE_FLAG)
///
/// Performance is optimized primarily for low-contention scenarios.
#[cfg_attr(not(loom), derive(VirtualAddressSpaceIndependent))]
#[repr(C)]
pub struct SelfContainedMutex<T> {
    futex: AtomicFutexWord,
    val: sync::UnsafeCell<T>,
}

unsafe impl<T> Send for SelfContainedMutex<T> where T: Send {}
unsafe impl<T> Sync for SelfContainedMutex<T> where T: Send {}

const UNLOCKED: u16 = 0;
const LOCKED: u16 = 1;
const LOCKED_DISCONNECTED: u16 = 2;

impl<T> SelfContainedMutex<T> {
    pub fn new(val: T) -> Self {
        Self {
            futex: AtomicFutexWord::new(FutexWord {
                lock_state: UNLOCKED,
                num_sleepers: 0,
            }),
            val: sync::UnsafeCell::new(val),
        }
    }

    pub fn lock(&self) -> SelfContainedMutexGuard<T> {
        // On first attempt, optimistically assume the lock is uncontended.
        let mut current = FutexWord {
            lock_state: UNLOCKED,
            num_sleepers: 0,
        };
        loop {
            if current.lock_state == UNLOCKED {
                // Try to take the lock.
                let current_res = self.futex.compare_exchange(
                    current,
                    FutexWord {
                        lock_state: LOCKED,
                        num_sleepers: current.num_sleepers,
                    },
                    sync::Ordering::Acquire,
                    sync::Ordering::Relaxed,
                );
                current = match current_res {
                    Ok(_) => {
                        // We successfully took the lock.
                        break;
                    }
                    // We weren't able to take the lock.
                    Err(i) => i,
                };
            }

            // If the lock is available, try again now that we've sync'd the
            // rest of the futex word (num_sleepers).
            if current.lock_state == UNLOCKED {
                continue;
            }

            // Try to sleep on the futex.

            // Since incrementing is a read-modify-write operation, this does
            // not break the release sequence since the last unlock.
            current = self.futex.inc_sleepers_and_fetch(sync::Ordering::Relaxed);
            loop {
                // We may now see an UNLOCKED state from having done the increment
                // above, or the load below.
                if current.lock_state == UNLOCKED {
                    break;
                }
                match sync::futex_wait(&self.futex.0, current.into()) {
                    Ok(_) | Err(nix::errno::Errno::EINTR) => break,
                    Err(nix::errno::Errno::EAGAIN) => {
                        // We may have gotten this because another thread is
                        // also trying to sleep on the futex, and just
                        // incremented the sleeper count. If we naively
                        // decremented the sleeper count and ran the whole lock
                        // loop again, both threads could theoretically end up
                        // in a live-lock where neither ever gets to sleep on
                        // the futex.
                        //
                        // To avoid that, we update our current view of the
                        // atomic and consider trying again before removing
                        // ourselves from the sleeper count.
                        current = self.futex.load(sync::Ordering::Relaxed)
                    }
                    Err(e) => panic!("Unexpected futex error {:?}", e),
                };
            }
            // Since decrementing is a read-modify-write operation, this does
            // not break the release sequence since the last unlock.
            current = self.futex.dec_sleepers_and_fetch(sync::Ordering::Relaxed);
        }
        SelfContainedMutexGuard {
            mutex: Some(self),
            ptr: Some(self.val.get_mut()),
            _phantom: PhantomData,
        }
    }

    pub fn lock_pinned<'a>(self: Pin<&'a Self>) -> Pin<SelfContainedMutexGuard<'a, T>> {
        // SAFETY: `SelfContainedMutexGuard` doesn't provide DerefMut when `T`
        // is `!Unpin`.
        unsafe { Pin::new_unchecked(self.get_ref().lock()) }
    }

    fn unlock(&self) {
        let current = self.futex.unlock_and_fetch(sync::Ordering::Release);

        // Only perform a FUTEX_WAKE operation if other threads are actually
        // sleeping on the lock.
        if current.num_sleepers > 0 {
            sync::futex_wake(&self.futex.0).unwrap();
        }
    }
}

pub struct SelfContainedMutexGuard<'a, T> {
    mutex: Option<&'a SelfContainedMutex<T>>,
    ptr: Option<sync::MutPtr<T>>,
    // For purposes of deriving Send, Sync, etc.,
    // this type should act as `&mut T`.
    _phantom: PhantomData<&'a mut T>,
}

impl<'a, T> SelfContainedMutexGuard<'a, T> {
    /// Drops the guard *without releasing the lock*.
    ///
    /// This is useful when a lock must be held across some span of code within
    /// a single thread, but it's difficult to pass the the guard between the
    /// two parts of the code.
    pub fn disconnect(mut self) {
        self.mutex
            .unwrap()
            .futex
            .disconnect(sync::Ordering::Relaxed);
        self.mutex.take();
        self.ptr.take();
    }

    /// Reconstitutes a guard that was previously disposed of via `disconnect`.
    ///
    /// Panics if the lock is not disconnected (i.e. if `reconnect` was
    /// already called).
    ///
    /// Ok to reconnect from a different thread,though some external
    /// synchronization may be needed to ensure the mutex is disconnected before
    /// it tries to do so.
    pub fn reconnect(mutex: &'a SelfContainedMutex<T>) -> Self {
        let mut current = FutexWord {
            lock_state: LOCKED_DISCONNECTED,
            num_sleepers: 0,
        };
        loop {
            assert_eq!(current.lock_state, LOCKED_DISCONNECTED);
            let current_res = mutex.futex.compare_exchange(
                current,
                FutexWord {
                    lock_state: LOCKED,
                    num_sleepers: current.num_sleepers,
                },
                sync::Ordering::Relaxed,
                sync::Ordering::Relaxed,
            );
            match current_res {
                Ok(_) => {
                    // Done.
                    return Self {
                        mutex: Some(mutex),
                        ptr: Some(mutex.val.get_mut()),
                        _phantom: PhantomData,
                    };
                }
                Err(c) => {
                    // Try again with updated state
                    current = c;
                }
            }
        }
    }

    /// Map the guard into a function of Pin<&mut T>.
    ///
    /// When T implements `Unpin`, the caller can just use deref_mut instead.
    ///
    // We can't provide an API that simply returns a Pin<&mut T>, since the Pin
    // API doesn't provide a way to get to the inner guard without consuming the outer Pin.
    pub fn map_pinned<F, O>(guard: Pin<Self>, f: F) -> O
    where
        F: FnOnce(Pin<&mut T>) -> O,
    {
        // SAFETY: We ensure that the &mut T made available from the unpinned guard isn't
        // moved-from, by only giving `f` access to a Pin<&mut T>.
        let guard: SelfContainedMutexGuard<T> = unsafe { Pin::into_inner_unchecked(guard) };
        // SAFETY: The pointer is valid because it came from the mutex, which we know is live.
        // The mutex ensures there can be no other live references to the internal data.
        let ref_t = unsafe { guard.ptr.as_ref().unwrap().deref() };
        // SAFETY: We know the original data is pinned, since the guard was Pin<Self>.
        let pinned_t: Pin<&mut T> = unsafe { Pin::new_unchecked(ref_t) };
        f(pinned_t)
    }
}

impl<'a, T> Drop for SelfContainedMutexGuard<'a, T> {
    fn drop(&mut self) {
        if let Some(mutex) = self.mutex {
            // We have to drop this pointer before unlocking when running
            // under loom, which could otherwise detect multiple mutable
            // references to the underlying cell. Under non loom, the drop
            // has no effect.
            #[allow(clippy::drop_non_drop)]
            drop(self.ptr.take());
            mutex.unlock();
        }
    }
}

impl<'a, T> std::ops::Deref for SelfContainedMutexGuard<'a, T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        // We can't call self.ptr.as_ref().unwrap().deref() here, since that
        // would create a `&mut T`, and there could already exist a `&T`
        // borrowed from `&self`.
        // https://github.com/tokio-rs/loom/issues/293
        self.ptr.as_ref().unwrap().with(|p| unsafe { &*p })
    }
}

/// When T is Unpin, we can implement DerefMut. Otherwise it's unsafe
/// to do so, since SelfContainedMutex is an Archive type.
impl<'a, T> std::ops::DerefMut for SelfContainedMutexGuard<'a, T>
where
    T: Unpin,
{
    fn deref_mut(&mut self) -> &mut Self::Target {
        unsafe { self.ptr.as_ref().unwrap().deref() }
    }
}

impl<S, T> rkyv::Serialize<S> for SelfContainedMutex<T>
where
    S: rkyv::Fallible + ?Sized,
    T: Archive + Serialize<S>,
{
    fn serialize(&self, serializer: &mut S) -> Result<Self::Resolver, S::Error> {
        let lock = self.lock();
        let res = lock.deref().serialize(serializer);
        if res.is_ok() {
            // We must hold the lock through Archive::resolve to ensure the
            // data doesn't change. However, we don't have a way to pass the
            // lock object through to Archive::resolve. We can't bundle it into
            // the Resolver object, because the associated traits don't support
            // a Resolver object with a lifetime bound.
            //
            // This addresses the soundness problem that rkyv::with::Lock has.
            // If and when rkyv changes their APIs to allow a nicer solution
            // there, we may able to apply it here too.
            // https://github.com/rkyv/rkyv/issues/309
            //
            // We solve this by dropping lock object *without releasing the
            // underlying lock*.
            lock.disconnect();
        }
        res
    }
}

impl<T> rkyv::Archive for SelfContainedMutex<T>
where
    T: rkyv::Archive,
{
    type Archived = SelfContainedMutex<<T as rkyv::Archive>::Archived>;
    type Resolver = <T as rkyv::Archive>::Resolver;

    unsafe fn resolve(&self, pos: usize, resolver: Self::Resolver, out: *mut Self::Archived) {
        // `self` should already have been locked in Serialize::Serialize, but the guard disconnected.
        // We reconstitute the guard here.
        let lock = SelfContainedMutexGuard::<T>::reconnect(self);

        // We're effectively cloning the original data, so always initialize the futex
        // into the unlocked state.
        unsafe {
            std::ptr::addr_of_mut!((*out).futex).write(AtomicFutexWord::new(FutexWord {
                lock_state: UNLOCKED,
                num_sleepers: 0,
            }))
        };

        // Resolve the inner value
        let (val_offset, out_val_ptr_unsafe_cell) = rkyv::out_field!(out.val);
        // Because UnsafeCell is repr(transparent), we can cast it to the inner type.
        let out_val_ptr = out_val_ptr_unsafe_cell as *mut <T as Archive>::Archived;
        unsafe { lock.resolve(pos + val_offset, resolver, out_val_ptr) };
    }
}

// For unit tests see tests/scmutex-tests.rs
