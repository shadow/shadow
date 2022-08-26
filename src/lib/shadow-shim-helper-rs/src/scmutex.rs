use std::{
    cell::UnsafeCell,
    sync::atomic::{AtomicI32, Ordering}, ops::Deref,
};
use rkyv::{Archive, Serialize};

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
        uaddr2: *const i32,
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
            let prev = self
                .futex
                .compare_exchange(
                    UNLOCKED,
                    LOCKED,
                    Ordering::Acquire,
                    Ordering::Relaxed,
                );
            let prev = match prev {
                Ok(_) => {
                    // We successfully took the lock.
                    break;
                },
                // We weren't able to take the lock.
                Err(i) =>  i,
            };
            // Sleep until unlocked.
            let res = unsafe {
                self.futex(
                    libc::FUTEX_WAIT,
                    prev,
                    std::ptr::null(),
                    std::ptr::null(),
                    0,
                )
            };
            if res.is_err() && res != Err(nix::errno::Errno::EAGAIN) {
                res.unwrap();
            }
        }
        SelfContainedMutexGuard { mutex: Some(self) }
    }

    fn unlock(&self) {
        self.futex.store(UNLOCKED, Ordering::Release);
        unsafe {
            self.futex(libc::FUTEX_WAKE, 1, std::ptr::null(), std::ptr::null(), 0)
                .unwrap()
        };
    }
}

pub struct SelfContainedMutexGuard<'a, T> {
    mutex: Option<&'a SelfContainedMutex<T>>,
}

impl <'a, T> SelfContainedMutexGuard<'a, T> {
    /// Drops the guard *without releasing the lock*.
    /// 
    /// This is useful when a lock must be held across some span of code within
    /// a single thread, but it's difficult to pass the the guard between the
    /// two parts of the code.
    pub fn disconnect(mut self) {
        self.mutex.unwrap().
                futex
                .compare_exchange(
                    LOCKED,
                    LOCKED_DISCONNECTED,
                    Ordering::Relaxed,
                    Ordering::Relaxed,
                ).unwrap();
        self.mutex.take();
    }

    /// Reconstitutes a guard that was previously disposed of via `disconnect`.
    /// 
    /// Panics if the lock is no longer disconnected (i.e. if `reconnect` was
    /// already called).
    pub fn reconnect(mutex: &'a SelfContainedMutex<T>) -> Self {
        let s = Self {
            mutex: Some(mutex)
        };
        s.mutex.unwrap().
                futex
                .compare_exchange(
                    LOCKED_DISCONNECTED,
                    LOCKED,
                    Ordering::Relaxed,
                    Ordering::Relaxed,
                ).unwrap();
        s
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

impl<'a, T> std::ops::DerefMut for SelfContainedMutexGuard<'a, T> {
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

impl <S, T> rkyv::Serialize<S> for SelfContainedMutex<T>
where S: rkyv::Fallible + ?Sized, T: Archive + Serialize<S>
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
            // We solve this by dropping lock object *without releasing the
            // underlying lock*.
            lock.disconnect();
        }
        res
    }
}

impl <T> rkyv::Archive for SelfContainedMutex<T>
where T: rkyv::Archive
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
        let out_val_ptr = out_val_ptr_unsafe_cell as  *mut <T as Archive>::Archived;
        unsafe { lock.resolve(pos + val_offset, resolver, out_val_ptr)};
    }
}

#[cfg(test)]
mod rkyv_tests {
    use std::sync::Arc;

    use super::*;

    #[test]
    fn test_basic() {
        type T = SelfContainedMutex<i32>;
        let original_mutex: T = SelfContainedMutex::new(10);
        let mut bytes = rkyv::to_bytes::<_, 256>(&original_mutex).unwrap();

        // The archived mutex can be used to mutate the data in place.
        {
            let archived = unsafe { rkyv::archived_root_mut::<T>(std::pin::Pin::new(&mut bytes[..])) };
            let mut lock = archived.lock();
            assert_eq!(*lock, 10);
            *lock += 1;
        }

        // Re-constituting the archive should still give the new value.
        let archived = unsafe { rkyv::archived_root_mut::<T>(std::pin::Pin::new(&mut bytes[..])) };
        let lock = archived.lock();
        assert_eq!(*lock, 11);
    }

    // XXX fails under miri
    #[cfg(not(miri))]
    #[test]
    fn test_threads() {
        type T = SelfContainedMutex<i32>;
        let original_mutex: T = SelfContainedMutex::new(0);
        let bytes = Arc::new(rkyv::to_bytes::<_, 256>(&original_mutex).unwrap());

        let threads: Vec<_> = (0..100)
            .map(|_| {
                let bytes = bytes.clone();
                std::thread::spawn(move || {
                    let archived = unsafe { rkyv::archived_root::<T>(&bytes[..]) };
                    let mut guard = archived.lock();
                    *guard += 1;
                })
            })
            .collect();

        for thread in threads {
            thread.join().unwrap();
        }

        let archived = unsafe { rkyv::archived_root::<T>(&bytes[..]) };
        let guard = archived.lock();
        assert_eq!(*guard, 100);
    }


    // I thought maybe wrapping in an RwLock might be sufficient, even if we only take
    // reader locks, since the underlying data will then be wrapped in an UnsafeCell, so at 
    // some level is allowed to mutate.
    //
    // XXX Still fails under miri
    #[cfg(not(miri))]
    #[test]
    fn test_threads_rwlock_read() {
        type T = SelfContainedMutex<i32>;
        let original_mutex: T = SelfContainedMutex::new(0);
        let bytes = Arc::new(std::sync::RwLock::new(rkyv::to_bytes::<_, 256>(&original_mutex).unwrap()));

        let threads: Vec<_> = (0..100)
            .map(|_| {
                let bytes = bytes.clone();
                std::thread::spawn(move || {
                    let bytes_lock = bytes.read().unwrap();
                    let archived = unsafe { rkyv::archived_root::<T>(&bytes_lock[..]) };
                    let mut guard = archived.lock();
                    *guard += 1;
                })
            })
            .collect();

        for thread in threads {
            thread.join().unwrap();
        }

        let bytes_lock = bytes.read().unwrap();
        let archived = unsafe { rkyv::archived_root::<T>(&bytes_lock[..]) };
        let guard = archived.lock();
        assert_eq!(*guard, 100);
    }

    // Taking a writer lock over the buffer makes the test pass under miri.
    //
    // This doesn't reflect the intended actual usage, though, where the buffer will
    // be mmap'd into multiple processes, and each should treat the buffer as a whole
    // as immutable.
    #[test]
    fn test_threads_rwlock_write() {
        type T = SelfContainedMutex<i32>;
        let original_mutex: T = SelfContainedMutex::new(0);
        let bytes = Arc::new(std::sync::RwLock::new(rkyv::to_bytes::<_, 256>(&original_mutex).unwrap()));

        let threads: Vec<_> = (0..100)
            .map(|_| {
                let bytes = bytes.clone();
                std::thread::spawn(move || {
                    let mut bytes_lock = bytes.write().unwrap();
                    let archived = unsafe { rkyv::archived_root_mut::<T>(std::pin::Pin::new(&mut bytes_lock[..])) };
                    let mut guard = archived.lock();
                    *guard += 1;
                })
            })
            .collect();

        for thread in threads {
            thread.join().unwrap();
        }

        let mut bytes_lock = bytes.write().unwrap();
        let archived = unsafe { rkyv::archived_root_mut::<T>(std::pin::Pin::new(&mut bytes_lock[..])) };
        let guard = archived.lock();
        assert_eq!(*guard, 100);
    }
}