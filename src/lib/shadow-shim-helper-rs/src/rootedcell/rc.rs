use super::{Root, Tag};
use std::{cell::Cell, ptr::NonNull};

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

/// Analagous to [std::rc::Rc]. In particular like [std::rc::Rc] and unlike
/// [std::sync::Arc], it doesn't perform any atomic operations internally,
/// making it relatively inexpensive
///
/// Unlike [std::rc::Rc], this type [Send] and [Sync] if `T` is. This is safe because
/// the owner is required to prove ownership of the associated [Root]
/// to perform any sensitive operations.
///
/// Instances must be destroyed using [RootedRc::safely_drop], which validates
/// that the [Root] is held before manipulating reference counts, etc.
/// Failing to call [RootedRc::safely_drop] results in a `panic` in debug builds,
/// or leaking the object in release builds.
pub struct RootedRc<T> {
    tag: Tag,
    // Option<NonNull<_>> here instead of `* mut` for
    // [covariance](https://doc.rust-lang.org/reference/subtyping.html#variance).
    internal: Option<NonNull<RootedRcInternal<T>>>,
}

impl<T> RootedRc<T> {
    /// Creates a new object associated with `root`.
    pub fn new(root: &Root, val: T) -> Self {
        Self {
            tag: root.tag(),
            internal: Some(
                NonNull::new(Box::into_raw(Box::new(RootedRcInternal::new(val)))).unwrap(),
            ),
        }
    }

    /// Like [Clone::clone], but requires that the corresponding Root is held.
    ///
    /// Intentionally named clone to shadow Self::deref()::clone().
    ///
    /// Panics if `root` is not the associated [Root].
    pub fn clone(&self, root: &Root) -> Self {
        assert_eq!(
            root.tag, self.tag,
            "Tried using root {:?} instead of {:?}",
            root.tag, self.tag
        );
        // SAFETY: We've verified that the lock is held by inspection of the
        // lock itself. We hold a reference to the guard, guaranteeing that the
        // lock is held while `unchecked_clone` runs.
        unsafe { self.unchecked_clone() }
    }

    /// # Safety
    ///
    /// There must be no other threads accessing this object, or clones of this object.
    unsafe fn unchecked_clone(&self) -> Self {
        // SAFETY: Pointer should be valid by construction. Caller is
        // responsible for ensuring no parallel access.
        let internal = unsafe { self.internal.unwrap().as_ref() };
        internal.inc_strong();
        Self {
            tag: self.tag,
            internal: self.internal,
        }
    }

    /// Safely drop this object, dropping the internal value if no other
    /// references to it remain.
    ///
    /// Instances that are dropped *without* calling this method cannot be
    /// safely cleaned up. In debug builds this will result in a `panic`.
    /// Otherwise the underlying reference count will simply not be decremented,
    /// ultimately resulting in the enclosed value never being dropped.
    pub fn safely_drop(mut self, root: &Root) {
        assert_eq!(
            root.tag, self.tag,
            "Tried using a root {:?} instead of {:?}",
            root.tag, self.tag
        );
        let internal = self.internal.take().unwrap();
        let drop_internal = {
            // SAFETY: pointer points to valid data by construction.
            let internal = unsafe { internal.as_ref() };
            internal.dec_strong();
            internal.strong_count.get() == 0
        };
        if drop_internal {
            // SAFETY: There are no remaining strong references to
            // self.internal, and we know that no other threads could be
            // manipulating the reference count in parallel since we have the
            // root lock.
            unsafe { Box::from_raw(internal.as_ptr()) };
        }
    }
}

impl<T> Drop for RootedRc<T> {
    fn drop(&mut self) {
        if !self.internal.is_none() {
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

// SAFETY: RootedRc ensures that its internals can only be accessed when the
// Root is held by the current thread, effectively synchronizing the reference
// count.
unsafe impl<T: Sync + Send> Send for RootedRc<T> {}
unsafe impl<T: Sync + Send> Sync for RootedRc<T> {}

impl<T> std::ops::Deref for RootedRc<T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        &unsafe { self.internal.unwrap().as_ref() }.val
    }
}

#[cfg(test)]
mod test_rooted_rc {
    use super::*;

    use std::{sync::Arc, thread};

    #[test]
    fn construct_and_drop() {
        let root = Root::new();
        let rc = RootedRc::new(&root, 0);
        rc.safely_drop(&root)
    }

    #[test]
    #[cfg(debug_assertions)]
    #[should_panic]
    fn drop_without_lock_panics_with_debug_assertions() {
        let root = Root::new();
        drop(RootedRc::new(&root, 0));
    }

    #[test]
    #[cfg(not(debug_assertions))]
    fn drop_without_lock_leaks_without_debug_assertions() {
        let root = Root::new();
        let rc = std::rc::Rc::new(());
        let rrc = RootedRc::new(&root, rc.clone());
        drop(rrc);
        // Because we didn't call `safely_drop`, RootedRc can't safely call the
        // inner rc's Drop. Instead of panicking, we just leak it.
        assert_eq!(std::rc::Rc::strong_count(&rc), 2);
    }

    #[test]
    fn send_to_worker_thread() {
        let root = Root::new();
        let rc = RootedRc::new(&root, 0);
        thread::spawn(move || {
            // Can access immutably
            let _ = *rc + 2;
            // Need to explicitly drop, since it mutates refcount.
            rc.safely_drop(&root);
        })
        .join()
        .unwrap();
    }

    #[test]
    fn send_to_worker_thread_and_retrieve() {
        let root = Root::new();
        let root = thread::spawn(move || {
            let rc = RootedRc::new(&root, 0);
            rc.safely_drop(&root);
            root
        })
        .join()
        .unwrap();
        let rc = RootedRc::new(&root, 0);
        rc.safely_drop(&root);
    }

    #[test]
    fn clone_to_worker_thread() {
        let root = Root::new();
        let rc = RootedRc::new(&root, 0);

        // Create a clone of rc that we'll pass to worker thread.
        let rc_thread = rc.clone(&root);

        // Worker takes ownership of rc_thread and root;
        // Returns ownership of root.
        let root = thread::spawn(move || {
            let _ = *rc_thread;
            rc_thread.safely_drop(&root);
            root
        })
        .join()
        .unwrap();

        // Take the lock to drop rc
        rc.safely_drop(&root);
    }

    #[test]
    fn threads_contend_over_lock() {
        let root = Arc::new(std::sync::Mutex::new(Root::new()));
        let rc = RootedRc::new(&root.lock().unwrap(), 0);

        let threads: Vec<_> = (0..100)
            .map(|_| {
                // Create a clone of rc that we'll pass to worker thread.
                let rc = rc.clone(&root.lock().unwrap());
                let root = root.clone();

                thread::spawn(move || {
                    let rootlock = root.lock().unwrap();
                    let rc2 = rc.clone(&rootlock);
                    rc.safely_drop(&rootlock);
                    rc2.safely_drop(&rootlock);
                })
            })
            .collect();

        for handle in threads {
            handle.join().unwrap();
        }

        rc.safely_drop(&root.lock().unwrap());
    }
}
