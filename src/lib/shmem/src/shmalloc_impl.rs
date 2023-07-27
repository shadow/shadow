#![allow(dead_code)]

use crate::raw_syscall::*;
use numtoa::NumToA;

const PATH_MAX_NBYTES: usize = 255;
pub(crate) const PATH_BUF_NBYTES: usize = PATH_MAX_NBYTES + 1;
pub(crate) type PathBuf = [u8; PATH_BUF_NBYTES];

use vasi::VirtualAddressSpaceIndependent;

enum AllocError {}

fn get_null_path_buf() -> PathBuf {
    [0; PATH_BUF_NBYTES]
}

#[cfg(debug_assertions)]
type MagicBuf = [u8; 4];

#[cfg(debug_assertions)]
fn get_magic_buf() -> MagicBuf {
    [0xDE, 0xAD, 0xBE, 0xEF]
}

#[cfg(not(debug_assertions))]
type MagicBuf = [u8; 0];

#[cfg(not(debug_assertions))]
fn get_magic_buf() -> MagicBuf {
    []
}

trait Magic {
    fn magic_init(&mut self);
    fn magic_check(&self) -> bool;

    #[cfg(debug_assertions)]
    fn magic_assert(&self) {
        assert!(self.magic_check());
    }

    #[cfg(not(debug_assertions))]
    fn magic_assert(&self) {}
}

fn format_shmem_name(buf: &mut [u8]) {
    static PREFIX: &str = "/dev/shm/shadow_shmemfile_";

    buf.iter_mut().for_each(|x| *x = 0);

    let mut pid_buf = [0u8; 32];
    let pid = getpid();
    let pid_buf = pid.numtoa(10, &mut pid_buf);

    let mut sec_buf = [0u8; 32];
    let mut nsec_buf = [0u8; 32];
    let ts = clock_gettime().unwrap();
    let sec_buf = ts.tv_sec.numtoa(10, &mut sec_buf);
    let nsec_buf = ts.tv_nsec.numtoa(10, &mut nsec_buf);

    let name_itr = PREFIX.as_bytes().iter().chain(
        sec_buf.iter().chain(
            ".".as_bytes().iter().chain(
                nsec_buf
                    .iter()
                    .chain("-".as_bytes().iter().chain(pid_buf.iter())),
            ),
        ),
    );

    buf.iter_mut().zip(name_itr).for_each(|(x, y)| *x = *y);
}

const CHUNK_NBYTES_DEFAULT: usize = 20971520;

fn create_map_shared_memory<'a>(
    path_buf: &PathBuf,
    nbytes: usize,
) -> Result<(&'a mut [u8], i32), i32> {
    use linux_api::fcntl::OFlag;
    use linux_api::mman::{MapFlags, ProtFlags};

    const MODE: u32 = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;

    let open_flags: i32 =
        (OFlag::O_RDWR | OFlag::O_CREAT | OFlag::O_EXCL | OFlag::O_CLOEXEC).bits();

    let prot: i32 = (ProtFlags::PROT_READ | ProtFlags::PROT_WRITE).bits() as i32;
    let map_flags: i32 = MapFlags::MAP_SHARED.bits() as i32;

    let fd = unsafe { open(path_buf.as_ptr(), open_flags, MODE)? };
    ftruncate(fd, nbytes.try_into().unwrap())?;

    let retval = unsafe {
        mmap(
            core::ptr::null_mut(),
            nbytes.try_into().unwrap(),
            prot,
            map_flags,
            fd,
            0,
        )?
    };

    Ok((retval, fd))
}

// Similar to `create_map_shared_memory` but no O_CREAT or O_EXCL and no ftruncate calls.
fn view_shared_memory<'a>(path_buf: &PathBuf, nbytes: usize) -> Result<(&'a mut [u8], i32), i32> {
    use linux_api::fcntl::OFlag;
    use linux_api::mman::{MapFlags, ProtFlags};

    let open_flags: i32 = (OFlag::O_RDWR | OFlag::O_CLOEXEC).bits();
    const MODE: u32 = libc::S_IRUSR | libc::S_IWUSR | libc::S_IRGRP | libc::S_IWGRP;
    let prot: i32 = (ProtFlags::PROT_READ | ProtFlags::PROT_WRITE).bits() as i32;
    let map_flags: i32 = MapFlags::MAP_SHARED.bits() as i32;

    let fd = unsafe { open(path_buf.as_ptr(), open_flags, MODE)? };

    let retval = unsafe {
        mmap(
            core::ptr::null_mut(),
            nbytes.try_into().unwrap(),
            prot,
            map_flags,
            fd,
            0,
        )?
    };

    Ok((retval, fd))
}

#[repr(C)]
#[derive(Debug)]
struct Chunk {
    magic_front: MagicBuf,
    chunk_name: PathBuf,
    chunk_fd: i32,
    chunk_nbytes: usize,
    data_cur: u32, // The current point at which the data starts from the start of the data segment
    next_chunk: *mut Chunk,
    magic_back: MagicBuf,
}

impl Chunk {
    fn get_mut_data_start(&mut self) -> *mut u8 {
        self.get_data_start() as *mut u8
    }

    fn get_data_start(&self) -> *const u8 {
        let p = self as *const Self as *const u8;
        unsafe { p.add(core::mem::size_of::<Self>()) }
    }
}

impl Magic for Chunk {
    fn magic_init(&mut self) {
        self.magic_front = get_magic_buf();
        self.magic_back = get_magic_buf();
    }

    fn magic_check(&self) -> bool {
        self.magic_front == get_magic_buf() && self.magic_back == get_magic_buf()
    }
}

fn allocate_shared_chunk(path_buf: &PathBuf, nbytes: usize) -> Result<*mut Chunk, i32> {
    let (p, fd) = create_map_shared_memory(path_buf, nbytes)?;

    // Zero the memory so that we do not have to worry about junk between blocks.
    unsafe {
        core::ptr::write_bytes::<u8>(p.as_mut_ptr(), 0x00, nbytes);
    }

    let chunk_meta: *mut Chunk = p.as_mut_ptr() as *mut Chunk;

    unsafe {
        (*chunk_meta).chunk_name = *path_buf;
        (*chunk_meta).chunk_fd = fd;
        (*chunk_meta).chunk_nbytes = nbytes;
        (*chunk_meta).data_cur = 0;
        (*chunk_meta).next_chunk = core::ptr::null_mut();
        (*chunk_meta).magic_init();
    }

    Ok(chunk_meta)
}

fn view_shared_chunk(path_buf: &PathBuf, nbytes: usize) -> Result<*mut Chunk, i32> {
    let (p, _) = view_shared_memory(path_buf, nbytes)?;
    let chunk_meta: *mut Chunk = p.as_mut_ptr() as *mut Chunk;
    Ok(chunk_meta)
}

fn deallocate_shared_chunk(chunk_meta: *const Chunk) -> Result<(), i32> {
    unsafe {
        (*chunk_meta).magic_assert();
    }

    let path_buf = unsafe { (*chunk_meta).chunk_name };
    let chunk_nbytes = unsafe { (*chunk_meta).chunk_nbytes };

    munmap(unsafe { core::slice::from_raw_parts_mut(chunk_meta as *mut u8, chunk_nbytes) })
        .unwrap();

    unsafe {
        unlink(path_buf.as_ptr())?;
    }

    Ok(())
}

#[repr(C)]
#[derive(Debug)]
pub(crate) struct Block {
    magic_front: MagicBuf,
    next_free_block: *mut Block, // This can't be a short pointer, because it may point across chunks.
    alloc_nbytes: u32,           // What is the size of the block
    data_offset: u32,            // From the start location of the block header
    magic_back: MagicBuf,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
pub(crate) struct BlockSerialized {
    pub(crate) chunk_name: PathBuf,
    pub(crate) offset: isize,
}

const BLOCK_STRUCT_NBYTES: usize = core::mem::size_of::<Block>();
const BLOCK_STRUCT_ALIGNMENT: usize = core::mem::align_of::<Block>();

impl Magic for Block {
    fn magic_init(&mut self) {
        self.magic_front = get_magic_buf();
        self.magic_back = get_magic_buf();
    }

    fn magic_check(&self) -> bool {
        self.magic_front == get_magic_buf() && self.magic_back == get_magic_buf()
    }
}

impl Block {
    /// Gets the data corresponding to the block
    ///
    /// # Parameters
    ///
    /// * `block` - An initialized memory block.
    ///
    /// # Return Value
    ///
    /// The begin and end of the aligned data corresponding to the block.
    ///
    /// # Pre
    ///
    /// `block` is not null and has an address correctly computed by the `init_block` function.
    pub(self) fn get_block_data_range(&self) -> (*const u8, *const u8) {
        self.magic_assert();

        let data_offset = self.data_offset;
        let alloc_nbytes = self.alloc_nbytes;
        let block = self as *const Block as *const u8;
        assert!(!block.is_null());

        let data_begin = unsafe { block.add(data_offset as usize) };
        let data_end = unsafe { data_begin.add(alloc_nbytes as usize) };

        (data_begin, data_end)
    }

    pub(crate) fn get_mut_block_data_range(&self) -> (*mut u8, *mut u8) {
        let (x, y) = self.get_block_data_range();
        (x as *mut u8, y as *mut u8)
    }

    pub(crate) fn get_mut_bytes(&mut self) -> &mut [u8] {
        let (begin_p, _) = self.get_mut_block_data_range();
        unsafe { core::slice::from_raw_parts_mut(begin_p, self.alloc_nbytes as usize) }
    }

    pub(crate) fn get_ref<T>(&self) -> &[T] {
        let (begin_p, end_p) = self.get_block_data_range();
        let block_len = unsafe { end_p.offset_from(begin_p) } as usize;
        assert!(block_len % core::mem::size_of::<T>() == 0);
        let nelems = block_len / core::mem::size_of::<T>();
        unsafe { core::slice::from_raw_parts(begin_p as *const T, nelems) }
    }

    pub(crate) fn get_mut_ref<T>(&mut self) -> &mut [T] {
        let x = self.get_ref();
        let nelems = x.len();
        let x_ptr: *const T = x.as_ptr();
        unsafe { core::slice::from_raw_parts_mut(x_ptr as *mut T, nelems) }
    }
}

fn seek_prv_aligned_ptr(p: *mut u8, alignment: usize) -> *mut u8 {
    if p.align_offset(alignment) == 0 {
        unsafe { p.offset(-(alignment as isize)) }
    } else {
        let offset = (p as usize) % alignment;
        let p = unsafe { p.offset(-(offset as isize)) };
        assert!(p.align_offset(alignment) == 0);
        p
    }
}

pub(crate) fn rewind(p: *mut u8) -> *mut Block {
    // This logic could be simplified if we use the layout information that the allocator gets on
    // free. We could stick the block header *behind* the block data in the first space it can fit.
    // That would allow us to find the header deterministically versus using this scan.

    // First, go to the first pointer offset that could possibly correspond to this block.

    let mut block_p = unsafe { p.offset(-(BLOCK_STRUCT_NBYTES as isize)) };

    if block_p.align_offset(BLOCK_STRUCT_ALIGNMENT) != 0 {
        block_p = seek_prv_aligned_ptr(block_p, BLOCK_STRUCT_ALIGNMENT);
    }

    loop {
        // Interpret block_p as a block. If the offset matches up, we are good to go.
        let block_offset = unsafe { (*(block_p as *mut Block)).data_offset };
        let real_offset = unsafe { p.offset_from(block_p) } as u32;

        if real_offset == block_offset {
            // `block_p` now points to a valid block.
            break;
        } else {
            block_p = seek_prv_aligned_ptr(block_p, BLOCK_STRUCT_ALIGNMENT);
        }
    }

    block_p as *mut Block
}

#[derive(Debug)]
pub(crate) struct FreelistAllocator {
    first_chunk: *mut Chunk,
    next_free_block: *mut Block,
    chunk_nbytes: usize,
}

impl FreelistAllocator {
    pub const fn new() -> Self {
        FreelistAllocator {
            first_chunk: core::ptr::null_mut(),
            next_free_block: core::ptr::null_mut(),
            chunk_nbytes: CHUNK_NBYTES_DEFAULT,
        }
    }

    pub fn init(&mut self) -> Result<(), i32> {
        self.add_chunk()
    }

    fn add_chunk(&mut self) -> Result<(), i32> {
        let mut path_buf = get_null_path_buf();
        format_shmem_name(&mut path_buf);

        // TODO(rwails) Unwrap not safe here
        let new_chunk = allocate_shared_chunk(&path_buf, self.chunk_nbytes).unwrap();

        unsafe {
            (*new_chunk).next_chunk = self.first_chunk;
        }

        self.first_chunk = new_chunk;

        Ok(())
    }

    /// Returns the block and its predecessor (if it exists)
    fn check_free_list_for_acceptable_block(
        &mut self,
        alloc_nbytes: usize,
        alloc_alignment: usize,
    ) -> (*mut Block, *mut Block) // (pred, block)
    {
        let mut block = self.next_free_block;
        let mut pred: *mut Block = core::ptr::null_mut();

        while !block.is_null() {
            let (start_p, _) = unsafe { (*block).get_block_data_range() };

            if unsafe { (*block).alloc_nbytes as usize == alloc_nbytes }
                && start_p.align_offset(alloc_alignment) == 0
            {
                return (pred, block);
            }

            pred = block;
            unsafe {
                block = (*block).next_free_block;
            }
        }

        (pred, core::ptr::null_mut())
    }

    fn find_next_suitable_positions(
        p: *mut u8,
        alloc_nbytes: usize,
        alloc_alignment: usize,
    ) -> (*mut u8, *mut u8) {
        let off = p.align_offset(alloc_alignment);
        let start = unsafe { p.add(off) };
        let end = unsafe { start.add(alloc_nbytes) };
        (start, end)
    }

    fn try_creating_block_in_chunk(
        chunk: &mut Chunk,
        alloc_nbytes: usize,
        alloc_alignment: usize,
    ) -> *mut Block {
        let chunk_start = chunk as *mut Chunk as *mut u8;
        let chunk_end = unsafe { chunk_start.add(chunk.chunk_nbytes) };

        let data_start = unsafe { chunk.get_mut_data_start().add(chunk.data_cur as usize) };

        let (block_struct_start, block_struct_end) = Self::find_next_suitable_positions(
            data_start,
            BLOCK_STRUCT_NBYTES,
            BLOCK_STRUCT_ALIGNMENT,
        );
        let (block_data_start, block_data_end) =
            Self::find_next_suitable_positions(block_struct_end, alloc_nbytes, alloc_alignment);

        let data_offset = unsafe { block_data_start.offset_from(block_struct_start) };

        assert!(data_offset > 0);

        if block_data_end <= chunk_end {
            // The block fits.
            // Initialize the block
            let block = block_struct_start as *mut Block;

            unsafe {
                (*block).magic_init();
                (*block).next_free_block = core::ptr::null_mut();
                (*block).alloc_nbytes = alloc_nbytes as u32;
                (*block).data_offset = data_offset as u32;
            }

            return block;
        }

        core::ptr::null_mut()
    }

    pub fn alloc(&mut self, alloc_nbytes: usize, alloc_alignment: usize) -> *mut Block {
        // First, check the free list
        let (pred, mut block) =
            self.check_free_list_for_acceptable_block(alloc_nbytes, alloc_alignment);

        if !block.is_null() {
            // We found a hit off the free list, we can just return that.
            // But first we update the free list.
            if pred.is_null() {
                // The block was the first element on the list.
                self.next_free_block = unsafe { (*block).next_free_block };
            } else {
                // We can just update the predecessor
                unsafe {
                    (*pred).next_free_block = (*block).next_free_block;
                }
            }

            let (p, _) = unsafe { (*block).get_block_data_range() };
            assert!(p.align_offset(alloc_alignment) == 0);
            return block;
        }

        // If nothing in the free list, then check if the current chunk can handle the allocation
        block = Self::try_creating_block_in_chunk(
            unsafe { &mut (*self.first_chunk) },
            alloc_nbytes,
            alloc_alignment,
        );

        if block.is_null() {
            // Chunk didn't have enough capacity...
            self.add_chunk().unwrap();
        }

        block = Self::try_creating_block_in_chunk(
            unsafe { &mut (*self.first_chunk) },
            alloc_nbytes,
            alloc_alignment,
        );

        let block_p = block as *mut u8;

        let block_end = unsafe {
            let data_offset = (*block).data_offset;
            assert!(data_offset > 0);
            block_p.add(data_offset as usize).add(alloc_nbytes)
        };

        let chunk_p = self.first_chunk as *mut u8;
        unsafe {
            let data_cur = block_end.offset_from(chunk_p);
            (*self.first_chunk).data_cur = data_cur as u32;
        }

        assert!(!block.is_null());
        let (p, _) = unsafe { (*block).get_block_data_range() };
        assert!(p.align_offset(alloc_alignment) == 0);

        block
    }

    pub fn dealloc(&mut self, block: *mut Block) {
        if block.is_null() {
            return;
        }

        unsafe {
            (*block).magic_assert();
        }
        let old_block = self.next_free_block;
        unsafe {
            (*block).next_free_block = old_block;
        }
        self.next_free_block = block;
    }

    // PRE: Block was allocated with this allocator
    fn find_chunk(&self, block: *const Block) -> Option<*const Chunk> {
        unsafe {
            (*block).magic_assert();
        }

        if !self.first_chunk.is_null() {
            let mut chunk_to_check = self.first_chunk;

            while !chunk_to_check.is_null() {
                // Safe to deref throughout this block because we checked for null above
                unsafe {
                    (*chunk_to_check).magic_assert();
                }
                let data_start = unsafe { (*chunk_to_check).get_data_start() };
                let data_end = unsafe { (chunk_to_check as *const u8).add(self.chunk_nbytes) };

                // Now we just see if the block is in the range.
                let block_p = block as *const u8;

                if block_p >= data_start && block_p < data_end {
                    return Some(chunk_to_check);
                }

                chunk_to_check = unsafe { (*chunk_to_check).next_chunk };
            }
        }

        None
    }

    // PRE: Block was allocated with this allocator
    pub fn serialize(&self, block: *const Block) -> BlockSerialized {
        unsafe {
            (*block).magic_assert();
        }

        if let Some(chunk) = self.find_chunk(block) {
            let chunk_p = chunk as *const u8;
            let block_p = block as *const u8;
            let offset = unsafe { block_p.offset_from(chunk_p) };
            assert!(offset > 0);

            BlockSerialized {
                chunk_name: unsafe { (*chunk).chunk_name },
                offset,
            }
        } else {
            panic!("Block attempted to be serialized with wrong allocator.");
        }
    }

    pub fn destruct(&mut self) {
        if !self.first_chunk.is_null() {
            let mut chunk_to_dealloc = self.first_chunk;

            while !chunk_to_dealloc.is_null() {
                // Safe due to check above
                let tmp = unsafe { (*chunk_to_dealloc).next_chunk };

                // TODO(rwails) unwrap here is bad.
                deallocate_shared_chunk(chunk_to_dealloc).unwrap();

                chunk_to_dealloc = tmp;
            }

            self.first_chunk = core::ptr::null_mut();
        }
    }
}

const CHUNK_CAPACITY: usize = 128;

#[repr(C)]
#[derive(Debug)]
pub(crate) struct FreelistDeserializer {
    chunks: [*mut Chunk; CHUNK_CAPACITY],
    nmapped_chunks: usize,
    chunk_nbytes: usize,
}

impl FreelistDeserializer {
    pub fn new() -> FreelistDeserializer {
        FreelistDeserializer {
            chunks: [core::ptr::null_mut(); CHUNK_CAPACITY],
            nmapped_chunks: 0,
            chunk_nbytes: CHUNK_NBYTES_DEFAULT,
        }
    }

    fn find_chunk(&self, chunk_name: &PathBuf) -> *mut Chunk {
        for idx in 0..self.nmapped_chunks {
            let chunk = self.chunks[idx];

            // Safe here to deref because we are only checking within the allocated range.
            if unsafe { (*chunk).chunk_name == *chunk_name } {
                return chunk;
            }
        }

        core::ptr::null_mut()
    }

    fn map_chunk(&mut self, chunk_name: &PathBuf) -> *mut Chunk {
        // TODO(rwails) Fix unwrap on view shared chunk
        let chunk = view_shared_chunk(chunk_name, self.chunk_nbytes).unwrap();

        if self.nmapped_chunks == CHUNK_CAPACITY {
            // Ran out of chunk slots -- we're going to leak the handle.
        } else {
            self.chunks[self.nmapped_chunks] = chunk;
            self.nmapped_chunks += 1;
        }

        chunk
    }

    pub fn deserialize(&mut self, block_ser: &BlockSerialized) -> *mut Block {
        let mut block_chunk = self.find_chunk(&block_ser.chunk_name);

        if block_chunk.is_null() {
            block_chunk = self.map_chunk(&block_ser.chunk_name);
        }

        let chunk_p = block_chunk as *mut u8;

        assert!(block_ser.offset > 0);
        let block_p = unsafe { chunk_p.add(block_ser.offset as usize) };

        assert!(!block_p.is_null());
        unsafe {
            (*(block_p as *mut Block)).magic_assert();
        };

        block_p as *mut Block
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_serialize() {
        let mut alloc = FreelistAllocator::new();
        alloc.init().unwrap();

        let b1 = alloc.alloc(8, 4);
        unsafe {
            (*b1).get_mut_ref::<u32>()[0] = 42;
        }
        let b1_ser = alloc.serialize(b1);

        let mut deserial = FreelistDeserializer::new();
        let b1_2 = deserial.deserialize(&b1_ser);
        unsafe {
            (*b1_2).get_mut_ref::<u32>()[1] = 29;
        }

        println!("{:?} {:?}", b1, b1_2);

        println!(
            "{:?} {:?} {:?}",
            unsafe { (*b1).get_ref::<u32>() },
            b1_ser,
            unsafe { (*b1_2).get_ref::<u32>() }
        );

        alloc.destruct();
    }

    #[test]
    fn test_allocator() {
        let mut v: Vec<*mut Block> = Default::default();

        let mut alloc = FreelistAllocator::new();
        alloc.init().unwrap();

        for _ in 0..10 {
            v.push(alloc.alloc(32, 32));
        }

        let mut idx: u32 = 0;
        for block in &v[..] {
            unsafe {
                let r = (**block).get_mut_ref::<u32>();
                r[0] = idx;
                idx += 1;
                println!("{:?} {:?}", block, (**block));
            }
        }

        for block in &v[..] {
            unsafe {
                let r = (**block).get_ref::<u32>();
                println!("{:?}", r);
            }
        }

        let b1 = alloc.alloc(10, 8);
        let b2 = alloc.alloc(20, 8);
        let b3 = alloc.alloc(30, 8);

        let (p, _) = unsafe { (*b1).get_mut_block_data_range() };
        let bk = rewind(p);
        unsafe {
            println!("{:?} {:?}", *b1, *bk);
        }

        let (p, _) = unsafe { (*b2).get_mut_block_data_range() };
        let bk = rewind(p);
        unsafe {
            println!("{:?} {:?}", *b2, *bk);
        }

        let (p, _) = unsafe { (*b3).get_mut_block_data_range() };
        let bk = rewind(p);
        unsafe {
            println!("{:?} {:?}", *b3, *bk);
        }

        println!("{:?} {:?} {:?}", b1, b2, b3);
        alloc.dealloc(b3);
        alloc.dealloc(b2);
        alloc.dealloc(b1);

        let b3 = alloc.alloc(30, 8);
        let b2 = alloc.alloc(20, 8);
        let b1 = alloc.alloc(10, 8);
        println!("{:?} {:?} {:?}", b1, b2, b3);
        println!("{:?}", alloc);
        alloc.dealloc(b3);
        println!("{:?}", alloc);
        alloc.dealloc(b2);
        alloc.dealloc(b1);

        alloc.destruct();

        /*

        for b in &v[..] {
            let serialized = alloc.serialize(*b);
            println!("{:?}", serialized);
        }

        alloc.dealloc(v.remove(0));

        alloc.destruct();
        */
    }

    #[test]
    fn test_open() {
        let mut path_buf = get_null_path_buf();
        format_shmem_name(&mut path_buf);

        let (p, _) = create_map_shared_memory(&path_buf, 100).unwrap();
        p[0] = 65;
        p[1] = 66;
        p[2] = 67;

        println!("p={:?}", p);
        munmap(p).unwrap();

        unsafe {
            unlink(path_buf.as_ptr()).unwrap();
        }
    }
}
