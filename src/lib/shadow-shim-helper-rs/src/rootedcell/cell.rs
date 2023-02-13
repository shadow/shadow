use super::{Root, RootScope, Tag};
use std::cell::UnsafeCell;
use std::marker::PhantomData;
use vasi::VirtualAddressSpaceIndependent;

/// Analagous to [std::cell::Cell]. In particular like [std::cell::Cell], it
/// doesn't perform any atomic operations internally, making it relatively
/// inexpensive.
///
/// Unlike [std::cell::Cell], this type is [Send] and [Sync] if `T` is
/// [Send]. This is safe because the owner is required to prove access to the
/// associated [Root], which is `![Sync]`, to access.
#[derive(Debug)]
#[repr(C)]
pub struct RootedCell<T> {
    tag: Tag,
    val: UnsafeCell<T>,
}

// SAFETY: RootedCell is VirtualAddressSpaceIndependent as long as T is.
unsafe impl<T> VirtualAddressSpaceIndependent for RootedCell<T> where
    T: VirtualAddressSpaceIndependent
{
}

impl<T> RootedCell<T> {
    /// Create a RootedCell associated with `root`.
    pub fn new(root: &Root, val: T) -> Self {
        Self {
            tag: root.tag(),
            val: UnsafeCell::new(val),
        }
    }

    pub fn get_mut(&mut self) -> &mut T {
        // Since we have the only reference to `self`, we don't need to check the root.
        unsafe { &mut *self.val.get() }
    }

    pub fn set(&self, root: &Root, val: T) {
        // Replace the current value, and just drop the old value.
        drop(self.replace(root, val))
    }

    pub fn replace(&self, root: &Root, val: T) -> T {
        // Prove that the root is held for this tag.
        assert_eq!(
            root.tag, self.tag,
            "Expected {:?} Got {:?}",
            self.tag, root.tag
        );

        unsafe { self.val.get().replace(val) }
    }

    pub fn into_inner(self) -> T {
        self.val.into_inner()
    }
}

impl<T: Copy> RootedCell<T> {
    pub fn get(&self, root: &Root) -> T {
        // Prove that the root is held for this tag.
        assert_eq!(
            root.tag, self.tag,
            "Expected {:?} Got {:?}",
            self.tag, root.tag
        );

        unsafe { *self.val.get() }
    }
}

unsafe impl<T: Send> Send for RootedCell<T> where T: Copy {}
unsafe impl<T: Send> Sync for RootedCell<T> where T: Copy {}

#[cfg(test)]
mod test_rooted_cell {
    use super::*;

    use crate::rootedcell::rc::RootedRc;
    use std::thread;

    #[test]
    fn get() {
        let root = Root::new();
        let c = RootedCell::new(&root, 1);
        assert_eq!(c.get(&root), 1);
    }

    #[test]
    fn get_mut() {
        let root = Root::new();
        let mut c = RootedCell::new(&root, 1);
        assert_eq!(*c.get_mut(), 1);
    }

    #[test]
    fn set() {
        let root = Root::new();
        let c = RootedCell::new(&root, 1);
        c.set(&root, 2);
        assert_eq!(c.get(&root), 2);
    }

    #[test]
    fn replace() {
        let root = Root::new();
        let c = RootedCell::new(&root, 1);
        let old = c.replace(&root, 2);
        assert_eq!(old, 1);
        assert_eq!(c.get(&root), 2);
    }

    #[test]
    fn share_with_worker_thread() {
        let root = Root::new();
        let rc = RootedRc::new(&root, RootedCell::new(&root, 0));
        let root = {
            let rc = { rc.clone(&root) };
            thread::spawn(move || {
                rc.set(&root, 3);
                rc.safely_drop(&root);
                root
            })
            .join()
            .unwrap()
        };
        assert_eq!(rc.get(&root), 3);
        rc.safely_drop(&root);
    }

    #[test]
    fn worker_thread_get_mut() {
        let root = Root::new();
        let cell = RootedCell::new(&root, 0);
        let cell = {
            thread::spawn(move || {
                // Move into closure and make mutable.
                let mut cell = cell;
                // Since we have a mutable reference, we don't
                // need the root to access.
                *cell.get_mut() = 3;
                // Return cell to parent thread.
                cell
            })
            .join()
            .unwrap()
        };
        assert_eq!(cell.get(&root), 3);
    }
}

pub struct ScopedRootedCell<ScopeT, T> {
    val: RootedCell<T>,
    _phantom: PhantomData<ScopeT>,
}

impl<ScopeT, T> ScopedRootedCell<ScopeT, T> {
    /// Create a RootedCell associated with `root`.
    pub fn new_explicit(root: &Root, val: T) -> Self {
        Self {
            val: RootedCell::new(root, val),
            _phantom: PhantomData,
        }
    }

    /// Create a RootedCell associated with `root`.
    pub fn new(val: T) -> Self {
        let root = RootScope::<ScopeT>::current().unwrap();
        Self::new_explicit(&root, val)
    }

    pub fn get_mut(&mut self) -> &mut T {
        self.val.get_mut()
    }

    pub fn set_explicit(&self, root: &Root, val: T) {
        self.val.set(root, val)
    }

    pub fn set(&self, val: T) {
        let root = RootScope::<ScopeT>::current().unwrap();
        self.set_explicit(&root, val)
    }

    pub fn replace_explicit(&self, root: &Root, val: T) -> T {
        self.val.replace(root, val)
    }

    pub fn replace(&self, val: T) -> T {
        let root = RootScope::<ScopeT>::current().unwrap();
        self.replace_explicit(&root, val)
    }

    pub fn into_inner(self) -> T {
        self.val.into_inner()
    }
}

impl<ScopeT, T: Copy> ScopedRootedCell<ScopeT, T> {
    pub fn get_explicit(&self, root: &Root) -> T {
        self.val.get(root)
    }

    pub fn get(&self) -> T {
        let root = RootScope::<ScopeT>::current().unwrap();
        self.get_explicit(&root)
    }
}

unsafe impl<T: Send, ScopeT> Send for ScopedRootedCell<T, ScopeT> {}
unsafe impl<T: Send, ScopeT> Sync for ScopedRootedCell<T, ScopeT> {}

#[cfg(test)]
mod test_scoped_rooted_cell {
    use super::*;

    use std::sync::Arc;
    use std::thread;

    struct HostRoot(());
    type HostRootScope = RootScope<HostRoot>;

    #[test]
    fn get() {
        HostRootScope::with_current_set_to(Root::new(), || {
            // We need to specify the type of scope on construction.
            let c = ScopedRootedCell::<HostRoot, _>::new(1);
            // No need to explicitly reference the root on method calls.
            assert_eq!(c.get(), 1);
        });
    }

    #[test]
    fn get_mut() {
        HostRootScope::with_current_set_to(Root::new(), || {
            let mut c = ScopedRootedCell::<HostRoot, _>::new(1);
            assert_eq!(*c.get_mut(), 1);
        });
    }

    #[test]
    fn set() {
        HostRootScope::with_current_set_to(Root::new(), || {
            let c = ScopedRootedCell::<HostRoot, _>::new(1);
            c.set(2);
            assert_eq!(c.get(), 2);
        });
    }

    #[test]
    fn replace() {
        HostRootScope::with_current_set_to(Root::new(), || {
            let c = ScopedRootedCell::<HostRoot, _>::new(1);
            let old = c.replace(2);
            assert_eq!(old, 1);
            assert_eq!(c.get(), 2);
        });
    }

    #[test]
    fn share_with_worker_thread() {
        let root = Root::new();
        let rc = Arc::new(ScopedRootedCell::<HostRoot, _>::new_explicit(&root, 0));
        let root = {
            let rc = { rc.clone() };
            thread::spawn(move || {
                HostRootScope::with_current_set_to(root, || {
                    rc.set(3);
                })
            })
            .join()
            .unwrap()
        };
        assert_eq!(rc.get_explicit(&root), 3);
    }
}
