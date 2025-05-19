//! A simple interface to make system calls without relying on libc or nix, which are not no-std.
//!
//! The goal of this module is to be as no-std as possible to be compatible with Shadow's shim
//! preload library.
//!
//! Public functions for system calls are named according to their standard names found in man(2).
//! The public function accept have parameters with types that are natural for Rust.
//! For a list of syscalls, a good reference is:
//! <https://blog.rchapman.org/posts/Linux_System_Call_Table_for_x86_64/>

// TODO(rwails): Adjust this crate to use rustix; configure linux-api to produce S_* mode
// constants. We may also use already-existing definitions for some of the syscalls implemented
// here.

use linux_api::errno::Errno;
use linux_syscall::Result as LinuxSyscallResult;
use linux_syscall::syscall;

pub const S_IRUSR: u32 = 0o400;
pub const S_IWUSR: u32 = 0o200;
pub const S_IRGRP: u32 = S_IRUSR >> 3;
pub const S_IWGRP: u32 = S_IWUSR >> 3;

fn null_terminated(string: &[u8]) -> bool {
    string.contains(&0)
}

/// # Safety
///
/// Assumes filename is a null-terminated ASCII string and that flags and mode are valid as defined
/// by the x86-64 system call interface.
pub unsafe fn open(
    filename: &[u8],
    flags: linux_api::fcntl::OFlag,
    mode: u32,
) -> Result<i32, Errno> {
    assert!(null_terminated(filename));

    let rc = unsafe {
        syscall!(
            linux_syscall::SYS_open,
            filename.as_ptr(),
            flags.bits(),
            mode
        )
    };

    rc.check().map_err(Errno::from)?;

    Ok(rc.as_u64_unchecked() as i32)
}

pub fn close(fd: i32) -> Result<(), Errno> {
    unsafe { syscall!(linux_syscall::SYS_close, fd) }
        .check()
        .map_err(Errno::from)
}

/// # Safety
///
/// Assumes filename is a null-terminated ASCII string.
pub unsafe fn unlink(filename: &[u8]) -> Result<(), Errno> {
    assert!(null_terminated(filename));

    unsafe { syscall!(linux_syscall::SYS_unlink, filename.as_ptr()) }
        .check()
        .map_err(Errno::from)
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
) -> Result<&'a mut [u8], Errno> {
    let rc = unsafe {
        syscall!(
            linux_syscall::SYS_mmap,
            addr,
            length,
            prot.bits(),
            flags.bits(),
            fd,
            offset
        )
    };

    rc.check().map_err(Errno::from)?;

    let rc = rc.as_u64_unchecked();

    Ok(unsafe { core::slice::from_raw_parts_mut(rc as *mut u8, length.try_into().unwrap()) })
}

pub fn munmap(bytes: &mut [u8]) -> Result<(), Errno> {
    unsafe { syscall!(linux_syscall::SYS_munmap, bytes.as_mut_ptr(), bytes.len()) }
        .check()
        .map_err(Errno::from)
}

pub fn ftruncate(fd: i32, nbytes: u64) -> Result<(), Errno> {
    unsafe { syscall!(linux_syscall::SYS_ftruncate, fd, nbytes) }
        .check()
        .map_err(Errno::from)
}

pub fn clock_monotonic_gettime() -> Result<linux_api::time::timespec, Errno> {
    let cm: i32 = linux_api::time::ClockId::CLOCK_MONOTONIC.into();

    let mut ts = linux_api::time::timespec {
        tv_sec: 0,
        tv_nsec: 0,
    };

    let rc = unsafe { syscall!(linux_syscall::SYS_clock_gettime, cm, &mut ts) };

    rc.check().map_err(Errno::from)?;

    Ok(ts)
}

pub fn getpid() -> Result<i32, Errno> {
    let rc = unsafe { syscall!(linux_syscall::SYS_getpid) };

    rc.check().map_err(Errno::from)?;

    Ok(rc.as_u64_unchecked() as i32)
}

pub fn gettid() -> Result<i32, Errno> {
    let rc = unsafe { syscall!(linux_syscall::SYS_gettid) };

    rc.check().map_err(Errno::from)?;

    Ok(rc.as_u64_unchecked() as i32)
}

pub fn kill(pid: i32, signal: i32) -> Result<(), Errno> {
    unsafe { syscall!(linux_syscall::SYS_kill, pid, signal) }
        .check()
        .map_err(Errno::from)
}

pub fn tgkill(pid: i32, tid: i32, signal: i32) -> Result<(), Errno> {
    unsafe { syscall!(linux_syscall::SYS_tgkill, pid, tid, signal) }
        .check()
        .map_err(Errno::from)
}

pub fn fsync(fd: i32) -> Result<(), Errno> {
    unsafe { syscall!(linux_syscall::SYS_fsync, fd) }
        .check()
        .map_err(Errno::from)
}

/// # Safety
///
/// `count` should not exceed the number of valid bytes in `buf`.
pub unsafe fn write(fd: i32, buf: *const core::ffi::c_void, count: usize) -> Result<isize, Errno> {
    let rc = unsafe { syscall!(linux_syscall::SYS_write, fd, buf, count) };
    rc.check().map_err(Errno::from)?;
    Ok(rc.as_u64_unchecked() as isize)
}
