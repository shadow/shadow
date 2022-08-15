use crate::{Root, RootGuard, Tag};
use std::cell::{Cell, UnsafeCell};

pub struct RootedRefCell<T> {
    tag: Tag,
    val: UnsafeCell<T>,
    reader_count: Cell<u32>,
    writer: Cell<bool>,
}

impl<T> RootedRefCell<T> {
    /// Create a RootedRefCell bound to the given tag.
    pub fn new(root: &Root, val: T) -> Self {
        Self {
            tag: root.tag(),
            val: UnsafeCell::new(val),
            reader_count: Cell::new(0),
            writer: Cell::new(false),
        }
    }

    /// Borrow a reference. Panics if `root_guard` is for the wrong tag, or if
    /// this object is alread mutably borrowed.
    pub fn borrow<'a>(
        &'a self,
        // This 'a statically enforces that the root lock can't be dropped
        // while the returned guard is still outstanding. i.e. it is part
        // of the safety proof for making Self Send and Sync.
        //
        // Alternatively we could drop that requirement and add a dynamic check.
        root_guard: &'a RootGuard<'a>,
    ) -> RootedRefCellRef<'a, T> {
        // Prove that the lock is held for this tag.
        assert_eq!(
            root_guard.guard.tag, self.tag,
            "Expected {:?} Got {:?}",
            self.tag, root_guard.guard.tag
        );

        assert!(!self.writer.get());

        self.reader_count.set(self.reader_count.get() + 1);

        // Borrow from the guard to ensure the lock can't be dropped.
        RootedRefCellRef { guard: &self }
    }

    /// Borrow a mutable reference. Panics if `root_guard` is for the wrong tag,
    /// or if this object is already borrowed.
    pub fn borrow_mut<'a>(
        &'a self,
        // 'a required here for safety, as for `borrow`.
        root_guard: &'a RootGuard<'a>,
    ) -> RootedRefCellRefMut<'a, T> {
        // Prove that the lock is held for this tag.
        assert_eq!(
            root_guard.guard.tag, self.tag,
            "Expected {:?} Got {:?}",
            self.tag, root_guard.guard.tag
        );

        assert!(!self.writer.get());
        assert!(self.reader_count.get() == 0);

        self.writer.set(true);

        RootedRefCellRefMut { guard: &self }
    }
}

unsafe impl<T: Send> Send for RootedRefCell<T> {}
unsafe impl<T: Send> Sync for RootedRefCell<T> {}

pub struct RootedRefCellRef<'a, T> {
    guard: &'a RootedRefCell<T>,
}

impl<'a, T> std::ops::Deref for RootedRefCellRef<'a, T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        unsafe { &*self.guard.val.get() }
    }
}

impl<'a, T> Drop for RootedRefCellRef<'a, T> {
    fn drop(&mut self) {
        self.guard
            .reader_count
            .set(self.guard.reader_count.get() - 1);
    }
}

pub struct RootedRefCellRefMut<'a, T> {
    guard: &'a RootedRefCell<T>,
}

impl<'a, T> std::ops::Deref for RootedRefCellRefMut<'a, T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        unsafe { &*self.guard.val.get() }
    }
}

impl<'a, T> std::ops::DerefMut for RootedRefCellRefMut<'a, T> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        unsafe { &mut *self.guard.val.get() }
    }
}

impl<'a, T> Drop for RootedRefCellRefMut<'a, T> {
    fn drop(&mut self) {
        self.guard.writer.set(false);
    }
}

#[cfg(test)]
mod test_rooted_refcell {
    use std::thread;

    use super::*;

    use crate::rc::RootedRc;
    use crate::Root;

    #[test]
    fn construct_and_drop() {
        let root = Root::new();
        let _lock = root.lock();
        let _ = RootedRefCell::new(&root, 0);
    }

    #[test]
    fn share_with_worker_thread() {
        let root = Root::new();
        let rc = RootedRc::new(&root, RootedRefCell::new(&root, 0));
        let root = {
            let rc = {
                let lock = root.lock();
                rc.clone(&lock)
            };
            thread::spawn(move || {
                let lock = root.lock();
                let mut borrow = rc.borrow_mut(&lock);
                *borrow = 3;
                // Drop rc with lock still held.
                drop(borrow);
                rc.safely_drop(&lock);
                drop(lock);
                root
            })
            .join()
            .unwrap()
        };
        // Lock root again ourselves to inspect and drop rc.
        let lock = root.lock();
        let borrow = rc.borrow(&lock);
        assert_eq!(*borrow, 3);
        drop(borrow);
        rc.safely_drop(&lock);
        drop(lock);
    }
}
