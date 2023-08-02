//! This module provides a simple interface to make system calls without relying on libc or nix,
//! which are not no-std. The goal of this module is to be as no-std as possible to be compatible
//! with Shadow's shim preload library.
//!
//! Public functions for system calls are named according to their standard names found in man(2).
//! The public function accept have parameters with types that are natural for Rust. Most of these
//! public functions are accompanied by a *_impl() function that has a signature which more closely
//! matches the x86_64 ABI:
//! <https://blog.rchapman.org/posts/Linux_System_Call_Table_for_x86_64/>

use core::result::Result;

use linux_syscall::*;

pub const S_IRUSR: u32 = 0o400;
pub const S_IWUSR: u32 = 0o200;
pub const S_IRGRP: u32 = S_IRUSR >> 3;
pub const S_IWGRP: u32 = S_IWUSR >> 3;

fn convert_i32_rv_to_rv_errno(rv: i32) -> Result<i32, i32> {
    if rv < -1 {
        Err(-rv)
    } else {
        Ok(rv)
    }
}

fn open_impl(filename: *const char, flags: i32, mode: u32) -> i32 {
    unsafe { syscall!(SYS_open, filename, flags, mode).as_u64_unchecked() as i32 }
}

/// # Safety
///
/// Assumes filename is a null-terminated ASCII string and that flags and mode are valid as defined
/// by the x86-64 system call interface.
pub unsafe fn open(filename: *const u8, flags: i32, mode: u32) -> Result<i32, i32> {
    convert_i32_rv_to_rv_errno(open_impl(
        unsafe { core::mem::transmute::<*const u8, *const char>(filename) },
        flags,
        mode,
    ))
}

pub fn close(fd: i32) -> Result<i32, i32> {
    unsafe { convert_i32_rv_to_rv_errno(syscall!(SYS_close, fd).as_u64_unchecked() as i32) }
}

fn unlink_impl(filename: *const char) -> i32 {
    unsafe { syscall!(SYS_unlink, filename).as_u64_unchecked() as i32 }
}

/// # Safety
///
/// Assumes filename is a null-terminated ASCII string.
pub unsafe fn unlink(filename: *const u8) -> Result<i32, i32> {
    convert_i32_rv_to_rv_errno(unlink_impl(unsafe {
        core::mem::transmute::<*const u8, *const char>(filename)
    }))
}

fn mmap_impl(addr: u64, len: u64, prot: u64, flags: u64, fd: u64, off: u64) -> u64 {
    unsafe { syscall!(SYS_mmap, addr, len, prot, flags, fd, off).as_u64_unchecked() }
}

/// # Safety
///
/// `addr` should be a pointer hinting at a mapping location, or null. The other arguments should
/// be valid as defined by the x86-64 system call interface.
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
    unsafe { syscall!(SYS_munmap, addr, nbytes).as_u64_unchecked() as i32 }
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
    convert_i32_rv_to_rv_errno(unsafe {
        syscall!(SYS_ftruncate, fd, nbytes).as_u64_unchecked() as i32
    })
}

fn clock_gettime_impl(clockid: i32, ts: *mut linux_api::time::timespec) -> i32 {
    unsafe { syscall!(SYS_clock_gettime, clockid, ts).as_u64_unchecked() as i32 }
}

pub fn clock_gettime() -> Result<linux_api::time::timespec, i32> {
    let mut ts = linux_api::time::timespec {
        tv_sec: 0,
        tv_nsec: 0,
    };
    let rv = clock_gettime_impl(linux_api::time::ClockId::CLOCK_MONOTONIC.into(), &mut ts);

    if rv == 0 {
        Ok(ts)
    } else {
        Err(-rv)
    }
}

pub fn getpid() -> i32 {
    unsafe { syscall!(SYS_getpid).as_u64_unchecked() as i32 }
}
