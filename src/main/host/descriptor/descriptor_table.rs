use crate::cshadow as c;
use crate::host::descriptor::{CompatFile, Descriptor};
use crate::utility::notnull::*;

use std::collections::{BTreeSet, HashMap};

use log::*;

/// Map of file handles to file descriptors. Typically owned by a Process.
pub struct DescriptorTable {
    descriptors: HashMap<u32, Descriptor>,

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
    pub fn add(&mut self, descriptor: Descriptor, min_index: u32) -> u32 {
        let idx = if let Some(idx) = self.available_indices.range(min_index..).next() {
            // Un-borrow from `available_indices`.
            let idx = *idx;
            // Take from `available_indices`
            trace!("Reusing available index {}", idx);
            self.available_indices.remove(&idx);
            idx
        } else {
            // Start our search at either the next likely available index or the minimum index,
            // whichever is larger.
            let mut idx = std::cmp::max(self.next_index, min_index);

            // Only update next_index if we started at it, otherwise there may be other
            // available indexes lower than idx.
            let should_update_next_index = idx == self.next_index;

            // Skip past any indexes that are in use. This can happen after
            // calling `set` with a value greater than `next_index`.
            while self.descriptors.contains_key(&idx) {
                trace!("Skipping past in-use index {}", idx);
                idx += 1;
            }

            if should_update_next_index {
                self.next_index = idx + 1;
            }

            // Take the next index.
            trace!("Using index {}", idx);
            idx
        };

        let prev = self.descriptors.insert(idx, descriptor);
        debug_assert!(prev.is_none(), "Already a descriptor at {}", idx);

        idx
    }

    // Call after inserting to `available_indices`, to free any that are contiguous
    // with `next_index`.
    fn trim_tail(&mut self) {
        while let Some(last_in_available) = self.available_indices.iter().next_back().copied() {
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
    pub fn remove(&mut self, idx: u32) -> Option<Descriptor> {
        let maybe_descriptor = self.descriptors.remove(&idx);
        self.available_indices.insert(idx);
        self.trim_tail();
        maybe_descriptor
    }

    /// Get the descriptor at `idx`, if any.
    pub fn get(&self, idx: u32) -> Option<&Descriptor> {
        self.descriptors.get(&idx)
    }

    /// Get the descriptor at `idx`, if any.
    pub fn get_mut(&mut self, idx: u32) -> Option<&mut Descriptor> {
        self.descriptors.get_mut(&idx)
    }

    /// Insert a descriptor at `index`. If a descriptor is already present at
    /// that index, it is unregistered from that index and returned.
    pub fn set(&mut self, index: u32, descriptor: Descriptor) -> Option<Descriptor> {
        // We ensure the index is no longer in `self.available_indices`. We *don't* ensure
        // `self.next_index` is > `index`, since that'd require adding the indices in between to
        // `self.available_indices`. It uses less memory and is no more expensive to iterate when
        // *using* `self.available_indices` instead.
        self.available_indices.remove(&index);

        if let Some(prev) = self.descriptors.insert(index, descriptor) {
            trace!("Overwriting index {}", index);
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
            match descriptor.file() {
                CompatFile::New(_) => continue,
                CompatFile::Legacy(f) => unsafe { c::legacyfile_shutdownHelper(f.ptr()) },
            };
        }
    }

    /// Remove and return all descriptors.
    pub fn remove_all<'a>(&mut self) -> impl Iterator<Item = Descriptor> {
        // reset the descriptor table
        let old_self = std::mem::replace(self, Self::new());
        // return the old descriptors
        old_self.descriptors.into_values()
    }
}

mod export {
    use super::*;
    use crate::host::{descriptor::CallbackQueue, host::Host};
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

    /// Store the given descriptor at the given index. Any previous descriptor that was
    /// stored there will be returned. This consumes a ref to the given descriptor as in
    /// add(), and any returned descriptor must be freed manually.
    #[no_mangle]
    pub unsafe extern "C" fn descriptortable_set(
        table: *mut DescriptorTable,
        index: c_int,
        descriptor: *mut Descriptor,
    ) -> *mut Descriptor {
        let table = unsafe { table.as_mut().unwrap() };
        let descriptor = Descriptor::from_raw(descriptor);

        match table.set(index.try_into().unwrap(), *descriptor.unwrap()) {
            Some(d) => Descriptor::into_raw(Box::new(d)),
            None => std::ptr::null_mut(),
        }
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

    /// Close all descriptors. The `host` option is a legacy option for legacy files.
    #[no_mangle]
    pub unsafe extern "C" fn descriptortable_removeAndCloseAll(
        table: *mut DescriptorTable,
        host: *const Host,
    ) {
        let table = unsafe { table.as_mut().unwrap() };
        let host = unsafe { host.as_ref().unwrap() };

        CallbackQueue::queue_and_run(|cb_queue| {
            for desc in table.remove_all() {
                desc.close(host, cb_queue);
            }
        });
    }
}
