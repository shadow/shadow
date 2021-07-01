use super::CompatDescriptor;
use crate::cshadow;
use crate::utility::notnull::*;
use log::*;
use std::collections::{BTreeSet, HashMap};
use std::convert::TryInto;

/// Table of (file) descriptors. Typically owned by a Process.
pub struct DescriptorTable {
    descriptors: HashMap<u32, CompatDescriptor>,

    // Indices less than `next_index` known to be available.
    available_indices: BTreeSet<u32>,

    // Lowest index not in `available_indices` that *might* be available. We still need to verify
    // availability in `descriptors`, though.
    next_index: u32,
}

impl DescriptorTable {
    pub fn new() -> Self {
        DescriptorTable {
            descriptors: HashMap::new(),
            available_indices: BTreeSet::new(),
            next_index: 0,
        }
    }

    /// Add the descriptor at an unused index, and return the index.
    pub fn add(&mut self, mut descriptor: CompatDescriptor) -> u32 {
        let idx = if let Some(idx) = self.available_indices.iter().next() {
            // Un-borrow from `available_indices`.
            let idx = *idx;
            // Take from `available_indices`
            trace!("Reusing available index {}", idx);
            self.available_indices.remove(&idx);
            idx
        } else {
            // Skip past any indexes that are in use. This can happen after
            // calling `set` with a value greater than `next_index`.
            while self.descriptors.contains_key(&self.next_index) {
                trace!("Skipping past in-use index {}", self.next_index);
                self.next_index += 1;
            }
            // Take the next index.
            let idx = self.next_index;
            trace!("Using index {}", idx);
            self.next_index += 1;
            idx
        };

        descriptor.set_handle(idx);
        let prev = self.descriptors.insert(idx, descriptor);
        debug_assert!(prev.is_none(), "Already a descriptor at {}", idx);

        idx
    }

    // Call after inserting to `available_indices`, to free any that are contiguous
    // with `next_index`.
    fn trim_tail(&mut self) {
        loop {
            let last_in_available = match self.available_indices.iter().next_back() {
                Some(i) => *i,
                None => break,
            };
            if (last_in_available + 1) == self.next_index {
                // Last entry in available_indices is adjacent to next_index.
                // We can merge them, freeing an entry in `available_indices`.
                self.next_index -= 1;
                self.available_indices.remove(&last_in_available);
            } else {
                break;
            }
        }
    }

    /// Remove the descriptor at the given index and return it.
    pub fn remove(&mut self, idx: u32) -> Option<CompatDescriptor> {
        let mut maybe_descriptor = self.descriptors.remove(&idx);
        self.available_indices.insert(idx);
        self.trim_tail();
        if let Some(descriptor) = &mut maybe_descriptor {
            descriptor.set_handle(0);
        }
        maybe_descriptor
    }

    /// Get the descriptor at `idx`, if any.
    pub fn get(&self, idx: u32) -> Option<&CompatDescriptor> {
        self.descriptors.get(&idx)
    }

    /// Insert a descriptor at `index`. If a descriptor is already present at
    /// that index, it is unregistered from that index and returned.
    pub fn set(
        &mut self,
        index: u32,
        mut descriptor: CompatDescriptor,
    ) -> Option<CompatDescriptor> {
        descriptor.set_handle(index);

        // We ensure the index is no longer in `self.available_indices`. We *don't* ensure
        // `self.next_index` is > `index`, since that'd require adding the indices in between to
        // `self.available_indices`. It uses less memory and is no more expensive to iterate when
        // *using* `self.available_indices` instead.
        self.available_indices.remove(&index);

        if let Some(mut prev) = self.descriptors.insert(index, descriptor) {
            trace!("Overwriting index {}", index);
            prev.set_handle(0);
            Some(prev)
        } else {
            trace!("Setting to unused index {}", index);
            None
        }
    }

    /// This is a helper function that handles some corner cases where some
    /// descriptors are linked to each other and we must remove that link in
    /// order to ensure that the reference count reaches zero and they are properly
    /// freed. Otherwise the circular reference will prevent the free operation.
    /// TODO: remove this once the TCP layer is better designed.
    pub fn shutdown_helper(&mut self) {
        for descriptor in self.descriptors.values() {
            match descriptor {
                CompatDescriptor::New(_) => continue,
                CompatDescriptor::Legacy(d) => unsafe {
                    cshadow::descriptor_shutdownHelper(d.ptr())
                },
            };
        }
    }
}

mod export {
    use super::*;
    use libc::c_int;

    /// Create an object that can be used to store all descriptors created by a
    /// process. When the table is no longer required, use descriptortable_free
    /// to release the reference.
    #[no_mangle]
    pub unsafe extern "C" fn descriptortable_new() -> *mut DescriptorTable {
        Box::into_raw(Box::new(DescriptorTable::new()))
    }

    /// Free the table.
    #[no_mangle]
    pub unsafe extern "C" fn descriptortable_free(table: *mut DescriptorTable) {
        unsafe { Box::from_raw(notnull_mut_debug(table)) };
    }

    #[no_mangle]
    pub unsafe extern "C" fn descriptortable_iter(
        table: *mut DescriptorTable,
        f: unsafe extern "C" fn(*mut CompatDescriptor, *mut libc::c_void),
        data: *mut libc::c_void,
    ) {
        let table = unsafe { table.as_mut().unwrap() };

        for desc in table.descriptors.values_mut() {
            unsafe { f(desc as *mut _, data) };
        }
    }

    /// Store a descriptor object for later reference at the next available index
    /// in the table. The chosen table index is stored in the descriptor object and
    /// returned. The descriptor is guaranteed to be stored successfully.
    ///
    /// The table takes ownership of the descriptor, invalidating the caller's pointer.
    #[no_mangle]
    pub unsafe extern "C" fn descriptortable_add(
        table: *mut DescriptorTable,
        descriptor: *mut CompatDescriptor,
    ) -> c_int {
        let table = unsafe { table.as_mut().unwrap() };
        let descriptor = CompatDescriptor::from_raw(descriptor).unwrap();
        table.add(*descriptor).try_into().unwrap()
    }

    /// Stop storing the descriptor so that it can no longer be referenced. The table
    /// index that was used to store the descriptor is cleared from the descriptor
    /// and may be assigned to new descriptors that are later added to the table.
    ///
    /// Returns an owned pointer to the CompatDescriptor if the descriptor was found
    /// in the table and removed, and NULL otherwise.
    #[no_mangle]
    pub unsafe extern "C" fn descriptortable_remove(
        table: *mut DescriptorTable,
        index: c_int,
    ) -> *mut CompatDescriptor {
        let table = unsafe { table.as_mut().unwrap() };
        match table.remove(index.try_into().unwrap()) {
            Some(d) => CompatDescriptor::into_raw(Box::new(d)),

            None => std::ptr::null_mut(),
        }
    }

    /// Returns the descriptor at the given table index, or NULL if we are not
    /// storing a descriptor at the given index.
    #[no_mangle]
    pub unsafe extern "C" fn descriptortable_get(
        table: *const DescriptorTable,
        index: c_int,
    ) -> *const CompatDescriptor {
        let table = unsafe { table.as_ref().unwrap() };
        let index: u32 = match index.try_into() {
            Ok(i) => i,
            Err(e) => {
                debug!("Bad descriptor idx {}: {:?}", index, e);
                return std::ptr::null();
            }
        };
        match table.get(index) {
            Some(d) => d as *const CompatDescriptor,
            None => std::ptr::null(),
        }
    }

    /// Store the given descriptor at given index. Any previous descriptor that was
    /// stored there will be removed and its table index will be cleared. This
    /// unrefs any existing descriptor stored at index as in remove(), and consumes
    /// a ref to the existing descriptor as in add().
    #[no_mangle]
    pub unsafe extern "C" fn descriptortable_set(
        table: *mut DescriptorTable,
        index: c_int,
        descriptor: *mut CompatDescriptor,
    ) {
        let table = unsafe { table.as_mut().unwrap() };
        let descriptor = CompatDescriptor::from_raw(descriptor);
        // Let returned value drop, if any.
        table.set(index.try_into().unwrap(), *descriptor.unwrap());
    }

    /// This is a helper function that handles some corner cases where some
    /// descriptors are linked to each other and we must remove that link in
    /// order to ensure that the reference count reaches zero and they are properly
    /// freed. Otherwise the circular reference will prevent the free operation.
    /// TODO: remove this once the TCP layer is better designed.
    #[no_mangle]
    pub unsafe extern "C" fn descriptortable_shutdownHelper(table: *mut DescriptorTable) {
        let table = unsafe { table.as_mut().unwrap() };
        table.shutdown_helper();
    }
}
