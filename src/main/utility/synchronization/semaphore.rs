use std::cell::UnsafeCell;
use std::sync::Arc;

/// A libc semaphore that provides synchronization between threads. Any memory barrier/ordering
/// properties are the same as provided by [`libc::sem_post`] and [`libc::sem_wait`].
///
/// We could use a third-party semaphore library, but most seem to not have the semantics we need (a
/// simple signaling mechanism with the ability to block). Other libraries are either meant for an
/// async context, don't have the ability to block/wait, or are intended to protect some resource
/// like a lock. We also are already familiar with the performance charactersitics of the libc
/// semaphore.
#[derive(Clone)]
pub struct LibcSemaphore {
    // SAFETY: the `LibcSemWrapper` must not be moved
    inner: Arc<LibcSemWrapper>,
}

impl LibcSemaphore {
    /// Create a new semaphore. See `sem_init(3)` for details.
    pub fn new(val: libc::c_uint) -> Self {
        let rv = Self {
            // this will move the LibcSemWrapper into the Arc, but that's fine since we haven't
            // initialized it yet
            inner: Arc::new(LibcSemWrapper::new()),
        };

        // SAFETY: do not wait, post, get, format/print, etc here before it's initialized

        // the LibcSemWrapper is in the Arc and we will never move it again
        unsafe { rv.inner.init(val) };

        rv
    }

    /// Lock the semaphore. See `sem_wait(3)` for details.
    pub fn wait(&self) {
        unsafe { self.inner.wait() }
    }

    /// Unlock the semaphore. See `sem_post(3)` for details.
    pub fn post(&self) {
        unsafe { self.inner.post() }
    }

    /// Get the semaphore value. See `sem_getvalue(3)` for details.
    pub fn get(&self) -> libc::c_int {
        unsafe { self.inner.get() }
    }
}

impl std::fmt::Debug for LibcSemaphore {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("LibcSemaphore")
            // it should have been initialized by this point
            .field("value", unsafe { &self.inner.get() })
            .finish()
    }
}

/// A wrapper for a [`libc::sem_t`].
struct LibcSemWrapper {
    // SAFETY: the `sem_t` must not be moved
    inner: UnsafeCell<libc::sem_t>,
}

unsafe impl Send for LibcSemWrapper {}
unsafe impl Sync for LibcSemWrapper {}

impl LibcSemWrapper {
    /// Create a new libc semaphore. After this semaphore object is moved to its final memory
    /// location, it must be initialized using [`init`].
    pub fn new() -> Self {
        Self {
            inner: UnsafeCell::new(unsafe { std::mem::zeroed() }),
        }
        // we cannot call sem_init() here since we will be copying/moving Self (and the sem_t) out
        // of this function
    }

    /// Initialize the semaphore. See `sem_init(3)` for details.
    ///
    /// SAFETY:
    ///   - this must not be called more than once
    pub unsafe fn init(&self, val: libc::c_uint) {
        unsafe { libc::sem_init(self.inner.get(), 0, val) };
    }

    /// Lock the semaphore. See `sem_wait(3)` for details.
    ///
    /// SAFETY:
    ///   - the `LibcSemWrapper` must have been initialized using [`init`]
    ///   - the `LibcSemWrapper` must not have been moved between the call to [`init`] and here
    pub unsafe fn wait(&self) {
        loop {
            if unsafe { libc::sem_wait(self.inner.get()) } == 0 {
                break;
            }

            match std::io::Error::last_os_error().kind() {
                std::io::ErrorKind::Interrupted => {}
                e => panic!("Unexpected semaphore wait error: {e}"),
            }
        }
    }

    /// Unlock the semaphore. See `sem_post(3)` for details.
    ///
    /// SAFETY:
    ///   - the `LibcSemWrapper` must have been initialized using [`init`]
    ///   - the `LibcSemWrapper` must not have been moved between the call to [`init`] and here
    pub unsafe fn post(&self) {
        if unsafe { libc::sem_post(self.inner.get()) } == 0 {
            return;
        }

        panic!(
            "Unexpected semaphore post error: {}",
            std::io::Error::last_os_error().kind()
        );
    }

    /// Get the semaphore value. See `sem_getvalue(3)` for details.
    ///
    /// SAFETY:
    ///   - the `LibcSemWrapper` must have been initialized using [`init`]
    ///   - the `LibcSemWrapper` must not have been moved between the call to [`init`] and here
    pub unsafe fn get(&self) -> libc::c_int {
        let mut val = 0;
        if unsafe { libc::sem_getvalue(self.inner.get(), &mut val) } == 0 {
            return val;
        }

        panic!(
            "Unexpected semaphore post error: {}",
            std::io::Error::last_os_error().kind()
        );
    }
}

impl std::ops::Drop for LibcSemWrapper {
    fn drop(&mut self) {
        unsafe { libc::sem_destroy(self.inner.get()) };
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    #[cfg_attr(miri, ignore)]
    fn test_clone() {
        let sem = LibcSemaphore::new(0);
        let sem_clone = sem.clone();

        assert_eq!(sem.get(), 0);
        sem.post();
        assert_eq!(sem.get(), 1);
        sem_clone.wait();
        assert_eq!(sem.get(), 0);
    }

    #[test]
    #[cfg_attr(miri, ignore)]
    fn test_single_thread() {
        let sem = LibcSemaphore::new(0);
        sem.post();
        sem.wait();

        let sem = LibcSemaphore::new(0);
        sem.post();
        sem.post();
        sem.post();
        sem.wait();
        sem.wait();
        sem.wait();

        let sem = LibcSemaphore::new(3);
        sem.wait();
        sem.wait();
        sem.wait();

        let sem = LibcSemaphore::new(0);
        sem.post();
        sem.wait();
        sem.post();
        sem.wait();
        sem.post();
        sem.wait();
    }

    #[test]
    #[cfg_attr(miri, ignore)]
    fn test_multi_thread() {
        let sem = LibcSemaphore::new(0);
        let sem_clone = sem.clone();

        let t0 = std::time::Instant::now();

        let handle = std::thread::spawn(move || {
            sem_clone.post();
            std::thread::sleep(std::time::Duration::from_millis(50));
            sem_clone.post();
        });

        sem.wait();
        let elapsed = t0.elapsed().as_millis();
        assert!(
            (0..30).contains(&elapsed),
            "Unexpected elapsed time: {elapsed}"
        );

        sem.wait();
        let elapsed = t0.elapsed().as_millis();
        assert!(
            (50..80).contains(&elapsed),
            "Unexpected elapsed time: {elapsed}"
        );

        handle.join().unwrap();
    }
}
