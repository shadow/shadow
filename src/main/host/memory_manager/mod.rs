//! Access and manage memory of a plugin process.
//!
//! The starting point for the public API is [`MemoryManager`].
//! [`MemoryManager`] can be used to:
//!
//! * Directly read or write process memory
//! * Obtain cursors to process memory implementing `std::io::Seek` and either
//!   `std::io::Read` or `std::io::Write` ([`MemoryReaderCursor`] and
//!   [`MemoryWriterCursor`])

use std::fmt::Debug;
use std::mem::MaybeUninit;
use std::os::raw::c_void;

use linux_api::errno::Errno;
use linux_api::mman::{MapFlags, ProtFlags};
use linux_api::posix_types::Pid;
use log::*;
use memory_copier::MemoryCopier;
use shadow_pod::Pod;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use super::context::ThreadContext;
use crate::host::syscall::types::{ForeignArrayPtr, SyscallError};

mod memory_copier;

/// An object implementing std::io::Read and std::io::Seek for
/// a range of plugin memory.
pub struct MemoryReaderCursor<'a> {
    memory_manager: &'a MemoryManager,
    ptr: ForeignArrayPtr<u8>,
    offset: usize,
}

impl std::io::Read for MemoryReaderCursor<'_> {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        let ptr = self.ptr.slice(self.offset..);
        let toread = std::cmp::min(buf.len(), ptr.len());
        if toread == 0 {
            return Ok(0);
        }
        self.memory_manager
            .copy_from_ptr(&mut buf[..toread], ptr.slice(..toread))?;
        self.offset += toread;
        Ok(toread)
    }
}

/// Shared implementation of seek for both MemoryReaderCursor and MemoryWriterCursor.
fn seek_helper(offset: &mut usize, len: usize, pos: std::io::SeekFrom) -> std::io::Result<u64> {
    use std::io::SeekFrom;
    let new_offset = match pos {
        SeekFrom::Current(x) => *offset as i64 + x,
        SeekFrom::End(x) => len as i64 + x,
        SeekFrom::Start(x) => x as i64,
    };
    // Seeking before the beginning is an error (but seeking to or past the
    // end isn't).
    if new_offset < 0 {
        return Err(Errno::EFAULT.into());
    }
    *offset = new_offset as usize;
    Ok(new_offset as u64)
}

impl std::io::Seek for MemoryReaderCursor<'_> {
    fn seek(&mut self, pos: std::io::SeekFrom) -> std::io::Result<u64> {
        seek_helper(&mut self.offset, self.ptr.len(), pos)
    }
}

/// An object implementing std::io::Write and std::io::Seek for
/// a range of plugin memory.
pub struct MemoryWriterCursor<'a> {
    memory_manager: &'a mut MemoryManager,
    ptr: ForeignArrayPtr<u8>,
    offset: usize,
}

impl std::io::Write for MemoryWriterCursor<'_> {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        let ptr = self.ptr.slice(self.offset..);
        let towrite = std::cmp::min(buf.len(), ptr.len());
        if towrite == 0 {
            return Ok(0);
        }
        self.memory_manager
            .copy_to_ptr(ptr.slice(..towrite), &buf[..towrite])?;
        self.offset += towrite;
        Ok(towrite)
    }

    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

impl std::io::Seek for MemoryWriterCursor<'_> {
    fn seek(&mut self, pos: std::io::SeekFrom) -> std::io::Result<u64> {
        seek_helper(&mut self.offset, self.ptr.len(), pos)
    }
}

fn page_size() -> usize {
    nix::unistd::sysconf(nix::unistd::SysconfVar::PAGE_SIZE)
        .unwrap()
        .unwrap()
        .try_into()
        .unwrap()
}

/// Provides accessors for reading and writing another process's memory.
///
/// When in use, any operation that touches that process's memory must go
/// through the MemoryManager to ensure soundness. See MemoryManager::new.
//
// The MemoryManager is the Rust representation of a plugin process's address
// space.  For every access it tries to go through the more-efficient
// MemoryMapper helper first, and falls back to the MemoryCopier if it hasn't
// been initialized yet, or the access isn't contained entirely within a region
// that's been remapped.
#[derive(Debug)]
pub struct MemoryManager {
    // Memory accessor that works by copying data to and from process memory.
    // This is the most robust mechanism, but requires some syscalls, and in
    // some cases extra copies of the referenced data.
    memory_copier: MemoryCopier,

    // Native pid of the plugin process.
    pid: Pid,
}

impl MemoryManager {
    pub fn new(pid: Pid) -> Self {
        Self {
            pid,
            memory_copier: MemoryCopier::new(pid),
        }
    }

    /// Copy data from the beginning of the given
    /// pointer to the last address in the pointer that's accessible. Useful for
    /// accessing string data of unknown size.
    pub fn read_prefix<T: Pod>(&self, ptr: ForeignArrayPtr<T>) -> Result<Vec<T>, Errno> {
        let mut values = Box::<[T]>::new_uninit_slice(ptr.len());
        let ptr = ptr.cast::<MaybeUninit<T>>().unwrap();
        let copied = self.memory_copier.copy_prefix_from_ptr(&mut values, ptr)?;

        // Drop the still uninitd values. Is there a way to directly resize the boxed slice
        // without having to go through Vec?
        let mut values = Vec::from(values);
        values.resize(copied, MaybeUninit::uninit());
        let values = values.into_boxed_slice();

        // SAFETY: should now contain only initialized values.
        let values = unsafe { values.assume_init() };

        Ok(Vec::from(values))
    }

    /// Creates a std::io::Read accessor for the specified plugin memory. Useful
    /// for handing off the ability to read process memory to non-Shadow APIs,
    /// without copying it to local memory first.
    pub fn reader(&self, ptr: ForeignArrayPtr<u8>) -> MemoryReaderCursor<'_> {
        MemoryReaderCursor {
            memory_manager: self,
            ptr,
            offset: 0,
        }
    }

    /// Reads the memory into a local copy.
    ///
    /// Examples:
    ///
    /// ```no_run
    /// # use shadow_shim_helper_rs::syscall_types::ForeignPtr;
    /// # use shadow_rs::host::memory_manager::MemoryManager;
    /// # use linux_api::errno::Errno;
    /// # fn foo() -> Result<(), Errno> {
    /// # let memory_manager: MemoryManager = todo!();
    /// let ptr: ForeignPtr<u32> = todo!();
    /// let val: u32 = memory_manager.read(ptr)?;
    /// # Ok(())
    /// # }
    /// ```
    ///
    /// ```no_run
    /// # use shadow_shim_helper_rs::syscall_types::ForeignPtr;
    /// # use shadow_rs::host::memory_manager::MemoryManager;
    /// # use linux_api::errno::Errno;
    /// # fn foo() -> Result<(), Errno> {
    /// # let memory_manager: MemoryManager = todo!();
    /// let ptr: ForeignPtr<[u32; 2]> = todo!();
    /// let val: [u32; 2] = memory_manager.read(ptr)?;
    /// # Ok(())
    /// # }
    /// ```
    pub fn read<T: Pod>(&self, ptr: ForeignPtr<T>) -> Result<T, Errno> {
        let ptr = ptr.cast::<MaybeUninit<T>>();
        let mut res: MaybeUninit<T> = MaybeUninit::uninit();

        self.copy_from_ptr(std::slice::from_mut(&mut res), ForeignArrayPtr::new(ptr, 1))?;
        // SAFETY: any values are valid for Pod
        Ok(unsafe { res.assume_init() })
    }

    /// Read an array of `T` into a `Vec`. If you already have storage allocated,
    /// consider `copy_from_ptr` instead.
    pub fn read_vec<T: Pod>(&self, ptr: ForeignArrayPtr<T>) -> Result<Vec<T>, Errno> {
        let mut values = Box::<[T]>::new_uninit_slice(ptr.len());
        let ptr = ptr.cast::<MaybeUninit<T>>().unwrap();
        self.copy_from_ptr(&mut values, ptr)?;
        // SAFETY: we've initialized the data.
        let value = unsafe { values.assume_init() };
        Ok(Vec::from(value))
    }

    /// Writes a local value `val` into the memory at `ptr`.
    ///
    /// ```no_run
    /// # use shadow_shim_helper_rs::syscall_types::ForeignPtr;
    /// # use shadow_rs::host::memory_manager::MemoryManager;
    /// # use linux_api::errno::Errno;
    /// # fn foo() -> Result<(), Errno> {
    /// # let mut memory_manager: MemoryManager = todo!();
    /// let ptr: ForeignPtr<u32> = todo!();
    /// let val = 5;
    /// memory_manager.write(ptr, &val)?;
    /// # Ok(())
    /// # }
    /// ```
    // take a `&T` rather than a `T` since all `Pod` types are `Copy`, and it's probably more
    // performant to accept a reference than copying the type here if `T` is large
    pub fn write<T: Pod>(&mut self, ptr: ForeignPtr<T>, val: &T) -> Result<(), Errno> {
        self.copy_to_ptr(ForeignArrayPtr::new(ptr, 1), std::slice::from_ref(val))
    }

    /// Similar to `read`, but saves a copy if you already have a `dst` to copy the data into.
    pub fn copy_from_ptr<T: Pod>(
        &self,
        dst: &mut [T],
        src: ForeignArrayPtr<T>,
    ) -> Result<(), Errno> {
        self.memory_copier.copy_from_ptr(dst, src)
    }

    // Copies memory from the beginning of the given pointer to the last address
    // in the pointer that's accessible. Not exposed as a public interface
    // because this is generally only useful for strings, and
    // `copy_str_from_ptr` provides a more convenient interface.
    fn copy_prefix_from_ptr<T: Pod>(
        &self,
        buf: &mut [T],
        ptr: ForeignArrayPtr<T>,
    ) -> Result<usize, Errno> {
        self.memory_copier.copy_prefix_from_ptr(buf, ptr)
    }

    /// Copies a NULL-terminated string starting from the beginning of `src` and
    /// contained completely within `src`. Still works if some of `src` isn't
    /// readable, as long as a NULL-terminated-string is contained in the
    /// readable prefix.
    ///
    /// If holding a reference to the MemoryManager for the lifetime of the
    /// string is acceptable, use `memory_ref_prefix` and
    /// `ProcessMemoryRef::get_str` to potentially avoid an extra copy.
    pub fn copy_str_from_ptr<'a>(
        &self,
        dst: &'a mut [u8],
        src: ForeignArrayPtr<u8>,
    ) -> Result<&'a std::ffi::CStr, Errno> {
        let nread = self.copy_prefix_from_ptr(dst, src)?;
        let dst = &dst[..nread];
        std::ffi::CStr::from_bytes_until_nul(dst).or(Err(Errno::ENAMETOOLONG))
    }

    /// Writes the memory from a local copy. If `src` doesn't already exist,
    /// using `memory_ref_mut_uninit` and initializing the data in that
    /// reference saves a copy.
    pub fn copy_to_ptr<T: Pod>(&mut self, dst: ForeignArrayPtr<T>, src: &[T]) -> Result<(), Errno> {
        self.memory_copier.copy_to_ptr(dst, src)
    }

    /// Which process's address space this MemoryManager manages.
    pub fn pid(&self) -> Pid {
        self.pid
    }

    /// Create a write accessor for the specified plugin memory.
    pub fn writer(&mut self, ptr: ForeignArrayPtr<u8>) -> MemoryWriterCursor<'_> {
        MemoryWriterCursor {
            memory_manager: self,
            ptr,
            offset: 0,
        }
    }

    pub fn handle_brk(
        &mut self,
        _ctx: &ThreadContext,
        _ptr: ForeignPtr<u8>,
    ) -> Result<ForeignPtr<u8>, SyscallError> {
        Err(SyscallError::Native)
    }

    pub fn do_mmap(
        &mut self,
        ctx: &ThreadContext,
        addr: ForeignPtr<u8>,
        length: usize,
        prot: ProtFlags,
        flags: MapFlags,
        fd: i32,
        offset: i64,
    ) -> Result<ForeignPtr<u8>, Errno> {
        let addr = {
            let (ctx, thread) = ctx.split_thread();
            thread.native_mmap(&ctx, addr, length, prot, flags, fd, offset)?
        };
        Ok(addr)
    }

    pub fn handle_munmap(
        &mut self,
        _ctx: &ThreadContext,
        _addr: ForeignPtr<u8>,
        _length: usize,
    ) -> Result<(), SyscallError> {
        // We don't need to know the result, and it's more efficient to let
        // the original syscall complete than to do it ourselves.
        Err(SyscallError::Native)
    }

    fn do_munmap(
        &mut self,
        ctx: &ThreadContext,
        addr: ForeignPtr<u8>,
        length: usize,
    ) -> Result<(), Errno> {
        let (ctx, thread) = ctx.split_thread();
        thread.native_munmap(&ctx, addr, length)?;
        Ok(())
    }

    pub fn handle_mremap(
        &mut self,
        _ctx: &ThreadContext,
        _old_address: ForeignPtr<u8>,
        _old_size: usize,
        _new_size: usize,
        _flags: i32,
        _new_address: ForeignPtr<u8>,
    ) -> Result<ForeignPtr<u8>, SyscallError> {
        Err(SyscallError::Native)
    }

    pub fn handle_mprotect(
        &mut self,
        _ctx: &ThreadContext,
        _addr: ForeignPtr<u8>,
        _size: usize,
        _prot: ProtFlags,
    ) -> Result<(), SyscallError> {
        Err(SyscallError::Native)
    }
}

/// Memory allocated by Shadow, in a remote address space.
pub struct AllocdMem<T>
where
    T: Pod,
{
    ptr: ForeignArrayPtr<T>,
    // Whether the pointer has been freed.
    freed: bool,
}

impl<T> AllocdMem<T>
where
    T: Pod,
{
    /// Allocate memory in the current active process.
    /// Must be freed explicitly via `free`.
    pub fn new(ctx: &ThreadContext, len: usize) -> Self {
        let prot = ProtFlags::PROT_READ | ProtFlags::PROT_WRITE;

        // Allocate through the MemoryManager, so that it knows about this region.
        let ptr = ctx
            .process
            .memory_borrow_mut()
            .do_mmap(
                ctx,
                ForeignPtr::null(),
                len * std::mem::size_of::<T>(),
                prot,
                MapFlags::MAP_ANONYMOUS | MapFlags::MAP_PRIVATE,
                -1,
                0,
            )
            .unwrap();

        Self {
            ptr: ForeignArrayPtr::new(ptr.cast::<T>(), len),
            freed: false,
        }
    }

    /// Pointer to the allocated memory.
    pub fn ptr(&self) -> ForeignArrayPtr<T> {
        self.ptr
    }

    pub fn free(mut self, ctx: &ThreadContext) {
        ctx.process
            .memory_borrow_mut()
            .do_munmap(
                ctx,
                self.ptr.ptr().cast::<u8>(),
                self.ptr.len() * std::mem::size_of::<T>(),
            )
            .unwrap();
        self.freed = true;
    }
}

impl<T> Drop for AllocdMem<T>
where
    T: Pod,
{
    fn drop(&mut self) {
        // We need the thread context to free the memory. Nothing to do now but
        // complain.
        if !self.freed {
            warn!("Memory leak: failed to free {:?}", self.ptr)
        }
        debug_assert!(self.freed);
    }
}

mod export {
    use shadow_shim_helper_rs::notnull::*;
    use shadow_shim_helper_rs::syscall_types::UntypedForeignPtr;

    use super::*;

    /// Copy `n` bytes from `src` to `dst`. Returns 0 on success or -EFAULT if any of the specified
    /// range couldn't be accessed. Always succeeds with n==0.
    #[unsafe(no_mangle)]
    pub extern "C-unwind" fn memorymanager_readPtr(
        mem: *const MemoryManager,
        dst: *mut c_void,
        src: UntypedForeignPtr,
        n: usize,
    ) -> i32 {
        let mem = unsafe { mem.as_ref() }.unwrap();
        let src = ForeignArrayPtr::new(src.cast::<u8>(), n);
        let dst = unsafe { std::slice::from_raw_parts_mut(notnull_mut_debug(dst) as *mut u8, n) };

        match mem.copy_from_ptr(dst, src) {
            Ok(_) => 0,
            Err(e) => {
                trace!("Couldn't read {src:?} into {dst:?}: {e:?}");
                e.to_negated_i32()
            }
        }
    }

    /// Copy `n` bytes from `src` to `dst`. Returns 0 on success or -EFAULT if any of the specified
    /// range couldn't be accessed. The write is flushed immediately.
    #[unsafe(no_mangle)]
    pub unsafe extern "C-unwind" fn memorymanager_writePtr(
        mem: *mut MemoryManager,
        dst: UntypedForeignPtr,
        src: *const c_void,
        n: usize,
    ) -> i32 {
        let mem = unsafe { mem.as_mut() }.unwrap();
        let dst = ForeignArrayPtr::new(dst.cast::<u8>(), n);
        let src = unsafe { std::slice::from_raw_parts(notnull_debug(src) as *const u8, n) };
        match mem.copy_to_ptr(dst, src) {
            Ok(_) => 0,
            Err(e) => {
                trace!("Couldn't write {src:?} into {dst:?}: {e:?}");
                e.to_negated_i32()
            }
        }
    }
}
