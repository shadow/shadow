use core::cell::{Cell, UnsafeCell};

use vasi::VirtualAddressSpaceIndependent;

use crate::explicit_drop::ExplicitDrop;

use super::{Root, Tag};

/// Analagous to [core::cell::RefCell]. In particular like [core::cell::RefCell]
/// and unlike [std::sync::Mutex], it  doesn't perform any atomic operations
/// internally, making it relatively inexpensive.
///
/// Unlike [core::cell::RefCell], this type is [Send] and [Sync] if `T` is
/// [Send]. This is safe because the owner is required to prove access to the
/// associated [Root], which is `![Sync]`, to borrow.
#[derive(Debug, VirtualAddressSpaceIndependent)]
#[repr(C)]
pub struct RootedRefCell<T> {
    tag: Tag,
    val: UnsafeCell<T>,
    reader_count: Cell<u32>,
    writer: Cell<bool>,
}

impl<T> RootedRefCell<T> {
    /// Create a RootedRefCell associated with `root`.
    #[inline]
    pub fn new(root: &Root, val: T) -> Self {
        Self {
            tag: root.tag(),
            val: UnsafeCell::new(val),
            reader_count: Cell::new(0),
            writer: Cell::new(false),
        }
    }

    /// Borrow a reference. Panics if `root` is for the wrong [Root], or
    /// if this object is alread mutably borrowed.
    #[inline]
    pub fn borrow<'a>(&'a self, root: &'a Root) -> RootedRefCellRef<'a, T> {
        // Prove that the root is held for this tag.
        assert_eq!(
            root.tag, self.tag,
            "Expected {:?} Got {:?}",
            self.tag, root.tag
        );

        assert!(!self.writer.get());

        self.reader_count.set(self.reader_count.get() + 1);

        RootedRefCellRef { guard: self }
    }

    /// Borrow a mutable reference. Panics if `root` is for the wrong
    /// [Root], or if this object is already borrowed.
    #[inline]
    pub fn borrow_mut<'a>(&'a self, root: &'a Root) -> RootedRefCellRefMut<'a, T> {
        // Prove that the root is held for this tag.
        assert_eq!(
            root.tag, self.tag,
            "Expected {:?} Got {:?}",
            self.tag, root.tag
        );

        assert!(!self.writer.get());
        assert!(self.reader_count.get() == 0);

        self.writer.set(true);

        RootedRefCellRefMut { guard: self }
    }

    #[inline]
    pub fn into_inner(self) -> T {
        self.val.into_inner()
    }
}

unsafe impl<T: Send> Send for RootedRefCell<T> {}
unsafe impl<T: Send> Sync for RootedRefCell<T> {}

impl<T> ExplicitDrop for RootedRefCell<T>
where
    T: ExplicitDrop,
{
    type ExplicitDropParam = <T as ExplicitDrop>::ExplicitDropParam;
    type ExplicitDropResult = <T as ExplicitDrop>::ExplicitDropResult;

    fn explicit_drop(self, param: &Self::ExplicitDropParam) -> Self::ExplicitDropResult {
        self.val.into_inner().explicit_drop(param)
    }
}

pub struct RootedRefCellRef<'a, T> {
    guard: &'a RootedRefCell<T>,
}

impl<T> core::ops::Deref for RootedRefCellRef<'_, T> {
    type Target = T;

    #[inline]
    fn deref(&self) -> &Self::Target {
        unsafe { self.guard.val.get().as_ref().unwrap() }
    }
}

impl<T> Drop for RootedRefCellRef<'_, T> {
    #[inline]
    fn drop(&mut self) {
        self.guard
            .reader_count
            .set(self.guard.reader_count.get() - 1);
    }
}

pub struct RootedRefCellRefMut<'a, T> {
    guard: &'a RootedRefCell<T>,
}

impl<T> core::ops::Deref for RootedRefCellRefMut<'_, T> {
    type Target = T;

    #[inline]
    fn deref(&self) -> &Self::Target {
        unsafe { self.guard.val.get().as_ref().unwrap() }
    }
}

impl<T> core::ops::DerefMut for RootedRefCellRefMut<'_, T> {
    #[inline]
    fn deref_mut(&mut self) -> &mut Self::Target {
        unsafe { self.guard.val.get().as_mut().unwrap() }
    }
}

impl<T> Drop for RootedRefCellRefMut<'_, T> {
    #[inline]
    fn drop(&mut self) {
        self.guard.writer.set(false);
    }
}

#[cfg(test)]
mod test_rooted_refcell {
    use std::thread;

    use super::*;
    use crate::explicit_drop::ExplicitDrop;
    use crate::rootedcell::rc::RootedRc;

    #[test]
    fn construct_and_drop() {
        let root = Root::new();
        let _ = RootedRefCell::new(&root, 0);
    }

    #[test]
    fn share_with_worker_thread() {
        let root = Root::new();
        let rc = RootedRc::new(&root, RootedRefCell::new(&root, 0));
        let root = {
            let rc = { rc.clone(&root) };
            thread::spawn(move || {
                let mut borrow = rc.borrow_mut(&root);
                *borrow = 3;
                // Drop rc with lock still held.
                drop(borrow);
                rc.explicit_drop(&root);
                root
            })
            .join()
            .unwrap()
        };
        let borrow = rc.borrow(&root);
        assert_eq!(*borrow, 3);
        drop(borrow);
        rc.explicit_drop(&root);
    }
}
