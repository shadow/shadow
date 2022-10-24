//! Access and manage memory of a plugin process.
//!
//! The starting point for the public API is [`MemoryManager`].
//! [`MemoryManager`] can be used to:
//!
//! * Directly read or write process memory
//! * Obtain smart pointers ([`ProcessMemoryRef`] and [`ProcessMemoryRefMut`])
//! to process memory
//! * Obtain cursors to process memory implementing `std::io::Seek` and either
//! `std::io::Read` or `std::io::Write` ([`MemoryReaderCursor`] and
//! [`MemoryWriterCursor`])
//!
//! For the [`MemoryManager`] to maintain a consistent view of the process's address space,
//! and for it to be able to enforce Rust's safety requirements for references and sharing,
//! all access to process memory must go through it. This includes servicing syscalls that
//! modify the process address space (such as `mmap`).

use crate::cshadow as c;
use crate::host::syscall_types::{PluginPtr, SyscallError, SyscallResult, TypedPluginPtr};
use crate::host::thread::ThreadRef;
use crate::utility::notnull::*;
use crate::utility::pod;
use crate::utility::pod::Pod;
use log::*;
use memory_copier::MemoryCopier;
use memory_mapper::MemoryMapper;
use nix::{errno::Errno, unistd::Pid};
use std::fmt::Debug;
use std::mem::MaybeUninit;
use std::ops::{Deref, DerefMut};
use std::os::raw::c_void;

use super::context::ThreadContext;

mod memory_copier;
mod memory_mapper;

/// An object implementing std::io::Read and std::io::Seek for
/// a range of plugin memory.
pub struct MemoryReaderCursor<'a> {
    memory_manager: &'a MemoryManager,
    ptr: TypedPluginPtr<u8>,
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
            .copy_from_ptr(&mut buf[..toread], ptr.slice(..toread))
            .map_err(|e| std::io::Error::from_raw_os_error(e as i32))?;
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
        return Err(std::io::Error::from_raw_os_error(Errno::EFAULT as i32));
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
    ptr: TypedPluginPtr<u8>,
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
            .copy_to_ptr(ptr.slice(..towrite), &buf[..towrite])
            .map_err(|e| std::io::Error::from_raw_os_error(e as i32))?;
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
/// let tpp = TypedPluginPtr::<u32>::new(ptr, 10);
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
        let nullpos = self.iter().position(|c| *c == 0);
        match nullpos {
            // SAFETY: We just got the null position above.
            Some(nullpos) => {
                Ok(unsafe { std::ffi::CStr::from_bytes_with_nul_unchecked(&self[..=nullpos]) })
            }
            None => Err(Errno::ENAMETOOLONG),
        }
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
    Copied(MemoryCopier, TypedPluginPtr<T>, Vec<T>),
    // Memory-mapped process memory.
    Mapped(&'a mut [T]),
}

/// A mutable reference to a slice of plugin memory. Implements `DerefMut<[T]>`,
/// allowing, e.g.:
///
/// ```ignore
/// let tpp = TypedPluginPtr::<u32>::new(ptr, 10);
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
    fn new_copied(copier: MemoryCopier, ptr: TypedPluginPtr<T>, v: Vec<T>) -> Self {
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
                    ptr.len(),
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
    /// SAFETY: `pid`'s memory must not be modified without holding an exclusive
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
    fn mapped_ref<'a, T: Pod + Debug>(&'a self, ptr: TypedPluginPtr<T>) -> Option<&[T]> {
        let mm = self.memory_mapper.as_ref()?;
        // SAFETY: No mutable refs to process memory exist by preconditions of
        // MemoryManager::new + we have a reference.
        unsafe { mm.get_ref(ptr) }
    }

    // Internal helper for getting a reference to memory via the
    // `memory_mapper`.  Calling methods should fall back to the `memory_copier`
    // on failure.
    fn mapped_mut<'a, T: Pod + Debug>(&'a mut self, ptr: TypedPluginPtr<T>) -> Option<&mut [T]> {
        let mm = self.memory_mapper.as_ref()?;
        // SAFETY: No other refs to process memory exist by preconditions of
        // MemoryManager::new + we have an exclusive reference.
        unsafe { mm.get_mut(ptr) }
    }

    /// Returns a reference to the given memory, copying to a local buffer if
    /// the memory isn't mapped into Shadow.
    pub fn memory_ref<'a, T: Pod + Debug>(
        &'a self,
        ptr: TypedPluginPtr<T>,
    ) -> Result<ProcessMemoryRef<'a, T>, Errno> {
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
        ptr: TypedPluginPtr<T>,
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
    pub fn reader<'a>(&'a self, ptr: TypedPluginPtr<u8>) -> MemoryReaderCursor<'a> {
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
    pub fn read_vals<T: Pod + Debug, const N: usize>(
        &self,
        ptr: TypedPluginPtr<T>,
    ) -> Result<[T; N], Errno> {
        assert_eq!(ptr.len(), N);

        // SAFETY: any values are valid for Pod.
        let mut res: [T; N] = unsafe { MaybeUninit::uninit().assume_init() };
        self.copy_from_ptr(&mut res, ptr)?;
        Ok(res)
    }

    /// Similar to `read_vals`, but saves a copy if you already have a `dst` to
    /// copy the data into.
    pub fn copy_from_ptr<T: Debug + Pod>(
        &self,
        dst: &mut [T],
        src: TypedPluginPtr<T>,
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
        ptr: TypedPluginPtr<T>,
    ) -> Result<usize, Errno> {
        if let Some(src) = self.mapped_ref(ptr) {
            buf.copy_from_slice(src);
            return Ok(src.len());
        }
        unsafe { self.memory_copier.copy_prefix_from_ptr(buf, ptr) }
    }

    /// Copies a NULL-terminated string starting from the beginning of `dst` and
    /// contained completely within `dst`. Still works if some of `dst` isn't
    /// readable, as long as a NULL-terminated-string is contained in the
    /// readable prefix.
    ///
    /// If holding a reference to the MemoryManager for the lifetime of the
    /// string is acceptable, use `memory_ref_prefix` and
    /// `ProcessMemoryRef::get_str` to potentially avoid an extra copy.
    pub fn copy_str_from_ptr<'a>(
        &self,
        dst: &'a mut [u8],
        src: TypedPluginPtr<u8>,
    ) -> Result<&'a std::ffi::CStr, Errno> {
        let nread = self.copy_prefix_from_ptr(dst, src)?;
        let dst = &dst[..nread];
        let nullpos = match dst.iter().position(|c| *c == 0) {
            Some(i) => i,
            None => return Err(Errno::ENAMETOOLONG),
        };
        Ok(unsafe { std::ffi::CStr::from_bytes_with_nul_unchecked(&dst[..=nullpos]) })
    }

    /// Returns a mutable reference to the given memory. If the memory isn't
    /// mapped into Shadow, copies the data to a local buffer, which is written
    /// back into the process if and when the reference is flushed.
    pub fn memory_ref_mut<'a, T: Pod + Debug>(
        &'a mut self,
        ptr: TypedPluginPtr<T>,
    ) -> Result<ProcessMemoryRefMut<'a, T>, Errno> {
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
    ///
    /// WARNING: If the reference is flushed without initializing its contents,
    /// the unspecified contents will be written back into process memory.
    /// This can be avoided by calling `noflush` on the reference.
    pub fn memory_ref_mut_uninit<'a, T: Pod + Debug>(
        &'a mut self,
        ptr: TypedPluginPtr<T>,
    ) -> Result<ProcessMemoryRefMut<'a, T>, Errno> {
        // Work around a limitation of the borrow checker by getting this
        // immutable borrow of self out of the way before we do a mutable
        // borrow.
        let pid = self.pid;

        let mut mref = if let Some(mref) = self.mapped_mut(ptr) {
            ProcessMemoryRefMut::new_mapped(mref)
        } else {
            let mut v = Vec::with_capacity(ptr.len());
            unsafe { v.set_len(v.capacity()) };
            ProcessMemoryRefMut::new_copied(MemoryCopier::new(pid), ptr, v)
        };

        // In debug builds, overwrite with garbage to shake out bugs where
        // caller treats as initd; e.g. by reading the data or flushing it
        // back to the process without initializing it.
        if cfg!(debug_assertions) {
            pod::to_u8_slice_mut(&mut mref[..]).fill(0x42);
        }

        Ok(mref)
    }

    /// Writes the memory from a local copy. If `src` doesn't already exist,
    /// using `memory_ref_mut_uninit` and initializing the data in that
    /// reference saves a copy.
    pub fn copy_to_ptr<T: Pod + Debug>(
        &mut self,
        dst: TypedPluginPtr<T>,
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
    pub fn init_mapper(&mut self, thread: &mut ThreadRef) {
        assert!(self.memory_mapper.is_none());
        self.memory_mapper = Some(MemoryMapper::new(self, thread));
    }

    /// Whether the internal MemoryMapper has been initialized.
    pub fn has_mapper(&self) -> bool {
        self.memory_mapper.is_some()
    }

    /// Create a write accessor for the specified plugin memory.
    pub fn writer<'a>(&'a mut self, ptr: TypedPluginPtr<u8>) -> MemoryWriterCursor<'a> {
        MemoryWriterCursor {
            memory_manager: self,
            ptr,
            offset: 0,
        }
    }

    fn handle_brk(&mut self, thread: &mut ThreadRef, ptr: PluginPtr) -> SyscallResult {
        match &mut self.memory_mapper {
            Some(mm) => mm.handle_brk(thread, ptr),
            None => Err(SyscallError::Native),
        }
    }

    #[allow(clippy::too_many_arguments)]
    fn do_mmap(
        &mut self,
        thread: &mut ThreadRef,
        addr: PluginPtr,
        length: usize,
        prot: i32,
        flags: i32,
        fd: i32,
        offset: i64,
    ) -> SyscallResult {
        let addr = thread.native_mmap(addr, length, prot, flags, fd, offset)?;
        if let Some(mm) = &mut self.memory_mapper {
            mm.handle_mmap_result(
                thread,
                TypedPluginPtr::new::<u8>(addr, length),
                prot,
                flags,
                fd,
            );
        }
        Ok(addr.into())
    }

    fn handle_munmap(
        &mut self,
        thread: &mut ThreadRef,
        addr: PluginPtr,
        length: usize,
    ) -> SyscallResult {
        if self.memory_mapper.is_some() {
            // Do it ourselves so that we can update our mappings based on
            // whether it succeeded.
            self.do_munmap(thread, addr, length)?;
            Ok(0.into())
        } else {
            // We don't need to know the result, and it's more efficient to let
            // the original syscall complete than to do it ourselves.
            Err(SyscallError::Native)
        }
    }

    fn do_munmap(
        &mut self,
        thread: &mut ThreadRef,
        addr: PluginPtr,
        length: usize,
    ) -> nix::Result<()> {
        thread.native_munmap(addr, length)?;
        if let Some(mm) = &mut self.memory_mapper {
            mm.handle_munmap_result(addr, length);
        }
        Ok(())
    }

    fn handle_mremap(
        &mut self,
        thread: &mut ThreadRef,
        old_address: PluginPtr,
        old_size: usize,
        new_size: usize,
        flags: i32,
        new_address: PluginPtr,
    ) -> SyscallResult {
        match &mut self.memory_mapper {
            Some(mm) => {
                mm.handle_mremap(thread, old_address, old_size, new_size, flags, new_address)
            }
            None => Err(SyscallError::Native),
        }
    }

    fn handle_mprotect(
        &mut self,
        thread: &mut ThreadRef,
        addr: PluginPtr,
        size: usize,
        prot: i32,
    ) -> SyscallResult {
        match &mut self.memory_mapper {
            Some(mm) => mm.handle_mprotect(thread, addr, size, prot),
            None => Err(SyscallError::Native),
        }
    }
}

/// Memory allocated by Shadow, in a remote address space.
pub struct AllocdMem<T>
where
    T: Pod,
{
    ptr: TypedPluginPtr<T>,
    // Whether the pointer has been freed.
    freed: bool,
}

impl<T> AllocdMem<T>
where
    T: Pod,
{
    /// Allocate memory in the current active process.
    /// Must be freed explicitly via `free`.
    pub fn new(ctx: &mut ThreadContext, len: usize) -> Self {
        let prot = libc::PROT_READ | libc::PROT_WRITE;

        // Allocate through the MemoryManager, so that it knows about this region.
        let ptr = PluginPtr::from(
            ctx.process
                .memory_mut()
                .do_mmap(
                    ctx.thread,
                    PluginPtr::from(0usize),
                    len * std::mem::size_of::<T>(),
                    prot,
                    libc::MAP_ANONYMOUS | libc::MAP_PRIVATE,
                    -1,
                    0,
                )
                .unwrap(),
        );

        Self {
            ptr: TypedPluginPtr::new::<T>(ptr, len),
            freed: false,
        }
    }

    /// Pointer to the allocated memory.
    pub fn ptr(&self) -> TypedPluginPtr<T> {
        self.ptr
    }

    pub fn free(mut self, ctx: &mut ThreadContext) {
        ctx.process
            .memory_mut()
            .do_munmap(ctx.thread, self.ptr.ptr(), self.ptr.len())
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
    use crate::{core::worker::Worker, host::context::ThreadContextObjs};

    use super::*;

    /// # Safety
    /// * `thread` must point to a valid object.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_new(pid: libc::pid_t) -> *mut MemoryManager {
        Box::into_raw(Box::new(unsafe {
            MemoryManager::new(nix::unistd::Pid::from_raw(pid))
        }))
    }

    /// # Safety
    /// * `mm` must point to a valid object.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_free(mm: *mut MemoryManager) {
        unsafe { mm.as_mut().map(|mm| Box::from_raw(notnull_mut_debug(mm))) };
    }

    #[no_mangle]
    pub unsafe extern "C" fn allocdmem_new(
        thread: *mut c::Thread,
        len: usize,
    ) -> *mut AllocdMem<u8> {
        Worker::with_active_host(|host| {
            let mut objs =
                unsafe { ThreadContextObjs::from_thread(host, notnull_mut_debug(thread)) };
            Box::into_raw(Box::new(AllocdMem::new(&mut objs.borrow(), len)))
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn allocdmem_free(
        thread: *mut c::Thread,
        allocd_mem: *mut AllocdMem<u8>,
    ) {
        Worker::with_active_host(|host| {
            let allocd_mem = unsafe { Box::from_raw(notnull_mut_debug(allocd_mem)) };
            let mut objs =
                unsafe { ThreadContextObjs::from_thread(host, notnull_mut_debug(thread)) };
            allocd_mem.free(&mut objs.borrow());
        })
        .unwrap()
    }

    #[no_mangle]
    pub unsafe extern "C" fn allocdmem_pluginPtr(allocd_mem: *const AllocdMem<u8>) -> c::PluginPtr {
        unsafe { allocd_mem.as_ref().unwrap().ptr().ptr().into() }
    }

    /// Initialize the MemoryMapper if it isn't already initialized. `thread` must
    /// be running and ready to make native syscalls.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_initMapperIfNeeded(
        memory_manager: *mut MemoryManager,
        thread: *mut c::Thread,
    ) {
        let memory_manager = unsafe { memory_manager.as_mut().unwrap() };
        if !memory_manager.has_mapper() {
            let mut thread = unsafe { ThreadRef::new(notnull_mut_debug(thread)) };
            memory_manager.init_mapper(&mut thread)
        }
    }

    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_freeRef<'a>(memory_ref: *mut ProcessMemoryRef<'a, u8>) {
        unsafe { Box::from_raw(notnull_mut_debug(memory_ref)) };
    }

    #[no_mangle]
    pub unsafe extern "C" fn memorymanagerref_ptr<'a>(
        memory_ref: *const ProcessMemoryRef<'a, u8>,
    ) -> *const c_void {
        unsafe { memory_ref.as_ref() }.unwrap().as_ptr() as *const c_void
    }

    #[no_mangle]
    pub unsafe extern "C" fn memorymanagerref_sizeof<'a>(
        memory_ref: *const ProcessMemoryRef<'a, u8>,
    ) -> libc::size_t {
        unsafe { memory_ref.as_ref() }.unwrap().len()
    }

    /// Get a read-accessor to the specified plugin memory.
    /// Must be freed via `memorymanager_freeReader`.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_getReadablePtr<'a>(
        memory_manager: *const MemoryManager,
        plugin_src: c::PluginPtr,
        n: usize,
    ) -> *mut ProcessMemoryRef<'a, u8> {
        let memory_manager = unsafe { memory_manager.as_ref().unwrap() };
        let plugin_src: PluginPtr = plugin_src.into();
        let memory_ref = memory_manager.memory_ref(TypedPluginPtr::new::<u8>(plugin_src.into(), n));
        match memory_ref {
            Ok(mr) => Box::into_raw(Box::new(mr)),
            Err(e) => {
                warn!("Failed to get memory ref: {:?}", e);
                std::ptr::null_mut()
            }
        }
    }

    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_getReadablePtrPrefix<'a>(
        memory_manager: *const MemoryManager,
        plugin_src: c::PluginPtr,
        n: usize,
    ) -> *mut ProcessMemoryRef<'a, u8> {
        let memory_manager = unsafe { memory_manager.as_ref().unwrap() };
        match memory_manager
            .memory_ref_prefix(TypedPluginPtr::new::<u8>(PluginPtr::from(plugin_src), n))
        {
            Ok(mr) => Box::into_raw(Box::new(mr)),
            Err(e) => {
                warn!("Couldn't read memory for string: {:?}", e);
                std::ptr::null_mut()
            }
        }
    }

    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_readString<'a>(
        memory_manager: *const MemoryManager,
        ptr: c::PluginPtr,
        strbuf: *mut libc::c_char,
        maxlen: libc::size_t,
    ) -> libc::ssize_t {
        let memory_manager = unsafe { memory_manager.as_ref().unwrap() };
        let buf =
            unsafe { std::slice::from_raw_parts_mut(notnull_mut_debug(strbuf) as *mut u8, maxlen) };
        let cstr = match memory_manager
            .copy_str_from_ptr(buf, TypedPluginPtr::new::<u8>(PluginPtr::from(ptr), maxlen))
        {
            Ok(cstr) => cstr,
            Err(e) => return -(e as libc::ssize_t),
        };
        cstr.to_bytes().len().try_into().unwrap()
    }

    /// Copy data from this reader's memory.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_readPtr(
        memory_manager: *const MemoryManager,
        dst: *mut c_void,
        src: c::PluginPtr,
        n: usize,
    ) -> i32 {
        let memory_manager = unsafe { memory_manager.as_ref().unwrap() };
        let src = TypedPluginPtr::new::<u8>(src.into(), n);
        let dst = unsafe { std::slice::from_raw_parts_mut(notnull_mut_debug(dst) as *mut u8, n) };
        match memory_manager.copy_from_ptr(dst, src) {
            Ok(_) => 0,
            Err(e) => {
                trace!("Couldn't read {:?} into {:?}: {:?}", src, dst, e);
                -(e as i32)
            }
        }
    }

    /// Write data to this writer's memory.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_writePtr(
        memory_manager: *mut MemoryManager,
        dst: c::PluginPtr,
        src: *const c_void,
        n: usize,
    ) -> i32 {
        let memory_manager = unsafe { memory_manager.as_mut().unwrap() };
        let dst = TypedPluginPtr::new::<u8>(dst.into(), n);
        let src = unsafe { std::slice::from_raw_parts(notnull_debug(src) as *const u8, n) };
        match memory_manager.copy_to_ptr(dst, src) {
            Ok(_) => 0,
            Err(e) => {
                trace!("Couldn't write {:?} into {:?}: {:?}", src, dst, e);
                -(e as i32)
            }
        }
    }

    /// Get a writable pointer to this writer's memory. Initial contents are unspecified.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_getWritablePtr<'a>(
        memory_manager: *mut MemoryManager,
        plugin_src: c::PluginPtr,
        n: usize,
    ) -> *mut ProcessMemoryRefMut<'a, u8> {
        let memory_manager = unsafe { memory_manager.as_mut().unwrap() };
        let plugin_src = TypedPluginPtr::new::<u8>(PluginPtr::from(plugin_src), n);
        let memory_ref = memory_manager.memory_ref_mut_uninit(plugin_src);
        match memory_ref {
            Ok(mr) => Box::into_raw(Box::new(mr)),
            Err(e) => {
                warn!("Failed to get memory ref: {:?}", e);
                std::ptr::null_mut()
            }
        }
    }

    /// Get a readable and writable pointer to this writer's memory.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_getMutablePtr<'a>(
        memory_manager: *mut MemoryManager,
        plugin_src: c::PluginPtr,
        n: usize,
    ) -> *mut ProcessMemoryRefMut<'a, u8> {
        let memory_manager = unsafe { memory_manager.as_mut().unwrap() };
        let plugin_src = TypedPluginPtr::new::<u8>(PluginPtr::from(plugin_src), n);
        let memory_ref = memory_manager.memory_ref_mut(plugin_src);
        match memory_ref {
            Ok(mr) => Box::into_raw(Box::new(mr)),
            Err(e) => {
                warn!("Failed to get memory ref: {:?}", e);
                std::ptr::null_mut()
            }
        }
    }

    #[no_mangle]
    pub unsafe extern "C" fn memorymanagermut_ptr<'a>(
        memory_ref: *mut ProcessMemoryRefMut<'a, u8>,
    ) -> *mut c_void {
        unsafe { memory_ref.as_ref() }.unwrap().as_ptr() as *mut c_void
    }

    #[no_mangle]
    pub unsafe extern "C" fn memorymanagermut_sizeof<'a>(
        memory_ref: *mut ProcessMemoryRefMut<'a, u8>,
    ) -> libc::size_t {
        unsafe { memory_ref.as_ref() }.unwrap().len()
    }

    /// Write-back any previously returned writable memory, and free the writer.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_freeMutRefWithFlush<'a>(
        mref: *mut ProcessMemoryRefMut<'a, u8>,
    ) -> i32 {
        let mref = unsafe { Box::from_raw(notnull_mut_debug(mref)) };
        // No way to safely recover here if the flush fails.
        match mref.flush() {
            Ok(()) => 0,
            Err(e) => {
                warn!("Failed to flush writes");
                -(e as i32)
            }
        }
    }

    /// Write-back any previously returned writable memory, and free the writer.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_freeMutRefWithoutFlush<'a>(
        mref: *mut ProcessMemoryRefMut<'a, u8>,
    ) {
        let mref = unsafe { Box::from_raw(notnull_mut_debug(mref)) };
        // No way to safely recover here if the flush fails.
        mref.noflush()
    }

    /// Fully handles the `brk` syscall, keeping the "heap" mapped in our shared mem file.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_handleBrk(
        memory_manager: *mut MemoryManager,
        thread: *mut c::Thread,
        plugin_src: c::PluginPtr,
    ) -> c::SysCallReturn {
        let memory_manager = unsafe { memory_manager.as_mut().unwrap() };
        let mut thread = unsafe { ThreadRef::new(notnull_mut_debug(thread)) };
        memory_manager
            .handle_brk(&mut thread, PluginPtr::from(plugin_src))
            .into()
    }

    /// Fully handles the `mmap` syscall
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_handleMmap(
        memory_manager: *mut MemoryManager,
        thread: *mut c::Thread,
        addr: c::PluginPtr,
        len: usize,
        prot: i32,
        flags: i32,
        fd: i32,
        offset: i64,
    ) -> c::SysCallReturn {
        let memory_manager = unsafe { memory_manager.as_mut().unwrap() };
        let mut thread = unsafe { ThreadRef::new(notnull_mut_debug(thread)) };
        memory_manager
            .do_mmap(
                &mut thread,
                PluginPtr::from(addr),
                len,
                prot,
                flags,
                fd,
                offset,
            )
            .into()
    }

    /// Fully handles the `munmap` syscall
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_handleMunmap(
        memory_manager: *mut MemoryManager,
        thread: *mut c::Thread,
        addr: c::PluginPtr,
        len: usize,
    ) -> c::SysCallReturn {
        let memory_manager = unsafe { memory_manager.as_mut().unwrap() };
        let mut thread = unsafe { ThreadRef::new(notnull_mut_debug(thread)) };
        memory_manager
            .handle_munmap(&mut thread, PluginPtr::from(addr), len)
            .into()
    }

    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_handleMremap(
        memory_manager: *mut MemoryManager,
        thread: *mut c::Thread,
        old_addr: c::PluginPtr,
        old_size: usize,
        new_size: usize,
        flags: i32,
        new_addr: c::PluginPtr,
    ) -> c::SysCallReturn {
        let memory_manager = unsafe { memory_manager.as_mut().unwrap() };
        let mut thread = unsafe { ThreadRef::new(notnull_mut_debug(thread)) };
        memory_manager
            .handle_mremap(
                &mut thread,
                PluginPtr::from(old_addr),
                old_size,
                new_size,
                flags,
                PluginPtr::from(new_addr),
            )
            .into()
    }

    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_handleMprotect(
        memory_manager: *mut MemoryManager,
        thread: *mut c::Thread,
        addr: c::PluginPtr,
        size: usize,
        prot: i32,
    ) -> c::SysCallReturn {
        let memory_manager = unsafe { memory_manager.as_mut().unwrap() };
        let mut thread = unsafe { ThreadRef::new(notnull_mut_debug(thread)) };
        memory_manager
            .handle_mprotect(&mut thread, PluginPtr::from(addr), size, prot)
            .into()
    }
}
