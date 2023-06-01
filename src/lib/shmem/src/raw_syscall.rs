//! This module provides a simple interface to make system calls without relying on libc or nix,
//! which are not no-std. The goal of this module is to be as no-std as possible to be compatible
//! with Shadow's shim preload library.
//!
//! Public functions for system calls are named according to their standard names found in man(2).
//! The public function accept have parameters with types that are natural for Rust. Most of these
//! public functions are accompanied by a *_impl() function that has a signature which more closely
//! matches the x86_64 ABI:
//! <https://blog.rchapman.org/posts/Linux_System_Call_Table_for_x86_64/>

use core::arch::asm;

fn convert_i32_rv_to_rv_errno(rv: i32) -> Result<i32, i32> {
    if rv < -1 {
        Err(-rv)
    } else {
        Ok(rv)
    }
}

fn x86_64_syscall(
    syscall_number: u64,
    arg1: u64,
    arg2: u64,
    arg3: u64,
    arg4: u64,
    arg5: u64,
    arg6: u64,
) -> u64 {
    let result: u64;

    unsafe {
        asm!(
            "syscall",
            inlateout("rax") syscall_number => result,
            inlateout("rdi") arg1 => _,
            inlateout("rsi") arg2 => _,
            inlateout("rdx") arg3 => _,
            inlateout("r10") arg4 => _,
            inlateout("r8") arg5 => _,
            inlateout("r9") arg6 => _,
            lateout("rcx") _,
            lateout("r11") _,
        );
    }

    result
}

fn open_impl(filename: *const char, flags: i32, mode: u32) -> i32 {
    const SYS_OPEN: u64 = 2;
    x86_64_syscall(
        SYS_OPEN,
        filename as u64,
        flags as u64,
        mode as u64,
        0,
        0,
        0,
    ) as i32
}

pub unsafe fn open(filename: *const u8, flags: i32, mode: u32) -> Result<i32, i32> {
    convert_i32_rv_to_rv_errno(open_impl(
        unsafe { core::mem::transmute::<*const u8, *const char>(filename) },
        flags,
        mode,
    ))
}

pub fn close(fd: i32) -> Result<i32, i32> {
    const SYS_CLOSE: u64 = 3;
    convert_i32_rv_to_rv_errno(x86_64_syscall(SYS_CLOSE, fd as u64, 0, 0, 0, 0, 0) as i32)
}

fn unlink_impl(filename: *const char) -> i32 {
    const SYS_UNLINK: u64 = 87;
    x86_64_syscall(SYS_UNLINK, filename as u64, 0, 0, 0, 0, 0) as i32
}

pub unsafe fn unlink(filename: *const u8) -> Result<i32, i32> {
    convert_i32_rv_to_rv_errno(unlink_impl(unsafe {
        core::mem::transmute::<*const u8, *const char>(filename)
    }))
}

fn mmap_impl(addr: u64, len: u64, prot: u64, flags: u64, fd: u64, off: u64) -> u64 {
    const SYS_MMAP: u64 = 9;
    x86_64_syscall(SYS_MMAP, addr, len, prot, flags, fd, off)
}

pub unsafe fn mmap<'a>(
    addr: *mut core::ffi::c_void,
    length: u64,
    prot: i32,
    flags: i32,
    fd: i32,
    offset: u64,
) -> Result<&'a mut [u8], i32> {
    let rv = mmap_impl(
        addr as u64,
        length,
        prot as u64,
        flags as u64,
        fd as u64,
        offset,
    );

    let rv = unsafe { core::mem::transmute::<u64, i64>(rv) };

    if rv < 0 {
        Err(-TryInto::<i32>::try_into(rv).unwrap())
    } else {
        Ok(unsafe { core::slice::from_raw_parts_mut(rv as *mut u8, length.try_into().unwrap()) })
    }
}

fn munmap_impl(addr: u64, nbytes: u64) -> i32 {
    const SYS_MUNMAP: u64 = 11;
    x86_64_syscall(SYS_MUNMAP, addr, nbytes, 0, 0, 0, 0) as i32
}

pub fn munmap(bytes: &mut [u8]) -> Result<(), (&mut [u8], i32)> {
    let nbytes = bytes.len();

    let rv = munmap_impl(bytes.as_mut_ptr() as u64, nbytes.try_into().unwrap());

    if rv == 0 {
        Ok(())
    } else {
        Err((bytes, -rv))
    }
}

pub fn ftruncate(fd: i32, nbytes: u64) -> Result<i32, i32> {
    const SYS_FTRUNCATE: u64 = 77;
    convert_i32_rv_to_rv_errno(x86_64_syscall(SYS_FTRUNCATE, fd as u64, nbytes, 0, 0, 0, 0) as i32)
}

fn clock_gettime_impl(clockid: libc::clockid_t, ts: *mut libc::timespec) -> i32 {
    const SYS_CLOCK_GETTIME: u64 = 228;
    x86_64_syscall(SYS_CLOCK_GETTIME, clockid as u64, ts as u64, 0, 0, 0, 0) as i32
}

pub fn clock_gettime() -> Result<libc::timespec, i32> {
    let mut ts = libc::timespec {
        tv_sec: 0,
        tv_nsec: 0,
    };
    let rv = clock_gettime_impl(libc::CLOCK_MONOTONIC, &mut ts);

    if rv == 0 {
        Ok(ts)
    } else {
        Err(-rv)
    }
}

pub fn getpid() -> i32 {
    const SYS_GETPID: u64 = 39;
    x86_64_syscall(SYS_GETPID, 0, 0, 0, 0, 0, 0) as i32
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::{Cursor, Write};

    #[test]
    fn test_clock() {
        let ts = clock_gettime().unwrap();
        println!("{:?} {:?}", ts.tv_sec, ts.tv_nsec);
        let ts = clock_gettime().unwrap();
        println!("{:?} {:?}", ts.tv_sec, ts.tv_nsec);
    }

    #[test]
    fn test_getpid() {
        let pid = getpid();
        println!("{:?}", pid);
    }

    #[test]
    fn test_all() {
        const OPEN_FLAGS: i32 = libc::O_RDWR | libc::O_CREAT | libc::O_EXCL | libc::O_CLOEXEC;
        const MODE: u32 = libc::S_IRUSR | libc::S_IWUSR | libc::S_IRGRP | libc::S_IWGRP;
        const PROT: i32 = libc::PROT_READ | libc::PROT_WRITE;
        const MAP_FLAGS: i32 = libc::MAP_SHARED;

        let path = "/dev/shm/foo";
        let nbytes = 100;

        let mut buf: [u8; 32] = [0; 32];
        let mut csr = Cursor::new(&mut buf[..]);
        csr.write(&path.as_bytes()[..]).unwrap();

        let fd = unsafe { open(buf.as_ptr(), OPEN_FLAGS, MODE).unwrap() };
        ftruncate(fd, nbytes.try_into().unwrap()).unwrap();

        let data = unsafe {
            mmap(
                std::ptr::null_mut(),
                nbytes.try_into().unwrap(),
                PROT,
                MAP_FLAGS,
                fd,
                0,
            )
            .unwrap()
        };

        munmap(data).unwrap();

        close(fd).unwrap();

        unsafe {
            unlink(buf.as_ptr()).unwrap();
        }
    }
}
