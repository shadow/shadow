use std::{
    cell::{Cell, UnsafeCell},
    ptr::NonNull,
};

use super::{Root, Tag};

struct RootedRcInternal<T> {
    val: UnsafeCell<Option<T>>,
    strong_count: Cell<u32>,
    weak_count: Cell<u32>,
}

impl<T> RootedRcInternal<T> {
    pub fn new(val: T) -> Self {
        Self {
            val: UnsafeCell::new(Some(val)),
            strong_count: Cell::new(1),
            weak_count: Cell::new(0),
        }
    }

    pub fn inc_strong(&self) {
        self.strong_count.set(self.strong_count.get() + 1)
    }

    pub fn dec_strong(&self) {
        self.strong_count.set(self.strong_count.get() - 1)
    }

    pub fn inc_weak(&self) {
        self.weak_count.set(self.weak_count.get() + 1)
    }

    pub fn dec_weak(&self) {
        self.weak_count.set(self.weak_count.get() - 1)
    }
}

enum RefType {
    Weak,
    Strong,
}

// Shared implementation for strong and weak references
struct RootedRcCommon<T> {
    tag: Tag,
    internal: Option<NonNull<RootedRcInternal<T>>>,
}

impl<T> RootedRcCommon<T> {
    pub fn new(root: &Root, val: T) -> Self {
        Self {
            tag: root.tag(),
            internal: Some(
                NonNull::new(Box::into_raw(Box::new(RootedRcInternal::new(val)))).unwrap(),
            ),
        }
    }

    // Validates that no other thread currently has access to self.internal, and
    // return a reference to it.
    pub fn borrow_internal(&self, root: &Root) -> &RootedRcInternal<T> {
        assert_eq!(
            root.tag, self.tag,
            "Tried using root {:?} instead of {:?}",
            root.tag, self.tag
        );
        // SAFETY:
        // * Holding a reference to `root` proves no other threads can currently
        //   access `self.internal`.
        // * `self.internal` is accessible since we hold a strong reference.
        unsafe { self.internal.unwrap().as_ref() }
    }

    pub fn safely_drop(mut self, root: &Root, t: RefType) {
        let internal: &RootedRcInternal<T> = self.borrow_internal(root);
        match t {
            RefType::Weak => internal.dec_weak(),
            RefType::Strong => internal.dec_strong(),
        };
        let strong_count = internal.strong_count.get();
        let weak_count = internal.weak_count.get();

        // If there are no more strong references, prepare to drop the value.
        // If the value was already dropped (e.g. because we're now dropping a
        // weak reference after all the strong refs were already dropped), this
        // is a no-op.
        let val: Option<T> = if strong_count == 0 {
            // SAFETY: Since no strong references remain, nothing else can be
            // referencing the internal value.
            unsafe { internal.val.get().as_mut().unwrap().take() }
        } else {
            None
        };

        // Clear `self.internal`, so that `drop` knows that `safely_drop` ran.
        let internal: NonNull<RootedRcInternal<T>> = self.internal.take().unwrap();

        // If there are neither strong nor weak references, drop `internal` itself.
        if strong_count == 0 && weak_count == 0 {
            // SAFETY: We know the pointer is still valid since we had the last
            // reference, and since the counts are now zero, there can be no
            // other references.
            drop(unsafe { Box::from_raw(internal.as_ptr()) });
        }

        // (Potentially) drop the internal value only after we've finished with
        // the Rc bookkeeping, so that it's in a valid state even if the value's
        // drop implementation panics.
        drop(val);
    }

    pub fn clone(&self, root: &Root, t: RefType) -> Self {
        let internal: &RootedRcInternal<T> = self.borrow_internal(root);
        match t {
            RefType::Weak => internal.inc_weak(),
            RefType::Strong => internal.inc_strong(),
        };
        Self {
            tag: self.tag,
            internal: self.internal,
        }
    }
}

impl<T> Drop for RootedRcCommon<T> {
    #[inline]
    fn drop(&mut self) {
        if self.internal.is_some() {
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

// SAFETY: RootedRcCommon ensures that its internals can only be accessed when
// the Root is held by the current thread, effectively synchronizing the
// reference count.
unsafe impl<T: Sync + Send> Send for RootedRcCommon<T> {}
unsafe impl<T: Sync + Send> Sync for RootedRcCommon<T> {}

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
    common: RootedRcCommon<T>,
}

impl<T> RootedRc<T> {
    /// Creates a new object associated with `root`.
    #[inline]
    pub fn new(root: &Root, val: T) -> Self {
        Self {
            common: RootedRcCommon::new(root, val),
        }
    }

    /// Create a weak reference.
    ///
    /// We use fully qualified syntax here for consistency with Rc and Arc and
    /// to avoid name conflicts with `T`'s methods.
    #[inline]
    pub fn downgrade(this: &Self, root: &Root) -> RootedRcWeak<T> {
        RootedRcWeak {
            common: this.common.clone(root, RefType::Weak),
        }
    }

    /// Like [Clone::clone], but requires that the corresponding Root is held.
    ///
    /// Intentionally named clone to shadow Self::deref()::clone().
    ///
    /// Panics if `root` is not the associated [Root].
    #[inline]
    pub fn clone(&self, root: &Root) -> Self {
        Self {
            common: self.common.clone(root, RefType::Strong),
        }
    }

    /// Safely drop this object, dropping the internal value if no other
    /// references to it remain.
    ///
    /// Instances that are dropped *without* calling this method cannot be
    /// safely cleaned up. In debug builds this will result in a `panic`.
    /// Otherwise the underlying reference count will simply not be decremented,
    /// ultimately resulting in the enclosed value never being dropped.
    #[inline]
    pub fn safely_drop(self, root: &Root) {
        self.common.safely_drop(root, RefType::Strong);
    }
}

impl<T> std::ops::Deref for RootedRc<T> {
    type Target = T;

    #[inline]
    fn deref(&self) -> &Self::Target {
        // No need to require a reference to `Root` here since we're not
        // touching the counts, only the value itself, which we already required
        // to be Sync and Send for RootedRc<T> to be Sync and Send.

        // SAFETY: Pointer to `internal` is valid, since we hold a strong ref.
        let internal = unsafe { self.common.internal.unwrap().as_ref() };

        // SAFETY: Since we hold a strong ref, we know that `val` is valid, and
        // that there are no mutable references to it. (The only time we create
        // a mutable reference is to drop the T value when the strong ref count
        // reaches zero)
        let val = unsafe { &*internal.val.get() };
        val.as_ref().unwrap()
    }
}

#[cfg(test)]
mod test_rooted_rc {
    use std::{sync::Arc, thread};

    use super::*;

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

pub struct RootedRcWeak<T> {
    common: RootedRcCommon<T>,
}

impl<T> RootedRcWeak<T> {
    #[inline]
    pub fn upgrade(&self, root: &Root) -> Option<RootedRc<T>> {
        let internal = self.common.borrow_internal(root);

        if internal.strong_count.get() == 0 {
            return None;
        }

        Some(RootedRc {
            common: self.common.clone(root, RefType::Strong),
        })
    }

    /// Like [Clone::clone], but requires that the corresponding Root is held.
    ///
    /// Intentionally named clone to shadow Self::deref()::clone().
    ///
    /// Panics if `root` is not the associated [Root].
    #[inline]
    pub fn clone(&self, root: &Root) -> Self {
        Self {
            common: self.common.clone(root, RefType::Weak),
        }
    }

    #[inline]
    pub fn safely_drop(self, root: &Root) {
        self.common.safely_drop(root, RefType::Weak)
    }
}

// SAFETY: RootedRc ensures that its internals can only be accessed when the
// Root is held by the current thread, effectively synchronizing the reference
// count.
unsafe impl<T: Sync + Send> Send for RootedRcWeak<T> {}
unsafe impl<T: Sync + Send> Sync for RootedRcWeak<T> {}

#[cfg(test)]
mod test_rooted_rc_weak {
    use super::*;

    #[test]
    fn successful_upgrade() {
        let root = Root::new();
        let strong = RootedRc::new(&root, 42);
        let weak = RootedRc::downgrade(&strong, &root);

        let upgraded = weak.upgrade(&root).unwrap();

        assert_eq!(*upgraded, *strong);

        upgraded.safely_drop(&root);
        weak.safely_drop(&root);
        strong.safely_drop(&root);
    }

    #[test]
    fn failed_upgrade() {
        let root = Root::new();
        let strong = RootedRc::new(&root, 42);
        let weak = RootedRc::downgrade(&strong, &root);

        strong.safely_drop(&root);

        assert!(weak.upgrade(&root).is_none());

        weak.safely_drop(&root);
    }

    #[test]
    #[cfg(debug_assertions)]
    #[should_panic]
    fn drop_without_lock_panics_with_debug_assertions() {
        let root = Root::new();
        let strong = RootedRc::new(&root, 42);
        drop(RootedRc::downgrade(&strong, &root));
        strong.safely_drop(&root);
    }

    // Validate that circular references are cleaned up correctly.
    #[test]
    fn circular_reference() {
        std::thread_local! {
            static THREAD_ROOT: Root = Root::new();
        }

        struct MyStruct {
            // Circular reference
            weak_self: Cell<Option<RootedRcWeak<Self>>>,
        }
        impl MyStruct {
            fn new() -> RootedRc<Self> {
                THREAD_ROOT.with(|root| {
                    let rv = RootedRc::new(
                        root,
                        MyStruct {
                            weak_self: Cell::new(None),
                        },
                    );
                    let weak = RootedRc::downgrade(&rv, root);
                    rv.weak_self.set(Some(weak));
                    rv
                })
            }
        }
        impl Drop for MyStruct {
            fn drop(&mut self) {
                let weak = self.weak_self.replace(None).unwrap();
                THREAD_ROOT.with(|root| {
                    weak.safely_drop(root);
                });
            }
        }

        let val = MyStruct::new();
        THREAD_ROOT.with(|root| {
            val.safely_drop(root);
        })
    }

    #[test]
    #[cfg(not(debug_assertions))]
    fn drop_without_lock_doesnt_leak_value() {
        let root = Root::new();
        let rc = std::rc::Rc::new(());
        let strong = RootedRc::new(&root, rc.clone());
        drop(RootedRc::downgrade(&strong, &root));
        strong.safely_drop(&root);

        // Because we safely dropped all of the strong references,
        // the internal std::rc::Rc value should still have been dropped.
        // The `internal` field itself will be leaked since the weak count
        // never reaches 0.
        assert_eq!(std::rc::Rc::strong_count(&rc), 1);
    }
}
