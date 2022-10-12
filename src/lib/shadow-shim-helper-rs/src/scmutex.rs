use rkyv::{Archive, Serialize};
use std::{
    cell::UnsafeCell,
    marker::PhantomData,
    ops::Deref,
    pin::Pin,
    sync::atomic::{AtomicI32, Ordering},
};
use vasi::VirtualAddressSpaceIndependent;

/// Simple mutex that is suitable for use in shared memory:
///
/// * It has a fixed layout (repr(C))
/// * It's self-contained
/// * Works across processes (e.g. doesn't use FUTEX_PRIVATE_FLAG)
#[repr(C)]
pub struct SelfContainedMutex<T> {
    futex: AtomicI32,
    val: UnsafeCell<T>,
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
            futex: AtomicI32::new(UNLOCKED),
            val: UnsafeCell::new(val),
        }
    }

    unsafe fn futex(
        &self,
        futex_op: i32,
        val: i32,
        timeout: *const libc::timespec,
        uaddr2: *mut i32,
        val3: i32,
    ) -> nix::Result<i64> {
        nix::errno::Errno::result(unsafe {
            libc::syscall(
                libc::SYS_futex,
                &self.futex,
                futex_op,
                val,
                timeout,
                uaddr2,
                val3,
            )
        })
    }

    pub fn lock(&self) -> SelfContainedMutexGuard<T> {
        loop {
            // Try to take the lock.
            let prev =
                self.futex
                    .compare_exchange(UNLOCKED, LOCKED, Ordering::Acquire, Ordering::Relaxed);
            let prev = match prev {
                Ok(_) => {
                    // We successfully took the lock.
                    break;
                }
                // We weren't able to take the lock.
                Err(i) => i,
            };
            // Sleep until unlocked.
            let res = unsafe {
                self.futex(
                    libc::FUTEX_WAIT,
                    prev,
                    std::ptr::null(),
                    std::ptr::null_mut(),
                    0,
                )
            };
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
        self.futex.store(UNLOCKED, Ordering::Release);
        unsafe {
            self.futex(
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
                Ordering::Relaxed,
                Ordering::Relaxed,
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
                Ordering::Relaxed,
                Ordering::Relaxed,
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
        let ptr_t: *mut T = guard.mutex.unwrap().val.get();
        // SAFETY: The pointer is valid because it came from the mutex, which we know is live.
        // The mutex ensures there can be no other live references to the internal data.
        let ref_t: &mut T = unsafe { &mut *ptr_t };
        // SAFETY: We know the original data is pinned, since the guard was Pin<Self>.
        let pinned_t: Pin<&mut T> = unsafe { Pin::new_unchecked(ref_t) };
        f(pinned_t)
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
        unsafe { &*self.mutex.unwrap().val.get() }
    }
}

/// When T is Unpin, we can implement DerefMut. Otherwise it's unsafe
/// to do so, since SelfContainedMutex is an Archive type.
impl<'a, T> std::ops::DerefMut for SelfContainedMutexGuard<'a, T>
where
    T: Unpin,
{
    fn deref_mut(&mut self) -> &mut Self::Target {
        unsafe { &mut *self.mutex.unwrap().val.get() }
    }
}

#[cfg(test)]
mod tests {
    use std::sync::Arc;

    use super::*;

    #[test]
    fn test_basic() {
        let mutex = SelfContainedMutex::new(0);
        let mut guard = mutex.lock();
        *guard += 1;
    }

    #[test]
    fn test_threads() {
        let mutex = Arc::new(SelfContainedMutex::new(0));

        let threads: Vec<_> = (0..100)
            .map(|_| {
                let mutex = mutex.clone();
                std::thread::spawn(move || {
                    let mut guard = mutex.lock();
                    *guard += 1;
                })
            })
            .collect();

        for thread in threads {
            thread.join().unwrap();
        }

        let guard = mutex.lock();
        assert_eq!(*guard, 100);
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
        unsafe { std::ptr::addr_of_mut!((*out).futex).write(AtomicI32::new(UNLOCKED)) };

        // Resolve the inner value
        let (val_offset, out_val_ptr_unsafe_cell) = rkyv::out_field!(out.val);
        // Because UnsafeCell is repr(transparent), we can cast it to the inner type.
        let out_val_ptr = out_val_ptr_unsafe_cell as *mut <T as Archive>::Archived;
        unsafe { lock.resolve(pos + val_offset, resolver, out_val_ptr) };
    }
}

#[cfg(test)]
mod rkyv_tests {
    use std::pin::Pin;
    use std::sync::Arc;

    use rkyv::string::ArchivedString;

    use super::*;

    /// Taking the AlignedVec here instead of a slice avoids a gray safety area
    /// that violates miri's current Stacked Borrows check.
    /// https://github.com/rkyv/rkyv/issues/303
    ///
    /// SAFETY:
    /// * `vec` must contain a value of type `T` at the end.
    unsafe fn archived_root<T>(vec: &rkyv::AlignedVec) -> &T::Archived
    where
        T: rkyv::Archive,
    {
        let pos = vec.len() - std::mem::size_of::<T::Archived>();
        // SAFETY:
        // * Caller guarantees that this memory contains a valid `T`
        // * The reference borrows from `vec`, ensuring that it can't
        //   be dropped or mutated.
        // * `AlignedVec::as_ptr` explicitly supports this use-case,
        //   including for `T` that has internal mutability via `UnsafeCell`.
        unsafe { &*vec.as_ptr().add(pos).cast() }
    }

    #[test]
    fn test_basic() {
        type T = SelfContainedMutex<i32>;
        let original_mutex: T = SelfContainedMutex::new(10);
        let bytes = rkyv::to_bytes::<_, 256>(&original_mutex).unwrap();

        // The archived mutex can be used to mutate the data in place.
        {
            let archived = unsafe { archived_root::<T>(&bytes) };
            let mut lock = archived.lock();
            assert_eq!(*lock, 10);
            *lock += 1;
        }

        // Re-constituting the archive should still give the new value.
        let archived = unsafe { archived_root::<T>(&bytes) };
        let lock = archived.lock();
        assert_eq!(*lock, 11);
    }

    #[test]
    fn test_basic_compound() {
        type T = [SelfContainedMutex<(i32, i32)>; 2];
        let original_mutexes: T = [
            SelfContainedMutex::new((1, 2)),
            SelfContainedMutex::new((3, 4)),
        ];
        let bytes = rkyv::to_bytes::<_, 256>(&original_mutexes).unwrap();

        let archived = unsafe { archived_root::<T>(&bytes) };
        assert_eq!(archived[0].lock().0, 1);
        assert_eq!(archived[0].lock().1, 2);
        assert_eq!(archived[1].lock().0, 3);
        assert_eq!(archived[1].lock().1, 4);
    }

    #[test]
    fn test_inner_not_unpin() {
        type T = SelfContainedMutex<String>;
        let original_mutex: T = SelfContainedMutex::new(String::from("test"));
        let mut bytes = rkyv::to_bytes::<_, 256>(&original_mutex).unwrap();

        {
            let archived: &SelfContainedMutex<ArchivedString> =
                unsafe { archived_root::<T>(&bytes) };
            // Because `ArchivedString` is `!Unpin`, we need to pin the mutex for it to allow
            // mutable access.
            //
            // SAFETY: We never move the underlying data (e.g. by mutating `bytes`)
            let archived = unsafe { Pin::new_unchecked(archived) };
            let lock = archived.lock_pinned();
            assert_eq!(*lock, "test");

            // Because `ArchivedString` is `!Unpin`, we can't directly access the mutable
            // reference to it. We need to use `map_pinned` instead.
            SelfContainedMutexGuard::map_pinned(lock, |strref| {
                let mut strref = strref.pin_mut_str();
                strref.make_ascii_uppercase();
                assert_eq!(&*strref, "TEST");
            });
        }

        // Re-constituting the archive should still give the new value.
        let archived = unsafe { rkyv::archived_root_mut::<T>(std::pin::Pin::new(&mut bytes[..])) };
        let lock = archived.lock();
        assert_eq!(*lock, "TEST");
    }

    #[test]
    fn test_threads() {
        type T = SelfContainedMutex<i32>;
        let original_mutex: T = SelfContainedMutex::new(0);
        let bytes = Arc::new(rkyv::to_bytes::<_, 256>(&original_mutex).unwrap());

        let threads: Vec<_> = (0..100)
            .map(|_| {
                let bytes = bytes.clone();
                std::thread::spawn(move || {
                    // No need to pin here, since i32 implements Unpin.
                    let archived = unsafe { archived_root::<T>(&bytes) };
                    let mut guard = archived.lock();
                    *guard += 1;
                })
            })
            .collect();

        for thread in threads {
            thread.join().unwrap();
        }

        let archived = unsafe { archived_root::<T>(&bytes) };
        let guard = archived.lock();
        assert_eq!(*guard, 100);
    }
}
