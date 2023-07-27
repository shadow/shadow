#![allow(dead_code)]

use numtoa::NumToA;

use lazy_static::lazy_static;
use vasi::VirtualAddressSpaceIndependent;
use vasi_sync::scmutex::SelfContainedMutex;

#[derive(Debug)]
pub struct Block<'allocator, T>
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    block: *mut crate::shmalloc_impl::Block,
    phantom: core::marker::PhantomData<&'allocator T>,
}

impl<'allocator, T> Block<'allocator, T>
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    pub fn serialize(&self) -> BlockSerialized {
        let serialized = SHMALLOC.lock().internal.serialize(self.block);
        BlockSerialized {
            internal: serialized,
        }
    }
}

// SAFETY: T is already required to be Sync, and ShMemBlock only exposes
// immutable references to the underlying data.
unsafe impl<'allocator, T> Sync for Block<'allocator, T> where
    T: Sync + VirtualAddressSpaceIndependent
{
}
unsafe impl<'allocator, T> Send for Block<'allocator, T> where
    T: Send + Sync + VirtualAddressSpaceIndependent
{
}

impl<'allocator, T> core::ops::Deref for Block<'allocator, T>
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    type Target = T;

    fn deref(&self) -> &Self::Target {
        let block = unsafe { &*self.block };
        &block.get_ref::<T>()[0]
    }
}

impl<'allocator, T> core::ops::Drop for Block<'allocator, T>
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

// A path is up to 256 bytes; a isize is 20 bytes; and one byte for delimiter.
const STRING_BUF_NBYTES: usize = 256 + 20 + 1;
type StringBuf = [u8; STRING_BUF_NBYTES];

#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
#[repr(transparent)]
pub struct BlockSerialized {
    internal: crate::shmalloc_impl::BlockSerialized,
}

impl BlockSerialized {
    fn to_string_buf(&self) -> StringBuf {
        let mut retval: StringBuf = [0; STRING_BUF_NBYTES];

        let mut offset_buf = [0u8; 32];
        self.internal.offset.numtoa(10, &mut offset_buf);

        let i1 = offset_buf.iter().filter(|x| **x != 0);
        let i2 = ";".as_bytes().iter();
        let i3 = self.internal.chunk_name.iter();

        let i_all = i1.chain(i2).chain(i3);

        retval.iter_mut().zip(i_all).for_each(|(x, y)| *x = *y);

        retval
    }

    fn from_string_buf(string_buf: &StringBuf) -> Self {
        const NEEDLE: u8 = 59; // Decimal value of ;

        let lhs_itr = string_buf.iter();
        let mut rhs_itr = string_buf.iter();
        rhs_itr.find(|x| **x == NEEDLE);

        let offset_itr = lhs_itr.take_while(|x| **x != NEEDLE);

        let mut offset_buf = [0u8; 32];
        offset_buf
            .iter_mut()
            .zip(offset_itr)
            .for_each(|(x, y)| *x = *y);

        let mut path_buf: crate::shmalloc_impl::PathBuf =
            [0; crate::shmalloc_impl::PATH_BUF_NBYTES];

        path_buf.iter_mut().zip(rhs_itr).for_each(|(x, y)| *x = *y);

        println!("{:?}", offset_buf);

        let end = offset_buf.iter().position(|&x| x == 0).unwrap();

        let offset = unsafe {
            core::str::from_utf8_unchecked(&offset_buf[0..end])
                .parse::<isize>()
                .unwrap()
        };

        BlockSerialized {
            internal: crate::shmalloc_impl::BlockSerialized {
                chunk_name: path_buf,
                offset,
            },
        }
    }
}

lazy_static! {
    pub static ref SHMALLOC: SelfContainedMutex<SharedMemAllocator<'static>> = {
        let alloc = SharedMemAllocator::new();
        SelfContainedMutex::new(alloc)
    };
    pub static ref SHDESERIALIZER: SelfContainedMutex<SharedMemDeserializer<'static>> = {
        let deserial = SharedMemDeserializer::new();
        SelfContainedMutex::new(deserial)
    };
}

// lazy_static wants these.

unsafe impl Send for SharedMemAllocator<'_> {}
unsafe impl Sync for SharedMemAllocator<'_> {}

unsafe impl Send for SharedMemDeserializer<'_> {}
unsafe impl Sync for SharedMemDeserializer<'_> {}

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

    fn alloc<T: Sync + VirtualAddressSpaceIndependent>(&mut self, val: T) -> Block<'alloc, T> {
        let t_nbytes: usize = core::mem::size_of::<T>();
        let t_alignment: usize = core::mem::align_of::<T>();

        let block = self.internal.alloc(t_nbytes, t_alignment);
        unsafe {
            (*block).get_mut_ref::<T>()[0] = val;
        }

        self.nallocs += 1;
        Block::<'alloc, T> {
            block,
            phantom: Default::default(),
        }
    }

    fn free<T: Sync + VirtualAddressSpaceIndependent>(&mut self, mut block: Block<'alloc, T>) {
        self.nallocs -= 1;
        block.block = core::ptr::null_mut();
        self.internal.dealloc(block.block);
    }

    fn destruct(&mut self) {
        self.internal.destruct();

        if self.nallocs != 0 {
            // Memory leak! What do we want to do? Blow up?
            // panic!();
        }
    }
}

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

    pub fn deserialize<T>(&mut self, serialized: &BlockSerialized) -> Block<'alloc, T>
    where
        T: Sync + VirtualAddressSpaceIndependent,
    {
        let block = self.internal.deserialize(&serialized.internal);

        Block {
            block,
            phantom: Default::default(),
        }
    }
}

struct SharedMemAllocatorDropGuard;

impl Drop for SharedMemAllocatorDropGuard {
    fn drop(&mut self) {
        SHMALLOC.lock().destruct();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use rand::Rng;
    use std::sync::atomic::{AtomicI32, Ordering};

    extern "C" fn shmalloc_teardown() {
        SHMALLOC.lock().destruct();
    }

    // Just needed because we can't put the drop guard in a global place. We don't want to drop
    // after every function, and there's no global test main routine we can take advantage of. No
    // big deal.
    fn register_teardown() {
        use std::sync::Mutex;
        static MTX: Mutex<i32> = Mutex::new(0);
        let _guard = MTX.lock();

        static mut INIT: bool = false;

        unsafe {
            if !INIT {
                libc::atexit(shmalloc_teardown);
                INIT = true;
            }
        }
    }

    #[test]
    fn test_allocator_random() {
        register_teardown();

        const NROUNDS: usize = 100;
        let mut marked_blocks: Vec<(u32, Block<u32>)> = Default::default();
        let mut rng = rand::thread_rng();

        let mut execute_round = || {
            // Some allocations
            for i in 0..255 {
                let b = SHMALLOC.lock().alloc(i);
                marked_blocks.push((i, b));
            }

            // Generate some number of items to pop
            let n1: u8 = rng.gen();

            for _ in 0..n1 {
                let last_marked_block = marked_blocks.pop().unwrap();
                assert_eq!(last_marked_block.0, *last_marked_block.1);
                SHMALLOC.lock().free(last_marked_block.1);
            }

            // Then check all blocks
            for idx in 0..marked_blocks.len() {
                assert_eq!(marked_blocks[idx].0, *marked_blocks[idx].1);
            }
        };

        for _ in 0..NROUNDS {
            execute_round();
        }

        while marked_blocks.len() > 0 {
            let b = marked_blocks.pop().unwrap();
            SHMALLOC.lock().free(b.1);
        }
    }

    #[test]
    fn foo() {
        register_teardown();

        let block = SHMALLOC.lock().alloc(5u32);
        println!("{:?}, {:?}", block, *block);
        let s = block.serialize();
        let sb = s.to_string_buf();
        println!("{:?} {:?}", s, sb);

        BlockSerialized::from_string_buf(&sb);

        let block2: Block<u32> = SHDESERIALIZER.lock().deserialize(&s);
        println!("{:?}, {:?}", block2, *block2);

        SHMALLOC.lock().free(block);
    }

    #[cfg_attr(miri, ignore)]
    fn round_trip_through_serializer() {
        register_teardown();
        type T = i32;
        let x: T = 42;

        let original_block: Block<T> = SHMALLOC.lock().alloc(x);
        {
            let serialized_block = original_block.serialize();
            let serialized_str = serialized_block.to_string_buf();
            let serialized_block =
                BlockSerialized::from_string_buf(&serialized_str);
            let block = SHDESERIALIZER.lock().deserialize::<i32>(&serialized_block);
            assert_eq!(*block, 42);
        }
    }

    #[test]
    // Uses FFI
    #[cfg_attr(miri, ignore)]
    fn mutations() {
        register_teardown();

        println!("{:?}", SHMALLOC.lock().internal);
        println!("{:?}", SHDESERIALIZER.lock().internal);

        type T = AtomicI32;
        let original_block = SHMALLOC.lock().alloc(AtomicI32::new(0));

        let serialized_block = original_block.serialize();
        let deserialized_block = SHDESERIALIZER.lock().deserialize::<T>(&serialized_block);

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
}
