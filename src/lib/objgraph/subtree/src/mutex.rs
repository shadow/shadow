use std::{mem::MaybeUninit, sync::atomic::AtomicI32, ops::{Deref, DerefMut}, cell::UnsafeCell};

/// Safe wrapper around libc::pthread_mutex_t.
/// 
/// As with libc::pthread_mutex_t it is self-contained and FFI-safe (allowing it
/// to be stored in memory shared across processes). 
/// 
/// Also like libc::pthread_mutex_t, it cannot be safely moved once initialized.
#[repr(transparent)]
pub struct PthreadMutex {
    mutex: std::cell::UnsafeCell<libc::pthread_mutex_t>,
    // Mark this type as *not* safe to move once pinned.
    _unpin: std::marker::PhantomPinned,
}

impl PthreadMutex {
    /// Initialize the mutex and return a pinned pointer to it.
    /// 
    /// We only ever expose or use a pinned pointer to the mutex once
    /// initialized, since the internal libc::pthread_mutex_t cannot be safely
    /// moved.
    /// 
    /// # Safety
    /// 
    /// `mutex` must not have already been initialized.
    pub unsafe fn init(mutex: &mut std::mem::MaybeUninit<PthreadMutex>) -> std::pin::Pin<&Self> {
        let mut mutex_attr : libc::pthread_mutexattr_t = unsafe { std::mem::zeroed() };
        let rv = unsafe { libc::pthread_mutexattr_init(&mut mutex_attr)};
        assert_eq!(rv, 0);
        let rv = unsafe { libc::pthread_mutexattr_setpshared(&mut mutex_attr, libc::PTHREAD_PROCESS_SHARED )};
        assert_eq!(rv, 0);

        let mutex = unsafe { &mut *mutex.as_mut_ptr() };
        let pthread_mutex = mutex.mutex.get_mut();
        unsafe { libc::pthread_mutex_init(pthread_mutex, &mut mutex_attr)};

        unsafe { std::pin::Pin::new_unchecked(mutex) }
    }

    pub fn lock(self: std::pin::Pin<&Self>) {
        let mutex: &PthreadMutex = self.get_ref();
        let inner_mutex: &mut libc::pthread_mutex_t = unsafe { &mut *mutex.mutex.get() };
        unsafe { libc::pthread_mutex_lock(inner_mutex)};
    }

    pub fn unlock(self: std::pin::Pin<&Self>) {
        let mutex: &PthreadMutex = self.get_ref();
        let inner_mutex: &mut libc::pthread_mutex_t = unsafe { &mut *mutex.mutex.get() };
        unsafe { libc::pthread_mutex_unlock(inner_mutex)};
    }
}

mod test {
    use std::{mem::MaybeUninit};
    use super::*;
 
    #[test]
    fn test_mutex() {
        let mut storage = MaybeUninit::uninit();
        let mutex = unsafe { PthreadMutex::init(&mut storage) };
        mutex.lock();
        mutex.unlock();
    }
}

/*
#[repr(C)]
struct PthreadMutexWithData<T> {
    val: T,
    mutex_storage: MaybeUninit<PthreadMutex>,
    mutex: std::pin::Pin<&'static PthreadMutex>,
}

impl <T>PthreadMutexWithData<T> {
    /// Initialize the mutex and return a pinned pointer to it.
    /// 
    /// We only ever expose or use a pinned pointer to the mutex once
    /// initialized, since the internal libc::pthread_mutex_t cannot be safely
    /// moved.
    /// 
    /// # Safety
    /// 
    /// `mutex` must not have already been initialized.
    pub unsafe fn init(mutex: &mut std::mem::MaybeUninit<PthreadMutexWithData>, val: T) -> std::pin::Pin<&Self> {
        let mut res = mutex.write(Self {
            val,
            MaybeUninit::uninit(),
            mutex: 
        });
        PthreadMutex::init(mutex.as_mut_ptr())
        mutex.ma
        let mutex = unsafe { &mut *mutex.as_mut_ptr() };
        let pthread_mutex = mutex.mutex.get_mut();
        unsafe { libc::pthread_mutex_init(pthread_mutex, &mut mutex_attr)};

        unsafe { std::pin::Pin::new_unchecked(mutex) }
    }
}
*/

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

impl <T> SelfContainedMutex<T> {
    const LOCKED : i32 = 1;
    const UNLOCKED : i32 = 0;

    pub fn new(val: T) -> Self {
        Self {
            futex: AtomicI32::new(Self::UNLOCKED),
            val: UnsafeCell::new(val),
        }
    }

    unsafe fn futex(&self, futex_op: i32, val: i32, timeout: *const libc::timespec, uaddr2: *const i32, val3: i32) {
        let rv = unsafe { 
            libc::syscall(
                libc::SYS_futex,
                &self.futex,
                futex_op,
                val,
                timeout,
                uaddr2,
                val3) };
        todo!("check rv");
    }

    pub fn lock(&self) -> SelfContainedMutexGuard<T> {
        loop {
            // Try to take the lock.
            if self.futex.compare_exchange(Self::UNLOCKED, Self::LOCKED, std::sync::atomic::Ordering::Acquire, std::sync::atomic::Ordering::Relaxed).is_ok() {
                break;
            }
            // Sleep until unlocked.
            unsafe { self.futex(libc::FUTEX_WAIT, Self::LOCKED, std::ptr::null(), std::ptr::null(), 0) };
        }
        SelfContainedMutexGuard { mutex: self }
    }

    fn unlock(&self) {
        loop {
            // Try to take the lock.
            if self.futex.compare_exchange(Self::UNLOCKED, Self::LOCKED, std::sync::atomic::Ordering::Acquire, std::sync::atomic::Ordering::Relaxed).is_ok() {
                break;
            }
            // Sleep until unlocked.
            unsafe { self.futex(libc::FUTEX_WAIT, Self::LOCKED, std::ptr::null(), std::ptr::null(), 0) };
        }
    }
}

pub struct SelfContainedMutexGuard<'a, T> {
    mutex: &'a SelfContainedMutex<T>,
}

impl <'a, T> Drop for SelfContainedMutexGuard<'a, T> {
    fn drop(&mut self) {
        self.mutex.unlock();
    }
}

impl <'a, T> Deref for SelfContainedMutexGuard<'a, T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        unsafe { &*self.mutex.val.get() }
    }
}

impl <'a, T> DerefMut for SelfContainedMutexGuard<'a, T> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        unsafe { &mut *self.mutex.val.get() }
    }
}