#![allow(dead_code)]

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
    pub fn serialize(&self) {
        let serialized = SHMALLOC.lock().internal.serialize(self.block);
        println!("{:?}", serialized);
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

#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
#[repr(transparent)]
pub struct BlockSerialized {
    internal: crate::shmalloc_impl::BlockSerialized,
}

lazy_static! {
    pub static ref SHMALLOC: SelfContainedMutex<SharedMemAllocator<'static>> = {
        let alloc = SharedMemAllocator::new();
        SelfContainedMutex::new(alloc)
    };
}

// lazy_static wants these.

unsafe impl Send for SharedMemAllocator<'_> {}
unsafe impl Sync for SharedMemAllocator<'_> {}

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
}

impl<'alloc> Drop for SharedMemAllocator<'alloc> {
    fn drop(&mut self) {
        self.internal.destruct();

        if self.nallocs != 0 {
            // Memory leak! What do we want to do? Blow up?
            panic!();
        }
    }
}

unsafe impl Sync for GlobalAllocator {}
unsafe impl Send for GlobalAllocator {}

struct GlobalAllocator {
}

unsafe impl core::alloc::GlobalAlloc for GlobalAllocator {
    unsafe fn alloc(&self, layout: core::alloc::Layout) -> *mut u8 {
        let block_p = SHMALLOC.lock().internal.alloc(layout.size(), layout.align());
        let (p, _) = unsafe { (*block_p).get_mut_block_data_range() };
        p
    }

    unsafe fn dealloc(&self, ptr: *mut u8, _layout: core::alloc::Layout) {
        let block_p = crate::shmalloc_impl::rewind(ptr);
        SHMALLOC.lock().internal.dealloc(block_p);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use rand::Rng;

    #[global_allocator]
    static ALLOCATOR: GlobalAllocator = GlobalAllocator{};

    #[test]
    fn xx() {
        let x: Box<u32> = Box::<u32>::new(8);
        println!("{:?}", x);
    }

    #[test]
    fn test_allocator_random() {
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

        SHMALLOC.lock().internal.destruct();
    }

    #[test]
    fn foo() {
        let block = SHMALLOC.lock().alloc(5);
        println!("{:?}, {:?}", block, *block);
        block.serialize();
        SHMALLOC.lock().free(block);
        // SHMALLOC.lock().internal.destruct();
    }
}
