use super::syscall_types::{PluginPtr, SyscallError, SyscallResult, TypedPluginPtr};
use super::thread::{CThread, Thread};
use crate::core::worker::Worker;
use crate::cshadow as c;
use crate::utility::interval_map::{Interval, IntervalMap, Mutation};
use crate::utility::pod;
use crate::utility::pod::Pod;
use crate::utility::proc_maps;
use crate::utility::proc_maps::{MappingPath, Sharing};
use log::*;
use nix::errno::Errno;
use nix::{fcntl, sys};
use std::cell::RefCell;
use std::collections::HashMap;
use std::convert::TryFrom;
use std::fmt::Debug;
use std::fs::File;
use std::fs::OpenOptions;
use std::os::raw::c_void;
use std::os::unix::io::AsRawFd;
use std::path::PathBuf;
use std::process;

/// Low-level functions for reading plugin process memory.
trait MemoryReaderTrait<T>: Debug
where
    T: Pod,
{
    /// Get a reference to the readable prefix of the region.
    fn ref_some(&self) -> Result<&[T], Errno>;

    /// Reads up to min(self.len(), buf.len()). May read less if the end of the
    /// region is inaccessible.
    fn read_some(&self, offset: usize, buf: &mut [T]) -> Result<usize, Errno>;

    fn len(&self) -> usize;

    // Reads exactly buf.len() or returns an error.
    fn read_exact(&self, offset: usize, buf: &mut [T]) -> Result<(), Errno> {
        let n = self.read_some(offset, buf)?;
        if n == buf.len() {
            Ok(())
        } else {
            warn!("Partial read: got:{} expected:{}", buf.len(), n);
            Err(Errno::EFAULT)
        }
    }

    /// Get a reference to the whole region, or return an error.
    fn ref_exact(&self) -> Result<&[T], Errno> {
        let slice = self.ref_some()?;
        if slice.len() == self.len() {
            Ok(slice)
        } else {
            warn!("Partial ref: got:{} expected:{}", slice.len(), self.len());
            Err(Errno::EFAULT)
        }
    }
}

/// Read-accessor to plugin memory.
pub struct MemoryReader<'a, T> {
    // Wrapping the trait object lets us implement traits such as std::io::Read.
    // It can also be passed across the FFI boundary whereas trait objects cannot.
    reader: Box<dyn MemoryReaderTrait<T> + 'a>,
}

impl<'a, T> MemoryReader<'a, T>
where
    T: Pod,
{
    fn new(reader: Box<dyn MemoryReaderTrait<T> + 'a>) -> Self {
        Self { reader }
    }

    /// Get a reference to the readable prefix of the region.
    pub fn ref_some(&self) -> Result<&[T], Errno> {
        self.reader.ref_some()
    }

    /// Get a reference to the whole region, or return an error.
    pub fn ref_exact(&self) -> Result<&[T], Errno> {
        self.reader.ref_exact()
    }

    /// Copies up to min(self.len(), buf.len()). May read less if the end
    /// of the region is inaccessible.
    pub fn read_some(&self, buf: &mut [T]) -> Result<usize, Errno> {
        self.reader.read_some(0, buf)
    }

    pub fn read_exact(&self, buf: &mut [T]) -> Result<(), Errno> {
        self.reader.read_exact(0, buf)
    }

    /// Reads N values.
    pub fn as_values<const N: usize>(&self) -> Result<[T; N], Errno> {
        // SAFETY: Any bit-pattern is valid for Pod.
        let mut value: [T; N] = unsafe { std::mem::MaybeUninit::uninit().assume_init() };
        self.reader.read_exact(0, &mut value)?;
        Ok(value)
    }

    /// Number of items referenced by this reader.
    pub fn len(&self) -> usize {
        self.reader.len()
    }
}

impl<'a> MemoryReader<'a, u8> {
    /// Wraps `self` into a `MemoryReaderCursor`
    pub fn cursor(self) -> MemoryReaderCursor<'a> {
        MemoryReaderCursor {
            reader: self,
            offset: 0,
        }
    }

    fn read_string(&self, buf: &mut [u8]) -> Result<usize, Errno> {
        let nread = self.reader.read_some(0, buf)?;
        let buf = &buf[..nread];
        let nullpos = buf.iter().position(|c| *c == 0);
        match nullpos {
            Some(i) => Ok(i),
            None => Err(Errno::ENAMETOOLONG),
        }
    }

    fn ref_string(&self) -> Result<&std::ffi::CStr, Errno> {
        let buf = self.reader.ref_some()?;
        let nullpos = match buf.iter().position(|c| *c == 0) {
            Some(i) => i,
            None => return Err(Errno::ENAMETOOLONG),
        };
        std::ffi::CStr::from_bytes_with_nul(&buf[..=nullpos]).map_err(|_| Errno::ENAMETOOLONG)
    }
}

/// A MemoryReader that tracks a position within the region of memory it reads.
/// Useful primarily via the std::io::Read trait.
pub struct MemoryReaderCursor<'a> {
    reader: MemoryReader<'a, u8>,
    offset: usize,
}

impl<'a> std::io::Read for MemoryReaderCursor<'a> {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        let reader_size = self.reader.len() - self.offset;
        let toread = std::cmp::min(buf.len(), reader_size);
        if toread == 0 {
            return Ok(0);
        }
        self.reader
            .reader
            .read_exact(self.offset, &mut buf[..toread])
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
        seek_helper(&mut self.offset, self.reader.len(), pos)
    }
}

// Low level methods for writing to memory.
trait MemoryWriterTrait<T>: Debug
where
    T: Pod,
{
    fn as_mut(&mut self) -> Result<&mut [T], Errno>;
    fn as_mut_uninit(&mut self) -> Result<&mut [T], Errno>;
    fn flush(&mut self) -> Result<(), Errno>;
    fn copy(&mut self, offset: usize, buf: &[T]) -> Result<(), Errno>;
    fn len(&self) -> usize;
}

/// Write-accessor to plugin memory.
pub struct MemoryWriter<'a, T>
where
    T: Pod,
{
    // Wrapping the trait object lets us implement traits such as std::io::Read.
    // It can also be passed across the FFI boundary (trait objects cannot).
    writer: Box<dyn MemoryWriterTrait<T> + 'a>,
    dirty: bool,
}

impl<'a, T> MemoryWriter<'a, T>
where
    T: Pod,
{
    fn new(writer: Box<dyn MemoryWriterTrait<T> + 'a>) -> Self {
        Self {
            writer,
            dirty: false,
        }
    }

    /// Access the data as a mutable slice. May require 2 copies: reading the
    /// current contents of the region, and writing it back.
    ///
    /// Caller must later call `flush`, which writes back the data if needed.
    pub fn as_mut(&mut self) -> Result<&mut [T], Errno> {
        self.dirty = true;
        (&mut *self.writer).as_mut()
    }

    /// Access the data as a write-only slice. May require an extra copy to write
    /// back the contents of the buffer.
    ///
    /// Caller must later call `flush`, which writes back the data if needed.
    pub fn as_mut_uninit(&mut self) -> Result<&mut [T], Errno> {
        self.dirty = true;
        self.writer.as_mut_uninit()
    }

    /// Write regions previously returned by `as_mut` or `as_mut_uninit` back to
    /// process memory, if needed.
    pub fn flush(&mut self) -> Result<(), Errno> {
        self.writer.flush()?;
        self.dirty = false;
        Ok(())
    }

    /// Write the data from `buf`, which must be of size `self.len()`.
    pub fn copy(&mut self, buf: &[T]) -> Result<(), Errno> {
        assert_eq!(buf.len(), self.len());
        self.writer.copy(0, buf)
    }

    /// Number of items referenced by this writer.
    pub fn len(&self) -> usize {
        self.writer.len()
    }
}

impl<'a> MemoryWriter<'a, u8> {
    /// Wraps this object in a `MemoryWriterCursor`.
    pub fn cursor(self) -> MemoryWriterCursor<'a> {
        MemoryWriterCursor {
            writer: self,
            offset: 0,
        }
    }
}

impl<'a, T> Drop for MemoryWriter<'a, T>
where
    T: Pod,
{
    fn drop(&mut self) {
        // It's a bug to drop the writer without flushing and handling errors,
        // but doesn't warrant crashing in production if the flush comples
        // successfully.
        debug_assert!(!self.dirty);
        if self.dirty {
            warn!("BUG: dropped writer without flushing");
        }
        // Crash if we're unable to flush, since proceeding could produce subtly
        // wrong results.
        self.flush().unwrap()
    }
}

/// Wrapper around `MemoryWriter` that tracks a position within the buffer.
/// Primarily for use with the `std::io::Write` trait.
pub struct MemoryWriterCursor<'a> {
    writer: MemoryWriter<'a, u8>,
    offset: usize,
}

impl<'a> std::io::Write for MemoryWriterCursor<'a> {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        let writer_size = self.writer.len() - self.offset;
        let towrite = std::cmp::min(buf.len(), writer_size);
        if towrite == 0 {
            return Ok(0);
        }
        self.writer
            .writer
            .copy(self.offset, &buf[..towrite])
            .map_err(|e| std::io::Error::from_raw_os_error(e as i32))?;
        self.offset += towrite;
        Ok(towrite)
    }

    fn flush(&mut self) -> std::io::Result<()> {
        self.writer
            .flush()
            .map_err(|e| std::io::Error::from_raw_os_error(e as i32))
    }
}

impl<'a> std::io::Seek for MemoryWriterCursor<'a> {
    fn seek(&mut self, pos: std::io::SeekFrom) -> std::io::Result<u64> {
        seek_helper(&mut self.offset, self.writer.len(), pos)
    }
}

/// A reader that copies data from the plugin process, resulting in extra copies
/// when the data is accessed by reference.
#[derive(Debug)]
struct CopyingMemoryReader<'a, T>
where
    T: Pod,
{
    memory_manager: &'a MemoryManager,
    readable_ptr: once_cell::unsync::OnceCell<Result<Box<[T]>, Errno>>,
    ptr: TypedPluginPtr<T>,
}

impl<'a, T> CopyingMemoryReader<'a, T>
where
    T: Pod + Debug,
{
    fn new(memory_manager: &'a MemoryManager, ptr: TypedPluginPtr<T>) -> Self {
        Self {
            memory_manager,
            readable_ptr: once_cell::unsync::OnceCell::new(),
            ptr,
        }
    }
}

impl<'a, T> MemoryReaderTrait<T> for CopyingMemoryReader<'a, T>
where
    T: Pod + Debug,
{
    fn ref_some(&self) -> Result<&[T], Errno> {
        if self.len() == 0 {
            trace!("returning empty slice");
            return Ok(&mut [][..]);
        }

        match self
            .readable_ptr
            .get_or_init(|| -> Result<Box<[T]>, Errno> {
                let mut vec = Vec::<T>::with_capacity(self.len());
                // SAFETY: any value is valid for Pod.
                unsafe { vec.set_len(self.len()) };
                let nread = self.read_some(0, &mut vec)?;
                vec.truncate(nread);
                Ok(vec.into_boxed_slice())
            }) {
            // Convert box to slice
            Ok(v) => Ok(v),
            // Convert &Errno to Errno
            Err(e) => Err(*e),
        }
    }

    fn read_some(&self, offset: usize, buf: &mut [T]) -> Result<usize, Errno> {
        self.memory_manager.read_some(buf, self.ptr.slice(offset..))
    }

    fn len(&self) -> usize {
        self.ptr.len()
    }
}

#[derive(Debug)]
struct CopyingMemoryWriter<'a, T>
where
    T: Pod + Debug,
{
    memory_manager: &'a mut MemoryManager,
    writable_ptr: Option<Box<[T]>>,
    ptr: TypedPluginPtr<T>,
}

impl<'a, T> CopyingMemoryWriter<'a, T>
where
    T: Pod + Debug,
{
    fn new(memory_manager: &'a mut MemoryManager, ptr: TypedPluginPtr<T>) -> Self {
        Self {
            memory_manager,
            writable_ptr: None,
            ptr,
        }
    }
}

impl<'a, T> MemoryWriterTrait<T> for CopyingMemoryWriter<'a, T>
where
    T: Pod + Debug,
{
    fn len(&self) -> usize {
        self.ptr.len()
    }

    fn as_mut(&mut self) -> Result<&mut [T], Errno> {
        if self.len() == 0 {
            trace!("as_mut_init returning empty slice");
            return Ok(&mut [][..]);
        }

        let mut vec = Vec::<T>::with_capacity(self.len());
        // No need to initialize the contents of `vec`, since we're about to
        // overwrite it.
        // SAFETY: any bit pattern is valid Pod.
        unsafe { vec.set_len(self.len()) };
        self.memory_manager.read_exact(&mut vec, self.ptr)?;

        debug_assert!(self.writable_ptr.is_none());
        self.writable_ptr = Some(vec.into_boxed_slice());

        Ok(&mut *self.writable_ptr.as_mut().unwrap())
    }

    fn as_mut_uninit(&mut self) -> Result<&mut [T], Errno> {
        if self.len() == 0 {
            trace!("as_mut_init returning empty slice");
            return Ok(&mut [][..]);
        }

        let mut vec = Vec::<T>::with_capacity(self.len());
        // Ideally we'd use `vec.set_len` and not initialize this memory at all.
        // That can lead to some non-deterministic simulation behavior though if the caller doesn't
        // actually write to the whole buffer. So instead we zero-initialize.
        vec.resize(self.len(), pod::zeroed::<T>());

        debug_assert!(self.writable_ptr.is_none());
        self.writable_ptr = Some(vec.into_boxed_slice());

        Ok(&mut *self.writable_ptr.as_mut().unwrap())
    }

    fn copy(&mut self, offset: usize, buf: &[T]) -> Result<(), Errno> {
        self.memory_manager.write_ptr(buf, self.ptr, offset)
    }

    fn flush(&mut self) -> Result<(), Errno> {
        if self.writable_ptr.is_none() {
            return Ok(());
        }
        let mut boxed = self.writable_ptr.take().unwrap();

        self.memory_manager.write_ptr(&mut *boxed, self.ptr, 0)
    }
}

/// A reader that accesses plugin memory directly.
#[derive(Debug)]
struct MappedMemoryReader<'a, T>
where
    T: Pod,
{
    memory: &'a [T],
    ptr: TypedPluginPtr<T>,
}

impl<'a, T> MappedMemoryReader<'a, T>
where
    T: Pod,
{
    fn new(memory_manager: &'a MemoryManager, ptr: TypedPluginPtr<T>) -> Option<Self> {
        let u8ptr = ptr.cast::<u8>().unwrap();
        if let Some(mapper) = &memory_manager.memory_mapper {
            if let Some(cptr) = mapper.get_mapped_ptr(u8ptr.ptr(), u8ptr.len()) {
                let memory =
                    unsafe { std::slice::from_raw_parts::<T>(cptr as *const T, ptr.len()) };
                return Some(Self { memory, ptr });
            }
        }
        None
    }
}

impl<'a, T> MemoryReaderTrait<T> for MappedMemoryReader<'a, T>
where
    T: Pod + Debug,
{
    fn len(&self) -> usize {
        self.memory.len()
    }

    fn ref_some(&self) -> Result<&[T], Errno> {
        // We only create a MappedMemoryReader when we already know the whole
        // region is accessible.
        Ok(self.memory)
    }

    fn read_some(&self, offset: usize, buf: &mut [T]) -> Result<usize, Errno> {
        buf.copy_from_slice(&self.memory[offset..offset + buf.len()]);
        Ok(buf.len())
    }
}

/// A writer that accesses plugin memory directly.
#[derive(Debug)]
struct MappedMemoryWriter<'a, T>
where
    T: Pod,
{
    memory: &'a mut [T],
    ptr: TypedPluginPtr<T>,
}

impl<'a, T> MappedMemoryWriter<'a, T>
where
    T: Pod,
{
    fn new(memory_manager: &'a mut MemoryManager, ptr: TypedPluginPtr<T>) -> Option<Self> {
        let u8ptr = ptr.cast::<u8>().unwrap();

        if let Some(mapper) = &memory_manager.memory_mapper {
            if let Some(cptr) = mapper.get_mapped_ptr(u8ptr.ptr(), u8ptr.len()) {
                let memory =
                    unsafe { std::slice::from_raw_parts_mut::<T>(cptr as *mut T, ptr.len()) };
                return Some(Self { memory, ptr });
            }
        }
        None
    }
}

impl<'a, T> MemoryWriterTrait<T> for MappedMemoryWriter<'a, T>
where
    T: Pod + Debug,
{
    fn len(&self) -> usize {
        self.memory.len()
    }

    fn as_mut(&mut self) -> Result<&mut [T], Errno> {
        Ok(self.memory)
    }

    fn as_mut_uninit(&mut self) -> Result<&mut [T], Errno> {
        Ok(self.memory)
    }

    fn copy(&mut self, offset: usize, buf: &[T]) -> Result<(), Errno> {
        let available = self.memory.len() - offset;
        assert!(buf.len() <= available);
        self.memory[offset..offset + buf.len()].copy_from_slice(buf);
        Ok(())
    }

    fn flush(&mut self) -> Result<(), Errno> {
        Ok(())
    }
}

static HEAP_PROT: i32 = libc::PROT_READ | libc::PROT_WRITE;
static STACK_PROT: i32 = libc::PROT_READ | libc::PROT_WRITE;

#[cfg(test)]
#[test]
/// We assume throughout that we can do arithmetic on void pointers as if the size of "void" was 1.
/// While this seems like a reasonable assumption, it doesn't seem to be documented or guaranteed
/// anywhere, so we validate it:
fn test_validate_void_size() {
    assert_eq!(std::mem::size_of::<c_void>(), 1);
}

fn page_size() -> usize {
    unsafe { libc::sysconf(libc::_SC_PAGESIZE) as usize }
}

// Represents a region of plugin memory.
#[derive(Clone, Debug)]
struct Region {
    // Where the region is mapped into shadow's address space, or NULL if it isn't.
    shadow_base: *mut c_void,
    prot: i32,
    sharing: proc_maps::Sharing,
    // The *original* path. Not the path to our mem file.
    original_path: Option<proc_maps::MappingPath>,
}

#[allow(dead_code)]
fn log_regions<It: Iterator<Item = (Interval, Region)>>(level: log::Level, regions: It) {
    if log::log_enabled!(level) {
        log!(level, "MemoryManager regions:");
        for (interval, mapping) in regions {
            // Invoking the logger multiple times may cause these to be interleaved with other
            // log statements, but loggers may truncate the result if we instead formatted this
            // into one giant string.
            log!(
                level,
                "{:x}-{:x} {:?}",
                interval.start,
                interval.end,
                mapping
            );
        }
    }
}

/// Manages the address-space for a plugin process.
///
/// The MemoryMapper's primary purpose is to make plugin process's memory directly accessible to
/// Shadow. It does this by tracking what regions of program memory in the plugin are mapped to
/// what (analagous to /proc/<pid>/maps), and *remapping* parts of the plugin's address space into
/// a shared memory-file, which is also mapped into Shadow.
///
/// For the MemoryManager to maintain consistent state, and to remap regions of memory it knows how
/// to remap, Shadow must delegate handling of mman-related syscalls (such as `mmap`) to the
/// MemoryMapper via its `handle_*` methods.
#[derive(Debug)]
struct MemoryMapper {
    shm_file: ShmFile,
    regions: IntervalMap<Region>,

    misses_by_path: RefCell<HashMap<String, u32>>,

    /// The bounds of the heap. Note that before the plugin's first `brk` syscall this will be a
    /// zero-sized interval (though in the case of thread-preload that'll have already happened
    /// before we get control).
    heap: Interval,
}

/// Shared memory file into which we relocate parts of the plugin's address space.
#[derive(Debug)]
struct ShmFile {
    shm_file: File,
    shm_plugin_fd: i32,
    len: libc::off_t,
}

impl ShmFile {
    /// Allocate space in the file for the given interval.
    fn alloc(&mut self, interval: &Interval) {
        let needed_len = interval.end as libc::off_t;
        // Ensure that the file size extends through the end of the interval.
        // Unlike calling fallocate or posix_fallocate, this does not pre-reserve
        // any space. The OS will allocate the space on-demand as it's written.
        if needed_len > self.len {
            nix::unistd::ftruncate(self.shm_file.as_raw_fd(), needed_len).unwrap();
            self.len = needed_len;
        }
    }

    /// De-allocate space in the file for the given interval.
    fn dealloc(&self, interval: &Interval) {
        trace!("dealloc {:?}", interval);
        fcntl::fallocate(
            self.shm_file.as_raw_fd(),
            fcntl::FallocateFlags::FALLOC_FL_PUNCH_HOLE
                | fcntl::FallocateFlags::FALLOC_FL_KEEP_SIZE,
            interval.start as i64,
            interval.len() as i64,
        )
        .unwrap();
    }

    /// Map the given interval of the file into shadow's address space.
    fn mmap_into_shadow(&self, interval: &Interval, prot: i32) -> *mut c_void {
        unsafe {
            sys::mman::mmap(
                std::ptr::null_mut(),
                interval.len(),
                sys::mman::ProtFlags::from_bits(prot).unwrap(),
                sys::mman::MapFlags::MAP_SHARED,
                self.shm_file.as_raw_fd(),
                interval.start as i64,
            )
        }
        .unwrap()
    }

    /// Copy data from the plugin's address space into the file. `interval` must be contained within
    /// `region_interval`. It can be the whole region, but notably for the stack we only copy in
    /// the part of the stack that's already allocated and initialized.
    fn copy_into_file(
        &self,
        memory_manager: &MemoryManager,
        region_interval: &Interval,
        region: &Region,
        interval: &Interval,
    ) {
        if interval.len() == 0 {
            return;
        }
        assert!(!region.shadow_base.is_null());
        assert!(region_interval.contains(&interval.start));
        assert!(region_interval.contains(&(interval.end - 1)));
        let offset = interval.start - region_interval.start;
        let dst = unsafe {
            std::slice::from_raw_parts_mut(
                region.shadow_base.add(offset) as *mut u8,
                interval.len(),
            )
        };
        let reader = memory_manager.reader(
            TypedPluginPtr::<u8>::new(PluginPtr::from(interval.start), interval.len()).unwrap(),
        );
        reader.read_exact(dst).unwrap();
    }

    /// Map the given range of the file into the plugin's address space.
    fn mmap_into_plugin(&self, interval: &Interval, prot: i32) {
        Worker::with_active_thread_mut(|thread| {
            thread
                .native_mmap(
                    PluginPtr::from(interval.start),
                    interval.len(),
                    prot,
                    libc::MAP_SHARED | libc::MAP_FIXED,
                    self.shm_plugin_fd,
                    interval.start as i64,
                )
                .unwrap();
        });
    }
}

fn get_regions(pid: nix::unistd::Pid) -> IntervalMap<Region> {
    let mut regions = IntervalMap::new();
    for mapping in proc_maps::mappings_for_pid(pid.as_raw()).unwrap() {
        let mut prot = 0;
        if mapping.read {
            prot |= libc::PROT_READ;
        }
        if mapping.write {
            prot |= libc::PROT_WRITE;
        }
        if mapping.execute {
            prot |= libc::PROT_EXEC;
        }
        let mutations = regions.insert(
            mapping.begin..mapping.end,
            Region {
                shadow_base: std::ptr::null_mut(),
                prot,
                sharing: mapping.sharing,
                original_path: mapping.path,
            },
        );
        // Regions shouldn't overlap.
        assert_eq!(mutations.len(), 0);
    }
    regions
}

/// Find the heap range, and map it if non-empty.
fn get_heap(
    shm_file: &mut ShmFile,
    thread: &mut impl Thread,
    memory_manager: &MemoryManager,
    regions: &mut IntervalMap<Region>,
) -> Interval {
    // If there's already a region labeled heap, we use those bounds.
    let heap_mapping = {
        let mut it = regions
            .iter()
            .fuse()
            .skip_while(|m| m.1.original_path != Some(proc_maps::MappingPath::Heap));
        let heap_mapping = it.next();
        // There should only be one heap region.
        debug_assert!(
            it.filter(|m| m.1.original_path == Some(proc_maps::MappingPath::Heap))
                .count()
                == 0
        );
        heap_mapping
    };
    if heap_mapping.is_none() {
        // There's no heap region allocated yet. Get the address where it will be and return.
        let start = usize::from(thread.native_brk(PluginPtr::from(0usize)).unwrap());
        return start..start;
    }
    let (heap_interval, heap_region) = heap_mapping.unwrap();

    shm_file.alloc(&heap_interval);
    let mut heap_region = heap_region.clone();
    heap_region.shadow_base = shm_file.mmap_into_shadow(&heap_interval, HEAP_PROT);
    shm_file.copy_into_file(memory_manager, &heap_interval, &heap_region, &heap_interval);
    shm_file.mmap_into_plugin(&heap_interval, HEAP_PROT);

    {
        let mutations = regions.insert(heap_interval.clone(), heap_region);
        // Should have overwritten the old heap region and not affected any others.
        assert!(mutations.len() == 1);
    }

    heap_interval
}

/// Finds where the stack is located and maps the region bounding the maximum
/// stack size.
fn map_stack(
    memory_manager: &mut MemoryManager,
    shm_file: &mut ShmFile,
    regions: &mut IntervalMap<Region>,
) {
    // Find the current stack region. There should be exactly one.
    let mut iter = regions
        .iter()
        .filter(|(_i, r)| r.original_path == Some(MappingPath::InitialStack));
    // Get the stack region, panicking if none.
    let (current_stack_bounds, region) = iter.next().unwrap();
    // Panic if there's more than one.
    assert!(iter.next().is_none());

    // TODO: get actual max stack limit via getrlimit.
    let max_stack_size: usize = 8 * (1 << 20); // 8 MB.

    // Omit the top page of the stack so that there is still a "stack" region in
    // the process's maps. This is where the program arguments and environment
    // are stored; overwriting the region breaks /proc/*/cmdline and
    // /proc/*/environ, which are used by tools such as ps and htop.
    let remapped_stack_end = current_stack_bounds.end - page_size();

    let remapped_stack_begin = current_stack_bounds.end - max_stack_size;
    let remapped_stack_bounds = remapped_stack_begin..remapped_stack_end;
    let mut region = region.clone();
    region.shadow_base = shm_file.mmap_into_shadow(&remapped_stack_bounds, STACK_PROT);

    // Allocate as much space as we might need.
    shm_file.alloc(&remapped_stack_bounds);

    let remapped_overlaps_current = current_stack_bounds.start < remapped_stack_bounds.end;

    // Copy the current contents of the remapped part of the current stack, if any.
    if remapped_overlaps_current {
        shm_file.copy_into_file(
            memory_manager,
            &remapped_stack_bounds,
            &region,
            &(current_stack_bounds.start..remapped_stack_bounds.end),
        );
    }

    shm_file.mmap_into_plugin(&remapped_stack_bounds, STACK_PROT);

    let mutations = regions.insert(remapped_stack_bounds, region);
    if remapped_overlaps_current {
        debug_assert_eq!(mutations.len(), 1);
    } else {
        debug_assert_eq!(mutations.len(), 0);
    }
}

impl Drop for MemoryMapper {
    fn drop(&mut self) {
        let misses = self.misses_by_path.borrow();
        if misses.is_empty() {
            debug!("MemoryManager misses: None");
        } else {
            debug!("MemoryManager misses: (consider extending MemoryManager to remap regions with a high miss count)");
            for (path, count) in misses.iter() {
                debug!("\t{} in {}", count, path);
            }
        }

        // Mappings are no longer valid. Clear out our map, and unmap those regions from Shadow's
        // address space.
        let mutations = self.regions.clear(std::usize::MIN..std::usize::MAX);
        for m in mutations {
            if let Mutation::Removed(interval, region) = m {
                if !region.shadow_base.is_null() {
                    unsafe { sys::mman::munmap(region.shadow_base, interval.len()) }
                        .unwrap_or_else(|e| warn!("munmap: {}", e));
                }
            }
        }
    }
}

impl MemoryMapper {
    fn new(memory_manager: &mut MemoryManager, thread: &mut impl Thread) -> MemoryMapper {
        let shm_path = format!(
            "/dev/shm/shadow_memory_manager_{}_{}_{}",
            process::id(),
            thread.get_host_id(),
            thread.get_process_id()
        );
        let shm_file = OpenOptions::new()
            .read(true)
            .write(true)
            .create_new(true)
            .open(&shm_path)
            .unwrap();

        // We don't need the file anymore in the file system. Unlinking it now
        // ensures that it will be removed when there are no more open file
        // descriptors to it.
        match std::fs::remove_file(&shm_path) {
            Ok(_) => (),
            Err(e) => warn!("removing '{}': {}", shm_path, e),
        }

        // The file can no longer be accessed by its original path, but *can*
        // be accessed via the file-descriptor link in /proc.
        let shm_path = format!("/proc/{}/fd/{}\0", process::id(), shm_file.as_raw_fd());

        let shm_plugin_fd = {
            let path_buf_plugin_ptr = TypedPluginPtr::new(
                thread.malloc_plugin_ptr(shm_path.len()).unwrap(),
                shm_path.len(),
            )
            .unwrap();
            memory_manager
                .writer(path_buf_plugin_ptr)
                .copy(shm_path.as_bytes())
                .unwrap();
            let shm_plugin_fd = thread
                .native_open(path_buf_plugin_ptr.ptr(), libc::O_RDWR | libc::O_CLOEXEC, 0)
                .unwrap();
            thread
                .free_plugin_ptr(path_buf_plugin_ptr.ptr(), path_buf_plugin_ptr.len())
                .unwrap();
            shm_plugin_fd
        };

        let mut shm_file = ShmFile {
            shm_file,
            shm_plugin_fd,
            len: 0,
        };
        let mut regions = get_regions(memory_manager.pid);
        let heap = get_heap(&mut shm_file, thread, memory_manager, &mut regions);
        map_stack(memory_manager, &mut shm_file, &mut regions);

        MemoryMapper {
            shm_file,
            regions,
            misses_by_path: RefCell::new(HashMap::new()),
            heap,
        }
    }

    /// Processes the mutations returned by an IntervalMap::insert or IntervalMap::clear operation.
    /// Each mutation describes a mapping that has been partly or completely overwritten (in the
    /// case of an insert) or cleared (in the case of clear).
    ///
    /// Potentially:
    /// * Updates `shadow_base` on affected regions.
    /// * Deallocates space from shm_file.
    /// * Reclaims Shadow's address space via unmap.
    ///
    /// When used on mutations after an insert, if the inserted region is to be mapped into shadow,
    /// be sure to call this *before* doing that mapping; otherwise we'll end up deallocating some
    /// or all of the space in that new mapping.
    fn unmap_mutations(&mut self, mutations: Vec<Mutation<Region>>) {
        for mutation in mutations {
            match mutation {
                Mutation::ModifiedBegin(interval, new_start) => {
                    let (_, region) = self.regions.get_mut(new_start).unwrap();
                    if region.shadow_base.is_null() {
                        continue;
                    }
                    let removed_range = interval.start..new_start;

                    // Deallocate
                    self.shm_file.dealloc(&removed_range);

                    // Unmap range from Shadow's address space.
                    unsafe { sys::mman::munmap(region.shadow_base, removed_range.len()) }
                        .unwrap_or_else(|e| warn!("munmap: {}", e));

                    // Adjust base
                    region.shadow_base = unsafe { region.shadow_base.add(removed_range.len()) };
                }
                Mutation::ModifiedEnd(interval, new_end) => {
                    let (_, region) = self.regions.get(interval.start).unwrap();
                    if region.shadow_base.is_null() {
                        continue;
                    }
                    let removed_range = new_end..interval.end;

                    // Deallocate
                    self.shm_file.dealloc(&removed_range);

                    // Unmap range from Shadow's address space.
                    unsafe {
                        sys::mman::munmap(
                            region.shadow_base.add((interval.start..new_end).len()),
                            removed_range.len(),
                        )
                    }
                    .unwrap_or_else(|e| warn!("munmap: {}", e));
                }
                Mutation::Split(_original, left, right) => {
                    let (_, left_region) = self.regions.get(left.start).unwrap();
                    let (_, right_region) = self.regions.get(right.start).unwrap();
                    debug_assert_eq!(left_region.shadow_base, right_region.shadow_base);
                    if left_region.shadow_base.is_null() {
                        continue;
                    }
                    let removed_range = left.end..right.start;

                    // Deallocate
                    self.shm_file.dealloc(&removed_range);

                    // Unmap range from Shadow's address space.
                    unsafe {
                        sys::mman::munmap(
                            (left_region.shadow_base.add(left.len())) as *mut c_void,
                            removed_range.len(),
                        )
                    }
                    .unwrap_or_else(|e| warn!("munmap: {}", e));

                    // Adjust start of right region.
                    let (_, right_region) = self.regions.get_mut(right.start).unwrap();
                    right_region.shadow_base =
                        unsafe { right_region.shadow_base.add(right.start - left.start) };
                }
                Mutation::Removed(interval, region) => {
                    if region.shadow_base.is_null() {
                        continue;
                    }

                    // Deallocate
                    self.shm_file.dealloc(&interval);

                    // Unmap range from Shadow's address space.
                    unsafe { sys::mman::munmap(region.shadow_base, interval.len()) }
                        .unwrap_or_else(|e| warn!("munmap: {}", e));
                }
            }
        }
    }

    /// Shadow should delegate a plugin's call to mmap to this method.  The caller is responsible
    /// for ensuring that `fd` is open and pointing to the right file in the plugin process.
    ///
    /// Executes the actual mmap operation in the plugin, updates the MemoryManager's understanding of
    /// the plugin's address space, and in some cases remaps the given region into the
    /// MemoryManager's shared memory file for fast access. Currently only private anonymous
    /// mappings are remapped.
    #[allow(clippy::too_many_arguments)]
    fn handle_mmap(
        &mut self,
        thread: &mut impl Thread,
        addr: PluginPtr,
        length: usize,
        prot: i32,
        flags: i32,
        fd: i32,
        offset: i64,
    ) -> SyscallResult {
        let result = thread.native_mmap(addr, length, prot, flags, fd, offset)?;
        if length == 0 {
            return Ok(result.into());
        }
        let addr = usize::from(result);
        let interval = addr..(addr + length);
        let is_anonymous = flags & libc::MAP_ANONYMOUS != 0;
        let sharing = if flags & libc::MAP_PRIVATE != 0 {
            Sharing::Private
        } else {
            Sharing::Shared
        };
        let original_path = if is_anonymous {
            None
        } else {
            // Get the original path; this is a slightly roundabout way of doing it, but makes more
            // sense to eventually move the mechanics of opening the child fd into here (in which
            // case we'll already have it) than to pipe the string through this API.
            Some(MappingPath::Path(
                std::fs::read_link(format!("/proc/{}/fd/{}", thread.get_system_pid(), fd))
                    .unwrap_or_else(|_| PathBuf::from(format!("bad-fd-{}", fd))),
            ))
        };
        let mut region = Region {
            shadow_base: std::ptr::null_mut(),
            prot,
            sharing,
            original_path,
        };

        // Clear out metadata and mappings for anything that was already there.
        let mutations = self.regions.clear(interval.clone());
        self.unmap_mutations(mutations);

        if is_anonymous && sharing == Sharing::Private {
            // Overwrite the freshly mapped region with a region from the shared mem file and map
            // it. In principle we might be able to avoid doing the first mmap above in this case,
            // but doing so lets the OS decide if it's a legal mapping, and where to put it.
            self.shm_file.alloc(&interval);
            region.shadow_base = self.shm_file.mmap_into_shadow(&interval, prot);
            self.shm_file.mmap_into_plugin(&interval, prot);
        }

        // TODO: We *could* handle file mappings and some shared mappings as well. Doesn't make
        // sense to add that complexity until if/when we see a lot of misses in such regions,
        // though.

        {
            // There shouldn't be any mutations here; we already cleared a hole above.
            let mutations = self.regions.insert(interval, region);
            assert!(mutations.is_empty());
        }

        Ok(result.into())
    }

    /// Shadow should delegate a plugin's call to munmap to this method.
    ///
    /// Executes the actual mmap operation in the plugin, updates the MemoryManager's understanding of
    /// the plugin's address space, and unmaps the affected memory from Shadow if it was mapped in.
    fn handle_munmap(
        &mut self,
        thread: &mut impl Thread,
        addr: PluginPtr,
        length: usize,
    ) -> SyscallResult {
        trace!("handle_munmap({:?}, {})", addr, length);
        thread.native_munmap(addr, length)?;
        if length == 0 {
            return Ok(0.into());
        }

        // Clear out metadata and mappings for anything unmapped.
        let start = usize::from(addr);
        let end = start + length;
        let mutations = self.regions.clear(start..end);
        self.unmap_mutations(mutations);

        Ok(0.into())
    }

    /// Shadow should delegate a plugin's call to mremap to this method.
    ///
    /// Executes the actual mremap operation in the plugin, updates the MemoryManager's
    /// understanding of the plugin's address space, and updates Shadow's mappings of that region
    /// if applicable.
    fn handle_mremap(
        &mut self,
        thread: &mut impl Thread,
        old_address: PluginPtr,
        old_size: usize,
        new_size: usize,
        flags: i32,
        new_address: PluginPtr,
    ) -> SyscallResult {
        let new_address =
            thread.native_mremap(old_address, old_size, new_size, flags, new_address)?;
        let old_interval = usize::from(old_address)..(usize::from(old_address) + old_size);
        let new_interval = usize::from(new_address)..(usize::from(new_address) + new_size);

        // From mremap(2):
        // If the value of old_size is zero, and old_address refers to a shareable mapping (see
        // mmap(2) MAP_SHARED), then mremap() will create a new mapping of the same pages.  new_size
        // will be the size of the new mapping and the location of the new  mapping  may  be
        // specified with new_address; see the description of MREMAP_FIXED below.  If a new mapping
        // is requested via this method, then the MREMAP_MAYMOVE flag must also be specified.
        if (flags & libc::MREMAP_MAYMOVE) != 0 && old_size == 0 {
            let region = {
                let (_, region) = self.regions.get(usize::from(old_address)).unwrap();
                region.clone()
            };
            assert_eq!(region.sharing, Sharing::Shared);
            // This shouldn't be mapped into Shadow, since we don't support remapping shared mappings into Shadow yet.
            assert_eq!(region.shadow_base, std::ptr::null_mut());
            let mutations = self.regions.insert(new_interval, region);
            self.unmap_mutations(mutations);
            return Ok(new_address.into());
        }

        // Clear and retrieve the old mapping.
        // For the remap to have succeeded, it should have corresponded exactly to an old mapping.
        let mut region = {
            let mut mutations = self.regions.clear(old_interval.clone());
            assert_eq!(mutations.len(), 1);
            if let Some(Mutation::Removed(removed_interval, region)) = mutations.pop() {
                assert_eq!(removed_interval, old_interval);
                region
            } else {
                panic!("Unexpected mutation {:?}", mutations[0])
            }
        };

        // Clear any mappings that are about to be overwritten by the new mapping. We have to do
        // this *before* potentially mapping the new region into Shadow, so that we don't end up
        // freeing space for that new mapping.
        {
            let mutations = self.regions.clear(new_interval.clone());
            self.unmap_mutations(mutations);
        }

        if !region.shadow_base.is_null() {
            // We currently only map in anonymous mmap'd regions, stack, and heap.  We don't bother
            // implementing mremap for stack or heap regions for now; that'd be pretty weird.
            assert_eq!(region.original_path, None);

            if new_interval.start != old_interval.start {
                // region has moved

                // Note that mremap(2) should have failed if the regions overlap.
                assert!(!new_interval.contains(&old_interval.start));
                assert!(!old_interval.contains(&new_interval.start));

                // Ensure there's space allocated at the new location in the memory file.
                self.shm_file.alloc(&new_interval);

                // Remap the region in the child to the new position in the mem file.
                self.shm_file.mmap_into_plugin(&new_interval, region.prot);

                // Map the new location into Shadow.
                let new_shadow_base = self.shm_file.mmap_into_shadow(&new_interval, region.prot);

                // Copy the data.
                unsafe {
                    libc::memcpy(
                        new_shadow_base,
                        region.shadow_base,
                        std::cmp::min(old_size, new_size),
                    )
                };

                // Unmap the old location from Shadow.
                unsafe { sys::mman::munmap(region.shadow_base, old_size) }
                    .unwrap_or_else(|e| warn!("munmap: {}", e));

                // Update the region metadata.
                region.shadow_base = new_shadow_base;

                // Deallocate the old location.
                self.shm_file.dealloc(&old_interval);
            } else if new_size < old_size {
                // Deallocate the part no longer in use.
                self.shm_file.dealloc(&(new_interval.end..old_interval.end));

                // Shrink Shadow's mapping.
                // TODO: use nix wrapper once it exists. https://github.com/nix-rust/nix/issues/1295
                assert_ne!(
                    unsafe { libc::mremap(region.shadow_base, old_size, new_size, 0) },
                    libc::MAP_FAILED
                );
            } else if new_size > old_size {
                // Allocate space in the file.
                self.shm_file.alloc(&new_interval);

                // Grow Shadow's mapping into the memory file, allowing the mapping to move if
                // needed.
                // TODO: use nix wrapper once it exists. https://github.com/nix-rust/nix/issues/1295
                region.shadow_base = unsafe {
                    libc::mremap(region.shadow_base, old_size, new_size, libc::MREMAP_MAYMOVE)
                };
                assert_ne!(region.shadow_base, libc::MAP_FAILED);
            }
        }
        // Insert the new mapping. There shouldn't be any mutations since we already cleared
        // this interval, above.
        let mutations = self.regions.insert(new_interval, region);
        assert_eq!(mutations.len(), 0);

        Ok(new_address.into())
    }

    /// Execute the requested `brk` and update our mappings accordingly. May invalidate outstanding
    /// pointers. (Rust won't allow mutable methods such as this one to be called with outstanding
    /// borrowed references).
    fn handle_brk(&mut self, thread: &mut impl Thread, ptr: PluginPtr) -> SyscallResult {
        let requested_brk = usize::from(ptr);

        // On error, brk syscall returns current brk (end of heap). The only errors we specifically
        // handle is trying to set the end of heap before the start. In practice this case is
        // generally triggered with a NULL argument to get the current brk value.
        if requested_brk < self.heap.start {
            return Ok(PluginPtr::from(self.heap.end).into());
        }

        // Unclear how to handle a non-page-size increment. panic for now.
        assert!(requested_brk % page_size() == 0);

        // Not aware of this happening in practice, but handle this case specifically so we can
        // assume it's not the case below.
        if requested_brk == self.heap.end {
            return Ok(ptr.into());
        }

        let opt_heap_interval_and_region = self.regions.get(self.heap.start);
        let new_heap = self.heap.start..requested_brk;

        if requested_brk > self.heap.end {
            // Grow the heap.
            let shadow_base = match opt_heap_interval_and_region {
                None => {
                    // Initialize heap region.
                    assert_eq!(self.heap.start, self.heap.end);
                    self.shm_file.alloc(&new_heap);
                    let shadow_base = self.shm_file.mmap_into_shadow(&new_heap, HEAP_PROT);
                    self.shm_file.mmap_into_plugin(&new_heap, HEAP_PROT);
                    shadow_base
                }
                Some((_, heap_region)) => {
                    // Grow heap region.
                    self.shm_file.alloc(&self.heap);
                    // mremap in plugin, enforcing that base stays the same.
                    thread
                        .native_mremap(
                            /* old_addr: */ PluginPtr::from(self.heap.start),
                            /* old_len: */ self.heap.end - self.heap.start,
                            /* new_len: */ new_heap.end - new_heap.start,
                            /* flags: */ 0,
                            /* new_addr: */ PluginPtr::from(0usize),
                        )
                        .unwrap();
                    // mremap in shadow, allowing mapping to move if needed.
                    // TODO: use nix wrapper once it exists. https://github.com/nix-rust/nix/issues/1295
                    let shadow_base = unsafe {
                        libc::mremap(
                            /* old_addr: */ heap_region.shadow_base,
                            /* old_len: */ self.heap.end - self.heap.start,
                            /* new_len: */ new_heap.end - new_heap.start,
                            /* flags: */ libc::MREMAP_MAYMOVE,
                        )
                    };
                    assert_ne!(shadow_base as i32, -1);
                    shadow_base
                }
            };
            self.regions.insert(
                new_heap.clone(),
                Region {
                    shadow_base,
                    prot: HEAP_PROT,
                    sharing: Sharing::Private,
                    original_path: Some(MappingPath::Heap),
                },
            );
        } else {
            // Shrink the heap
            if new_heap.start == new_heap.end {
                // Reducing heap to size zero unhandled.
                unimplemented!();
            }
            // handle shrink
            let (_, heap_region) = opt_heap_interval_and_region.unwrap();

            // mremap in plugin, enforcing that base stays the same.
            thread
                .native_mremap(
                    /* old_addr: */ PluginPtr::from(self.heap.start),
                    /* old_len: */ self.heap.len(),
                    /* new_len: */ new_heap.len(),
                    /* flags: */ 0,
                    /* new_addr: */ PluginPtr::from(0usize),
                )
                .unwrap();
            // mremap in shadow, assuming no need to move.
            // TODO: use nix wrapper once it exists. https://github.com/nix-rust/nix/issues/1295
            let shadow_base = unsafe {
                libc::mremap(
                    /* old_addr: */ heap_region.shadow_base,
                    /* old_len: */ self.heap.len(),
                    /* new_len: */ new_heap.len(),
                    /* flags: */ 0,
                )
            };
            assert_eq!(shadow_base, heap_region.shadow_base);
            self.regions.clear(new_heap.end..self.heap.end);
            self.shm_file.dealloc(&(new_heap.end..self.heap.end));
        }
        self.heap = new_heap;

        Ok(PluginPtr::from(requested_brk).into())
    }

    /// Shadow should delegate a plugin's call to mprotect to this method.
    ///
    /// Executes the actual mprotect operation in the plugin, updates the MemoryManager's
    /// understanding of the plugin's address space, and updates Shadow's mappings of that region
    /// if applicable.
    ///
    /// Alternatively when Shadow maps a region it would always map everything to be both readable
    /// and writeable by Shadow, in which case we wouldn't need to worry about updating Shadow's
    /// protections when the plugin calls mprotect. However, mirroring the plugin's protection
    /// settings can help catch bugs earlier. Shadow should never have reason to access plugin
    /// memory in a way that the plugin itself can't.
    fn handle_mprotect(
        &mut self,
        thread: &mut impl Thread,
        addr: PluginPtr,
        size: usize,
        prot: i32,
    ) -> SyscallResult {
        trace!("mprotect({:?}, {}, {:?})", addr, size, prot);
        thread.native_mprotect(addr, size, prot)?;
        let protflags = sys::mman::ProtFlags::from_bits(prot).unwrap();

        // Update protections. We remove the affected range, and then update and re-insert affected
        // regions.
        let mutations = self
            .regions
            .clear(usize::from(addr)..(usize::from(addr) + size));
        for mutation in mutations {
            match mutation {
                Mutation::ModifiedBegin(interval, new_start) => {
                    // Modified prot of beginning of region.
                    let (_extant_interval, extant_region) =
                        self.regions.get_mut(new_start).unwrap();
                    let modified_interval = interval.start..new_start;
                    let mut modified_region = extant_region.clone();
                    modified_region.prot = prot;
                    // Update shadow_base if applicable.
                    if !extant_region.shadow_base.is_null() {
                        extant_region.shadow_base =
                            unsafe { extant_region.shadow_base.add(modified_interval.len()) };
                        unsafe {
                            sys::mman::mprotect(
                                modified_region.shadow_base,
                                modified_interval.len(),
                                protflags,
                            )
                        }
                        .unwrap_or_else(|e| {
                            warn!(
                                "mprotect({:?}, {:?}, {:?}): {}",
                                modified_region.shadow_base,
                                modified_interval.len(),
                                protflags,
                                e
                            );
                        });
                    }
                    // Reinsert region with updated prot.
                    assert!(self
                        .regions
                        .insert(modified_interval, modified_region)
                        .is_empty());
                }
                Mutation::ModifiedEnd(interval, new_end) => {
                    // Modified prot of end of region.
                    let (extant_interval, extant_region) =
                        self.regions.get_mut(new_end - 1).unwrap();
                    let modified_interval = new_end..interval.end;
                    let mut modified_region = extant_region.clone();
                    modified_region.prot = prot;
                    if !modified_region.shadow_base.is_null() {
                        modified_region.shadow_base =
                            unsafe { modified_region.shadow_base.add(extant_interval.len()) };
                        unsafe {
                            sys::mman::mprotect(
                                modified_region.shadow_base,
                                modified_interval.len(),
                                protflags,
                            )
                        }
                        .unwrap_or_else(|e| warn!("mprotect: {}", e));
                    }
                    assert!(self
                        .regions
                        .insert(modified_interval, modified_region)
                        .is_empty());
                }
                Mutation::Split(_original, left_interval, right_interval) => {
                    let right_region = self.regions.get_mut(right_interval.start).unwrap().1;
                    let modified_interval = left_interval.end..right_interval.start;
                    let mut modified_region = right_region.clone();
                    modified_region.prot = prot;
                    if !modified_region.shadow_base.is_null() {
                        modified_region.shadow_base =
                            unsafe { modified_region.shadow_base.add(left_interval.len()) };
                        right_region.shadow_base = unsafe {
                            right_region
                                .shadow_base
                                .add(left_interval.len() + modified_interval.len())
                        };
                        unsafe {
                            sys::mman::mprotect(
                                modified_region.shadow_base,
                                modified_interval.len(),
                                protflags,
                            )
                        }
                        .unwrap_or_else(|e| warn!("mprotect: {}", e));
                    }
                    assert!(self
                        .regions
                        .insert(modified_interval, modified_region)
                        .is_empty());
                }
                Mutation::Removed(modified_interval, mut modified_region) => {
                    modified_region.prot = prot;
                    if !modified_region.shadow_base.is_null() {
                        unsafe {
                            sys::mman::mprotect(
                                modified_region.shadow_base,
                                modified_interval.len(),
                                protflags,
                            )
                        }
                        .unwrap_or_else(|e| warn!("mprotect: {}", e));
                    }
                    assert!(self
                        .regions
                        .insert(modified_interval, modified_region)
                        .is_empty());
                }
            }
        }
        Ok(0.into())
    }

    /// Get a raw pointer to the plugin's memory, if it's been remapped into Shadow.
    fn get_mapped_ptr_int(&self, src: PluginPtr, n: usize) -> Option<*mut c_void> {
        if n == 0 {
            // Length zero pointer should never be deref'd. Just return null.
            warn!("returning NULL for zero-length pointer");
            return Some(std::ptr::null_mut());
        }

        let src = usize::from(src);
        let opt_interval_and_region = self.regions.get(src);
        if opt_interval_and_region.is_none() {
            warn!("src {:x} isn't in any mapped region", src);
            return None;
        }
        let (interval, region) = opt_interval_and_region.unwrap();
        if region.shadow_base.is_null() {
            // region isn't mapped into shadow
            return None;
        }
        if !interval.contains(&(src + n - 1)) {
            // End isn't in the region.
            return None;
        }

        let offset = src - interval.start;
        let ptr = (region.shadow_base as usize + offset) as *mut c_void;

        Some(ptr)
    }

    /// Get a raw pointer to the plugin's memory, if it's been remapped into Shadow.
    fn get_mapped_ptr(&self, src: PluginPtr, n: usize) -> Option<*mut c_void> {
        let res = self.get_mapped_ptr_int(src, n);
        if res.is_none() {
            self.inc_misses(src)
        }
        res
    }

    /// Counts accesses where we had to fall back to the thread's (slow) apis.
    fn inc_misses(&self, addr: PluginPtr) {
        let key = match self.regions.get(usize::from(addr)) {
            Some((_, original_path)) => format!("{:?}", original_path),
            None => "not found".to_string(),
        };
        let mut misses = self.misses_by_path.borrow_mut();
        let counter = misses.entry(key).or_insert(0);
        *counter += 1;
    }
}

/// Manages memory of a plugin process.
#[derive(Debug)]
pub struct MemoryManager {
    // Native pid of the plugin process.
    pid: nix::unistd::Pid,
    // Lazily initialized MemoryMapper.
    memory_mapper: Option<MemoryMapper>,
}

impl MemoryManager {
    pub fn new(pid: nix::unistd::Pid) -> Self {
        Self {
            pid,
            memory_mapper: None,
        }
    }

    /// Which process's address space this MemoryManager manages.
    pub fn pid(&self) -> nix::unistd::Pid {
        self.pid
    }

    /// Initialize the MemoryMapper, allowing for more efficient access. Needs a
    /// running thread.
    pub fn init_mapper(&mut self, thread: &mut impl Thread) {
        assert!(self.memory_mapper.is_none());
        self.memory_mapper = Some(MemoryMapper::new(self, thread));
    }

    /// Whether the internal MemoryMapper has been initialized.
    pub fn has_mapper(&self) -> bool {
        self.memory_mapper.is_some()
    }

    /// Create a read accessor for the specified plugin memory.
    pub fn reader<'a, T>(&'a self, ptr: TypedPluginPtr<T>) -> MemoryReader<'a, T>
    where
        T: Pod + Debug,
    {
        let reader_trait: Box<dyn MemoryReaderTrait<T> + 'a> =
            if let Some(mapped_reader) = MappedMemoryReader::new(self, ptr) {
                Box::new(mapped_reader)
            } else {
                Box::new(CopyingMemoryReader::new(self, ptr))
            };
        MemoryReader::new(reader_trait)
    }

    /// Create a write accessor for the specified plugin memory.
    pub fn writer<'a, T>(&'a mut self, ptr: TypedPluginPtr<T>) -> MemoryWriter<'a, T>
    where
        T: Pod + Debug,
    {
        let writer_trait: Box<dyn MemoryWriterTrait<T> + 'a> =
            if let Some(_) = MappedMemoryWriter::new(self, ptr) {
                // If we directly return the contents of the `Option` created in the
                // outer scope, the borrow-checker gets confused and doesn't realize
                // the first mutable borrow of `self` has gone out of scope when we
                // try to do another mutable borrow in the `else` clause. Returning
                // a borrowed reference that was created in this inner scope makes
                // it happy.
                //
                //  Alternatively we could store a pointer to `self` at the beginning
                //  of this function, and dereference it to create the
                //  CopyingMemoryWriter.
                // https://play.rust-lang.org/?version=stable&mode=debug&edition=2018&gist=3de34f0eee732c998e05f27b1a55f3cd
                Box::new(MappedMemoryWriter::new(self, ptr).unwrap())
            } else {
                Box::new(CopyingMemoryWriter::new(self, ptr))
            };
        MemoryWriter::new(writer_trait)
    }

    fn handle_brk(&mut self, thread: &mut impl Thread, ptr: PluginPtr) -> SyscallResult {
        match &mut self.memory_mapper {
            Some(mm) => mm.handle_brk(thread, ptr),
            None => Err(SyscallError::Native),
        }
    }

    #[allow(clippy::too_many_arguments)]
    fn handle_mmap(
        &mut self,
        thread: &mut impl Thread,
        addr: PluginPtr,
        length: usize,
        prot: i32,
        flags: i32,
        fd: i32,
        offset: i64,
    ) -> SyscallResult {
        match &mut self.memory_mapper {
            Some(mm) => mm.handle_mmap(thread, addr, length, prot, flags, fd, offset),
            None => Err(SyscallError::Native),
        }
    }

    fn handle_munmap(
        &mut self,
        thread: &mut impl Thread,
        addr: PluginPtr,
        length: usize,
    ) -> SyscallResult {
        match &mut self.memory_mapper {
            Some(mm) => mm.handle_munmap(thread, addr, length),
            None => Err(SyscallError::Native),
        }
    }

    fn handle_mremap(
        &mut self,
        thread: &mut impl Thread,
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
        thread: &mut impl Thread,
        addr: PluginPtr,
        size: usize,
        prot: i32,
    ) -> SyscallResult {
        match &mut self.memory_mapper {
            Some(mm) => mm.handle_mprotect(thread, addr, size, prot),
            None => Err(SyscallError::Native),
        }
    }

    // Read as much of `ptr` as is accessible into `buf`.
    fn read_some<T: Debug + Pod>(
        &self,
        buf: &mut [T],
        ptr: TypedPluginPtr<T>,
    ) -> Result<usize, Errno> {
        // Convert to u8
        let mut buf = pod::to_u8_slice_mut(buf);
        let ptr = ptr.cast::<u8>().unwrap();

        // Split at page boundaries to allow partial reads.
        let mut slices = Vec::with_capacity((buf.len() + page_size() - 1) / page_size() + 1);
        let mut total_bytes_toread = std::cmp::min(buf.len(), ptr.len());

        // First chunk to read is from pointer to beginning of next page.
        let prev_page_boundary = usize::from(ptr.ptr()) / page_size() * page_size();
        let next_page_boundary = prev_page_boundary + page_size();
        let mut next_bytes_toread = std::cmp::min(
            next_page_boundary - usize::from(ptr.ptr()),
            total_bytes_toread,
        );

        while next_bytes_toread > 0 {
            // Add the next chunk to read.
            let (prefix, suffix) = buf.split_at_mut(next_bytes_toread);
            buf = suffix;
            slices.push(prefix);
            total_bytes_toread -= next_bytes_toread;

            // Reads should now be page-aligned. Read a whole page at a time,
            // up to however much is left.
            next_bytes_toread = std::cmp::min(total_bytes_toread, page_size());
        }
        let bytes_read = self.readv_ptrs(&mut slices, &[ptr])?;
        Ok(bytes_read / std::mem::size_of::<T>())
    }

    // Read exactly enough to fill `buf`, or fail.
    fn read_exact<T: Debug + Pod>(
        &self,
        buf: &mut [T],
        ptr: TypedPluginPtr<T>,
    ) -> Result<(), Errno> {
        let buf = pod::to_u8_slice_mut(buf);
        let ptr = ptr.cast::<u8>().unwrap();
        let bytes_read = self.readv_ptrs(&mut [buf], &[ptr])?;
        if bytes_read != buf.len() {
            warn!(
                "Tried to read {} bytes but only got {}",
                buf.len(),
                bytes_read
            );
            return Err(Errno::EFAULT);
        }
        Ok(())
    }

    // Low level helper for reading directly from `srcs` to `dsts`.
    // Returns the number of bytes read. Panics if the
    // MemoryManager's process isn't currently active.
    fn readv_ptrs(
        &self,
        dsts: &mut [&mut [u8]],
        srcs: &[TypedPluginPtr<u8>],
    ) -> Result<usize, Errno> {
        let srcs: Vec<_> = srcs
            .iter()
            .map(|src| nix::sys::uio::RemoteIoVec {
                base: usize::from(src.ptr()),
                len: src.len(),
            })
            .collect();
        let dsts: Vec<_> = dsts
            .iter_mut()
            .map(|dst: &mut &mut [u8]| -> nix::sys::uio::IoVec<&mut [u8]> {
                nix::sys::uio::IoVec::from_mut_slice(*dst)
            })
            .collect();

        self.readv_iovecs(&dsts, &srcs)
    }

    // Low level helper for reading directly from `srcs` to `dsts`.
    // Returns the number of bytes read. Panics if the
    // MemoryManager's process isn't currently active.
    fn readv_iovecs(
        &self,
        dsts: &[nix::sys::uio::IoVec<&mut [u8]>],
        srcs: &[nix::sys::uio::RemoteIoVec],
    ) -> Result<usize, Errno> {
        trace!(
            "Reading from srcs of len {}",
            srcs.iter().map(|s| s.len).sum::<usize>()
        );
        trace!(
            "Reading to dsts of len {}",
            dsts.iter().map(|d| d.as_slice().len()).sum::<usize>()
        );

        // While the documentation for process_vm_readv says to use the pid, in
        // practice it needs to be the tid of a still-running thread. i.e. using the
        // pid after the thread group leader has exited will fail.
        let (active_tid, active_pid) = Worker::with_active_thread(|thread| {
            (
                nix::unistd::Pid::from_raw(thread.get_system_tid()),
                nix::unistd::Pid::from_raw(thread.get_system_pid()),
            )
        });
        // Don't access another process's memory.
        assert_eq!(active_pid, self.pid);

        let nread = nix::sys::uio::process_vm_readv(active_tid, &dsts, &srcs)
            .map_err(|e| e.as_errno().unwrap())?;

        Ok(nread)
    }

    // Low level helper for writing directly to `dst`. Panics if the
    // MemoryManager's process isn't currently active.
    fn write_ptr<T: Pod + Debug>(
        &mut self,
        src: &[T],
        dst: TypedPluginPtr<T>,
        offset: usize,
    ) -> Result<(), Errno> {
        let dst = dst.cast::<u8>().unwrap();
        let src = pod::to_u8_slice(src);
        let offset = offset * std::mem::size_of::<T>();
        assert!(src.len() <= dst.len() - offset);

        let towrite = src.len();
        trace!("write_ptr writing {} bytes", towrite);
        let local = [nix::sys::uio::IoVec::from_slice(src)];
        let remote = [nix::sys::uio::RemoteIoVec {
            base: usize::from(dst.ptr()) + offset,
            len: towrite,
        }];

        // While the documentation for process_vm_writev says to use the pid, in
        // practice it needs to be the tid of a still-running thread. i.e. using the
        // pid after the thread group leader has exited will fail.
        let (active_tid, active_pid) = Worker::with_active_thread(|thread| {
            (
                nix::unistd::Pid::from_raw(thread.get_system_tid()),
                nix::unistd::Pid::from_raw(thread.get_system_pid()),
            )
        });
        // Don't access another process's memory.
        assert_eq!(active_pid, self.pid);

        let nwritten = nix::sys::uio::process_vm_writev(active_tid, &local, &remote)
            .map_err(|e| e.as_errno().unwrap())?;
        // There shouldn't be any partial writes with a single remote iovec.
        assert_eq!(nwritten, towrite);
        Ok(())
    }
}

/// Memory allocated by Shadow, in a remote address space.
pub struct AllocdMem<T>
where
    T: Pod,
{
    ptr: TypedPluginPtr<T>,
}

impl<T> AllocdMem<T>
where
    T: Pod,
{
    /// Allocate memory in the current active process.
    pub fn new(len: usize) -> Self {
        let bytes_len = len * std::mem::size_of::<T>();
        let prot = libc::PROT_READ | libc::PROT_WRITE;

        // Allocate the memory in the plugin of the active thread.  We don't
        // bother with trying to mmap it into Shadow as well; since the memory
        // will typically only be accessed once by Shadow (to write to it),
        // doing so isn't worth the overhead or complexity.
        let ptr = Worker::with_active_thread_mut(|thread| {
            thread
                .native_mmap(
                    PluginPtr::from(0usize),
                    bytes_len,
                    prot,
                    libc::MAP_ANONYMOUS | libc::MAP_PRIVATE,
                    -1,
                    0,
                )
                .unwrap()
        });
        // Add region to known mappings.
        Worker::with_active_process_memory_mut(|mem| {
            if let Some(mapper) = &mut mem.memory_mapper {
                let base = usize::from(ptr);
                let mutations = mapper.regions.insert(
                    base..(base + bytes_len),
                    Region {
                        shadow_base: std::ptr::null_mut(),
                        prot,
                        sharing: Sharing::Private,
                        original_path: None,
                    },
                );
                // Shouldn't have overwritten any previous known mappings.
                debug_assert_eq!(mutations.len(), 0);
            }
        });
        Self {
            ptr: TypedPluginPtr::<T>::new(ptr, len).unwrap(),
        }
    }

    /// Pointer to the allocated memory.
    pub fn ptr(&self) -> TypedPluginPtr<T> {
        self.ptr
    }
}

impl<T> Drop for AllocdMem<T>
where
    T: Pod,
{
    fn drop(&mut self) {
        Worker::with_active_thread_mut(|thread| {
            thread
                .native_munmap(self.ptr.ptr(), self.ptr.len())
                .unwrap()
        });
        // Add region to known mappings.
        Worker::with_active_process_memory_mut(|mem| {
            if let Some(mapper) = &mut mem.memory_mapper {
                let base = usize::from(self.ptr.ptr());
                let bytes_len = self.ptr.len() * std::mem::size_of::<T>();
                let mutations = mapper.regions.clear(base..(base + bytes_len));
                // Should've dropped exactly one entry.
                debug_assert_eq!(mutations.len(), 1);
            }
        });
    }
}

mod export {
    use super::*;

    /// # Safety
    /// * `thread` must point to a valid object.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_new(pid: libc::pid_t) -> *mut MemoryManager {
        Box::into_raw(Box::new(MemoryManager::new(nix::unistd::Pid::from_raw(
            pid,
        ))))
    }

    /// # Safety
    /// * `mm` must point to a valid object.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_free(mm: *mut MemoryManager) {
        mm.as_mut().map(|mm| Box::from_raw(mm));
    }

    #[no_mangle]
    pub unsafe extern "C" fn allocdmem_new(len: usize) -> *mut AllocdMem<u8> {
        Box::into_raw(Box::new(AllocdMem::new(len)))
    }

    #[no_mangle]
    pub unsafe extern "C" fn allocdmem_free(allocd_mem: *mut AllocdMem<u8>) {
        allocd_mem
            .as_mut()
            .map(|allocd_mem| Box::from_raw(allocd_mem));
    }

    #[no_mangle]
    pub unsafe extern "C" fn allocdmem_pluginPtr(allocd_mem: *const AllocdMem<u8>) -> c::PluginPtr {
        allocd_mem.as_ref().unwrap().ptr().ptr().into()
    }

    /// Initialize the MemoryMapper if it isn't already initialized. `thread` must
    /// be running and ready to make native syscalls.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_initMapperIfNeeded(
        memory_manager: *mut MemoryManager,
        thread: *mut c::Thread,
    ) {
        let memory_manager = memory_manager.as_mut().unwrap();
        if !memory_manager.has_mapper() {
            let mut thread = CThread::new(thread);
            memory_manager.init_mapper(&mut thread)
        }
    }

    /// Get a read-accessor to the specified plugin memory.
    /// Must be freed via `memorymanager_freeReader`.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_getReader<'a>(
        memory_manager: *mut MemoryManager,
        plugin_src: c::PluginPtr,
        n: usize,
    ) -> *mut MemoryReader<'a, u8> {
        let memory_manager = memory_manager.as_mut().unwrap();
        let plugin_src: PluginPtr = plugin_src.into();
        Box::into_raw(Box::new(
            memory_manager.reader(TypedPluginPtr::new(plugin_src.into(), n).unwrap()),
        ))
    }

    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_freeReader<'a>(reader: *mut MemoryReader<'a, u8>) {
        Box::from_raw(reader);
    }

    /// Get a pointer to this reader's memory.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_getReadablePtr<'a>(
        reader: *mut MemoryReader<'a, u8>,
    ) -> *const c_void {
        debug_assert!(!reader.is_null());
        let reader = &*reader;
        match reader.ref_exact() {
            Ok(p) => p.as_ptr() as *const c_void,
            Err(_) => std::ptr::null(),
        }
    }

    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_getReadableString<'a>(
        reader: *const MemoryReader<'a, u8>,
        str: *mut *const libc::c_char,
        strlen: *mut libc::size_t,
    ) -> i32 {
        debug_assert!(!reader.is_null());
        debug_assert!(!str.is_null());
        let reader = &*reader;
        let cstr = match reader.ref_string() {
            Ok(c) => c,
            Err(e) => {
                return -(e as i32);
            }
        };
        *str = cstr.as_ptr();
        if !strlen.is_null() {
            *strlen = cstr.to_bytes().len();
        }
        0
    }

    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_readString<'a>(
        reader: *const MemoryReader<'a, u8>,
        str: *mut libc::c_char,
        strlen: libc::size_t,
    ) -> libc::ssize_t {
        debug_assert!(!reader.is_null());
        debug_assert!(!str.is_null());
        let reader = &*reader;
        let dst = std::slice::from_raw_parts_mut(str as *mut u8, strlen);
        match reader.read_string(dst) {
            Ok(n) => libc::ssize_t::try_from(n).unwrap_or(-(Errno::ENAMETOOLONG as libc::ssize_t)),
            Err(e) => return -(e as libc::ssize_t),
        }
    }

    /// Copy data from this reader's memory.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_readPtr(
        memory_manager: *mut MemoryManager,
        dst: *mut c_void,
        src: c::PluginPtr,
        n: usize,
    ) -> i32 {
        let memory_manager = memory_manager.as_mut().unwrap();
        let src = TypedPluginPtr::<u8>::new(src.into(), n).unwrap();
        let dst = std::slice::from_raw_parts_mut(dst as *mut u8, n);
        match memory_manager.reader(src).read_some(dst) {
            Ok(_) => 0,
            Err(_) => {
                trace!("Couldn't read {:?} into {:?}", src, dst);
                nix::errno::Errno::EFAULT as i32
            }
        }
    }

    /// Get a write-accessor to the specified plugin memory.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_getWriter<'a>(
        memory_manager: *mut MemoryManager,
        plugin_src: c::PluginPtr,
        n: usize,
    ) -> *mut MemoryWriter<'a, u8> {
        let memory_manager = memory_manager.as_mut().unwrap();
        let plugin_src: PluginPtr = plugin_src.into();
        let rv = Box::into_raw(Box::new(
            memory_manager.writer(TypedPluginPtr::new(plugin_src.into(), n).unwrap()),
        ));
        rv
    }

    /// Write-back any previously returned writable memory, and free the writer.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_flushAndFreeWriter<'a>(
        writer: *mut MemoryWriter<'a, u8>,
    ) -> i32 {
        let mut writer = Box::from_raw(writer);
        // No way to safely recover here if the flush fails.
        if writer.flush().is_ok() {
            0
        } else {
            warn!("Failed to flush writes");
            -(nix::errno::Errno::EFAULT as i32)
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
        let memory_manager = memory_manager.as_mut().unwrap();
        let dst = TypedPluginPtr::<u8>::new(dst.into(), n).unwrap();
        let src = std::slice::from_raw_parts(src as *const u8, n);
        match memory_manager.writer(dst).copy(src) {
            Ok(_) => 0,
            Err(_) => {
                trace!("Couldn't write {:?} into {:?}", dst, src);
                nix::errno::Errno::EFAULT as i32
            }
        }
    }

    /// Get a writable pointer to this writer's memory. Initial contents are unspecified.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_getWritablePtr<'a>(
        writer: *mut MemoryWriter<'a, u8>,
    ) -> *mut c_void {
        let writer = &mut *writer;
        match writer.as_mut_uninit() {
            Ok(p) => p.as_ptr() as *mut c_void,
            Err(_) => std::ptr::null_mut(),
        }
    }

    /// Get a readable and writable pointer to this writer's memory.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_getMutablePtr<'a>(
        writer: *mut MemoryWriter<'a, u8>,
    ) -> *mut c_void {
        let writer = &mut *writer;
        match writer.as_mut() {
            Ok(p) => p.as_ptr() as *mut c_void,
            Err(_) => std::ptr::null_mut(),
        }
    }

    /// Fully handles the `brk` syscall, keeping the "heap" mapped in our shared mem file.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_handleBrk(
        memory_manager: *mut MemoryManager,
        thread: *mut c::Thread,
        plugin_src: c::PluginPtr,
    ) -> c::SysCallReturn {
        let memory_manager = memory_manager.as_mut().unwrap();
        let mut thread = CThread::new(thread);
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
        let memory_manager = memory_manager.as_mut().unwrap();
        let mut thread = CThread::new(thread);
        memory_manager
            .handle_mmap(
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
        let memory_manager = memory_manager.as_mut().unwrap();
        let mut thread = CThread::new(thread);
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
        let memory_manager = memory_manager.as_mut().unwrap();
        let mut thread = CThread::new(thread);
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
        let memory_manager = memory_manager.as_mut().unwrap();
        let mut thread = CThread::new(thread);
        memory_manager
            .handle_mprotect(&mut thread, PluginPtr::from(addr), size, prot)
            .into()
    }
}
