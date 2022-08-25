use std::{
    cell::UnsafeCell,
    sync::atomic::{AtomicI32, Ordering},
};

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

impl<T> SelfContainedMutex<T> {
    const LOCKED: i32 = 1;
    const UNLOCKED: i32 = 0;

    pub fn new(val: T) -> Self {
        Self {
            futex: AtomicI32::new(Self::UNLOCKED),
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
            if self
                .futex
                .compare_exchange(
                    Self::UNLOCKED,
                    Self::LOCKED,
                    Ordering::Acquire,
                    Ordering::Relaxed,
                )
                .is_ok()
            {
                break;
            }
            // Sleep until unlocked.
            unsafe {
                self.futex(
                    libc::FUTEX_WAIT,
                    Self::LOCKED,
                    std::ptr::null(),
                    std::ptr::null(),
                    0,
                )
                .unwrap()
            };
        }
        SelfContainedMutexGuard { mutex: self }
    }

    fn unlock(&self) {
        self.futex.store(Self::UNLOCKED, Ordering::Release);
        unsafe {
            self.futex(libc::FUTEX_WAKE, 1, std::ptr::null(), std::ptr::null(), 0)
                .unwrap()
        };
    }
}

pub struct SelfContainedMutexGuard<'a, T> {
    mutex: &'a SelfContainedMutex<T>,
}

impl<'a, T> Drop for SelfContainedMutexGuard<'a, T> {
    fn drop(&mut self) {
        self.mutex.unlock();
    }
}

impl<'a, T> std::ops::Deref for SelfContainedMutexGuard<'a, T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        unsafe { &*self.mutex.val.get() }
    }
}

impl<'a, T> std::ops::DerefMut for SelfContainedMutexGuard<'a, T> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        unsafe { &mut *self.mutex.val.get() }
    }
}

#[cfg(test)]
mod tests {
    use std::sync::Arc;

    use super::*;

    #[test]
    fn basic() {
        let mutex = SelfContainedMutex::new(0);
        let mut guard = mutex.lock();
        *guard += 1;
    }

    #[test]
    fn threads() {
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
