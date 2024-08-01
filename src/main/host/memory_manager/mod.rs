//! Access and manage memory of a plugin process.
//!
//! The starting point for the public API is [`MemoryManager`].
//! [`MemoryManager`] can be used to:
//!
//! * Directly read or write process memory
//! * Obtain smart pointers ([`ProcessMemoryRef`] and [`ProcessMemoryRefMut`])
//!   to process memory
//! * Obtain cursors to process memory implementing `std::io::Seek` and either
//!   `std::io::Read` or `std::io::Write` ([`MemoryReaderCursor`] and
//!   [`MemoryWriterCursor`])
//!
//! For the [`MemoryManager`] to maintain a consistent view of the process's address space,
//! and for it to be able to enforce Rust's safety requirements for references and sharing,
//! all access to process memory must go through it. This includes servicing syscalls that
//! modify the process address space (such as `mmap`).

use std::fmt::Debug;
use std::mem::MaybeUninit;
use std::ops::{Deref, DerefMut};
use std::os::raw::c_void;

use linux_api::errno::Errno;
use linux_api::mman::{MapFlags, ProtFlags};
use linux_api::posix_types::Pid;
use log::*;
use memory_copier::MemoryCopier;
use memory_mapper::MemoryMapper;
use shadow_pod::Pod;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use super::context::ThreadContext;
use crate::host::syscall::types::{ForeignArrayPtr, SyscallError};

mod memory_copier;
mod memory_mapper;

/// An object implementing std::io::Read and std::io::Seek for
/// a range of plugin memory.
pub struct MemoryReaderCursor<'a> {
    memory_manager: &'a MemoryManager,
    ptr: ForeignArrayPtr<u8>,
    offset: usize,
}

impl<'a> std::io::Read for MemoryReaderCursor<'a> {
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

impl<'a> std::io::Seek for MemoryReaderCursor<'a> {
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

impl<'a> std::io::Write for MemoryWriterCursor<'a> {
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

impl<'a> std::io::Seek for MemoryWriterCursor<'a> {
    fn seek(&mut self, pos: std::io::SeekFrom) -> std::io::Result<u64> {
        seek_helper(&mut self.offset, self.ptr.len(), pos)
    }
}

enum CopiedOrMapped<'a, T: Debug + Pod> {
    // Data copied from plugin memory.
    Copied(Vec<T>),
    // Data memory-mapped from plugin memory.
    Mapped(&'a [T]),
}

/// An immutable reference to a slice of plugin memory. Implements `Deref<[T]>`,
/// allowing, e.g.:
///
/// ```ignore
/// let tpp = ForeignArrayPtr::<u32>::new(ptr, 10);
/// let pmr = memory_manager.memory_ref(ptr);
/// assert_eq!(pmr.len(), 10);
/// let x = pmr[5];
/// ```
pub struct ProcessMemoryRef<'a, T: Debug + Pod>(CopiedOrMapped<'a, T>);

impl<'a, T: Debug + Pod> ProcessMemoryRef<'a, T> {
    fn new_copied(v: Vec<T>) -> Self {
        Self(CopiedOrMapped::Copied(v))
    }

    fn new_mapped(s: &'a [T]) -> Self {
        Self(CopiedOrMapped::Mapped(s))
    }
}

impl<'a> ProcessMemoryRef<'a, u8> {
    /// Get a `cstr` from the reference. Fails with `ENAMETOOLONG` if there is no
    /// NULL byte.
    pub fn get_cstr(&self) -> Result<&std::ffi::CStr, Errno> {
        std::ffi::CStr::from_bytes_until_nul(self).or(Err(Errno::ENAMETOOLONG))
    }
}

impl<'a, T> Deref for ProcessMemoryRef<'a, T>
where
    T: Debug + Pod,
{
    type Target = [T];

    fn deref(&self) -> &Self::Target {
        match &self.0 {
            CopiedOrMapped::Copied(v) => v,
            CopiedOrMapped::Mapped(s) => s,
        }
    }
}

#[derive(Debug)]
enum CopiedOrMappedMut<'a, T: Debug + Pod> {
    // Data copied from process memory, to be written back.
    Copied(MemoryCopier, ForeignArrayPtr<T>, Vec<T>),
    // Memory-mapped process memory.
    Mapped(&'a mut [T]),
}

/// A mutable reference to a slice of plugin memory. Implements `DerefMut<[T]>`,
/// allowing, e.g.:
///
/// ```ignore
/// let tpp = ForeignArrayPtr::<u32>::new(ptr, 10);
/// let pmr = memory_manager.memory_ref_mut(ptr);
/// assert_eq!(pmr.len(), 10);
/// pmr[5] = 100;
/// ```
///
/// The object must be disposed of by calling `flush` or `noflush`.  Dropping
/// the object without doing so will result in a panic.
#[derive(Debug)]
pub struct ProcessMemoryRefMut<'a, T: Debug + Pod> {
    copied_or_mapped: CopiedOrMappedMut<'a, T>,
    dirty: bool,
}

impl<'a, T: Debug + Pod> ProcessMemoryRefMut<'a, T> {
    fn new_copied(copier: MemoryCopier, ptr: ForeignArrayPtr<T>, v: Vec<T>) -> Self {
        Self {
            copied_or_mapped: CopiedOrMappedMut::Copied(copier, ptr, v),
            dirty: true,
        }
    }

    fn new_mapped(s: &'a mut [T]) -> Self {
        Self {
            copied_or_mapped: CopiedOrMappedMut::Mapped(s),
            dirty: true,
        }
    }

    /// Call to dispose of the reference while writing back the contents
    /// to process memory (if it hasn't already effectively been done).
    ///
    /// WARNING: if this reference was obtained via
    /// `Memorymanager::memory_ref_mut_uninit`, and the contents haven't been
    /// overwritten, call `noflush` instead to avoid flushing back the
    /// unininitialized contents.
    pub fn flush(mut self) -> Result<(), Errno> {
        // Whether the flush succeeds or not, the buffer is no longer considered
        // dirty; the fact that it failed will be captured in an error result.
        self.dirty = false;

        match &self.copied_or_mapped {
            CopiedOrMappedMut::Copied(copier, ptr, v) => {
                trace!(
                    "Flushing {} bytes to {:x}",
                    ptr.len() * std::mem::size_of::<T>(),
                    usize::from(ptr.ptr())
                );
                unsafe { copier.copy_to_ptr(*ptr, v)? };
            }
            CopiedOrMappedMut::Mapped(_) => (),
        };
        Ok(())
    }

    /// Disposes of the reference *without* writing back the contents.
    /// This should be used instead of `flush` if and only if the contents
    /// of this reference hasn't been overwritten.
    pub fn noflush(mut self) {
        self.dirty = false;
    }
}

impl<'a, T: Debug + Pod> Drop for ProcessMemoryRefMut<'a, T> {
    fn drop(&mut self) {
        // Dropping without flushing is a bug.
        assert!(!self.dirty);
    }
}

impl<'a, T> Deref for ProcessMemoryRefMut<'a, T>
where
    T: Debug + Pod,
{
    type Target = [T];

    fn deref(&self) -> &Self::Target {
        match &self.copied_or_mapped {
            CopiedOrMappedMut::Copied(_, _, v) => v,
            CopiedOrMappedMut::Mapped(s) => s,
        }
    }
}

impl<'a, T> DerefMut for ProcessMemoryRefMut<'a, T>
where
    T: Debug + Pod,
{
    fn deref_mut(&mut self) -> &mut Self::Target {
        match &mut self.copied_or_mapped {
            CopiedOrMappedMut::Copied(_, _, v) => v,
            CopiedOrMappedMut::Mapped(s) => s,
        }
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

    // Memory accessor that works by remapping memory of the target process into
    // the calling process's address space. Individual accesses are fast, but
    // this accessor isn't available at program start, and doesn't support all
    // accesses.
    memory_mapper: Option<MemoryMapper>,

    // Native pid of the plugin process.
    pid: Pid,
}

impl MemoryManager {
    /// # Safety
    ///
    /// `pid`'s memory must not be modified without holding an exclusive
    /// (mutable) reference to the returned MemoryManager. In Shadow we ensure
    /// this by:
    /// * Creating only one MemoryManager for a given process.
    /// * TODO: Not allowing any thread in the process to execute without
    ///   holding a mutable reference to its MemoryManager.
    /// * Not directly modifying process memory via other techniques.
    /// * Assuming (!) nothing else concurrently modifies memory of the given process.
    ///   (e.g. that some other process doesn't start calling `process_vm_writev`
    ///   to write to the process's memory).
    /// * TODO: Validating that the process doesn't have any shared memory mappings
    ///   other than with Shadow or other simulated processes under Shadow's control.
    pub unsafe fn new(pid: Pid) -> Self {
        Self {
            pid,
            memory_copier: MemoryCopier::new(pid),
            memory_mapper: None,
        }
    }

    // Internal helper for getting a reference to memory via the
    // `memory_mapper`.  Calling methods should fall back to the `memory_copier`
    // on failure.
    fn mapped_ref<T: Pod + Debug>(&self, ptr: ForeignArrayPtr<T>) -> Option<&[T]> {
        let mm = self.memory_mapper.as_ref()?;
        // SAFETY: No mutable refs to process memory exist by preconditions of
        // MemoryManager::new + we have a reference.
        unsafe { mm.get_ref(ptr) }
    }

    // Internal helper for getting a reference to memory via the
    // `memory_mapper`.  Calling methods should fall back to the `memory_copier`
    // on failure.
    fn mapped_mut<T: Pod + Debug>(&mut self, ptr: ForeignArrayPtr<T>) -> Option<&mut [T]> {
        let mm = self.memory_mapper.as_ref()?;
        // SAFETY: No other refs to process memory exist by preconditions of
        // MemoryManager::new + we have an exclusive reference.
        unsafe { mm.get_mut(ptr) }
    }

    /// Returns a reference to the given memory, copying to a local buffer if
    /// the memory isn't mapped into Shadow.
    pub fn memory_ref<T: Pod + Debug>(
        &self,
        ptr: ForeignArrayPtr<T>,
    ) -> Result<ProcessMemoryRef<'_, T>, Errno> {
        if let Some(mref) = self.mapped_ref(ptr) {
            Ok(ProcessMemoryRef::new_mapped(mref))
        } else {
            Ok(ProcessMemoryRef::new_copied(unsafe {
                self.memory_copier.clone_mem(ptr)?
            }))
        }
    }

    /// Returns a reference to the memory from the beginning of the given
    /// pointer to the last address in the pointer that's accessible. Useful for
    /// accessing string data of unknown size. The data is copied to a local
    /// buffer if the memory isn't mapped into Shadow.
    pub fn memory_ref_prefix<T: Pod + Debug>(
        &self,
        ptr: ForeignArrayPtr<T>,
    ) -> Result<ProcessMemoryRef<T>, Errno> {
        // Only use the mapped ref if it's able to get the whole region,
        // since otherwise the copying version might be able to get more
        // data.
        //
        // TODO: Implement and use MemoryMapper::memory_ref_prefix if and
        // when we're confident that the MemoryMapper always knows about all
        // mapped regions and merges adjacent regions.
        if let Some(mref) = self.mapped_ref(ptr) {
            Ok(ProcessMemoryRef::new_mapped(mref))
        } else {
            Ok(ProcessMemoryRef::new_copied(unsafe {
                self.memory_copier.clone_mem_prefix(ptr)?
            }))
        }
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

    /// Reads the memory into a local copy. `memory_ref` is potentially more
    /// efficient, but this is useful to avoid borrowing from the MemoryManager;
    /// e.g. when we still want to be able to access the data while also writing
    /// to process memory.
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
    pub fn read<T: Pod + Debug>(&self, ptr: ForeignPtr<T>) -> Result<T, Errno> {
        let ptr = ptr.cast::<MaybeUninit<T>>();
        let mut res: MaybeUninit<T> = MaybeUninit::uninit();

        self.copy_from_ptr(std::slice::from_mut(&mut res), ForeignArrayPtr::new(ptr, 1))?;
        // SAFETY: any values are valid for Pod
        Ok(unsafe { res.assume_init() })
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
    pub fn write<T: Pod + Debug>(&mut self, ptr: ForeignPtr<T>, val: &T) -> Result<(), Errno> {
        self.copy_to_ptr(ForeignArrayPtr::new(ptr, 1), std::slice::from_ref(val))
    }

    /// Similar to `read`, but saves a copy if you already have a `dst` to copy the data into.
    pub fn copy_from_ptr<T: Debug + Pod>(
        &self,
        dst: &mut [T],
        src: ForeignArrayPtr<T>,
    ) -> Result<(), Errno> {
        if let Some(src) = self.mapped_ref(src) {
            dst.copy_from_slice(src);
            return Ok(());
        }
        unsafe { self.memory_copier.copy_from_ptr(dst, src) }
    }

    // Copies memory from the beginning of the given pointer to the last address
    // in the pointer that's accessible. Not exposed as a public interface
    // because this is generally only useful for strings, and
    // `copy_str_from_ptr` provides a more convenient interface.
    fn copy_prefix_from_ptr<T: Debug + Pod>(
        &self,
        buf: &mut [T],
        ptr: ForeignArrayPtr<T>,
    ) -> Result<usize, Errno> {
        if let Some(src) = self.mapped_ref(ptr) {
            buf.copy_from_slice(src);
            return Ok(src.len());
        }
        unsafe { self.memory_copier.copy_prefix_from_ptr(buf, ptr) }
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

    /// Returns a mutable reference to the given memory. If the memory isn't
    /// mapped into Shadow, copies the data to a local buffer, which is written
    /// back into the process if and when the reference is flushed.
    pub fn memory_ref_mut<T: Pod + Debug>(
        &mut self,
        ptr: ForeignArrayPtr<T>,
    ) -> Result<ProcessMemoryRefMut<'_, T>, Errno> {
        // Work around a limitation of the borrow checker by getting this
        // immutable borrow of self out of the way before we do a mutable
        // borrow.
        let pid = self.pid;

        if let Some(mref) = self.mapped_mut(ptr) {
            Ok(ProcessMemoryRefMut::new_mapped(mref))
        } else {
            let copier = MemoryCopier::new(pid);
            let v = unsafe { copier.clone_mem(ptr)? };
            Ok(ProcessMemoryRefMut::new_copied(copier, ptr, v))
        }
    }

    /// Returns a mutable reference to the given memory. If the memory isn't
    /// mapped into Shadow, just returns a buffer with unspecified contents,
    /// which will be written back into the process if and when the reference
    /// is flushed.
    //
    // In some cases we initialize data to avoid actually returning
    // uninitialized memory.  We use inline(always) so that the compiler can
    // hopefully optimize away this initialization, in cases where the caller
    // overwrites the data.
    // TODO: return ProcessMemoryRefMut<MaybeUninit<T>> instead.
    #[inline(always)]
    pub fn memory_ref_mut_uninit<T: Pod + Debug>(
        &mut self,
        ptr: ForeignArrayPtr<T>,
    ) -> Result<ProcessMemoryRefMut<'_, T>, Errno> {
        // Work around a limitation of the borrow checker by getting this
        // immutable borrow of self out of the way before we do a mutable
        // borrow.
        let pid = self.pid;

        let mut mref = if let Some(mref) = self.mapped_mut(ptr) {
            // Even if we haven't initialized the data from this process, the
            // data is initialized from the Rust compiler's perspective; it has
            // *some* set contents via mmap, even if the other process hasn't
            // initialized it either.
            ProcessMemoryRefMut::new_mapped(mref)
        } else {
            let mut v = Vec::with_capacity(ptr.len());
            v.resize(ptr.len(), shadow_pod::zeroed());
            ProcessMemoryRefMut::new_copied(MemoryCopier::new(pid), ptr, v)
        };

        // In debug builds, overwrite with garbage to shake out bugs where
        // caller treats as initd; e.g. by reading the data or flushing it
        // back to the process without initializing it.
        if cfg!(debug_assertions) {
            // SAFETY: We do not write uninitialized data into `bytes`.
            let bytes = unsafe { shadow_pod::to_u8_slice_mut(&mut mref[..]) };
            for byte in bytes {
                unsafe { byte.as_mut_ptr().write(0x42) }
            }
        }

        Ok(mref)
    }

    /// Writes the memory from a local copy. If `src` doesn't already exist,
    /// using `memory_ref_mut_uninit` and initializing the data in that
    /// reference saves a copy.
    pub fn copy_to_ptr<T: Pod + Debug>(
        &mut self,
        dst: ForeignArrayPtr<T>,
        src: &[T],
    ) -> Result<(), Errno> {
        if let Some(dst) = self.mapped_mut(dst) {
            dst.copy_from_slice(src);
            return Ok(());
        }
        // SAFETY: No other refs to process memory exist by preconditions of
        // MemoryManager::new + we have an exclusive reference.
        unsafe { self.memory_copier.copy_to_ptr(dst, src) }
    }

    /// Which process's address space this MemoryManager manages.
    pub fn pid(&self) -> Pid {
        self.pid
    }

    /// Initialize the MemoryMapper, allowing for more efficient access. Needs a
    /// running thread.
    pub fn init_mapper(&mut self, ctx: &ThreadContext) {
        assert!(self.memory_mapper.is_none());
        self.memory_mapper = Some(MemoryMapper::new(self, ctx));
    }

    /// Whether the internal MemoryMapper has been initialized.
    pub fn has_mapper(&self) -> bool {
        self.memory_mapper.is_some()
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
        ctx: &ThreadContext,
        ptr: ForeignPtr<u8>,
    ) -> Result<ForeignPtr<u8>, SyscallError> {
        match &mut self.memory_mapper {
            Some(mm) => Ok(mm.handle_brk(ctx, ptr)?),
            None => Err(SyscallError::Native),
        }
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
        if let Some(mm) = &mut self.memory_mapper {
            mm.handle_mmap_result(ctx, ForeignArrayPtr::new(addr, length), prot, flags, fd);
        }
        Ok(addr)
    }

    pub fn handle_munmap(
        &mut self,
        ctx: &ThreadContext,
        addr: ForeignPtr<u8>,
        length: usize,
    ) -> Result<(), SyscallError> {
        if self.memory_mapper.is_some() {
            // Do it ourselves so that we can update our mappings based on
            // whether it succeeded.
            self.do_munmap(ctx, addr, length)?;
            Ok(())
        } else {
            // We don't need to know the result, and it's more efficient to let
            // the original syscall complete than to do it ourselves.
            Err(SyscallError::Native)
        }
    }

    fn do_munmap(
        &mut self,
        ctx: &ThreadContext,
        addr: ForeignPtr<u8>,
        length: usize,
    ) -> Result<(), Errno> {
        let (ctx, thread) = ctx.split_thread();
        thread.native_munmap(&ctx, addr, length)?;
        if let Some(mm) = &mut self.memory_mapper {
            mm.handle_munmap_result(addr, length);
        }
        Ok(())
    }

    pub fn handle_mremap(
        &mut self,
        ctx: &ThreadContext,
        old_address: ForeignPtr<u8>,
        old_size: usize,
        new_size: usize,
        flags: i32,
        new_address: ForeignPtr<u8>,
    ) -> Result<ForeignPtr<u8>, SyscallError> {
        match &mut self.memory_mapper {
            Some(mm) => {
                Ok(mm.handle_mremap(ctx, old_address, old_size, new_size, flags, new_address)?)
            }
            None => Err(SyscallError::Native),
        }
    }

    pub fn handle_mprotect(
        &mut self,
        ctx: &ThreadContext,
        addr: ForeignPtr<u8>,
        size: usize,
        prot: ProtFlags,
    ) -> Result<(), SyscallError> {
        match &mut self.memory_mapper {
            Some(mm) => Ok(mm.handle_mprotect(ctx, addr, size, prot)?),
            None => Err(SyscallError::Native),
        }
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
    #[no_mangle]
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
                trace!("Couldn't read {:?} into {:?}: {:?}", src, dst, e);
                e.to_negated_i32()
            }
        }
    }

    /// Copy `n` bytes from `src` to `dst`. Returns 0 on success or -EFAULT if any of the specified
    /// range couldn't be accessed. The write is flushed immediately.
    #[no_mangle]
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
                trace!("Couldn't write {:?} into {:?}: {:?}", src, dst, e);
                e.to_negated_i32()
            }
        }
    }
}
