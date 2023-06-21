#![allow(dead_code)]

use crate::raw_syscall::*;
use numtoa::NumToA;

const PATH_MAX_NBYTES: usize = 255;
const PATH_BUF_NBYTES: usize = PATH_MAX_NBYTES + 1;
type PathBuf = [u8; PATH_BUF_NBYTES];

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

//const CHUNK_NBYTES: usize = 2097152;
const CHUNK_NBYTES: usize = 512;

fn create_map_shared_memory<'a>(
    path_buf: &PathBuf,
    nbytes: usize,
) -> Result<(&'a mut [u8], i32), i32> {
    const OPEN_FLAGS: i32 = libc::O_RDWR | libc::O_CREAT | libc::O_EXCL | libc::O_CLOEXEC;
    const MODE: u32 = libc::S_IRUSR | libc::S_IWUSR | libc::S_IRGRP | libc::S_IWGRP;
    const PROT: i32 = libc::PROT_READ | libc::PROT_WRITE;
    const MAP_FLAGS: i32 = libc::MAP_SHARED;

    let fd = unsafe { open(path_buf.as_ptr(), OPEN_FLAGS, MODE)? };
    ftruncate(fd, nbytes.try_into().unwrap())?;

    let retval = unsafe {
        mmap(
            core::ptr::null_mut(),
            nbytes.try_into().unwrap(),
            PROT,
            MAP_FLAGS,
            fd,
            0,
        )?
    };

    Ok((retval, fd))
}

// Similar to `create_map_shared_memory` but no O_CREAT or O_EXCL and no ftruncate calls.
fn view_shared_memory<'a>(
    path_buf: &PathBuf,
    nbytes: usize,
) -> Result<(&'a mut [u8], i32), i32> {
    const OPEN_FLAGS: i32 = libc::O_RDWR | libc::O_CLOEXEC;
    const MODE: u32 = libc::S_IRUSR | libc::S_IWUSR | libc::S_IRGRP | libc::S_IWGRP;
    const PROT: i32 = libc::PROT_READ | libc::PROT_WRITE;
    const MAP_FLAGS: i32 = libc::MAP_SHARED;

    let fd = unsafe { open(path_buf.as_ptr(), OPEN_FLAGS, MODE)? };

    let retval = unsafe {
        mmap(
            core::ptr::null_mut(),
            nbytes.try_into().unwrap(),
            PROT,
            MAP_FLAGS,
            fd,
            0,
        )?
    };

    Ok((retval, fd))
}

#[repr(C)]
#[derive(Debug)]
struct ChunkMeta {
    magic_front: MagicBuf,
    chunk_name: PathBuf,
    chunk_fd: i32,
    data_start: *mut u8,
    next_chunk: *mut ChunkMeta,
    magic_back: MagicBuf,
}

impl Magic for ChunkMeta {
    fn magic_init(&mut self) {
        self.magic_front = get_magic_buf();
        self.magic_back = get_magic_buf();
    }

    fn magic_check(&self) -> bool {
        self.magic_front == get_magic_buf() && self.magic_back == get_magic_buf()
    }
}

fn allocate_shared_chunk(path_buf: &PathBuf, nbytes: usize) -> Result<*mut ChunkMeta, i32> {
    let (p, fd) = create_map_shared_memory(path_buf, nbytes)?;
    let chunk_meta: *mut ChunkMeta = p.as_mut_ptr() as *mut ChunkMeta;

    unsafe {
        (*chunk_meta).chunk_name = *path_buf;
        (*chunk_meta).chunk_fd = fd;
        (*chunk_meta).data_start = (chunk_meta as *mut u8).add(core::mem::size_of::<ChunkMeta>());
        (*chunk_meta).next_chunk = core::ptr::null_mut();
        (*chunk_meta).magic_init();
    }

    Ok(chunk_meta)
}

fn view_shared_chunk(path_buf: &PathBuf, nbytes: usize) -> Result<*mut ChunkMeta, i32> {
    let (p, _) = view_shared_memory(path_buf, nbytes)?;
    let chunk_meta: *mut ChunkMeta = p.as_mut_ptr() as *mut ChunkMeta;
    Ok(chunk_meta)
}

fn deallocate_shared_chunk(chunk_meta: *const ChunkMeta) -> Result<(), i32> {
    unsafe {
        (*chunk_meta).magic_assert();
    }

    let path_buf = unsafe { (*chunk_meta).chunk_name };

    munmap(unsafe { core::slice::from_raw_parts_mut(chunk_meta as *mut u8, CHUNK_NBYTES) })
        .unwrap();

    unsafe {
        unlink(path_buf.as_ptr())?;
    }

    Ok(())
}

#[repr(C)]
#[derive(Debug)]
pub(crate) struct BlockHdr {
    magic_front: MagicBuf,
    next_free_block: *mut BlockHdr,
    data_start: *mut u8,
    magic_back: MagicBuf,
}

#[repr(C)]
#[derive(Debug)]
pub(crate) struct BlockHdrSerialized {
    chunk_name: PathBuf,
    offset: isize,
}

const MEM_BLOCK_NBYTES: usize = core::mem::size_of::<BlockHdr>();
const MEM_BLOCK_ALIGNMENT: usize = core::mem::align_of::<BlockHdr>();

impl Magic for BlockHdr {
    fn magic_init(&mut self) {
        self.magic_front = get_magic_buf();
        self.magic_back = get_magic_buf();
    }

    fn magic_check(&self) -> bool {
        self.magic_front == get_magic_buf() && self.magic_back == get_magic_buf()
    }
}

impl BlockHdr {
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
    pub(self) fn get_mut_block_data_range(
        &mut self,
        alloc_nbytes: usize,
        alloc_alignment: usize,
    ) -> (*mut u8, *mut u8) {
        let block: *mut BlockHdr = &mut *self;

        assert!(!block.is_null());

        let block_end = unsafe { (block as *mut u8).add(MEM_BLOCK_NBYTES) };

        let data_start_offset = block_end.align_offset(alloc_alignment);
        let data_begin = unsafe { block_end.add(data_start_offset) };
        let data_end = unsafe { data_begin.add(alloc_nbytes) };

        (data_begin, data_end)
    }

    pub(self) fn get_block_data_range(
        &self,
        alloc_nbytes: usize,
        alloc_alignment: usize,
    ) -> (*const u8, *const u8) {
        let block: *const BlockHdr = &*self;

        assert!(!block.is_null());

        let block_end = unsafe { (block as *const u8).add(MEM_BLOCK_NBYTES) };

        let data_start_offset = block_end.align_offset(alloc_alignment);
        let data_begin = unsafe { block_end.add(data_start_offset) };
        let data_end = unsafe { data_begin.add(alloc_nbytes) };

        (data_begin, data_end)
    }

    pub(crate) fn get_mut_bytes(&mut self, nbytes: usize, alignment: usize) -> &mut [u8] {
        let (begin_p, _) = self.get_mut_block_data_range(nbytes, alignment);
        unsafe { core::slice::from_raw_parts_mut(begin_p, nbytes) }
    }

    pub(crate) fn get_mut_ref<T>(&mut self, nbytes: usize, alignment: usize) -> &mut [T] {
        let (begin_p, end_p) = self.get_mut_block_data_range(nbytes, alignment);
        let block_len = unsafe { end_p.offset_from(begin_p) } as usize;
        assert!(block_len % core::mem::size_of::<T>() == 0);
        let nelems = block_len / core::mem::size_of::<T>();
        unsafe { core::slice::from_raw_parts_mut(begin_p as *mut T, nelems) }
    }

    pub(crate) fn get_ref<T>(&self, nbytes: usize, alignment: usize) -> &[T] {
        let (begin_p, end_p) = self.get_block_data_range(nbytes, alignment);
        let block_len = unsafe { end_p.offset_from(begin_p) } as usize;
        assert!(block_len % core::mem::size_of::<T>() == 0);
        let nelems = block_len / core::mem::size_of::<T>();
        unsafe { core::slice::from_raw_parts_mut(begin_p as *mut T, nelems) }
    }
}

#[derive(Debug)]
pub(crate) struct UniformFreelistAllocator {
    first_chunk: Option<*mut ChunkMeta>,
    next_free_block: *mut BlockHdr,
    alloc_nbytes: usize,
    alloc_alignment: usize,
}

impl UniformFreelistAllocator {
    ///
    /// # Parameters
    ///
    /// * `alloc_alignment` - Must be a power of two; leq than 4096.
    pub fn new(alloc_nbytes: usize, alloc_alignment: usize) -> Self {
        UniformFreelistAllocator {
            first_chunk: None,
            next_free_block: core::ptr::null_mut(),
            alloc_nbytes,
            alloc_alignment,
        }
    }

    pub fn init(&mut self) -> Result<(), i32> {
        self.add_chunk()
    }

    fn add_chunk(&mut self) -> Result<(), i32> {
        let mut path_buf = get_null_path_buf();
        format_shmem_name(&mut path_buf);

        // TODO(rwails) Unwrap not safe here
        let new_chunk = allocate_shared_chunk(&path_buf, CHUNK_NBYTES).unwrap();

        // This unwrap is safe, because we know the pointer is not NULL at this point.
        let (first_block, last_block) = self.init_chunk(unsafe { new_chunk.as_ref().unwrap() });

        if first_block.is_null() {
            panic!();
        }

        // Now update our linked lists

        unsafe {
            (*last_block).next_free_block = self.next_free_block;
        }

        if let Some(current_chunk) = self.first_chunk {
            unsafe {
                (*new_chunk).next_chunk = current_chunk;
            }
        }

        self.first_chunk = Some(new_chunk);
        self.next_free_block = first_block;

        Ok(())
    }

    pub fn alloc(&mut self) -> *mut BlockHdr {
        if !self.next_free_block.is_null() {
            // We just give the next block and update the free
            // list.
            let retval = self.next_free_block;
            self.next_free_block = unsafe { (*retval).next_free_block };
            unsafe {
                (*retval).magic_assert();
                retval
            }
        } else {
            // We need to allocate a new chunk
            self.add_chunk().unwrap();
            self.alloc()
        }
    }

    pub fn dealloc(&mut self, block: *mut BlockHdr) {
        if !block.is_null() {
            let mut block = unsafe { &mut *block };
            block.magic_check();
            block.next_free_block = self.next_free_block;
            self.next_free_block = block;
        } else {
            panic!();
        }
    }

    // PRE: Block was allocated with this allocator
    fn find_chunk(&self, block: *const BlockHdr) -> Option<*const ChunkMeta> {
        unsafe {
            (*block).magic_assert();
        }

        if let Some(chunk) = self.first_chunk {
            let mut chunk_to_check = chunk;

            while !chunk_to_check.is_null() {
                // Safe to deref throughout this block because we checked for null above
                unsafe {
                    (*chunk_to_check).magic_assert();
                }
                let data_start = unsafe { (*chunk_to_check).data_start };
                let data_end = unsafe { (chunk_to_check as *const u8).add(CHUNK_NBYTES) };

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
    pub fn serialize(&self, block: *const BlockHdr) -> BlockHdrSerialized {
        unsafe {
            (*block).magic_assert();
        }

        if let Some(chunk) = self.find_chunk(block) {
            let chunk_p = chunk as *const u8;
            let block_p = block as *const u8;
            let offset = unsafe { block_p.offset_from(chunk_p) };
            assert!(offset > 0);

            BlockHdrSerialized {
                chunk_name: unsafe { (*chunk).chunk_name },
                offset,
            }
        } else {
            panic!("Block attempted to be serialized with wrong allocator.");
        }
    }

    pub unsafe fn get_mut_bytes(&self, block: *mut BlockHdr) -> &mut [u8] {
        if !block.is_null() {
            let block = unsafe { &mut *block };
            block.get_mut_bytes(self.alloc_nbytes, self.alloc_alignment)
        } else {
            panic!();
        }
    }

    pub unsafe fn get_mut_ref<T>(&self, block: *mut BlockHdr) -> &mut [T] {
        if !block.is_null() {
            let block = unsafe { &mut *block };
            let (begin_p, end_p) =
                block.get_mut_block_data_range(self.alloc_nbytes, self.alloc_alignment);
            let block_len = unsafe { end_p.offset_from(begin_p) } as usize;
            assert!(block_len % core::mem::size_of::<T>() == 0);
            let nelems = block_len / core::mem::size_of::<T>();
            unsafe { core::slice::from_raw_parts_mut(begin_p as *mut T, nelems) }
        } else {
            panic!();
        }
    }

    /// # Return Value
    ///
    /// Returns pointers to the first and last block that was allocated.
    fn init_chunk(&self, chunk: &ChunkMeta) -> (*mut BlockHdr, *mut BlockHdr) {
        let mut p = chunk.data_start;

        let chunk_data_nbytes = CHUNK_NBYTES - core::mem::size_of::<ChunkMeta>();
        let end_p = unsafe { chunk.data_start.add(chunk_data_nbytes) };

        // Begin by trying to initialize the first block...
        let first_block: *mut BlockHdr = self.init_block(core::ptr::null_mut(), p, end_p);
        let mut block: *mut BlockHdr = first_block;
        let mut last_block: *mut BlockHdr = core::ptr::null_mut();

        while !block.is_null() {
            unsafe {
                (*block).magic_assert();
                (_, p) = (*block).get_mut_block_data_range(self.alloc_nbytes, self.alloc_alignment);
            }
            last_block = block;
            block = self.init_block(block, p, end_p);
        }

        (first_block, last_block)
    }

    pub fn destruct(&mut self) {
        if let Some(chunk) = self.first_chunk {
            let mut chunk_to_dealloc = chunk;

            while !chunk_to_dealloc.is_null() {
                // Safe due to check above
                let tmp = unsafe { (*chunk_to_dealloc).next_chunk };

                // TODO(rwails) unwrap here is bad.
                deallocate_shared_chunk(chunk_to_dealloc).unwrap();

                chunk_to_dealloc = tmp;
            }

            self.first_chunk = None;
        }
    }

    fn init_block(
        &self,
        prev_block: *mut BlockHdr,
        current_p: *mut u8,
        end_p: *mut u8,
    ) -> *mut BlockHdr {
        let block_start_offset = current_p.align_offset(MEM_BLOCK_ALIGNMENT);
        let block_begin = unsafe { current_p.add(block_start_offset) };
        let block_end = unsafe { block_begin.add(MEM_BLOCK_NBYTES) };

        let (data_begin, data_end) = unsafe {
            let block = block_begin as *mut BlockHdr;
            (*block).get_mut_block_data_range(self.alloc_nbytes, self.alloc_alignment)
        };

        assert!(unsafe { block_end.offset_from(block_begin) } as usize == MEM_BLOCK_NBYTES);
        assert!(unsafe { data_end.offset_from(data_begin) } as usize == self.alloc_nbytes);
        assert!(block_begin.align_offset(MEM_BLOCK_ALIGNMENT) == 0);
        assert!(data_begin.align_offset(self.alloc_alignment) == 0);
        assert!(block_end > block_begin && data_begin >= block_end && data_end > data_begin);

        if data_end < end_p {
            let this_block = block_begin as *mut BlockHdr;
            // Safe to unwrap here -- we validated above that this is a valid pointer.
            let mut this_block = unsafe { this_block.as_mut().unwrap() };
            this_block.magic_init();

            this_block.next_free_block = core::ptr::null_mut();

            if !prev_block.is_null() {
                // Safe to unwrap here, we just checked for nullness above.
                unsafe { prev_block.as_mut().unwrap().next_free_block = this_block };
            }

            block_begin as *mut BlockHdr
        } else {
            // Not enough room left in the chunk
            core::ptr::null_mut()
        }
    }
}

#[derive(Debug)]
pub(crate) struct UniformFreelistDeserializer<const ChunkCapacity: usize> {
    chunks: [*mut ChunkMeta; ChunkCapacity],
    nmapped_chunks: usize,
}

impl<const ChunkCapacity: usize> UniformFreelistDeserializer<ChunkCapacity> {
    fn new() -> UniformFreelistDeserializer<ChunkCapacity> {
        UniformFreelistDeserializer {
            chunks: [core::ptr::null_mut(); ChunkCapacity],
            nmapped_chunks: 0,
        }
    }

    fn find_chunk(&self, chunk_name: PathBuf) -> *mut ChunkMeta {
        for idx in 0..self.nmapped_chunks {
            let chunk = self.chunks[idx];

            // Safe here to deref because we are only checking within the allocated range.
            if unsafe { (*chunk).chunk_name == chunk_name } {
                return chunk;
            }
        }

        core::ptr::null_mut()
    }

    fn map_chunk(&mut self, chunk_name: PathBuf) {
        self.nmapped_chunks += 1;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_allocator() {
        let mut v: Vec<*mut BlockHdr> = Default::default();

        let mut alloc = UniformFreelistAllocator::new(32, 32);
        alloc.init().unwrap();

        for _ in 0..1000 {
            v.push(alloc.alloc());
        }

        for b in &v[..] {
            let serialized = alloc.serialize(*b);
            println!("{:?}", serialized);
        }

        alloc.dealloc(v.remove(0));

        alloc.destruct();
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

    #[test]
    fn test_chunk() {
        let mut path_buf = get_null_path_buf();
        format_shmem_name(&mut path_buf);

        let chunk = allocate_shared_chunk(&path_buf, 1000).unwrap();
        deallocate_shared_chunk(chunk).unwrap();
    }
}
