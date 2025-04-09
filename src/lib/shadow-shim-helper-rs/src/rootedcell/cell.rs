use core::cell::UnsafeCell;

use vasi::VirtualAddressSpaceIndependent;

use super::{Root, Tag};

/// Analagous to [core::cell::Cell]. In particular like [core::cell::Cell], it
/// doesn't perform any atomic operations internally, making it relatively
/// inexpensive.
///
/// Unlike [core::cell::Cell], this type is [Send] and [Sync] if `T` is
/// [Send]. This is safe because the owner is required to prove access to the
/// associated [Root], which is `![Sync]`, to access.
#[derive(Debug, VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct RootedCell<T> {
    tag: Tag,
    val: UnsafeCell<T>,
}

impl<T> RootedCell<T> {
    /// Create a RootedCell associated with `root`.
    #[inline]
    pub fn new(root: &Root, val: T) -> Self {
        Self {
            tag: root.tag(),
            val: UnsafeCell::new(val),
        }
    }

    #[inline]
    pub fn get_mut(&mut self) -> &mut T {
        // Since we have the only reference to `self`, we don't need to check the root.
        unsafe { &mut *self.val.get() }
    }

    #[inline]
    pub fn set(&self, root: &Root, val: T) {
        // Replace the current value, and just drop the old value.
        drop(self.replace(root, val))
    }

    #[inline]
    pub fn replace(&self, root: &Root, val: T) -> T {
        // Prove that the root is held for this tag.
        assert_eq!(
            root.tag, self.tag,
            "Expected {:?} Got {:?}",
            self.tag, root.tag
        );

        unsafe { self.val.get().replace(val) }
    }

    #[inline]
    pub fn into_inner(self) -> T {
        self.val.into_inner()
    }
}

impl<T: Copy> RootedCell<T> {
    #[inline]
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
    use std::thread;

    use super::*;
    use crate::explicit_drop::ExplicitDrop;
    use crate::rootedcell::rc::RootedRc;

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
                rc.explicit_drop(&root);
                root
            })
            .join()
            .unwrap()
        };
        assert_eq!(rc.get(&root), 3);
        rc.explicit_drop(&root);
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
