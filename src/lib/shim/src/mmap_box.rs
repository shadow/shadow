use core::ptr::NonNull;

use rustix::mm::{MapFlags, ProtFlags};

/// Analogous to `alloc::boxed::Box`, but directly uses `mmap` instead of a
/// global allocator.
///
/// Useful since we don't currently have a global allocator in the shim, and
/// probably don't want to install one that makes direct `mmap` calls for every
/// allocation, since that would be a performance footgun.
///
/// We should be able to replace this with `alloc::boxed::Box<T>` if and when we
/// implement a global allocator suitable for the shim.  (Or with
/// `alloc::boxed::Box<T, MmapAllocator>` when non-global allocators are
/// stabilized)
pub struct MmapBox<T> {
    ptr: Option<NonNull<T>>,
}
unsafe impl<T> Send for MmapBox<T> where T: Send {}
unsafe impl<T> Sync for MmapBox<T> where T: Sync {}

impl<T> MmapBox<T> {
    pub fn new(x: T) -> Self {
        #[cfg(not(miri))]
        {
            let ptr: *mut core::ffi::c_void = unsafe {
                rustix::mm::mmap_anonymous(
                    core::ptr::null_mut(),
                    core::mem::size_of::<T>(),
                    ProtFlags::READ | ProtFlags::WRITE,
                    MapFlags::PRIVATE,
                )
            }
            .unwrap();
            assert!(!ptr.is_null());

            // Memory returned by mmap is page-aligned, which is generally at least
            // 4096.  This should be enough for most types.
            assert_eq!(ptr.align_offset(core::mem::align_of::<T>()), 0);

            let ptr: *mut T = ptr.cast();
            unsafe { ptr.write(x) };
            Self {
                ptr: Some(NonNull::new(ptr).unwrap()),
            }
        }
        #[cfg(miri)]
        {
            Self {
                ptr: Some(NonNull::new(Box::into_raw(Box::new(x))).unwrap()),
            }
        }
    }

    #[allow(unused)]
    pub fn leak(mut this: MmapBox<T>) -> *mut T {
        this.ptr.take().unwrap().as_ptr()
    }
}

impl<T> core::ops::Deref for MmapBox<T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        unsafe { self.ptr.as_ref().unwrap().as_ref() }
    }
}

impl<T> core::ops::DerefMut for MmapBox<T> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        unsafe { self.ptr.as_mut().unwrap().as_mut() }
    }
}

impl<T> Drop for MmapBox<T> {
    fn drop(&mut self) {
        let Some(ptr) = self.ptr else {
            return;
        };
        let ptr = ptr.as_ptr();

        #[cfg(not(miri))]
        {
            unsafe { ptr.drop_in_place() }
            unsafe {
                rustix::mm::munmap(ptr.cast::<core::ffi::c_void>(), core::mem::size_of::<T>())
                    .unwrap()
            }
        }
        #[cfg(miri)]
        {
            drop(unsafe { Box::from_raw(ptr) })
        }
    }
}

#[cfg(test)]
mod test {
    use std::sync::Arc;

    use vasi_sync::lazy_lock::LazyLock;

    use super::*;

    #[test]
    fn test_basic() {
        let x = MmapBox::new(42);
        assert_eq!(*x, 42);
    }

    #[test]
    fn test_large_alloc() {
        // This should span multiple pages.
        let val = [0; 100_000];

        let x = MmapBox::new(val);
        assert_eq!(*x, val);
    }

    #[test]
    fn test_mutate() {
        let mut x = MmapBox::new(42);
        assert_eq!(*x, 42);
        *x += 1;
        assert_eq!(*x, 43);
    }

    #[test]
    fn test_drop() {
        let arc = Arc::new(());
        assert_eq!(Arc::strong_count(&arc), 1);
        {
            let _clone = MmapBox::new(arc.clone());
            assert_eq!(Arc::strong_count(&arc), 2);
        }
        assert_eq!(Arc::strong_count(&arc), 1);
    }

    #[test]
    fn test_leak() {
        static MY_LEAKED: LazyLock<&'static u32> =
            LazyLock::const_new(|| unsafe { &*MmapBox::leak(MmapBox::new(42)) });
        assert_eq!(**MY_LEAKED.force(), 42);
    }
}
