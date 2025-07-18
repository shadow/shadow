use std::collections::HashMap;
use std::collections::hash_map::Entry;

use shadow_shim_helper_rs::syscall_types::ManagedPhysicalMemoryAddr;
use shadow_shim_helper_rs::util::SyncSendPointer;

use crate::cshadow as c;
use crate::utility::ObjectCounter;

/// A map of [`ManagedPhysicalMemoryAddr`] to [`Futex`](c::Futex).
pub struct FutexTable {
    /// All futexes that we are tracking. Each futex has a unique physical address associated with
    /// it when it is stored in our table, which we refer to as a table index or table indices.
    futexes: HashMap<ManagedPhysicalMemoryAddr, FutexRef>,
    _counter: ObjectCounter,
}

impl FutexTable {
    #[allow(clippy::new_without_default)]
    pub fn new() -> Self {
        Self {
            futexes: HashMap::new(),
            _counter: ObjectCounter::new("FutexTable"),
        }
    }

    /// Add the futex to the table. If the futex already exists in the table, `futex` will be
    /// returned in the `Err` value.
    pub fn add(&mut self, futex: FutexRef) -> Result<(), FutexRef> {
        let addr = futex.physical_addr();

        match self.futexes.entry(addr) {
            Entry::Occupied(_) => Err(futex),
            Entry::Vacant(x) => {
                x.insert(futex);
                Ok(())
            }
        }
    }

    pub fn remove(&mut self, addr: ManagedPhysicalMemoryAddr) -> Option<FutexRef> {
        self.futexes.remove(&addr)
    }

    pub fn get(&self, addr: ManagedPhysicalMemoryAddr) -> Option<&FutexRef> {
        self.futexes.get(&addr)
    }
}

/// An owned reference to a [`Futex`][c::Futex].
#[derive(Debug)]
pub struct FutexRef(SyncSendPointer<c::Futex>);

impl FutexRef {
    /// Takes ownership of the reference.
    ///
    /// # Safety
    ///
    /// The pointer must be a valid [`Futex`][c::Futex].
    pub unsafe fn new(ptr: *mut c::Futex) -> Self {
        debug_assert!(!ptr.is_null());
        Self(unsafe { SyncSendPointer::new(ptr) })
    }

    pub fn ptr(&self) -> *mut c::Futex {
        self.0.ptr()
    }

    pub fn physical_addr(&self) -> ManagedPhysicalMemoryAddr {
        unsafe { c::futex_getAddress(self.ptr()) }
    }

    pub fn wake(&self, num_wakeups: libc::c_uint) -> libc::c_uint {
        unsafe { c::futex_wake(self.ptr(), num_wakeups) }
    }

    pub fn listener_count(&self) -> libc::c_uint {
        unsafe { c::futex_getListenerCount(self.ptr()) }
    }

    /// Ownership of the reference is transferred to the returned pointer.
    ///
    /// In otherwords, `self` is dropped without changing the futex's refcount, and the returned
    /// pointer can be safely used. The refcount should be decremented when the returned pointer is
    /// no longer used.
    pub fn into_c_ptr(self) -> *mut c::Futex {
        let ptr = self.ptr();
        unsafe { c::futex_ref(self.0.ptr()) };
        ptr
    }
}

impl Clone for FutexRef {
    fn clone(&self) -> Self {
        unsafe { c::futex_ref(self.0.ptr()) };
        Self(self.0)
    }
}

impl std::ops::Drop for FutexRef {
    fn drop(&mut self) {
        unsafe { c::futex_unref(self.0.ptr()) };
    }
}

mod export {
    use super::*;

    /// This does not consume the `futex` reference.
    #[unsafe(no_mangle)]
    pub unsafe extern "C-unwind" fn futextable_add(
        table: *mut FutexTable,
        futex: *mut c::Futex,
    ) -> bool {
        let table = unsafe { table.as_mut() }.unwrap();

        assert!(!futex.is_null());
        unsafe { c::futex_ref(futex) };
        let futex = unsafe { FutexRef::new(futex) };

        table.add(futex).is_ok()
    }

    #[unsafe(no_mangle)]
    pub unsafe extern "C-unwind" fn futextable_remove(
        table: *mut FutexTable,
        addr: ManagedPhysicalMemoryAddr,
    ) -> bool {
        let table = unsafe { table.as_mut() }.unwrap();
        table.remove(addr).is_some()
    }

    /// This returns a borrowed reference. If you don't increment the refcount of the returned
    /// futex, then the returned pointer will be invalidated if the futex table is mutated.
    #[unsafe(no_mangle)]
    pub unsafe extern "C-unwind" fn futextable_get(
        table: *mut FutexTable,
        addr: ManagedPhysicalMemoryAddr,
    ) -> *mut c::Futex {
        let table = unsafe { table.as_mut() }.unwrap();
        table
            .get(addr)
            .map(|x| x.ptr())
            .unwrap_or(std::ptr::null_mut())
    }
}
