//! This module provides a simple interface to make system calls without relying on libc or nix,
//! which are not no-std. The goal of this module is to be as no-std as possible to be compatible
//! with Shadow's shim preload library.
//!
//! Public functions for system calls are named according to their standard names found in man(2).
//! The public function accept have parameters with types that are natural for Rust.
//! For a list of syscalls, a good reference is:
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

fn null_terminated(string: &[u8]) -> bool {
    string.iter().any(|x| *x == 0)
}

/// # Safety
///
/// Assumes filename is a null-terminated ASCII string and that flags and mode are valid as defined
/// by the x86-64 system call interface.
pub unsafe fn open(filename: &[u8], flags: linux_api::fcntl::OFlag, mode: u32) -> Result<i32, i32> {
    assert!(null_terminated(filename));

    convert_i32_rv_to_rv_errno(unsafe {
        syscall!(SYS_open, filename.as_ptr(), flags.bits(), mode).as_u64_unchecked() as i32
    })
}

pub fn close(fd: i32) -> Result<i32, i32> {
    unsafe { convert_i32_rv_to_rv_errno(syscall!(SYS_close, fd).as_u64_unchecked() as i32) }
}

/// # Safety
///
/// Assumes filename is a null-terminated ASCII string.
pub unsafe fn unlink(filename: &[u8]) -> Result<i32, i32> {
    convert_i32_rv_to_rv_errno(unsafe {
        syscall!(SYS_unlink, filename.as_ptr()).as_u64_unchecked() as i32
    })
}

/// # Safety
///
/// `addr` should be a pointer hinting at a mapping location, or null. The other arguments should
/// be valid as defined by the x86-64 system call interface.
pub unsafe fn mmap<'a>(
    addr: *mut core::ffi::c_void,
    length: u64,
    prot: linux_api::mman::ProtFlags,
    flags: linux_api::mman::MapFlags,
    fd: i32,
    offset: u64,
) -> Result<&'a mut [u8], i32> {
    let rv = unsafe {
        syscall!(
            SYS_mmap,
            addr,
            length,
            prot.bits(),
            flags.bits(),
            fd,
            offset
        )
        .as_u64_unchecked()
    } as i64;

    if rv < 0 {
        Err(-TryInto::<i32>::try_into(rv).unwrap())
    } else {
        Ok(unsafe { core::slice::from_raw_parts_mut(rv as *mut u8, length.try_into().unwrap()) })
    }
}

pub fn munmap(bytes: &mut [u8]) -> Result<(), i32> {
    let rv =
        unsafe { syscall!(SYS_munmap, bytes.as_mut_ptr(), bytes.len()).as_u64_unchecked() } as i32;

    if rv == 0 {
        Ok(())
    } else {
        Err(-rv)
    }
}

pub fn ftruncate(fd: i32, nbytes: u64) -> Result<i32, i32> {
    convert_i32_rv_to_rv_errno(unsafe {
        syscall!(SYS_ftruncate, fd, nbytes).as_u64_unchecked() as i32
    })
}

pub fn clock_monotonic_gettime() -> Result<linux_api::time::timespec, i32> {
    let cm: i32 = linux_api::time::ClockId::CLOCK_MONOTONIC.into();

    let mut ts = linux_api::time::timespec {
        tv_sec: 0,
        tv_nsec: 0,
    };

    let rv = unsafe { syscall!(SYS_clock_gettime, cm, &mut ts).as_u64_unchecked() } as i32;

    if rv == 0 {
        Ok(ts)
    } else {
        Err(-rv)
    }
}

pub fn getpid() -> i32 {
    unsafe { syscall!(SYS_getpid).as_u64_unchecked() as i32 }
}

pub fn kill(pid: i32, signal: i32) -> Result<i32, i32> {
    convert_i32_rv_to_rv_errno(unsafe { syscall!(SYS_kill, pid, signal).as_u64_unchecked() as i32 })
}

pub fn fsync(fd: i32) -> Result<i32, i32> {
    convert_i32_rv_to_rv_errno(unsafe { syscall!(SYS_fsync, fd).as_u64_unchecked() as i32 })
}

/// # Safety
///
/// `count` should not exceed the number of valid bytes in `buf`.
pub unsafe fn write(fd: i32, buf: *const core::ffi::c_void, count: usize) -> isize {
    unsafe { syscall!(SYS_write, fd, buf, count).as_u64_unchecked() as isize }
}
