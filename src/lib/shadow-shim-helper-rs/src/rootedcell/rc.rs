use std::{
    cell::{Cell, UnsafeCell},
    ptr::NonNull,
};

use crate::explicit_drop::ExplicitDrop;

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

    /// Decrement the reference. If this was the last strong reference, return the value.
    pub fn safely_drop(mut self, root: &Root, t: RefType) -> Option<T> {
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

        val
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
            // `explicit_drop` is the public interface for the internal `safely_drop`
            log::error!("Dropped without calling `explicit_drop`");

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
                panic!("Dropped without calling `explicit_drop`");
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
/// Instances must be destroyed explicitly, using [`RootedRc::explicit_drop`],
/// [`RootedRc::explicit_drop_recursive`], or [`RootedRc::into_inner`].  These
/// validate that the [Root] is held before manipulating reference counts, etc.
///
/// Dropping `RootedRc` without calling one of these methods results in a
/// `panic` in debug builds, or leaking the object in release builds.
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

    /// Drop the `RootedRc`, and return the inner value if this was the last
    /// strong reference.
    #[inline]
    pub fn into_inner(this: Self, root: &Root) -> Option<T> {
        this.common.safely_drop(root, RefType::Strong)
    }

    /// Drops `self`, and if `self` was the last strong reference, call
    /// `ExplicitDrop::explicit_drop` on the internal value.
    pub fn explicit_drop_recursive(
        self,
        root: &Root,
        param: &T::ExplicitDropParam,
    ) -> Option<T::ExplicitDropResult>
    where
        T: ExplicitDrop,
    {
        Self::into_inner(self, root).map(|val| val.explicit_drop(param))
    }
}

impl<T> ExplicitDrop for RootedRc<T> {
    type ExplicitDropParam = Root;

    type ExplicitDropResult = ();

    /// If T itself implements `ExplicitDrop`, consider
    /// `RootedRc::explicit_drop_recursive` instead to call it when dropping the
    /// last strong reference.
    fn explicit_drop(self, root: &Self::ExplicitDropParam) -> Self::ExplicitDropResult {
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
        rc.explicit_drop(&root)
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
        // Because we didn't call `explicit_drop`, RootedRc can't safely call the
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
            rc.explicit_drop(&root)
        })
        .join()
        .unwrap();
    }

    #[test]
    fn send_to_worker_thread_and_retrieve() {
        let root = Root::new();
        let root = thread::spawn(move || {
            let rc = RootedRc::new(&root, 0);
            rc.explicit_drop(&root);
            root
        })
        .join()
        .unwrap();
        let rc = RootedRc::new(&root, 0);
        rc.explicit_drop(&root)
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
            rc_thread.explicit_drop(&root);
            root
        })
        .join()
        .unwrap();

        // Take the lock to drop rc
        rc.explicit_drop(&root);
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
                    rc.explicit_drop(&rootlock);
                    rc2.explicit_drop(&rootlock);
                })
            })
            .collect();

        for handle in threads {
            handle.join().unwrap();
        }

        rc.explicit_drop(&root.lock().unwrap());
    }

    #[test]
    fn into_inner_recursive() {
        let root = Root::new();
        let inner = RootedRc::new(&root, ());
        let outer1 = RootedRc::new(&root, inner);
        let outer2 = outer1.clone(&root);

        // Dropping the first outer returns None, since there is still another strong ref.
        assert!(RootedRc::into_inner(outer1, &root).is_none());

        // Dropping the second outer returns the inner ref.
        let inner = RootedRc::into_inner(outer2, &root).unwrap();

        // Now we can safely drop the inner ref.
        inner.explicit_drop(&root);
    }

    #[test]
    fn explicit_drop() {
        let root = Root::new();
        let rc = RootedRc::new(&root, ());
        rc.explicit_drop(&root);
    }

    #[test]
    fn explicit_drop_recursive() {
        // Defining `ExplicitDrop` for `MyOuter` lets us use `RootedRc::explicit_drop_recursive`
        // to safely drop the inner `RootedRc` when dropping a `RootedRc<MyOuter>`.
        struct MyOuter(RootedRc<()>);
        impl ExplicitDrop for MyOuter {
            type ExplicitDropParam = Root;
            type ExplicitDropResult = ();

            fn explicit_drop(self, root: &Self::ExplicitDropParam) -> Self::ExplicitDropResult {
                self.0.explicit_drop(root);
            }
        }

        let root = Root::new();
        let inner = RootedRc::new(&root, ());
        let outer1 = RootedRc::new(&root, MyOuter(inner));
        let outer2 = RootedRc::new(&root, MyOuter(outer1.0.clone(&root)));
        outer1.explicit_drop_recursive(&root, &root);
        outer2.explicit_drop_recursive(&root, &root);
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
}

impl<T> ExplicitDrop for RootedRcWeak<T> {
    type ExplicitDropParam = Root;

    type ExplicitDropResult = ();

    #[inline]
    fn explicit_drop(self, root: &Self::ExplicitDropParam) -> Self::ExplicitDropResult {
        let val = self.common.safely_drop(root, RefType::Weak);
        // Since this isn't a strong reference, this can't be the *last* strong
        // reference, so the value should never be returned.
        debug_assert!(val.is_none());
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

        upgraded.explicit_drop(&root);
        weak.explicit_drop(&root);
        strong.explicit_drop(&root);
    }

    #[test]
    fn failed_upgrade() {
        let root = Root::new();
        let strong = RootedRc::new(&root, 42);
        let weak = RootedRc::downgrade(&strong, &root);

        strong.explicit_drop(&root);

        assert!(weak.upgrade(&root).is_none());

        weak.explicit_drop(&root);
    }

    #[test]
    #[cfg(debug_assertions)]
    #[should_panic]
    fn drop_without_lock_panics_with_debug_assertions() {
        let root = Root::new();
        let strong = RootedRc::new(&root, 42);
        drop(RootedRc::downgrade(&strong, &root));
        strong.explicit_drop(&root);
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
                    weak.explicit_drop(root);
                });
            }
        }

        let val = MyStruct::new();
        THREAD_ROOT.with(|root| {
            val.explicit_drop(root);
        })
    }

    #[test]
    #[cfg(not(debug_assertions))]
    fn drop_without_lock_doesnt_leak_value() {
        let root = Root::new();
        let rc = std::rc::Rc::new(());
        let strong = RootedRc::new(&root, rc.clone());
        drop(RootedRc::downgrade(&strong, &root));
        strong.explicit_drop(&root);

        // Because we safely dropped all of the strong references,
        // the internal std::rc::Rc value should still have been dropped.
        // The `internal` field itself will be leaked since the weak count
        // never reaches 0.
        assert_eq!(std::rc::Rc::strong_count(&rc), 1);
    }
}
