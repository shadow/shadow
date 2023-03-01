use std::{marker::PhantomData, ops::Deref};

use once_cell::sync::OnceCell;
use vasi::VirtualAddressSpaceIndependent;

use crate::c_bindings;

/// A typed pointer to shared memory.
///
/// The pointer to the underlying data (e.g. as accessed via `deref`) is guaranteed
/// not to change even if the `ShMemBlock` itself is moved. (Host uses this to safely
/// cache a lock obtained from a ShMemBlock).
pub struct ShMemBlock<'allocator, T>
// T must be Sync, since it will be simultaneously available to multiple threads
// (and processes).
// T mut be VirtualAddressSpaceIndependent, since it may be simultaneously
// mapped into different virtual address spaces.
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    internal: c_bindings::ShMemBlock,
    allocator: &'allocator Allocator,
    _phantom: PhantomData<T>,
}

// SAFETY: T is already required to be Sync, and ShMemBlock only exposes
// immutable references to the underlying data.
unsafe impl<'allocator, T> Sync for ShMemBlock<'allocator, T> where
    T: Sync + VirtualAddressSpaceIndependent
{
}
unsafe impl<'allocator, T> Send for ShMemBlock<'allocator, T> where
    T: Send + Sync + VirtualAddressSpaceIndependent
{
}

impl<'allocator, T> ShMemBlock<'allocator, T>
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    /// Panics if the ShMemBlock is NULL, the incorrect size, or unaligned.
    ///
    /// SAFETY: The memory pointed to by `internal` should already be initialized
    /// to an instance of `T`, and must not be deallocated while the returned
    /// object is still alive.
    unsafe fn new(internal: c_bindings::ShMemBlock, allocator: &'allocator Allocator) -> Self {
        assert_eq!(internal.nbytes, std::mem::size_of::<T>());
        assert!(!internal.p.is_null());
        assert_eq!(internal.p.align_offset(std::mem::align_of::<T>()), 0);
        Self {
            internal,
            allocator,
            _phantom: PhantomData,
        }
    }

    pub fn serialize(&self) -> ShMemBlockSerialized {
        let serialized = unsafe {
            c_bindings::shmemallocator_blockSerialize(self.allocator.internal, &self.internal)
        };
        ShMemBlockSerialized {
            internal: serialized,
        }
    }

    // We require that T is FFI safe (and hence has a stable layout).
    // This function will fail to compile if it isn't.
    #[deny(improper_ctypes_definitions)]
    extern "C" fn _validate_stable_layout(_: T) {}
}

impl<'allocator, T> Drop for ShMemBlock<'allocator, T>
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    fn drop(&mut self) {
        unsafe {
            std::ptr::drop_in_place(self.internal.p as *mut T);
            c_bindings::shmemallocator_free(self.allocator.internal, &mut self.internal as *mut _)
        }
    }
}

impl<'allocator, T> Deref for ShMemBlock<'allocator, T>
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    type Target = T;

    fn deref(&self) -> &Self::Target {
        // SAFETY: Validity ensured by [`ShMemBlock::new`].
        unsafe { (self.internal.p as *const T).as_ref().unwrap() }
    }
}

/// An *alias* for a [`ShMemBlock`], which may live in a different process and
/// address space.
pub struct ShMemBlockAlias<'serializer, T>
// T must be Sync, since it will be simultaneously available to multiple threads
// (and processes).
// T mut be VirtualAddressSpaceIndependent, since it may be simultaneously
// mapped into different virtual address spaces.
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    internal: c_bindings::ShMemBlock,
    // TODO: Extend Serializer to allow cleaning up individual deserialized
    // blocks, and do so from this objects `drop` method.
    #[allow(unused)]
    serializer: &'serializer Serializer,
    _phantom: PhantomData<T>,
}

// SAFETY: T is already required to be Sync, and ShMemBlock only exposes
// immutable references to the underlying data.
unsafe impl<'serializer, T> Sync for ShMemBlockAlias<'serializer, T> where
    T: Sync + VirtualAddressSpaceIndependent
{
}
unsafe impl<'serializer, T> Send for ShMemBlockAlias<'serializer, T> where
    T: Send + Sync + VirtualAddressSpaceIndependent
{
}

impl<'serializer, T> ShMemBlockAlias<'serializer, T>
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    /// Panics if the ShMemBlock is NULL, the incorrect size, or unaligned.
    ///
    /// SAFETY: The memory pointed to by `internal` should already be initialized
    /// to an instance of `T`, and must not be deallocated while the returned
    /// object is still alive.
    unsafe fn new(internal: c_bindings::ShMemBlock, serializer: &'serializer Serializer) -> Self {
        assert_eq!(internal.nbytes, std::mem::size_of::<T>());
        assert!(!internal.p.is_null());
        assert_eq!(internal.p.align_offset(std::mem::align_of::<T>()), 0);
        Self {
            internal,
            serializer,
            _phantom: PhantomData,
        }
    }

    // We require that T is FFI safe (and hence has a stable layout).
    // This function will fail to compile if it isn't.
    #[deny(improper_ctypes_definitions)]
    extern "C" fn _validate_stable_layout(_: T) {}
}

impl<'origin, T> Drop for ShMemBlockAlias<'origin, T>
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    fn drop(&mut self) {
        // No cleanup of individual blocks implemented.
        // TODO: Probably ought to munmap.
    }
}

impl<'origin, T> Deref for ShMemBlockAlias<'origin, T>
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    type Target = T;

    fn deref(&self) -> &Self::Target {
        // SAFETY: Validity ensured by [`ShMemBlockAlias::new`].
        unsafe { (self.internal.p as *const T).as_ref().unwrap() }
    }
}

/// A serialized descriptor for a `ShMemBlock`, suitable to be transferred
/// across processes, which can be used to create a `ShMemBlockAlias` referencing
/// the original `ShMemBlock`.
#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
#[repr(transparent)]
pub struct ShMemBlockSerialized {
    internal: c_bindings::ShMemBlockSerialized,
}

// SAFETY: This is a serialized blob, designed to be VASI.
unsafe impl VirtualAddressSpaceIndependent for c_bindings::ShMemBlockSerialized {}

impl ShMemBlockSerialized {
    // Keep in sync with macro of same name in shmem_allocator.h.
    const SHD_SHMEM_BLOCK_SERIALIZED_MAX_STRLEN: usize = 21 + 21 + 21 + 256 + 1;

    pub fn encode_to_string(&self) -> String {
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

    pub fn decode_from_string(s: &str) -> Option<Self> {
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

/// An allocator for `ShMemBlock`s.
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

    pub fn alloc<T>(&self, val: T) -> ShMemBlock<T>
    where
        T: Sync + VirtualAddressSpaceIndependent,
    {
        let nbytes = std::mem::size_of_val(&val);
        let raw_block = unsafe { c_bindings::shmemallocator_alloc(self.internal, nbytes) };
        assert_eq!(raw_block.nbytes, nbytes);
        assert!(!raw_block.p.is_null());
        assert_eq!(raw_block.p.align_offset(std::mem::align_of::<T>()), 0);
        // Safety: We've validated non-null, size, and alignment.
        unsafe { (raw_block.p as *mut T).write(val) };
        // Safety: We've correctly initialized the raw_block, and are
        // transferring sole ownership to the ShMemBlock.
        unsafe { ShMemBlock::new(raw_block, self) }
    }
}

/// Transforms `ShMemBlockSerialized` to `ShMemBlockAlias`.
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

    /// # Safety
    ///
    /// * `block` must have been created from a `ShMemBlock<T>`
    /// * The returned `ShMemBlockAlias` must not outlive the original `ShMemBlock`
    ///   that `block` was serialized from. We can't guarantee this with normal
    ///   lifetime analysis, since the original block may be in another process.
    pub unsafe fn deserialize<'a, T>(
        &'a self,
        block: &ShMemBlockSerialized,
    ) -> ShMemBlockAlias<'a, T>
    where
        T: Sync + VirtualAddressSpaceIndependent,
    {
        let raw_blk =
            unsafe { c_bindings::shmemserializer_blockDeserialize(self.internal, &block.internal) };
        unsafe { ShMemBlockAlias::new(raw_blk, self) }
    }
}

#[cfg(test)]
mod tests {
    use std::sync::atomic::{AtomicI32, Ordering};

    use super::*;

    #[test]
    // Uses FFI
    #[cfg_attr(miri, ignore)]
    fn round_trip_through_serializer() {
        type T = i32;
        let x: T = 42;

        let original_block: ShMemBlock<T> = Allocator::global().alloc(x);
        {
            let serialized_block = original_block.serialize();
            let serialized_str = serialized_block.encode_to_string();
            let serialized_block =
                ShMemBlockSerialized::decode_from_string(&serialized_str).unwrap();
            let block = unsafe { Serializer::global().deserialize::<T>(&serialized_block) };
            assert_eq!(*block, 42);
        }
    }

    #[test]
    // Uses FFI
    #[cfg_attr(miri, ignore)]
    fn mutations() {
        type T = AtomicI32;
        let original_block = Allocator::global().alloc(AtomicI32::new(0));

        let serialized_block = original_block.serialize();
        let deserialized_block =
            unsafe { Serializer::global().deserialize::<T>(&serialized_block) };

        assert_eq!(original_block.load(Ordering::SeqCst), 0);
        assert_eq!(deserialized_block.load(Ordering::SeqCst), 0);

        // Mutate through original
        original_block.store(10, Ordering::SeqCst);
        assert_eq!(original_block.load(Ordering::SeqCst), 10);
        assert_eq!(deserialized_block.load(Ordering::SeqCst), 10);

        // Mutate through deserialized
        deserialized_block.store(20, Ordering::SeqCst);
        assert_eq!(original_block.load(Ordering::SeqCst), 20);
        assert_eq!(deserialized_block.load(Ordering::SeqCst), 20);
    }

    // Validate our guarantee that the data pointer doesn't move, even if the block does.
    // Host relies on this for soundness.
    #[test]
    // Uses FFI
    #[cfg_attr(miri, ignore)]
    fn shmemblock_stable_pointer() {
        type T = u32;
        let block: ShMemBlock<T> = Allocator::global().alloc(0);

        let block_addr = &block as *const ShMemBlock<T>;
        let data_addr = block.deref() as *const T;

        let block = Some(block);

        // Validate that the block itself actually moved.
        let new_block_addr = block.as_ref().unwrap() as *const ShMemBlock<T>;
        assert_ne!(block_addr, new_block_addr);

        // Validate that the data referenced by the block *hasn't* moved.
        let new_data_addr = block.as_ref().unwrap().deref() as *const T;
        assert_eq!(data_addr, new_data_addr);
    }

    // Validate our guarantee that the data pointer doesn't move, even if the block does.
    #[test]
    // Uses FFI
    #[cfg_attr(miri, ignore)]
    fn shmemblockremote_stable_pointer() {
        type T = u32;
        let allocd_block: ShMemBlock<T> = Allocator::global().alloc(0);
        let block = unsafe { Serializer::global().deserialize::<T>(&allocd_block.serialize()) };

        let block_addr = &block as *const _;
        let data_addr = block.deref() as *const T;

        let block = Some(block);

        // Validate that the block itself actually moved.
        let new_block_addr = block.as_ref().unwrap() as *const _;
        assert_ne!(block_addr, new_block_addr);

        // Validate that the data referenced by the block *hasn't* moved.
        let new_data_addr = block.as_ref().unwrap().deref() as *const T;
        assert_eq!(data_addr, new_data_addr);
    }
}
