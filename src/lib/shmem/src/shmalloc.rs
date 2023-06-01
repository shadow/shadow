#![allow(dead_code)]

use vasi::VirtualAddressSpaceIndependent;

#[derive(Debug)]
#[repr(transparent)]
pub struct Block<'alloc, T>
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    block_hdr: *mut crate::shmalloc_impl::BlockHdr,
    phantom: core::marker::PhantomData<&'alloc T>,
}

impl<'allocator, T> Block<'allocator, T>
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    const T_NBYTES: usize = core::mem::size_of::<T>();
    const T_ALIGNMENT: usize = core::mem::align_of::<T>();
}

impl<'allocator, T> core::ops::Deref for Block<'allocator, T>
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    type Target = T;

    fn deref(&self) -> &Self::Target {
        let block_hdr = unsafe { &*self.block_hdr };
        &block_hdr.get_ref::<T>(Self::T_NBYTES, Self::T_ALIGNMENT)[0]
    }
}

impl<'allocator, T> core::ops::DerefMut for Block<'allocator, T>
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    fn deref_mut(&mut self) -> &mut T {
        let block_hdr = unsafe { &mut *self.block_hdr };
        &mut block_hdr.get_mut_ref::<T>(Self::T_NBYTES, Self::T_ALIGNMENT)[0]
    }
}

struct SharedMemAllocator<'alloc, T>
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    internal: crate::shmalloc_impl::UniformFreelistAllocator,
    nallocs: isize,
    phantom: core::marker::PhantomData<&'alloc T>,
}

impl<'alloc, T> SharedMemAllocator<'alloc, T>
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    const T_NBYTES: usize = core::mem::size_of::<T>();
    const T_ALIGNMENT: usize = core::mem::align_of::<T>();

    fn new() -> Self {
        Self {
            internal: crate::shmalloc_impl::UniformFreelistAllocator::new(
                Self::T_NBYTES,
                Self::T_ALIGNMENT,
            ),
            nallocs: 0,
            phantom: Default::default(),
        }
    }

    fn alloc(&mut self) -> Block<'alloc, T> {
        self.nallocs += 1;
        Block::<'alloc, T> {
            block_hdr: self.internal.alloc(),
            phantom: Default::default(),
        }
    }

    fn free(&mut self, block: Block<'alloc, T>) {
        self.nallocs -= 1;
        self.internal.dealloc(block.block_hdr);
    }
}

impl<'alloc, T> Drop for SharedMemAllocator<'alloc, T>
where
    T: Sync + VirtualAddressSpaceIndependent,
{
    fn drop(&mut self) {
        self.internal.destruct();

        if self.nallocs != 0 {
            // Memory leak! What do we want to do? Blow up?
            println!("{:?}", self.nallocs);
            panic!();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use rand::Rng;

    #[test]
    fn test_allocator_random() {
        const NROUNDS: usize = 10;
        let mut marked_blocks: Vec<(u32, Block<u32>)> = Default::default();
        let mut allocator = SharedMemAllocator::<u32>::new();
        let mut rng = rand::thread_rng();

        let mut execute_round = || {
            // Some allocations
            for i in 0..255 {
                let mut b = allocator.alloc();
                *b = i;
                marked_blocks.push((i, b));
            }

            // Generate some number of items to pop
            let n1: u8 = rng.gen();

            for _ in 0..n1 {
                let last_marked_block = marked_blocks.pop().unwrap();
                assert_eq!(last_marked_block.0, *last_marked_block.1);
                allocator.free(last_marked_block.1);
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
            allocator.free(b.1);
        }
    }

    #[test]
    fn foo() {
    }
}
