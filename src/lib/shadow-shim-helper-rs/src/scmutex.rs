use rkyv::{Archive, Serialize};
use std::{marker::PhantomData, ops::Deref, pin::Pin};
use vasi::VirtualAddressSpaceIndependent;

// When running under the loom model checker, use its sync primitives.
// See https://docs.rs/loom/latest/loom/
mod sync {
    // Use std sync primitives, or loom equivalents
    #[cfg(loom)]
    pub use loom::{
        sync::atomic,
        sync::atomic::{AtomicI32, AtomicU32, Ordering},
        sync::Arc,
    };
    #[cfg(not(loom))]
    pub use std::{
        sync::atomic,
        sync::atomic::{AtomicI32, AtomicU32, Ordering},
        sync::Arc,
    };

    // Map a *virtual* address to a list of Condvars. This doesn't support mapping into multiple
    // processes, or into different virtual addresses in the same process, etc.
    #[cfg(loom)]
    use loom::sync::{Condvar, Mutex};
    #[cfg(loom)]
    use std::collections::HashMap;
    #[cfg(loom)]
    loom::lazy_static! {
        pub static ref FUTEXES: Mutex<HashMap<usize, Vec<Arc<Condvar>>>> = Mutex::new(HashMap::new());
    }

    #[cfg(not(loom))]
    pub unsafe fn futex(
        futex_word: &AtomicI32,
        futex_op: i32,
        val: i32,
        timeout: *const libc::timespec,
        uaddr2: *mut i32,
        val3: i32,
    ) -> nix::Result<i64> {
        nix::errno::Errno::result(unsafe {
            libc::syscall(
                libc::SYS_futex,
                futex_word,
                futex_op,
                val,
                timeout,
                uaddr2,
                val3,
            )
        })
    }
    #[cfg(loom)]
    // Models the OS's futex primitive, using loom primitives.
    // Probably not as "nondeterministic" as the real futex syscall, but have to
    // start somewhere...
    pub unsafe fn futex(
        futex_word: &AtomicI32,
        futex_op: i32,
        val: i32,
        _timeout: *const libc::timespec,
        _uaddr2: *mut i32,
        _val3: i32,
    ) -> nix::Result<i64> {
        match futex_op {
            libc::FUTEX_WAIT => {
                let mut hashmap = FUTEXES.lock().unwrap();
                let futex_word_val = futex_word.load(Ordering::AcqRel);
                if futex_word_val != val {
                    return Err(nix::errno::Errno::EAGAIN);
                }
                let waiters = hashmap
                    .entry(futex_word as *const _ as usize)
                    .or_insert(Vec::new());
                let condvar = Arc::new(Condvar::new());
                waiters.push(condvar.clone());
                condvar.wait(hashmap).unwrap();
                Ok(0)
            }
            libc::FUTEX_WAKE => {
                let mut hashmap = FUTEXES.lock().unwrap();
                let Some(waiters) = hashmap.get_mut(&(futex_word as *const _ as usize)) else {
                    return Ok(0);
                };
                let to_wake = std::cmp::min(waiters.len(), val as usize);
                // XXX: Some way to randomize which threads get woken in what order?
                for _ in 0..to_wake {
                    waiters.pop().unwrap().notify_all();
                }
                Ok(to_wake as i64)
            }
            _ => Err(nix::errno::Errno::EINVAL),
        }
    }

    // From https://docs.rs/loom/latest/loom/#handling-loom-api-differences
    #[cfg(not(loom))]
    #[derive(Debug)]
    #[repr(transparent)]
    pub struct UnsafeCell<T>(std::cell::UnsafeCell<T>);
    #[cfg(not(loom))]
    impl<T> UnsafeCell<T> {
        pub fn new(data: T) -> UnsafeCell<T> {
            UnsafeCell(std::cell::UnsafeCell::new(data))
        }

        pub fn with<R>(&self, f: impl FnOnce(*const T) -> R) -> R {
            f(self.0.get())
        }

        pub fn with_mut<R>(&self, f: impl FnOnce(*mut T) -> R) -> R {
            f(self.0.get())
        }
    }
    #[cfg(loom)]
    pub use loom::cell::UnsafeCell;
}

#[cfg(loom)]
// Lets us clear global state in between loom iterations
pub fn loom_reset() {
    #[cfg(loom)]
    sync::FUTEXES.lock().unwrap().clear();
}

/// Simple mutex that is suitable for use in shared memory:
///
/// * It has a fixed layout (repr(C))
/// * It's self-contained
/// * Works across processes (e.g. doesn't use FUTEX_PRIVATE_FLAG)
#[repr(C)]
pub struct SelfContainedMutex<T> {
    futex: sync::AtomicI32,
    sleepers: sync::AtomicU32,
    val: sync::UnsafeCell<T>,
}

unsafe impl<T> Send for SelfContainedMutex<T> where T: Send {}
unsafe impl<T> Sync for SelfContainedMutex<T> where T: Send {}

// SAFETY: SelfContainedMutex is VirtualAddressSpaceIndependent as long as T is.
unsafe impl<T> VirtualAddressSpaceIndependent for SelfContainedMutex<T> where
    T: VirtualAddressSpaceIndependent
{
}

const UNLOCKED: i32 = 0;
const LOCKED: i32 = 1;
const LOCKED_DISCONNECTED: i32 = 2;

impl<T> SelfContainedMutex<T> {
    pub fn new(val: T) -> Self {
        Self {
            futex: sync::AtomicI32::new(UNLOCKED),
            sleepers: sync::AtomicU32::new(0),
            val: sync::UnsafeCell::new(val),
        }
    }

    pub fn lock(&self) -> SelfContainedMutexGuard<T> {
        loop {
            // Try to take the lock.
            //
            // We use `Acquire` on the failure path as well to ensure that the
            // `sleepers.fetch_add` is observed strictly after this operation.
            let prev = self.futex.compare_exchange(
                UNLOCKED,
                LOCKED,
                sync::Ordering::Acquire,
                sync::Ordering::Acquire,
            );
            let prev = match prev {
                Ok(_) => {
                    // We successfully took the lock.
                    break;
                }
                // We weren't able to take the lock.
                Err(i) => i,
            };
            // Mark ourselves as waiting on the futex. No need for stronger
            // ordering here; the Acquire on the failure path above ensures
            // this happens after.
            self.sleepers.fetch_add(1, sync::Ordering::Relaxed);
            // Sleep until unlocked.
            let res = unsafe {
                sync::futex(
                    &self.futex,
                    libc::FUTEX_WAIT,
                    prev,
                    std::ptr::null(),
                    std::ptr::null_mut(),
                    0,
                )
            };
            self.sleepers.fetch_sub(1, sync::Ordering::Relaxed);
            if res.is_err()
                && res != Err(nix::errno::Errno::EAGAIN)
                && res != Err(nix::errno::Errno::EINTR)
            {
                res.unwrap();
            }
        }
        SelfContainedMutexGuard {
            mutex: Some(self),
            _phantom: PhantomData,
        }
    }

    pub fn lock_pinned<'a>(self: Pin<&'a Self>) -> Pin<SelfContainedMutexGuard<'a, T>> {
        // SAFETY: `SelfContainedMutexGuard` doesn't provide DerefMut when `T`
        // is `!Unpin`.
        unsafe { Pin::new_unchecked(self.get_ref().lock()) }
    }

    fn unlock(&self) {
        self.futex.store(UNLOCKED, sync::Ordering::Release);

        // Acquire: Ensure that the `sleepers.load` below can't be moved to before
        // this fence (and therefore before the `futex.store` above)
        // Release: Ensure that the `futex.store` above can't be moved to after
        // this fence (and therefore not after the `sleepers.load`)
        sync::atomic::fence(sync::Ordering::AcqRel);

        // Only perform a FUTEX_WAKE operation if other threads are actually
        // sleeping on the lock.
        //
        // Another thread that's about to `FUTEX_WAIT` on the futex but that hasn't yet
        // incremented `sleepers` will not result in deadlock: since we've already released
        // the futex, their `FUTEX_WAIT` operation will fail, and the other thread will correctly
        // retry to take the lock instead of waiting.
        if self.sleepers.load(sync::Ordering::Acquire) > 0 {
            unsafe {
                sync::futex(
                    &self.futex,
                    libc::FUTEX_WAKE,
                    1,
                    std::ptr::null(),
                    std::ptr::null_mut(),
                    0,
                )
                .unwrap()
            };
        }
    }
}

pub struct SelfContainedMutexGuard<'a, T> {
    mutex: Option<&'a SelfContainedMutex<T>>,
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
            .compare_exchange(
                LOCKED,
                LOCKED_DISCONNECTED,
                sync::Ordering::Relaxed,
                sync::Ordering::Relaxed,
            )
            .unwrap();
        self.mutex.take();
    }

    /// Reconstitutes a guard that was previously disposed of via `disconnect`.
    ///
    /// Panics if the lock is no longer disconnected (i.e. if `reconnect` was
    /// already called).
    pub fn reconnect(mutex: &'a SelfContainedMutex<T>) -> Self {
        mutex
            .futex
            .compare_exchange(
                LOCKED_DISCONNECTED,
                LOCKED,
                sync::Ordering::Relaxed,
                sync::Ordering::Relaxed,
            )
            .unwrap();
        Self {
            mutex: Some(mutex),
            _phantom: PhantomData,
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
        guard.mutex.unwrap().val.with_mut(|ptr_t| {
            // SAFETY: The pointer is valid because it came from the mutex, which we know is live.
            // The mutex ensures there can be no other live references to the internal data.
            let ref_t: &mut T = unsafe { &mut *ptr_t };
            // SAFETY: We know the original data is pinned, since the guard was Pin<Self>.
            let pinned_t: Pin<&mut T> = unsafe { Pin::new_unchecked(ref_t) };
            f(pinned_t)
        })
    }
}

impl<'a, T> Drop for SelfContainedMutexGuard<'a, T> {
    fn drop(&mut self) {
        if let Some(mutex) = self.mutex {
            mutex.unlock();
        }
    }
}

impl<'a, T> std::ops::Deref for SelfContainedMutexGuard<'a, T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        self.mutex.unwrap().val.with(|ptr| unsafe { &*ptr })
    }
}

/// When T is Unpin, we can implement DerefMut. Otherwise it's unsafe
/// to do so, since SelfContainedMutex is an Archive type.
impl<'a, T> std::ops::DerefMut for SelfContainedMutexGuard<'a, T>
where
    T: Unpin,
{
    fn deref_mut(&mut self) -> &mut Self::Target {
        self.mutex.unwrap().val.with_mut(|ptr| unsafe { &mut *ptr })
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
        unsafe { std::ptr::addr_of_mut!((*out).futex).write(sync::AtomicI32::new(UNLOCKED)) };

        // Resolve the inner value
        let (val_offset, out_val_ptr_unsafe_cell) = rkyv::out_field!(out.val);
        // Because UnsafeCell is repr(transparent), we can cast it to the inner type.
        let out_val_ptr = out_val_ptr_unsafe_cell as *mut <T as Archive>::Archived;
        unsafe { lock.resolve(pos + val_offset, resolver, out_val_ptr) };
    }
}

// For unit tests see tests/scmutex-tests.rs
