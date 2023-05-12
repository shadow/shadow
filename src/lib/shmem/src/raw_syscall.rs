//! This module provides a simple interface to make system calls without relying on libc or nix,
//! which are not no-std. The goal of this module is to be as no-std as possible to be compatible
//! with Shadow's shim preload library.
//!
//! Public functions for system calls are named according to their standard names found in man(2).
//! The public function accept have parameters with types that are natural for Rust. Most of these
//! public functions are accompanied by a *_impl() function that has a signature which more closely
//! matches the x86_64 ABI:
//! <https://blog.rchapman.org/posts/Linux_System_Call_Table_for_x86_64/>

use std::arch::asm;

unsafe fn interpret_str_to_char_ptr(s: &str) -> *const char {
    unsafe { core::mem::transmute::<*const u8, *const char>(s.as_bytes().as_ptr()) }
}

fn interpret_i32_as_u64(x: i32) -> u64 {
    u64::from_ne_bytes((x as i64).to_ne_bytes())
}

fn interpret_u64_as_i32(x: u64) -> i32 {
    let x_bytes = x.to_ne_bytes();
    if cfg!(target_endian = "big") {
        i32::from_ne_bytes(x_bytes[4..8].try_into().unwrap())
    } else {
        i32::from_ne_bytes(x_bytes[0..4].try_into().unwrap())
    }
}

fn convert_i32_rv_to_rv_errno(rv: i32) -> Result<i32, i32> {
    if rv < -1 {
        Err(-1 * rv)
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

    let filename = unsafe { core::mem::transmute::<*const char, u64>(filename) };

    interpret_u64_as_i32(x86_64_syscall(
        SYS_OPEN,
        filename,
        interpret_i32_as_u64(flags),
        mode as u64,
        0,
        0,
        0,
    ))
}

pub fn open(filename: &str, flags: i32, mode: u32) -> Result<i32, i32> {
    convert_i32_rv_to_rv_errno(open_impl(
        unsafe { interpret_str_to_char_ptr(filename) },
        flags,
        mode,
    ))
}

pub fn close(fd: i32) -> Result<i32, i32> {
    const SYS_CLOSE: u64 = 3;
    convert_i32_rv_to_rv_errno(interpret_u64_as_i32(x86_64_syscall(
        SYS_CLOSE,
        interpret_i32_as_u64(fd),
        0,
        0,
        0,
        0,
        0,
    )))
}

fn unlink_impl(filename: *const char) -> i32 {
    const SYS_UNLINK: u64 = 87;

    let filename = unsafe { core::mem::transmute::<*const char, u64>(filename) };

    interpret_u64_as_i32(x86_64_syscall(SYS_UNLINK, filename, 0, 0, 0, 0, 0))
}

pub fn unlink(filename: &str) -> Result<i32, i32> {
    convert_i32_rv_to_rv_errno(unlink_impl(unsafe { interpret_str_to_char_ptr(filename) }))
}

fn mmap_impl(addr: u64, len: u64, prot: u64, flags: u64, fd: u64, off: u64) -> u64 {
    const SYS_MMAP: u64 = 9;
    x86_64_syscall(SYS_MMAP, addr, len, prot, flags, fd, off)
}

pub fn mmap<'a>(
    addr: *mut std::ffi::c_void,
    length: u64,
    prot: i32,
    flags: i32,
    fd: i32,
    offset: u64,
) -> Result<&'a mut [u8], i32> {
    let rv = mmap_impl(
        unsafe { core::mem::transmute::<*mut std::ffi::c_void, u64>(addr) },
        length,
        interpret_i32_as_u64(prot),
        interpret_i32_as_u64(flags),
        interpret_i32_as_u64(fd),
        offset,
    );

    let rv = unsafe { core::mem::transmute::<u64, i64>(rv) };

    if rv < 0 {
        Err(TryInto::<i32>::try_into(rv).unwrap() * -1)
    } else {
        let p: *mut u8 = unsafe { core::mem::transmute::<i64, *mut u8>(rv) };
        Ok(unsafe { core::slice::from_raw_parts_mut(p, length.try_into().unwrap()) })
    }
}

fn munmap_impl(addr: u64, nbytes: u64) -> i32 {
    const SYS_MUNMAP: u64 = 11;
    interpret_u64_as_i32(x86_64_syscall(SYS_MUNMAP, addr, nbytes, 0, 0, 0, 0))
}

pub fn munmap(bytes: &mut [u8]) -> Result<(), (&mut [u8], i32)> {
    let p = unsafe { core::mem::transmute::<*mut u8, u64>(bytes.as_mut_ptr()) };
    let nbytes = bytes.len();

    let rv = munmap_impl(p, nbytes.try_into().unwrap());

    if rv == 0 {
        Ok(())
    } else {
        Err((bytes, -1 * rv))
    }
}

pub fn ftruncate(fd: i32, nbytes: u64) -> Result<i32, i32> {
    const SYS_FTRUNCATE: u64 = 77;
    convert_i32_rv_to_rv_errno(interpret_u64_as_i32(x86_64_syscall(
        SYS_FTRUNCATE,
        interpret_i32_as_u64(fd),
        nbytes,
        0,
        0,
        0,
        0,
    )))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::{Cursor, Write};

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

        let fd = open(
            unsafe { std::str::from_utf8_unchecked(&buf) },
            OPEN_FLAGS,
            MODE,
        )
        .unwrap();
        ftruncate(fd, nbytes.try_into().unwrap()).unwrap();

        let data = mmap(
            std::ptr::null_mut(),
            nbytes.try_into().unwrap(),
            PROT,
            MAP_FLAGS,
            fd,
            0,
        )
        .unwrap();

        munmap(data).unwrap();

        close(fd).unwrap();

        unlink(unsafe { std::str::from_utf8_unchecked(&buf) }).unwrap();
    }
}
