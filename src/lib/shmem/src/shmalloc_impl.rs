//! This module implements a low-level, unsafe shared memory allocator that uses mmap'ed shared
//! memory files as the backing store. The module is intended to be no-std so it can be used in
//! Shadow's shim library, which must async-signal-safe.
//!
//! The allocator chains together chunks of shared memory and divvies out portions of each chunk
//! using a first-fit strategy. The allocator also implements a freelist so that allocated blocks
//! can be reused efficiently after free. The allocator design isn't good for general-purpose
//! allocation, but should be OK when used for just a few types.
//!
//! This code is intended to be private; the `allocator` module is the public, safer-to-use
//! front end.

use crate::raw_syscall::*;
use core::fmt::Write;

use formatting_nostd::FormatBuffer;
use linux_api::errno::Errno;

use vasi::VirtualAddressSpaceIndependent;

use crate::util::PathBuf;

// TODO(rwails): This may be unified with `shadow_rs::utility::Magic`.
#[cfg(debug_assertions)]
type CanaryBuf = [u8; 4];

#[cfg(debug_assertions)]
const CANARY: CanaryBuf = [0xDE, 0xAD, 0xBE, 0xEF];

#[cfg(not(debug_assertions))]
type CanaryBuf = [u8; 0];

#[cfg(not(debug_assertions))]
const CANARY: CanaryBuf = [];

trait Canary {
    fn canary_init(&mut self);

    #[cfg_attr(not(debug_assertions), allow(dead_code))]
    fn canary_check(&self) -> bool;

    #[cfg(debug_assertions)]
    fn canary_assert(&self) {
        assert!(self.canary_check());
    }

    #[cfg(not(debug_assertions))]
    fn canary_assert(&self) {}
}

#[derive(Copy, Clone)]
pub(crate) enum AllocError {
    Clock,
    Open,
    FTruncate,
    MMap,
    MUnmap,
    Unlink,
    WrongAllocator,
    // Leak,
    GetPID,
}

const fn alloc_error_to_str(e: AllocError) -> Option<&'static str> {
    match e {
        AllocError::Clock => Some("Error calling clock_gettime()"),
        AllocError::Open => Some("Error calling open()"),
        AllocError::FTruncate => Some("Error calling ftruncate()"),
        AllocError::MMap => Some("Error calling mmap()"),
        AllocError::MUnmap => Some("Error calling munmap()"),
        AllocError::Unlink => Some("Error calling unlink()"),
        AllocError::WrongAllocator => Some("Block was passed to incorrect allocator"),
        // AllocError::Leak => Some("Allocator destroyed but not all blocks are deallocated first"),
        AllocError::GetPID => Some("Error calling getpid()"),
    }
}

impl core::fmt::Debug for AllocError {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> Result<(), core::fmt::Error> {
        match alloc_error_to_str(*self) {
            Some(s) => formatter.write_str(s),
            None => write!(formatter, "unknown allocator error"),
        }
    }
}

impl core::fmt::Display for AllocError {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> Result<(), core::fmt::Error> {
        match alloc_error_to_str(*self) {
            Some(s) => formatter.write_str(s),
            None => write!(formatter, "unknown allocator error"),
        }
    }
}

pub(crate) fn log_err(error: AllocError, errno: Option<Errno>) {
    let mut buf = FormatBuffer::<1024>::new();

    if let Some(e) = errno {
        write!(&mut buf, "{error} ({e})").unwrap();
    } else {
        write!(&mut buf, "{error}").unwrap();
    }

    log::error!("{}", buf.as_str());
}

pub(crate) fn log_err_and_exit(error: AllocError, errno: Option<Errno>) -> ! {
    log_err(error, errno);
    let _ = tgkill(
        getpid().unwrap(),
        gettid().unwrap(),
        linux_api::signal::Signal::SIGABRT.into(),
    );
    unreachable!()
}

fn format_shmem_name(buf: &mut PathBuf) {
    let pid = match getpid() {
        Ok(pid) => pid,
        Err(err) => log_err_and_exit(AllocError::GetPID, Some(err)),
    };

    let ts = match clock_monotonic_gettime() {
        Ok(ts) => ts,
        Err(errno) => log_err_and_exit(AllocError::Clock, Some(errno)),
    };

    let mut fb = FormatBuffer::<{ crate::util::PATH_MAX_NBYTES }>::new();
    write!(
        &mut fb,
        "/dev/shm/shadow_shmemfile_{}.{}-{}",
        ts.tv_sec, ts.tv_nsec, pid
    )
    .unwrap();

    *buf = crate::util::buf_from_utf8_str(fb.as_str()).unwrap();
}

const CHUNK_NBYTES_DEFAULT: usize = 8 * 1024 * 1024; // 8 MiB

fn create_map_shared_memory<'a>(path_buf: &PathBuf, nbytes: usize) -> (&'a mut [u8], i32) {
    use linux_api::fcntl::OFlag;
    use linux_api::mman::{MapFlags, ProtFlags};

    const MODE: u32 = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;
    let open_flags = OFlag::O_RDWR | OFlag::O_CREAT | OFlag::O_EXCL | OFlag::O_CLOEXEC;
    let prot = ProtFlags::PROT_READ | ProtFlags::PROT_WRITE;
    let map_flags = MapFlags::MAP_SHARED;

    let fd = match unsafe { open(path_buf, open_flags, MODE) } {
        Ok(fd) => fd,
        Err(err) => log_err_and_exit(AllocError::Open, Some(err)),
    };

    // u64 into usize should be safe to unwrap.
    if let Err(errno) = ftruncate(fd, nbytes.try_into().unwrap()) {
        log_err_and_exit(AllocError::FTruncate, Some(errno))
    };

    let retval = match unsafe {
        mmap(
            core::ptr::null_mut(),
            nbytes.try_into().unwrap(),
            prot,
            map_flags,
            fd,
            0,
        )
    } {
        Ok(retval) => retval,
        Err(errno) => log_err_and_exit(AllocError::MMap, Some(errno)),
    };

    (retval, fd)
}

// Similar to `create_map_shared_memory` but no O_CREAT or O_EXCL and no ftruncate calls.
fn view_shared_memory<'a>(path_buf: &PathBuf, nbytes: usize) -> (&'a mut [u8], i32) {
    use linux_api::fcntl::OFlag;
    use linux_api::mman::{MapFlags, ProtFlags};

    const MODE: u32 = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;
    let open_flags = OFlag::O_RDWR | OFlag::O_CLOEXEC;
    let prot = ProtFlags::PROT_READ | ProtFlags::PROT_WRITE;
    let map_flags = MapFlags::MAP_SHARED;

    let fd = match unsafe { open(path_buf, open_flags, MODE) } {
        Ok(fd) => fd,
        Err(errno) => log_err_and_exit(AllocError::Open, Some(errno)),
    };

    let retval = match unsafe {
        mmap(
            core::ptr::null_mut(),
            nbytes.try_into().unwrap(),
            prot,
            map_flags,
            fd,
            0,
        )
    } {
        Ok(retval) => retval,
        Err(errno) => log_err_and_exit(AllocError::MMap, Some(errno)),
    };

    (retval, fd)
}

#[repr(C)]
struct Chunk {
    canary_front: CanaryBuf,
    chunk_name: PathBuf,
    chunk_fd: i32,
    chunk_nbytes: usize,
    data_cur: u32, // The current point at which the data starts from the start of the data segment
    next_chunk: *mut Chunk,
    canary_back: CanaryBuf,
}

impl Chunk {
    fn get_mut_data_start(&mut self) -> *mut u8 {
        self.get_data_start().cast_mut()
    }

    fn get_data_start(&self) -> *const u8 {
        let p = core::ptr::from_ref(self) as *const u8;
        unsafe { p.add(core::mem::size_of::<Self>()) }
    }
}

impl Canary for Chunk {
    fn canary_init(&mut self) {
        self.canary_front = CANARY;
        self.canary_back = CANARY;
    }

    fn canary_check(&self) -> bool {
        self.canary_front == CANARY && self.canary_back == CANARY
    }
}

fn allocate_shared_chunk(path_buf: &PathBuf, nbytes: usize) -> *mut Chunk {
    let (p, fd) = create_map_shared_memory(path_buf, nbytes);

    let chunk_meta: *mut Chunk = p.as_mut_ptr() as *mut Chunk;

    unsafe {
        (*chunk_meta).chunk_name = *path_buf;
        (*chunk_meta).chunk_fd = fd;
        (*chunk_meta).chunk_nbytes = nbytes;
        (*chunk_meta).data_cur = 0;
        (*chunk_meta).next_chunk = core::ptr::null_mut();
        (*chunk_meta).canary_init();
    }

    chunk_meta
}

fn view_shared_chunk(path_buf: &PathBuf, nbytes: usize) -> *mut Chunk {
    let (p, _) = view_shared_memory(path_buf, nbytes);
    let chunk_meta: *mut Chunk = p.as_mut_ptr() as *mut Chunk;
    chunk_meta
}

fn deallocate_shared_chunk(chunk_meta: *const Chunk) {
    unsafe {
        (*chunk_meta).canary_assert();
    }

    let path_buf = unsafe { (*chunk_meta).chunk_name };
    let chunk_nbytes = unsafe { (*chunk_meta).chunk_nbytes };

    if let Err(errno) =
        munmap(unsafe { core::slice::from_raw_parts_mut(chunk_meta as *mut u8, chunk_nbytes) })
    {
        log_err(AllocError::MUnmap, Some(errno));
    }

    if let Err(errno) = unsafe { unlink(&path_buf) } {
        log_err(AllocError::Unlink, Some(errno));
    }
}

#[repr(C)]
#[derive(Debug)]
pub(crate) struct Block {
    canary_front: CanaryBuf,
    next_free_block: *mut Block, // This can't be a short pointer, because it may point across chunks.
    alloc_nbytes: u32,           // What is the size of the block
    data_offset: u32,            // From the start location of the block header
    canary_back: CanaryBuf,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, VirtualAddressSpaceIndependent)]
pub(crate) struct BlockSerialized {
    pub(crate) chunk_name: crate::util::PathBuf,
    pub(crate) offset: isize,
}

const BLOCK_STRUCT_NBYTES: usize = core::mem::size_of::<Block>();
const BLOCK_STRUCT_ALIGNMENT: usize = core::mem::align_of::<Block>();

impl Canary for Block {
    fn canary_init(&mut self) {
        self.canary_front = CANARY;
        self.canary_back = CANARY;
    }

    fn canary_check(&self) -> bool {
        self.canary_front == CANARY && self.canary_back == CANARY
    }
}

impl Block {
    pub(self) fn get_block_data_range(&self) -> (*const u8, *const u8) {
        self.canary_assert();

        let data_offset = self.data_offset;
        let alloc_nbytes = self.alloc_nbytes;
        let block = core::ptr::from_ref(self) as *const u8;
        assert!(!block.is_null());

        let data_begin = unsafe { block.add(data_offset as usize) };
        let data_end = unsafe { data_begin.add(alloc_nbytes as usize) };

        (data_begin, data_end)
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
        unsafe { core::slice::from_raw_parts_mut(x_ptr.cast_mut(), nelems) }
    }
}

/*

----> These functions are needed if implementing the global allocator API

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

*/

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
        let mut path_buf = crate::util::NULL_PATH_BUF;
        format_shmem_name(&mut path_buf);

        let new_chunk = allocate_shared_chunk(&path_buf, self.chunk_nbytes);

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
        let chunk_start = core::ptr::from_mut(chunk) as *mut u8;
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
                (*block).canary_init();
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
            (*block).canary_assert();
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
            (*block).canary_assert();
        }

        if !self.first_chunk.is_null() {
            let mut chunk_to_check = self.first_chunk;

            while !chunk_to_check.is_null() {
                // Safe to deref throughout this block because we checked for null above
                unsafe {
                    (*chunk_to_check).canary_assert();
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
            (*block).canary_assert();
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
            log_err_and_exit(AllocError::WrongAllocator, None);
        }
    }

    pub fn destruct(&mut self) {
        if !self.first_chunk.is_null() {
            let mut chunk_to_dealloc = self.first_chunk;

            while !chunk_to_dealloc.is_null() {
                // Safe due to check above
                let tmp = unsafe { (*chunk_to_dealloc).next_chunk };

                deallocate_shared_chunk(chunk_to_dealloc);

                chunk_to_dealloc = tmp;
            }

            self.first_chunk = core::ptr::null_mut();
        }
    }
}

const CHUNK_CAPACITY: usize = 64;

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
            if unsafe { (*chunk).chunk_name == (*chunk_name) } {
                return chunk;
            }
        }

        core::ptr::null_mut()
    }

    fn map_chunk(&mut self, chunk_name: &PathBuf) -> *mut Chunk {
        let chunk = view_shared_chunk(chunk_name, self.chunk_nbytes);

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
            (*(block_p as *mut Block)).canary_assert();
        };

        block_p as *mut Block
    }
}
