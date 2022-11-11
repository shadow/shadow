use std::{marker::PhantomData, ops::Deref};

use once_cell::sync::OnceCell;
use vasi::VirtualAddressSpaceIndependent;

use crate::c_bindings;

enum ShMemBlockOrigin<'a> {
    Allocator(&'a Allocator),
    Serializer(&'a Serializer),
}

/// A typed pointer to shared memory. Does *not* track the lifetime of the
/// underlying shared memory, which may have been allocated, and may be
/// deallocated, by another process. See also the `Safety` comments for methods
/// creating a ShMemBlock.
///
/// TODO: Wrap objects allocated via the shared memory allocator with some
/// validity flag/cookie and reference count in shared memory?
pub struct ShMemBlock<'origin, T>
// T must be Sync, since it will be simultaneously available to multiple threads
// (and processes).
// T mut be VirtualAddressSpaceIndependent, since it may be simultaneously
// mapped into different virtual address spaces.
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    internal: c_bindings::ShMemBlock,
    origin: ShMemBlockOrigin<'origin>,
    _phantom: PhantomData<T>,
}

// SAFETY: T is already required to be Sync, and ShMemBlock only exposes
// immutable references to the underlying data.
unsafe impl <'origin, T> Sync for ShMemBlock<'origin, T> where T: Sync + VirtualAddressSpaceIndependent {}
unsafe impl <'origin, T> Send for ShMemBlock<'origin, T> where T: Send + Sync + VirtualAddressSpaceIndependent {}

impl<'origin, T> ShMemBlock<'origin, T>
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    /// Panics if the ShMemBlock is NULL, the incorrect size, or unaligned.
    ///
    /// SAFETY: The memory pointed to by `internal` should already be initialized
    /// to an instance of `T`, and must not be deallocated while the returned
    /// object is still alive.
    unsafe fn new(internal: c_bindings::ShMemBlock, origin: ShMemBlockOrigin<'origin>) -> Self {
        assert_eq!(internal.nbytes as usize, std::mem::size_of::<T>());
        assert!(!internal.p.is_null());
        assert_eq!(internal.p.align_offset(std::mem::align_of::<T>()), 0);
        Self {
            internal,
            origin,
            _phantom: PhantomData,
        }
    }

    /// Deallocates the backing storage from shared memory. Panics
    /// if `block` was not created via an [`Allocator`].
    ///
    /// SAFETY: There must be no live references to this memory, including
    /// via other [`ShMemBlock`] objects that alias this one.
    pub unsafe fn deallocate(mut self) {
        match self.origin {
            ShMemBlockOrigin::Allocator(a) => {
                unsafe {
                    c_bindings::shmemallocator_free(a.internal, &mut self.internal as *mut _)
                };
            }
            ShMemBlockOrigin::Serializer(_) => {
                panic!("Cannot deallocate a block that was instantiated via a Serializer");
            }
        }
    }

    pub fn serialize(&self) -> ShMemBlockSerialized {
        let serialized = match self.origin {
            ShMemBlockOrigin::Allocator(a) => unsafe {
                c_bindings::shmemallocator_blockSerialize(a.internal, &self.internal)
            },
            ShMemBlockOrigin::Serializer(s) => unsafe {
                c_bindings::shmemserializer_blockSerialize(s.internal, &self.internal)
            },
        };
        ShMemBlockSerialized {
            internal: serialized,
        }
    }

    // This function will fail to compile for a T that isn't FFI-safe.
    #[deny(improper_ctypes_definitions)]
    extern "C" fn _validate_stable_layout(_: T) {}
}

impl<'origin, T> Deref for ShMemBlock<'origin, T>
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    type Target = T;

    fn deref(&self) -> &Self::Target {
        // SAFETY: Validity ensured by [`ShMemBlock::new`].
        unsafe { (self.internal.p as *const T).as_ref().unwrap() }
    }
}

pub struct ShMemBlockSerialized {
    internal: c_bindings::ShMemBlockSerialized,
}

impl ShMemBlockSerialized {
    // Keep in sync with macro of same name in shmem_allocator.h.
    const SHD_SHMEM_BLOCK_SERIALIZED_MAX_STRLEN: usize = 21 + 21 + 21 + 256 + 1;

    pub fn to_string(&self) -> String {
        let mut buf = Vec::new();
        buf.resize(Self::SHD_SHMEM_BLOCK_SERIALIZED_MAX_STRLEN, 0i8);
        unsafe { c_bindings::shmemblockserialized_toString(&self.internal, buf.as_mut_ptr()) };
        let buf = buf
            .iter()
            .take_while(|c| **c != 0)
            .map(|c| *c as u8)
            .collect();
        String::from_utf8(buf).unwrap()
    }

    pub fn from_string(s: &str) -> Option<Self> {
        let mut err: bool = false;
        let mut buf: Vec<i8> = s.as_bytes().iter().map(|b| *b as i8).collect();
        // Null terminate.
        buf.push(0);
        let res = Self {
            internal: unsafe {
                c_bindings::shmemblockserialized_fromString(buf.as_ptr(), &mut err)
            },
        };
        if err {
            None
        } else {
            Some(res)
        }
    }
}

pub struct Allocator {
    internal: *mut c_bindings::ShMemAllocator,
}

// SAFETY: The C bindings for ShMemAllocator use internal locking to ensure
// thread-safe access.
//
// TODO: Consider using SyncSendPointer instead of directly implementing these.
// Would require reorganizing crates to avoid a circular dependency, though.
unsafe impl Send for Allocator {}
unsafe impl Sync for Allocator {}

impl Allocator {
    pub fn global() -> &'static Self {
        static GLOBAL: OnceCell<Allocator> = OnceCell::new();
        GLOBAL.get_or_init(|| Self {
            internal: unsafe { c_bindings::shmemallocator_getGlobal() },
        })
    }

    /// Safety: The returned [`ShMemBlock`] must not be deallocated as long as
    /// any [`ShMemBlock`] aliasing it is still alive.
    pub unsafe fn alloc<T>(&self, val: T) -> ShMemBlock<T>
    where
        T: Sync + VirtualAddressSpaceIndependent,
    {
        let nbytes = std::mem::size_of_val(&val);
        let raw_block =
            unsafe { c_bindings::shmemallocator_alloc(self.internal, nbytes.try_into().unwrap()) };
        assert_eq!(raw_block.nbytes as usize, nbytes);
        assert!(!raw_block.p.is_null());
        assert_eq!(raw_block.p.align_offset(std::mem::align_of::<T>()), 0);
        // Safety: We've validated non-null, size, and alignment.
        unsafe { (raw_block.p as *mut T).write(val) };
        // Safety: We've validated and initialized; caller is responsible for
        // lifetime.
        unsafe { ShMemBlock::new(raw_block, ShMemBlockOrigin::Allocator(self)) }
    }

    /// Deserialize a `block` that was allocated by `self`. You probably want
    /// Serializer::deserialize instead.
    ///
    /// TODO: do we need this? Can only be called from the process
    /// that originally allocated the block, which shouldn't need to.
    ///
    /// SAFETY:
    /// * `block` must have been originally created by this allocator.
    /// * `block` must have been created from  a value of type `T`.
    /// * The backing memory must be live for the lifetime of the returned
    ///   ShMemBlock. e.g. `ShMemBlock::deallocate` must not have been called
    ///   on any block referencing that memory.
    pub unsafe fn deserialize<'a, T>(&'a self, block: &ShMemBlockSerialized) -> ShMemBlock<'a, T>
    where
        T: Sync + VirtualAddressSpaceIndependent,
    {
        let raw_blk =
            unsafe { c_bindings::shmemallocator_blockDeserialize(self.internal, &block.internal) };
        unsafe { ShMemBlock::new(raw_blk, ShMemBlockOrigin::Allocator(self)) }
    }
}

pub struct Serializer {
    internal: *mut crate::c_bindings::ShMemSerializer,
}

// SAFETY: The C bindings for ShMemSerializer use internal locking to ensure
// thread-safe access.
//
// TODO: Consider using SyncSendPointer instead of directly implementing these.
// Would require reorganizing crates to avoid a circular dependency, though.
unsafe impl Send for Serializer {}
unsafe impl Sync for Serializer {}

impl Serializer {
    pub fn global() -> &'static Self {
        static GLOBAL: OnceCell<Serializer> = OnceCell::new();
        GLOBAL.get_or_init(|| Self {
            internal: unsafe { c_bindings::shmemserializer_getGlobal() },
        })
    }

    /// SAFETY:
    /// * `block` must have been created from a `ShMemBlock<T>`
    /// * The backing memory must be live for the lifetime of the returned
    ///   ShMemBlock. e.g. `ShMemBlock::deallocate` must not have been called
    ///   on any block referencing that memory.
    pub unsafe fn deserialize<'a, T>(&'a self, block: &ShMemBlockSerialized) -> ShMemBlock<'a, T>
    where
        T: Sync + VirtualAddressSpaceIndependent,
    {
        let raw_blk =
            unsafe { c_bindings::shmemserializer_blockDeserialize(self.internal, &block.internal) };
        unsafe { ShMemBlock::new(raw_blk, ShMemBlockOrigin::Serializer(self)) }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    // Uses FFI
    #[cfg_attr(miri, ignore)]
    fn round_trip_through_serializer() {
        type T = i32;
        let x: T = 42;

        let original_block: ShMemBlock<T> = unsafe { Allocator::global().alloc(x) };
        {
            let serialized_block = original_block.serialize();
            let serialized_str = serialized_block.to_string();
            let serialized_block = ShMemBlockSerialized::from_string(&serialized_str).unwrap();
            let block = unsafe { Serializer::global().deserialize::<T>(&serialized_block) };
            assert_eq!(*block, 42);
        }
        unsafe { original_block.deallocate() };
    }

    // As above but uses Allocator::deserialize instead of Serializer::deserialize.
    // TODO: do we need this functionality?
    #[test]
    // Uses FFI
    #[cfg_attr(miri, ignore)]
    fn round_trip_through_allocator() {
        type T = i32;
        let x: T = 42;

        let original_block: ShMemBlock<T> = unsafe { Allocator::global().alloc(x) };
        {
            let serialized_block = original_block.serialize();
            let serialized_str = serialized_block.to_string();
            let serialized_block = ShMemBlockSerialized::from_string(&serialized_str).unwrap();
            let block = unsafe { Allocator::global().deserialize::<T>(&serialized_block) };
            assert_eq!(*block, 42);
        }
        unsafe { original_block.deallocate() };
    }
}
