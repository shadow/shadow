use std::collections::{BTreeSet, HashMap};

use log::*;
use shadow_shim_helper_rs::explicit_drop::ExplicitDrop;
use shadow_shim_helper_rs::syscall_types::SyscallReg;

use crate::host::descriptor::Descriptor;
use crate::host::host::Host;
use crate::utility::callback_queue::CallbackQueue;
use crate::utility::ObjectCounter;

/// POSIX requires fds to be assigned as `libc::c_int`, so we can't allow any fds larger than this.
pub const FD_MAX: u32 = i32::MAX as u32;

/// Map of file handles to file descriptors. Typically owned by a
/// [`Thread`][crate::host::thread::Thread].
#[derive(Clone)]
pub struct DescriptorTable {
    descriptors: HashMap<DescriptorHandle, Descriptor>,

    // Indices less than `next_index` known to be available.
    available_indices: BTreeSet<u32>,

    // Lowest index not in `available_indices` that *might* be available. We still need to verify
    // availability in `descriptors`, though.
    next_index: u32,

    _counter: ObjectCounter,
}

impl DescriptorTable {
    pub fn new() -> Self {
        DescriptorTable {
            descriptors: HashMap::new(),
            available_indices: BTreeSet::new(),
            next_index: 0,
            _counter: ObjectCounter::new("DescriptorTable"),
        }
    }

    /// Add the descriptor at an unused index, and return the index. If the descriptor could not be
    /// added, the descriptor is returned in the `Err`.
    fn add(
        &mut self,
        descriptor: Descriptor,
        min_index: DescriptorHandle,
    ) -> Result<DescriptorHandle, Descriptor> {
        let idx = if let Some(idx) = self.available_indices.range(min_index.val()..).next() {
            // Un-borrow from `available_indices`.
            let idx = *idx;
            // Take from `available_indices`
            trace!("Reusing available index {}", idx);
            self.available_indices.remove(&idx);
            idx
        } else {
            // Start our search at either the next likely available index or the minimum index,
            // whichever is larger.
            let mut idx = std::cmp::max(self.next_index, min_index.val());

            // Check if this index out of range.
            if idx > FD_MAX {
                return Err(descriptor);
            }

            // Only update next_index if we started at it, otherwise there may be other
            // available indexes lower than idx.
            let should_update_next_index = idx == self.next_index;

            // Skip past any indexes that are in use. This can happen after
            // calling `set` with a value greater than `next_index`.
            while self
                .descriptors
                .contains_key(&DescriptorHandle::new(idx).unwrap())
            {
                trace!("Skipping past in-use index {}", idx);

                // Check if the next index is out of range.
                if idx >= FD_MAX {
                    return Err(descriptor);
                }

                // Won't overflow because of the check above.
                idx += 1;
            }

            if should_update_next_index {
                self.next_index = idx + 1;
            }

            // Take the next index.
            trace!("Using index {}", idx);
            idx
        };

        let idx = DescriptorHandle::new(idx).unwrap();

        let prev = self.descriptors.insert(idx, descriptor);
        assert!(prev.is_none(), "Already a descriptor at {}", idx);

        Ok(idx)
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

    /// Get the descriptor at `idx`, if any.
    pub fn get(&self, idx: DescriptorHandle) -> Option<&Descriptor> {
        self.descriptors.get(&idx)
    }

    /// Get the descriptor at `idx`, if any.
    pub fn get_mut(&mut self, idx: DescriptorHandle) -> Option<&mut Descriptor> {
        self.descriptors.get_mut(&idx)
    }

    /// Insert a descriptor at `index`. If a descriptor is already present at that index, it is
    /// unregistered from that index and returned.
    #[must_use]
    fn set(&mut self, index: DescriptorHandle, descriptor: Descriptor) -> Option<Descriptor> {
        // We ensure the index is no longer in `self.available_indices`. We *don't* ensure
        // `self.next_index` is > `index`, since that'd require adding the indices in between to
        // `self.available_indices`. It uses less memory and is no more expensive to iterate when
        // *using* `self.available_indices` instead.
        self.available_indices.remove(&index.val());

        let prev = self.descriptors.insert(index, descriptor);

        if prev.is_some() {
            trace!("Overwriting index {}", index);
        } else {
            trace!("Setting to unused index {}", index);
        }

        prev
    }

    /// Register a descriptor and return its fd handle. Equivalent to
    /// [`register_descriptor_with_min_fd(desc, 0)`][Self::register_descriptor_with_min_fd]. If the
    /// descriptor could not be added, the descriptor is returned in the `Err`.
    pub fn register_descriptor(
        &mut self,
        desc: Descriptor,
    ) -> Result<DescriptorHandle, Descriptor> {
        const ZERO: DescriptorHandle = match DescriptorHandle::new(0) {
            Some(x) => x,
            None => unreachable!(),
        };
        self.add(desc, ZERO)
    }

    /// Register a descriptor and return its fd handle. If the descriptor could not be added, the
    /// descriptor is returned in the `Err`.
    pub fn register_descriptor_with_min_fd(
        &mut self,
        desc: Descriptor,
        min_fd: DescriptorHandle,
    ) -> Result<DescriptorHandle, Descriptor> {
        self.add(desc, min_fd)
    }

    /// Register a descriptor with a given fd handle and return the descriptor that it replaced.
    #[must_use]
    pub fn register_descriptor_with_fd(
        &mut self,
        desc: Descriptor,
        new_fd: DescriptorHandle,
    ) -> Option<Descriptor> {
        self.set(new_fd, desc)
    }

    /// Deregister the descriptor with the given fd handle and return it.
    #[must_use]
    pub fn deregister_descriptor(&mut self, fd: DescriptorHandle) -> Option<Descriptor> {
        let maybe_descriptor = self.descriptors.remove(&fd);
        self.available_indices.insert(fd.val());
        self.trim_tail();
        maybe_descriptor
    }

    /// Remove and return all descriptors.
    pub fn remove_all(&mut self) -> impl Iterator<Item = Descriptor> {
        // reset the descriptor table
        let old_self = std::mem::replace(self, Self::new());
        // return the old descriptors
        old_self.descriptors.into_values()
    }

    /// Remove and return all descriptors in the range. If you want to remove all descriptors, you
    /// should use [`remove_all`](Self::remove_all).
    pub fn remove_range(
        &mut self,
        range: impl std::ops::RangeBounds<DescriptorHandle>,
    ) -> impl Iterator<Item = Descriptor> {
        // This code is not very efficient but it shouldn't be called often, so it should be fine
        // for now. If we wanted something more efficient, we'd need to redesign the descriptor
        // table to not use a hash map.

        let fds: Vec<_> = self
            .iter()
            .filter_map(|(fd, _)| range.contains(fd).then_some(*fd))
            .collect();

        let mut descriptors = Vec::with_capacity(fds.len());
        for fd in fds {
            descriptors.push(self.deregister_descriptor(fd).unwrap());
        }

        descriptors.into_iter()
    }

    pub fn iter(&self) -> impl Iterator<Item = (&DescriptorHandle, &Descriptor)> {
        self.descriptors.iter()
    }

    pub fn iter_mut(&mut self) -> impl Iterator<Item = (&DescriptorHandle, &mut Descriptor)> {
        self.descriptors.iter_mut()
    }
}

impl Default for DescriptorTable {
    fn default() -> Self {
        Self::new()
    }
}

impl ExplicitDrop for DescriptorTable {
    type ExplicitDropParam = Host;
    type ExplicitDropResult = ();

    fn explicit_drop(mut self, host: &Host) {
        // Drop all descriptors using a callback queue.
        //
        // Doing this explicitly instead of letting `DescriptorTable`'s `Drop`
        // implementation implicitly close these individually is a performance
        // optimization so that all descriptors are closed before any of their
        // callbacks run.
        let descriptors = self.remove_all();
        crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
            CallbackQueue::queue_and_run(|cb_queue| {
                for desc in descriptors {
                    desc.close(host, cb_queue);
                }
            })
        });
    }
}

/// A handle for a file descriptor.
#[derive(Copy, Clone, Debug, Hash, PartialEq, Eq, PartialOrd, Ord)]
pub struct DescriptorHandle(u32);

impl DescriptorHandle {
    /// Returns `Some` if `fd` is less than [`FD_MAX`]. Can be used in `const` contexts.
    pub const fn new(fd: u32) -> Option<Self> {
        if fd > FD_MAX {
            return None;
        }

        Some(DescriptorHandle(fd))
    }

    pub fn val(&self) -> u32 {
        self.0
    }
}

impl std::fmt::Display for DescriptorHandle {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.0.fmt(f)
    }
}

impl From<DescriptorHandle> for u32 {
    fn from(x: DescriptorHandle) -> u32 {
        x.0
    }
}

impl From<DescriptorHandle> for u64 {
    fn from(x: DescriptorHandle) -> u64 {
        x.0.into()
    }
}

impl From<DescriptorHandle> for i32 {
    fn from(x: DescriptorHandle) -> i32 {
        const _: () = assert!(FD_MAX <= i32::MAX as u32);
        // the constructor makes sure this won't panic
        x.0.try_into().unwrap()
    }
}

impl From<DescriptorHandle> for i64 {
    fn from(x: DescriptorHandle) -> i64 {
        x.0.into()
    }
}

impl From<DescriptorHandle> for SyscallReg {
    fn from(x: DescriptorHandle) -> SyscallReg {
        x.0.into()
    }
}

impl TryFrom<u32> for DescriptorHandle {
    type Error = DescriptorHandleError;
    fn try_from(x: u32) -> Result<Self, Self::Error> {
        DescriptorHandle::new(x).ok_or(DescriptorHandleError())
    }
}

impl TryFrom<u64> for DescriptorHandle {
    // use the same error type as the conversion from u32
    type Error = <DescriptorHandle as TryFrom<u32>>::Error;
    fn try_from(x: u64) -> Result<Self, Self::Error> {
        u32::try_from(x)
            .or(Err(DescriptorHandleError()))?
            .try_into()
    }
}

impl TryFrom<i32> for DescriptorHandle {
    type Error = DescriptorHandleError;
    fn try_from(x: i32) -> Result<Self, Self::Error> {
        x.try_into()
            .ok()
            .and_then(DescriptorHandle::new)
            .ok_or(DescriptorHandleError())
    }
}

impl TryFrom<i64> for DescriptorHandle {
    // use the same error type as the conversion from i32
    type Error = <DescriptorHandle as TryFrom<i32>>::Error;
    fn try_from(x: i64) -> Result<Self, Self::Error> {
        i32::try_from(x)
            .or(Err(DescriptorHandleError()))?
            .try_into()
    }
}

/// The handle is not valid.
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct DescriptorHandleError();

impl std::fmt::Display for DescriptorHandleError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "Not a valid descriptor handle")
    }
}

impl std::error::Error for DescriptorHandleError {}
