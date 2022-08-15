use crate::{Root, RootGuard, Tag};
use std::cell::Cell;

struct RootedRcInternal<T> {
    val: T,
    strong_count: Cell<u32>,
}

impl<T> RootedRcInternal<T> {
    pub fn new(val: T) -> Self {
        Self {
            val,
            strong_count: Cell::new(1),
        }
    }

    pub fn inc_strong(&self) {
        self.strong_count.set(self.strong_count.get() + 1)
    }

    pub fn dec_strong(&self) {
        self.strong_count.set(self.strong_count.get() - 1)
    }
}

/// Analagous to `std::rc::Rc`; in particular like `Rc` and unlike
/// `std::sync::Arc`, it doesn't perform any atomic operations internally (which
/// are moderately expensive).
///
/// Unlike `Rc`, this type `Send` and `Sync` if `T` is. It leverages lock-tracking
/// to ensure `Rc` operations are protected.
///
/// Panics if dropped without the corresponding Root lock being held by the
/// current thread.
pub struct RootedRc<T> {
    tag: Tag,
    internal: *mut RootedRcInternal<T>,
}

impl<T> RootedRc<T> {
    /// Creates a new object guarded by the Root with the given `tag`.
    pub fn new(root: &Root, val: T) -> Self {
        Self {
            tag: root.tag(),
            internal: Box::into_raw(Box::new(RootedRcInternal::new(val))),
        }
    }

    /// Like Clone::clone, but requires that the corresponding Root is locked.
    ///
    /// Intentionally named clone to shadow Self::deref()::clone().
    ///
    /// Panics if `guard` doesn't match this objects tag.
    pub fn clone(&self, guard: &RootGuard) -> Self {
        assert_eq!(
            guard.guard.tag, self.tag,
            "Tried using a lock for {:?} instead of {:?}",
            guard.guard.tag, self.tag
        );
        // SAFETY: We've verified that the lock is held by inspection of the
        // lock itself. We hold a reference to the guard, guaranteeing that the
        // lock is held while `unchecked_clone` runs.
        unsafe { self.unchecked_clone() }
    }

    // SAFETY: The lock for the root with this object's tag must be held.
    pub unsafe fn unchecked_clone(&self) -> Self {
        let internal = unsafe { self.internal.as_ref().unwrap() };
        internal.inc_strong();
        Self {
            tag: self.tag,
            internal: self.internal,
        }
    }

    pub fn safely_drop(mut self, guard: &RootGuard) {
        assert_eq!(
            guard.guard.tag, self.tag,
            "Tried using a lock for {:?} instead of {:?}",
            guard.guard.tag, self.tag
        );
        // SAFETY: pointer points to valid data by construction.
        let internal = unsafe { self.internal.as_ref() }.unwrap();

        internal.dec_strong();
        self.internal = std::ptr::null_mut();
        if internal.strong_count.get() == 0 {
            // SAFETY: There are no remaining strong references to
            // self.internal, and we know that no other threads could be
            // manipulating the reference count in parallel since we have the
            // root lock.
            unsafe { Box::from_raw(self.internal) };
        }
    }
}

impl<T> Drop for RootedRc<T> {
    fn drop(&mut self) {
        if !self.internal.is_null() {
            log::error!("Dropped without calling `safely_drop`");

            // We *can* continue without violating Rust safety properties; the
            // underlying object will just be leaked, since the ref count will
            // never reach zero.
            //
            // If we're not already panicking, it's useful to panic here to make
            // the leak more visible.
            //
            // If we are already panicking though, that may already explain how
            // a call to `safely_drop` got skipped, and panicking again would
            // just obscure the original panic.
            #[cfg(debug_assertions)]
            if !std::thread::panicking() {
                panic!("Dropped without calling `safely_drop`");
            }
        }
    }
}

// SAFETY: Normally the inner `Rc` would inhibit this type from being `Send` and
// `Sync`. However, RootedRc ensures that `Rc`'s reference count can only be
// accessed when the root is locked by the current thread, effectively
// synchronizing the reference count.
unsafe impl<T: Sync + Send> Send for RootedRc<T> {}
unsafe impl<T: Sync + Send> Sync for RootedRc<T> {}

impl<T> std::ops::Deref for RootedRc<T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        &unsafe { self.internal.as_ref() }.unwrap().val
    }
}

#[cfg(test)]
mod test_rooted_rc {
    use std::thread;

    use crate::Root;

    use super::*;

    #[test]
    fn construct_and_drop() {
        let root = Root::new();
        let lock = root.lock();
        let rc = RootedRc::new(&root, 0);
        rc.safely_drop(&lock)
    }

    #[test]
    #[should_panic]
    fn drop_without_lock_panics() {
        let root = Root::new();
        let _ = RootedRc::new(&root, 0);
    }

    #[test]
    fn send_to_worker_thread() {
        let root = Root::new();
        let rc = RootedRc::new(&root, 0);
        thread::spawn(move || {
            // Can access immutably without lock.
            let _ = *rc + 2;
            // Need lock to drop, since it mutates refcount.
            let lock = root.lock();
            rc.safely_drop(&lock);
        })
        .join()
        .unwrap();
    }

    #[test]
    fn send_to_worker_thread_and_retrieve() {
        let root = Root::new();
        let root = thread::spawn(move || {
            let rc = RootedRc::new(&root, 0);
            rc.safely_drop(&root.lock());
            root
        })
        .join()
        .unwrap();
        let rc = RootedRc::new(&root, 0);
        rc.safely_drop(&root.lock());
    }

    #[test]
    fn clone_to_worker_thread() {
        let root = Root::new();
        let rc = RootedRc::new(&root, 0);

        // Create a clone of rc that we'll pass to worker thread.
        let rc_thread = rc.clone(&root.lock());

        // Worker takes ownership of rc_thread and root;
        // Returns ownership of root.
        let root = thread::spawn(move || {
            let _ = *rc_thread;
            // Need lock to drop, since it mutates refcount.
            rc_thread.safely_drop(&root.lock());
            root
        })
        .join()
        .unwrap();

        // Take the lock to drop rc
        rc.safely_drop(&root.lock());
    }
}
