//! In this module is a shared memory allocator that can be used in Shadow to share data between
//! the main simulator process and managed processes. There are three main global functions that
//! are provided:
//!
//! (1) `shmalloc()`, which places the input argument into shared memory and returns a `Block`
//! smart pointer.
//! (2) `shfree()`, which is used to deallocate allocated blocks.
//! (3) `shdeserialize()`, which is used to take a serialized block and convert it back into a
//! `Block` smart pointer that can be dereferenced.
//!
//! Blocks can be serialized with the `.serialize()` member function, which converts the block to a
//! process-memory-layout agnostic representation of the block. The serialized block can be
//! one-to-one converted to and from a string for passing in between different processes.
//!
//! The intended workflow is:
//!
//! (a) The main Shadow simulator process allocates a shared memory block containing an object.
//! (b) The block is serialized.
//! (c) The serialized block is turned into a string.
//! (d) The string is passed to one of Shadow's child, managed processes.
//! (e) The managed process converts the string back to a serialized block.
//! (f) The serialized block is deserialized into a shared memory block alias.
//! (g) The alias is dereferenced and the shared object is retrieved.

use lazy_static::lazy_static;

use shadow_pod::Pod;
use vasi::VirtualAddressSpaceIndependent;
use vasi_sync::scmutex::SelfContainedMutex;

/// This function moves the input parameter into a newly-allocated shared memory block. Analogous to
/// `malloc()`.
pub fn shmalloc<T>(val: T) -> ShMemBlock<'static, T>
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    register_teardown();
    SHMALLOC.lock().alloc(val)
}

/// This function frees a previously allocated block.
pub fn shfree<T>(block: ShMemBlock<'static, T>)
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    SHMALLOC.lock().free(block);
}

/// This function takes a serialized block and converts it back into a BlockAlias that can be
/// dereferenced.
///
/// # Safety
///
/// This function can violate type safety if a template type is provided that does not match
/// original block that was serialized.
pub unsafe fn shdeserialize<T>(serialized: &ShMemBlockSerialized) -> ShMemBlockAlias<'static, T>
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    unsafe { SHDESERIALIZER.lock().deserialize(serialized) }
}

#[cfg(test)]
extern "C" fn shmalloc_teardown() {
    SHMALLOC.lock().destruct();
}

// Just needed because we can't put the drop guard in a global place. We don't want to drop
// after every function, and there's no global test main routine we can take advantage of. No
// big deal.
#[cfg(test)]
#[cfg_attr(miri, ignore)]
fn register_teardown() {
    extern crate std;
    use std::sync::Once;

    static START: Once = Once::new();
    START.call_once(|| unsafe {
        libc::atexit(shmalloc_teardown);
    });
}

#[cfg(not(test))]
fn register_teardown() {}

// The global, singleton shared memory allocator and deserializer objects.
// TODO(rwails): Adjust to use lazy lock instead.
lazy_static! {
    static ref SHMALLOC: SelfContainedMutex<SharedMemAllocator<'static>> = {
        let alloc = SharedMemAllocator::new();
        SelfContainedMutex::new(alloc)
    };
    static ref SHDESERIALIZER: SelfContainedMutex<SharedMemDeserializer<'static>> = {
        let deserial = SharedMemDeserializer::new();
        SelfContainedMutex::new(deserial)
    };
}

/// The intended singleton destructor for the global singleton shared memory allocator.
///
/// Because the global allocator has static lifetime, drop() will never be called on it. Therefore,
/// necessary cleanup routines are not called. Instead, this object can be instantiated once, eg at
/// the start of main(), and then when it is dropped at program exit the cleanup routine is called.
pub struct SharedMemAllocatorDropGuard(());

impl SharedMemAllocatorDropGuard {
    /// # Safety
    ///
    /// Must outlive all `ShMemBlock` objects allocated by the current process.
    pub unsafe fn new() -> Self {
        Self(())
    }
}

impl Drop for SharedMemAllocatorDropGuard {
    fn drop(&mut self) {
        SHMALLOC.lock().destruct();
    }
}

/// A smart pointer class that holds a `Sync` and `VirtualAddressSpaceIndependent` object.
///
/// The pointer is obtained by a call to a shared memory allocator's `alloc()` function (or the
/// global `shalloc()` function. The memory is freed when the block is dropped.
///
/// This smart pointer is unique in that it may be serialized to a string, passed across process
/// boundaries, and deserialized in a (potentially) separate process to obtain a view of the
/// contained data.
#[derive(Debug)]
pub struct ShMemBlock<'allocator, T>
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    block: *mut crate::shmalloc_impl::Block,
    phantom: core::marker::PhantomData<&'allocator T>,
}

impl<T> ShMemBlock<'_, T>
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    pub fn serialize(&self) -> ShMemBlockSerialized {
        let serialized = SHMALLOC.lock().internal.serialize(self.block);
        ShMemBlockSerialized {
            internal: serialized,
        }
    }
}

// SAFETY: T is already required to be Sync, and ShMemBlock only exposes
// immutable references to the underlying data.
unsafe impl<T> Sync for ShMemBlock<'_, T> where T: Sync + VirtualAddressSpaceIndependent {}
unsafe impl<T> Send for ShMemBlock<'_, T> where T: Send + Sync + VirtualAddressSpaceIndependent {}

impl<T> core::ops::Deref for ShMemBlock<'_, T>
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    type Target = T;

    fn deref(&self) -> &Self::Target {
        let block = unsafe { &*self.block };
        &block.get_ref::<T>()[0]
    }
}

impl<T> core::ops::Drop for ShMemBlock<'_, T>
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    fn drop(&mut self) {
        if !self.block.is_null() {
            // Guard here to prevent deadlock on free.
            SHMALLOC.lock().internal.dealloc(self.block);
            self.block = core::ptr::null_mut();
        }
    }
}

/// This struct is analogous to the `ShMemBlock` smart pointer, except it does not assume ownership
/// of the underlying memory and thus does not free the memory when dropped.
///
/// An alias of a block is obtained with a call to `deserialize()` on a `SharedMemDeserializer`
/// object (or likely by using the `shdeserialize()` to make this call on the global shared memory
/// deserializer.
#[derive(Debug)]
pub struct ShMemBlockAlias<'deserializer, T>
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    block: *mut crate::shmalloc_impl::Block,
    phantom: core::marker::PhantomData<&'deserializer T>,
}

// SAFETY: T is already required to be Sync, and ShMemBlock only exposes
// immutable references to the underlying data.
unsafe impl<T> Sync for ShMemBlockAlias<'_, T> where T: Sync + VirtualAddressSpaceIndependent {}
unsafe impl<T> Send for ShMemBlockAlias<'_, T> where T: Send + Sync + VirtualAddressSpaceIndependent {}

impl<T> core::ops::Deref for ShMemBlockAlias<'_, T>
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    type Target = T;

    fn deref(&self) -> &Self::Target {
        let block = unsafe { &*self.block };
        &block.get_ref::<T>()[0]
    }
}

#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
#[repr(transparent)]
pub struct ShMemBlockSerialized {
    internal: crate::shmalloc_impl::BlockSerialized,
}

unsafe impl Pod for ShMemBlockSerialized {}

impl core::fmt::Display for ShMemBlockSerialized {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        let s =
            core::str::from_utf8(crate::util::trim_null_bytes(&self.internal.chunk_name).unwrap())
                .unwrap();
        write!(f, "{};{}", self.internal.offset, s)
    }
}

impl core::str::FromStr for ShMemBlockSerialized {
    type Err = anyhow::Error;

    // Required method
    fn from_str(s: &str) -> anyhow::Result<Self> {
        use core::fmt::Write;
        use formatting_nostd::FormatBuffer;

        if let Some((offset_str, path_str)) = s.split_once(';') {
            // let offset = offset_str.parse::<isize>().map_err(Err).unwrap();
            let offset = offset_str
                .parse::<isize>()
                .map_err(Err::<(), core::num::ParseIntError>)
                .unwrap();

            let mut chunk_format = FormatBuffer::<{ crate::util::PATH_MAX_NBYTES }>::new();

            write!(&mut chunk_format, "{}", &path_str).unwrap();

            let mut chunk_name = crate::util::NULL_PATH_BUF;
            chunk_name
                .iter_mut()
                .zip(chunk_format.as_str().as_bytes().iter())
                .for_each(|(x, y)| *x = *y);

            Ok(ShMemBlockSerialized {
                internal: crate::shmalloc_impl::BlockSerialized { chunk_name, offset },
            })
        } else {
            Err(anyhow::anyhow!("missing ;"))
        }
    }
}

/// Safe wrapper around our low-level, unsafe, nostd shared memory allocator.
///
/// This allocator type is not meant to be used directly, but can be accessed indirectly via calls
/// made to `shmalloc()` and `shfree()`.
pub struct SharedMemAllocator<'alloc> {
    internal: crate::shmalloc_impl::FreelistAllocator,
    nallocs: isize,
    phantom: core::marker::PhantomData<&'alloc ()>,
}

impl<'alloc> SharedMemAllocator<'alloc> {
    fn new() -> Self {
        let mut internal = crate::shmalloc_impl::FreelistAllocator::new();
        internal.init().unwrap();

        Self {
            internal,
            nallocs: 0,
            phantom: Default::default(),
        }
    }

    // TODO(rwails): Fix the lifetime of the allocated block to match the allocator's lifetime.
    fn alloc<T: Sync + VirtualAddressSpaceIndependent>(&mut self, val: T) -> ShMemBlock<'alloc, T> {
        let t_nbytes: usize = core::mem::size_of::<T>();
        let t_alignment: usize = core::mem::align_of::<T>();

        let block = self.internal.alloc(t_nbytes, t_alignment);
        unsafe {
            (*block).get_mut_ref::<T>()[0] = val;
        }

        self.nallocs += 1;
        ShMemBlock::<'alloc, T> {
            block,
            phantom: Default::default(),
        }
    }

    fn free<T: Sync + VirtualAddressSpaceIndependent>(&mut self, mut block: ShMemBlock<'alloc, T>) {
        self.nallocs -= 1;
        block.block = core::ptr::null_mut();
        self.internal.dealloc(block.block);
    }

    fn destruct(&mut self) {
        // if self.nallocs != 0 {
        //crate::shmalloc_impl::log_err(crate::shmalloc_impl::AllocError::Leak, None);

        // TODO(rwails): This condition currently occurs when running Shadow. It's not actually
        // a leak to worry about because the shared memory file backing store does get cleaned
        // up. It's possible that all blocks are not dropped before this allocator is dropped.
        // }

        self.internal.destruct();
    }
}

unsafe impl Send for SharedMemAllocator<'_> {}
unsafe impl Sync for SharedMemAllocator<'_> {}

// Experimental... implements the global allocator using the shared memory allocator.
/*
unsafe impl Sync for GlobalAllocator {}
unsafe impl Send for GlobalAllocator {}

struct GlobalAllocator {}

unsafe impl core::alloc::GlobalAlloc for GlobalAllocator {
    unsafe fn alloc(&self, layout: core::alloc::Layout) -> *mut u8 {
        let block_p = SHMALLOC
            .lock()
            .internal
            .alloc(layout.size(), layout.align());
        let (p, _) = unsafe { (*block_p).get_mut_block_data_range() };
        p
    }

    unsafe fn dealloc(&self, ptr: *mut u8, _layout: core::alloc::Layout) {
        let block_p = crate::shmalloc_impl::rewind(ptr);
        SHMALLOC.lock().internal.dealloc(block_p);
    }
}
*/

pub struct SharedMemDeserializer<'alloc> {
    internal: crate::shmalloc_impl::FreelistDeserializer,
    phantom: core::marker::PhantomData<&'alloc ()>,
}

impl<'alloc> SharedMemDeserializer<'alloc> {
    fn new() -> Self {
        let internal = crate::shmalloc_impl::FreelistDeserializer::new();

        Self {
            internal,
            phantom: Default::default(),
        }
    }

    /// # Safety
    ///
    /// This function can violate type safety if a template type is provided that does not match
    /// original block that was serialized.
    // TODO(rwails): Fix the lifetime of the allocated block to match the deserializer's lifetime.
    pub unsafe fn deserialize<T>(
        &mut self,
        serialized: &ShMemBlockSerialized,
    ) -> ShMemBlockAlias<'alloc, T>
    where
        T: Sync + VirtualAddressSpaceIndependent,
    {
        let block = self.internal.deserialize(&serialized.internal);

        ShMemBlockAlias {
            block,
            phantom: Default::default(),
        }
    }
}

unsafe impl Send for SharedMemDeserializer<'_> {}
unsafe impl Sync for SharedMemDeserializer<'_> {}

#[cfg(test)]
mod tests {
    use super::*;
    use rand::Rng;
    use std::str::FromStr;
    use std::string::ToString;
    use std::sync::atomic::{AtomicI32, Ordering};

    extern crate std;

    #[test]
    #[cfg_attr(miri, ignore)]
    fn allocator_random_allocations() {
        const NROUNDS: usize = 100;
        let mut marked_blocks: std::vec::Vec<(u32, ShMemBlock<u32>)> = Default::default();
        let mut rng = rand::rng();

        let mut execute_round = || {
            // Some allocations
            for i in 0..255 {
                let b = shmalloc(i);
                marked_blocks.push((i, b));
            }

            // Generate some number of items to pop
            let n1: u8 = rng.random();

            for _ in 0..n1 {
                let last_marked_block = marked_blocks.pop().unwrap();
                assert_eq!(last_marked_block.0, *last_marked_block.1);
                shfree(last_marked_block.1);
            }

            // Then check all blocks
            for block in &marked_blocks {
                assert_eq!(block.0, *block.1);
            }
        };

        for _ in 0..NROUNDS {
            execute_round();
        }

        while let Some(b) = marked_blocks.pop() {
            shfree(b.1);
        }
    }

    #[test]
    #[cfg_attr(miri, ignore)]
    fn round_trip_through_serializer() {
        type T = i32;
        let x: T = 42;

        let original_block: ShMemBlock<T> = shmalloc(x);
        {
            let serialized_block = original_block.serialize();
            let serialized_str = serialized_block.to_string();
            let serialized_block = ShMemBlockSerialized::from_str(&serialized_str).unwrap();
            let block = unsafe { shdeserialize::<i32>(&serialized_block) };
            assert_eq!(*block, 42);
        }

        shfree(original_block);
    }

    #[test]
    // Uses FFI
    #[cfg_attr(miri, ignore)]
    fn mutations() {
        type T = AtomicI32;
        let original_block = shmalloc(AtomicI32::new(0));

        let serialized_block = original_block.serialize();

        let deserialized_block = unsafe { shdeserialize::<T>(&serialized_block) };

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

        shfree(original_block);
    }

    // Validate our guarantee that the data pointer doesn't move, even if the block does.
    // Host relies on this for soundness.
    #[test]
    // Uses FFI
    #[cfg_attr(miri, ignore)]
    fn shmemblock_stable_pointer() {
        type T = u32;
        let original_block: ShMemBlock<T> = shmalloc(0);

        let block_addr = &original_block as *const ShMemBlock<T>;
        let data_addr = *original_block as *const T;

        // Use an `Option` to move the `ShMemBlock`. We have no guarantee here that it actually
        // moves and that the compiler doesn't optimize the move away, so the before/after addresses
        // are compared below.
        let block = Some(original_block);

        // Validate that the block itself actually moved.
        let new_block_addr = block.as_ref().unwrap() as *const ShMemBlock<T>;
        assert_ne!(block_addr, new_block_addr);

        // Validate that the data referenced by the block *hasn't* moved.
        let new_data_addr = **(block.as_ref().unwrap()) as *const T;
        assert_eq!(data_addr, new_data_addr);

        #[allow(clippy::unnecessary_literal_unwrap)]
        shfree(block.unwrap());
    }

    // Validate our guarantee that the data pointer doesn't move, even if the block does.
    #[test]
    // Uses FFI
    #[cfg_attr(miri, ignore)]
    fn shmemblockremote_stable_pointer() {
        type T = u32;
        let alloced_block: ShMemBlock<T> = shmalloc(0);

        let block = unsafe { shdeserialize::<T>(&alloced_block.serialize()) };

        let block_addr = &block as *const ShMemBlockAlias<T>;
        let data_addr = *block as *const T;

        let block = Some(block);

        // Validate that the block itself actually moved.
        let new_block_addr = block.as_ref().unwrap() as *const ShMemBlockAlias<T>;
        assert_ne!(block_addr, new_block_addr);

        // Validate that the data referenced by the block *hasn't* moved.
        let new_data_addr = **(block.as_ref().unwrap()) as *const T;
        assert_eq!(data_addr, new_data_addr);

        shfree(alloced_block);
    }
}
