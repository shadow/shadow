//! Synchronization primitives that are modeled in loom
//!
//! This module provides some very low-level primitives, such as atomics,
//! and futex. When testing under loom they model the corresponding operation
//! in loom instead of executing it natively.

// Use std sync primitives, or loom equivalents
#[cfg(not(loom))]
pub use core::{
    sync::atomic,
    sync::atomic::{AtomicBool, AtomicI32, AtomicI8, AtomicU32, AtomicUsize, Ordering},
};
#[cfg(loom)]
use std::collections::HashMap;

#[cfg(not(loom))]
pub use core::cell::Cell;
#[cfg(loom)]
pub use loom::cell::Cell;

// Map a *virtual* address to a list of Condvars. This doesn't support mapping into multiple
// processes, or into different virtual addresses in the same process, etc.
#[cfg(loom)]
use loom::sync::{Condvar, Mutex};
#[cfg(loom)]
pub use loom::{
    sync::atomic,
    sync::atomic::{AtomicBool, AtomicI32, AtomicI8, AtomicU32, AtomicUsize, Ordering},
    sync::Arc,
};
#[cfg(not(loom))]
use vasi::VirtualAddressSpaceIndependent;
#[cfg(loom)]
loom::lazy_static! {
    pub static ref FUTEXES: Mutex<HashMap<usize, Arc<Condvar>>> = Mutex::new(HashMap::new());
}

#[cfg(not(loom))]
pub fn sched_yield() {
    rustix::process::sched_yield();
}
#[cfg(loom)]
pub fn sched_yield() {
    loom::thread::yield_now();
}

// Rustix doesn't define its `FutexOperation` type under miri, so we can't use it in
// our interfaces. Use our own type and translate in our futex "backends".
enum FutexOperation {
    Wait,
    Wake,
}

#[cfg(not(loom))]
unsafe fn futex(
    futex_word: &AtomicU32,
    futex_operation: FutexOperation,
    val: u32,
) -> rustix::io::Result<usize> {
    #[cfg(not(miri))]
    {
        let futex_operation = match futex_operation {
            FutexOperation::Wait => rustix::thread::FutexOperation::Wait,
            FutexOperation::Wake => rustix::thread::FutexOperation::Wake,
        };

        unsafe {
            rustix::thread::futex(
                futex_word.as_ptr(),
                futex_operation,
                rustix::thread::FutexFlags::empty(),
                val,
                core::ptr::null(),
                core::ptr::null_mut(),
                0u32,
            )
        }
    }
    // Rustix doesn't include `futex` at all under miri. miri understands
    // futex syscalls made through libc.
    #[cfg(miri)]
    {
        let futex_operation = match futex_operation {
            FutexOperation::Wait => libc::FUTEX_WAIT,
            FutexOperation::Wake => libc::FUTEX_WAKE,
        };
        let rv = unsafe {
            libc::syscall(
                libc::SYS_futex,
                futex_word.as_ptr(),
                futex_operation,
                val,
                core::ptr::null() as *const libc::timespec,
                core::ptr::null_mut() as *mut u32,
                0u32,
            )
        };
        if rv >= 0 {
            Ok(rv.try_into().unwrap())
        } else {
            Err(rustix::io::Errno::from_raw_os_error(unsafe {
                *libc::__errno_location()
            }))
        }
    }
}

#[inline]
pub fn futex_wait(futex_word: &AtomicU32, val: u32) -> rustix::io::Result<usize> {
    // In "production" we use linux_syscall to avoid going through libc, and to
    // avoid touching libc's `errno` in particular.
    #[cfg(not(loom))]
    {
        unsafe { futex(futex_word, FutexOperation::Wait, val) }
    }
    #[cfg(loom)]
    {
        // From futex(2):
        //   This load, the comparison with the expected value, and starting to
        //   sleep are performed atomically and totally ordered with
        //   respect to other futex operations on the same futex word.
        //
        // We hold a lock on our FUTEXES to represent this.
        // TODO: If we want to run loom tests with multiple interacting locks,
        // we should have per-futex mutexes here, and not hold a lock over the
        // whole list the whole time.
        let mut hashmap = FUTEXES.lock().unwrap();
        let futex_word_val = futex_word.load(Ordering::Relaxed);
        if futex_word_val != val {
            return Err(rustix::io::Errno::AGAIN);
        }
        let condvar = hashmap
            .entry(futex_word as *const _ as usize)
            .or_insert(Arc::new(Condvar::new()))
            .clone();
        // We could get a spurious wakeup here, but that's ok.
        // Futexes are subject to spurious wakeups too.
        condvar.wait(hashmap).unwrap();
        Ok(0)
    }
}

#[inline]
pub fn futex_wake_one(futex_word: &AtomicU32) -> rustix::io::Result<()> {
    #[cfg(not(loom))]
    {
        unsafe { futex(futex_word, FutexOperation::Wake, 1) }.map(|_| ())
    }
    // loom doesn't understand syscalls; emulate via loom primitives.
    #[cfg(loom)]
    {
        let hashmap = FUTEXES.lock().unwrap();
        let Some(condvar) = hashmap.get(&(futex_word as *const _ as usize)) else {
            return Ok(());
        };
        condvar.notify_one();
        Ok(())
    }
}

#[inline]
pub fn futex_wake_all(futex_word: &AtomicU32) -> rustix::io::Result<()> {
    #[cfg(not(loom))]
    {
        // u32::MAX seems like it'd make sense here, but the man page says to use INT_MAX.
        // Better to go with that than risk some unexpected behavior...
        unsafe {
            futex(
                futex_word,
                FutexOperation::Wake,
                u32::try_from(i32::MAX).unwrap(),
            )
        }
        .map(|_| ())
    }
    // loom doesn't understand syscalls; emulate via loom primitives.
    #[cfg(loom)]
    {
        let hashmap = FUTEXES.lock().unwrap();
        let Some(condvar) = hashmap.get(&(futex_word as *const _ as usize)) else {
            return Ok(());
        };
        condvar.notify_all();
        Ok(())
    }
}

#[cfg(not(loom))]
pub struct MutPtr<T: ?Sized>(*mut T);
#[cfg(not(loom))]
impl<T: ?Sized> MutPtr<T> {
    /// # Safety
    ///
    /// See `loom::cell::MutPtr::deref`.
    #[inline]
    #[allow(clippy::mut_from_ref)]
    pub unsafe fn deref(&self) -> &mut T {
        unsafe { &mut *self.0 }
    }

    #[inline]
    pub fn with<F, R>(&self, f: F) -> R
    where
        F: FnOnce(*mut T) -> R,
    {
        f(self.0)
    }
}
// We have to wrap loom's MutPtr as well, since it's otherwise !Send.
// https://github.com/tokio-rs/loom/issues/294
#[cfg(loom)]
pub struct MutPtr<T: ?Sized>(loom::cell::MutPtr<T>);
#[cfg(loom)]
impl<T: ?Sized> MutPtr<T> {
    #[inline]
    #[allow(clippy::mut_from_ref)]
    pub unsafe fn deref(&self) -> &mut T {
        unsafe { self.0.deref() }
    }

    #[inline]
    pub fn with<F, R>(&self, f: F) -> R
    where
        F: FnOnce(*mut T) -> R,
    {
        self.0.with(f)
    }
}

unsafe impl<T: ?Sized> Send for MutPtr<T> where T: Send {}

#[cfg(not(loom))]
pub struct ConstPtr<T: ?Sized>(*const T);
#[cfg(not(loom))]
impl<T: ?Sized> ConstPtr<T> {
    /// # Safety
    ///
    /// See `loom::cell::ConstPtr::deref`.
    pub unsafe fn deref(&self) -> &T {
        unsafe { &*self.0 }
    }

    pub fn with<F, R>(&self, f: F) -> R
    where
        F: FnOnce(*const T) -> R,
    {
        f(self.0)
    }
}

#[cfg(loom)]
pub use loom::cell::ConstPtr;

/// From <https://docs.rs/loom/latest/loom/#handling-loom-api-differences>
#[cfg(not(loom))]
#[derive(Debug, VirtualAddressSpaceIndependent)]
#[repr(transparent)]
pub struct UnsafeCell<T>(core::cell::UnsafeCell<T>);
#[cfg(not(loom))]
impl<T> UnsafeCell<T> {
    #[inline]
    pub const fn new(data: T) -> UnsafeCell<T> {
        UnsafeCell(core::cell::UnsafeCell::new(data))
    }

    /// Note that this has a different signature from the method
    /// of the same name in `core::cell::UnsafeCell`.
    /// See <https://docs.rs/loom/latest/loom/#handling-loom-api-differences>
    #[inline]
    pub fn get_mut(&self) -> MutPtr<T> {
        MutPtr(self.0.get())
    }

    #[inline]
    pub fn get(&self) -> ConstPtr<T> {
        ConstPtr(self.0.get())
    }

    /// This is analogous to `core::UnsafeCell::get` in that it returns
    /// a raw pointer instead of an object.
    ///
    /// We can't provide this method under loom without giving up some of loom's
    /// analysis.
    pub fn untracked_get(&self) -> *mut T {
        self.0.get()
    }
}
#[cfg(loom)]
#[derive(Debug)]
pub struct UnsafeCell<T>(loom::cell::UnsafeCell<T>);
#[cfg(loom)]
impl<T> UnsafeCell<T> {
    // TODO: make this `const` if and when loom's UnsafeCell supports a const new.
    pub fn new(data: T) -> UnsafeCell<T> {
        UnsafeCell(loom::cell::UnsafeCell::new(data))
    }

    pub fn get_mut(&self) -> MutPtr<T> {
        MutPtr(self.0.get_mut())
    }

    pub fn get(&self) -> ConstPtr<T> {
        self.0.get()
    }
}

/// Lets us clear global state in between loom iterations, in loom tests.
#[cfg(loom)]
pub fn loom_reset() {
    FUTEXES.lock().unwrap().clear();
}
