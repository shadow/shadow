use crate::cshadow as c;

/// A trait we can use as a compile-time check to make sure that an object is Send.
trait IsSend: Send {}

/// A trait we can use as a compile-time check to make sure that an object is Sync.
trait IsSync: Sync {}

/// A type that allows us to make a pointer Send + Sync since there is no way
/// to add these traits to the pointer itself.
#[derive(Clone, Copy)]
pub struct SyncSendPointer<T>(*mut T);

unsafe impl<T> Send for SyncSendPointer<T> {}
unsafe impl<T> Sync for SyncSendPointer<T> {}

impl<T> SyncSendPointer<T> {
    /// Get the pointer.
    pub fn ptr(&self) -> *mut T {
        self.0
    }

    /// Get a mutable reference to the pointer.
    pub fn ptr_ref(&mut self) -> &mut *mut T {
        &mut self.0
    }
}

#[derive(Clone)]
pub struct Descriptor {
    // TODO: implement the descriptor
    flags: i32,
}

// don't implement copy or clone without considering the legacy descriptor's ref count
#[allow(dead_code)]
pub enum CompatDescriptor {
    New(Descriptor),
    Legacy(SyncSendPointer<c::LegacyDescriptor>),
}

// will not compile if `CompatDescriptor` is not Send + Sync
impl IsSend for CompatDescriptor {}
impl IsSync for CompatDescriptor {}

impl Drop for CompatDescriptor {
    fn drop(&mut self) {
        if let CompatDescriptor::Legacy(d) = self {
            // unref the legacy descriptor object
            unsafe { c::descriptor_unref(d.ptr() as *mut core::ffi::c_void) };
        }
    }
}

mod export {
    use super::*;

    /// The new compat descriptor takes ownership of the reference to the legacy descriptor and
    /// does not increment its ref count, but will decrement the ref count when this compat
    /// descriptor is freed/dropped.
    #[no_mangle]
    pub extern "C" fn compatdescriptor_fromLegacy(
        legacy_descriptor: *mut c::LegacyDescriptor,
    ) -> *mut CompatDescriptor {
        assert!(!legacy_descriptor.is_null());

        let descriptor = CompatDescriptor::Legacy(SyncSendPointer(legacy_descriptor));
        Box::into_raw(Box::new(descriptor))
    }

    /// If the compat descriptor is a legacy descriptor, returns a pointer to the legacy
    /// descriptor object. Otherwise returns NULL. The legacy descriptor's ref count is not
    /// modified, so the pointer must not outlive the lifetime of the compat descriptor.
    #[no_mangle]
    pub extern "C" fn compatdescriptor_asLegacy(
        descriptor: *const CompatDescriptor,
    ) -> *mut c::LegacyDescriptor {
        assert!(!descriptor.is_null());

        let descriptor = unsafe { &*descriptor };

        if let CompatDescriptor::Legacy(d) = descriptor {
            d.ptr()
        } else {
            std::ptr::null_mut()
        }
    }

    /// When the compat descriptor is freed/dropped, it will decrement the legacy descriptor's
    /// ref count.
    #[no_mangle]
    pub extern "C" fn compatdescriptor_free(descriptor: *mut CompatDescriptor) {
        if descriptor.is_null() {
            return;
        }

        let descriptor = unsafe { &mut *descriptor };
        unsafe { Box::from_raw(descriptor) };
    }

    /// This is a no-op for non-legacy descriptors.
    #[no_mangle]
    pub extern "C" fn compatdescriptor_setHandle(
        descriptor: *mut CompatDescriptor,
        handle: libc::c_int,
    ) {
        assert!(!descriptor.is_null());

        let descriptor = unsafe { &mut *descriptor };

        if let CompatDescriptor::Legacy(d) = descriptor {
            unsafe { c::descriptor_setHandle(d.ptr(), handle) }
        }

        // new descriptor types don't store their file handle, so do nothing
    }
}
