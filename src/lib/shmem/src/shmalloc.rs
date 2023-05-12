#![allow(dead_code)]

use crate::raw_syscall::*;
use std::io::{Cursor, Write};

const PATH_MAX_NBYTES: usize = 255;
const PATH_BUF_NBYTES: usize = PATH_MAX_NBYTES + 1;

#[repr(transparent)]
#[derive(Clone, Debug)]
struct PathBufWrapper {
    buf: [u8; PATH_BUF_NBYTES],
}

impl PathBufWrapper {
    pub fn new(mut buf: [u8; PATH_BUF_NBYTES]) -> Self {
        buf.iter_mut().for_each(|x| *x = 0);
        PathBufWrapper { buf }
    }

    pub fn format_shm_name(self: &mut Self, s: &str) {
        static PREFIX: &'static str = "/dev/shm";
        self.buf.iter_mut().for_each(|x| *x = 0);
        let mut csr = Cursor::new(&mut self.buf[..]);
        csr.write(&PREFIX.as_bytes()[..]).unwrap();
        csr.write(&s.as_bytes()[..]).unwrap();
        csr.flush().unwrap();
    }

    pub fn to_str<'a>(self: &'a Self) -> &'a str {
        unsafe { std::str::from_utf8_unchecked(&self.buf) }
    }
}

const CHUNK_SIZE: usize = 2097152;

fn create_map_shared_memory<'a>(
    path_buf: &PathBufWrapper,
    nbytes: usize,
) -> Result<(&'a mut [u8], i32), i32> {
    const OPEN_FLAGS: i32 = libc::O_RDWR | libc::O_CREAT | libc::O_EXCL | libc::O_CLOEXEC;
    const MODE: u32 = libc::S_IRUSR | libc::S_IWUSR | libc::S_IRGRP | libc::S_IWGRP;
    const PROT: i32 = libc::PROT_READ | libc::PROT_WRITE;
    const MAP_FLAGS: i32 = libc::MAP_SHARED;

    let fd = open(path_buf.to_str(), OPEN_FLAGS, MODE)?;
    ftruncate(fd, nbytes.try_into().unwrap())?;

    let retval = mmap(
        std::ptr::null_mut(),
        nbytes.try_into().unwrap(),
        PROT,
        MAP_FLAGS,
        fd,
        0,
    )?;

    Ok((retval, fd))
}

pub struct MemBlock {
    data: *mut u8,
    nbytes: usize,
}

#[repr(C)]
#[derive(Debug)]
struct ChunkMeta {
    chunk_name: PathBufWrapper,
    chunk_fd: i32,
    chunk_size: usize,
    data_start: *mut u8,
    next_chunk: *mut ChunkMeta,
}

fn allocate_shared_chunk(path_buf: &PathBufWrapper, nbytes: usize) -> Result<*mut ChunkMeta, i32> {
    let (p, fd) = create_map_shared_memory(&path_buf, nbytes)?;
    let chunk_meta: *mut ChunkMeta = p.as_mut_ptr() as *mut ChunkMeta;

    unsafe {
        (*chunk_meta).chunk_name = path_buf.clone();
        (*chunk_meta).chunk_fd = fd;
        (*chunk_meta).chunk_size = nbytes;
        (*chunk_meta).data_start = chunk_meta.add(std::mem::size_of::<ChunkMeta>()) as *mut u8;
        (*chunk_meta).next_chunk = std::ptr::null_mut();

        println!("{:?}", *chunk_meta);
    }

    Ok(chunk_meta)
}

fn deallocate_shared_chunk(chunk_meta: *const ChunkMeta) -> Result<(), i32> {
    let path_buf = unsafe { (*chunk_meta).chunk_name.clone() };

    munmap(unsafe {
        std::slice::from_raw_parts_mut(chunk_meta as *mut u8, (*chunk_meta).chunk_size)
    })
    .unwrap();

    unlink(path_buf.to_str())?;

    Ok(())
}

impl ChunkMeta {
    pub fn new(path_buf: PathBufWrapper) -> Self {
        todo!()
    }
}

#[repr(C)]
struct BlockImpl {}

struct UniformFreelistAllocator {
    first_chunk: ChunkMeta,
    next_free_block: *mut BlockImpl,
    alloc_nbytes: usize,
    alloc_alignment: usize,
}

impl UniformFreelistAllocator {
    pub fn new(alloc_nbytes: usize, alloc_alignment: usize) -> Self {
        todo!()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_open() {
        let buf: [u8; PATH_BUF_NBYTES] = [0; PATH_BUF_NBYTES];
        let mut path_buf: PathBufWrapper = PathBufWrapper::new(buf);
        path_buf.format_shm_name("/foo");

        let (p, _) = create_map_shared_memory(&path_buf, 100).unwrap();
        p[0] = 65;
        p[1] = 66;
        p[2] = 67;

        println!("p={:?}", p);
        munmap(p).unwrap();

        unlink(path_buf.to_str()).unwrap();
    }

    #[test]
    fn test_chunk() {
        let buf: [u8; PATH_BUF_NBYTES] = [0; PATH_BUF_NBYTES];
        let mut path_buf: PathBufWrapper = PathBufWrapper::new(buf);
        path_buf.format_shm_name("/bar");

        let chunk = allocate_shared_chunk(&path_buf, 1000).unwrap();
        deallocate_shared_chunk(chunk).unwrap();
    }
}
