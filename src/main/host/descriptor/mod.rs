use atomic_refcell::AtomicRefCell;
use std::sync::Arc;

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

pub enum PosixFile {}

// will not compile if `PosixFile` is not Send + Sync
impl IsSend for PosixFile {}
impl IsSync for PosixFile {}

#[derive(Clone)]
pub struct Descriptor {
    file: Arc<AtomicRefCell<PosixFile>>,
    flags: i32,
}

#[allow(dead_code)]
impl Descriptor {
    pub fn new(file: Arc<AtomicRefCell<PosixFile>>) -> Self {
        Self { file, flags: 0 }
    }

    pub fn get_file(&self) -> &Arc<AtomicRefCell<PosixFile>> {
        &self.file
    }

    pub fn get_flags(&self) -> i32 {
        self.flags
    }

    pub fn set_flags(&mut self, flags: i32) {
        self.flags = flags;
    }

    pub fn add_flags(&mut self, flags: i32) {
        self.flags |= flags;
    }
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

    /// An opaque type used when passing `*const AtomicRefCell<File>` to C.
    #[no_mangle]
    pub enum PosixFileArc {}

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

    /// If the compat descriptor is a new descriptor, returns a pointer to the reference-counted
    /// posix file object. Otherwise returns NULL. The posix file object's ref count is not
    /// modified, so the pointer must not outlive the lifetime of the compat descriptor.
    #[no_mangle]
    pub extern "C" fn compatdescriptor_borrowPosixFile(
        descriptor: *mut CompatDescriptor,
    ) -> *const PosixFileArc {
        assert!(!descriptor.is_null());

        let descriptor = unsafe { &mut *descriptor };

        (match descriptor {
            CompatDescriptor::Legacy(_) => std::ptr::null_mut(),
            CompatDescriptor::New(d) => Arc::as_ptr(d.get_file()),
        }) as *const PosixFileArc
    }

    /// If the compat descriptor is a new descriptor, returns a pointer to the reference-counted
    /// posix file object. Otherwise returns NULL. The posix file object's ref count is
    /// incremented, so the pointer must always later be passed to `posixfile_drop()`, otherwise
    /// the memory will leak.
    #[no_mangle]
    pub extern "C" fn compatdescriptor_newRefPosixFile(
        descriptor: *mut CompatDescriptor,
    ) -> *const PosixFileArc {
        assert!(!descriptor.is_null());

        let descriptor = unsafe { &mut *descriptor };

        (match descriptor {
            CompatDescriptor::Legacy(_) => std::ptr::null_mut(),
            CompatDescriptor::New(d) => Arc::into_raw(Arc::clone(&d.get_file())),
        }) as *const PosixFileArc
    }

    /// Decrement the ref count of the posix file object. The pointer must not be used after
    /// calling this function.
    #[no_mangle]
    pub extern "C" fn posixfile_drop(file: *const PosixFileArc) {
        assert!(!file.is_null());

        unsafe { Arc::from_raw(file as *const AtomicRefCell<PosixFile>) };
    }

    /// Get the status of the posix file object.
    #[allow(unused_variables)]
    #[no_mangle]
    pub extern "C" fn posixfile_getStatus(file: *const PosixFileArc) -> c::Status {
        assert!(!file.is_null());

        let file = file as *const AtomicRefCell<PosixFile>;
        let file = unsafe { &*file };

        todo!();
    }

    /// Add a status listener to the posix file object. This will increment the status
    /// listener's ref count, and will decrement the ref count when this status listener is
    /// removed or when the posix file is freed/dropped.
    #[allow(unused_variables)]
    #[no_mangle]
    pub extern "C" fn posixfile_addListener(
        file: *const PosixFileArc,
        listener: *mut c::StatusListener,
    ) {
        assert!(!file.is_null());
        assert!(!listener.is_null());

        let file = file as *const AtomicRefCell<PosixFile>;
        let file = unsafe { &*file };

        todo!();
    }

    /// Remove a listener from the posix file object.
    #[allow(unused_variables)]
    #[no_mangle]
    pub extern "C" fn posixfile_removeListener(
        file: *const PosixFileArc,
        listener: *mut c::StatusListener,
    ) {
        assert!(!file.is_null());
        assert!(!listener.is_null());

        let file = file as *const AtomicRefCell<PosixFile>;
        let file = unsafe { &*file };

        todo!();
    }
}
