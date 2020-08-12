#![allow(unused)]

use super::syscall_types::*;
use super::thread::{CThread, Thread};
use crate::cbindings as c;
use crate::utility::interval_map::{Interval, IntervalMap, Mutation};
use crate::utility::proc_maps;
use crate::utility::proc_maps::{MappingPath, Sharing};
use std::cell::{Cell, Ref, RefCell};
use std::collections::HashMap;
use std::fs::File;
use std::fs::OpenOptions;
use std::os::raw::c_void;
use std::os::unix::io::AsRawFd;
use std::process;

static HEAP_PROT: i32 = libc::PROT_READ | libc::PROT_WRITE;
static STACK_PROT: i32 = libc::PROT_READ | libc::PROT_WRITE;

fn page_size() -> usize {
    unsafe { libc::sysconf(libc::_SC_PAGESIZE) as usize }
}

/// TODO: why doesn't `cargo test` run doc tests?
/// ```
/// let ps = page_size();
/// assert_eq!(page_of(0), 0);
/// assert_eq!(page_of(page_size()-1), 0);
/// assert_eq!(page_of(page_size()), page_size());
/// assert_eq!(page_of(2*page_size()), 2*page_size());
/// assert_eq!(page_of(2*page_size())-1, page_size());
/// ```
fn page_of(p: usize) -> usize {
    p & !(page_size() - 1)
}

// Represents a region of plugin memory.
#[derive(Clone, Debug)]
struct Region {
    // Where the region is mapped into shadow's address space, or NULL if it isn't.
    shadow_base: *mut c_void,
    read: bool,
    write: bool,
    execute: bool,
    sharing: proc_maps::Sharing,
    // The *original* path. Not the path to our mem file.
    original_path: Option<proc_maps::MappingPath>,
}

/// Manages the address-space for a plugin process.
pub struct MemoryManager {
    shm_file: RefCell<ShmFile>,
    regions: RefCell<IntervalMap<Region>>,
    host_id: u32,
    pid: u32,
    misses_by_path: RefCell<HashMap<String, u32>>,

    // We need to reinitialize some state after an exec, but need to wait until the next syscall.
    need_post_exec_cleanup: Cell<bool>,

    // The part of the stack that we've already remapped in the plugin.
    // We initially mmap enough *address space* in Shadow to accomodate a large stack, but we only
    // lazily allocate it. This is both to prevent wasting memory, and to handle that we can't map
    // the current "working area" of the stack in thread-preload.
    stack_copied: RefCell<Interval>,

    // The bounds of the heap. Note that before the plugin's first `brk` syscall this will be a
    // zero-sized interval (though in the case of thread-preload that'll have already happened
    // before we get control).
    heap: RefCell<Interval>,
}

// Shared memory file into which we relocate parts of the plugin's address space.
struct ShmFile {
    shm_path: String,
    shm_file: File,
    shm_plugin_fd: i32,
}

impl Drop for ShmFile {
    fn drop(&mut self) {
        std::fs::remove_file(&self.shm_path);
    }
}

impl ShmFile {
    // Allocate space in the file for the given interval.
    fn alloc(&self, interval: &Interval) {
        let res = unsafe {
            libc::posix_fallocate(
                self.shm_file.as_raw_fd(),
                interval.start as i64,
                (interval.end - interval.start) as i64,
            )
        };
        assert!(res == 0);
    }

    // De-allocate space in the file for the given interval.
    fn dealloc(&self, interval: &Interval) {
        let res = unsafe {
            libc::fallocate(
                self.shm_file.as_raw_fd(),
                libc::FALLOC_FL_PUNCH_HOLE & libc::FALLOC_FL_KEEP_SIZE,
                interval.start as i64,
                (interval.end - interval.start) as i64,
            )
        };
        assert!(res == 0);
    }

    // Map the given interval of the file into shadow's address space.
    fn mmap_into_shadow(&self, interval: &Interval, prot: i32) -> *mut c_void {
        let res = unsafe {
            libc::mmap(
                std::ptr::null_mut(),
                interval.end - interval.start,
                prot,
                libc::MAP_SHARED,
                self.shm_file.as_raw_fd(),
                interval.start as i64,
            )
        };
        assert!(res as i64 != -1);
        res
    }

    // Copy data from the plugin's address space into the file. `interval` must be contained within
    // `region_interval`. It can be the whole region, but notably for the stack we only copy in
    // parts of the region as needed.
    fn copy_into_file(
        &self,
        thread: &impl Thread,
        region_interval: &Interval,
        region: &Region,
        interval: &Interval,
    ) {
        assert!(region.shadow_base != std::ptr::null_mut());
        assert!(region_interval.contains(&interval.start));
        let size = interval.end - interval.start;
        if size == 0 {
            return;
        }
        assert!(region_interval.contains(&(interval.end - 1)));
        let offset = interval.start - region_interval.start;
        let src = unsafe {
            std::slice::from_raw_parts(
                thread
                    .get_readable_ptr(PluginPtr::from(interval.start), size)
                    .unwrap() as *const u8,
                size,
            )
        };
        let dst = unsafe {
            std::slice::from_raw_parts_mut((region.shadow_base as usize + offset) as *mut u8, size)
        };
        dst.copy_from_slice(src);
    }

    // Map the given range of the file into the plugin's address space.
    fn mmap_into_plugin(&self, thread: &impl Thread, interval: &Interval, prot: i32) {
        let res = unsafe {
            thread.native_mmap(
                PluginPtr::from(interval.start),
                (interval.end - interval.start),
                prot,
                libc::MAP_SHARED | libc::MAP_FIXED,
                self.shm_plugin_fd,
                interval.start as i64,
            )
        };
        assert!(res.is_ok());
    }

    fn truncate(&mut self) {
        let res = unsafe { libc::ftruncate(self.shm_file.as_raw_fd(), 0) };
        if res != 0 {
            println!("Warning: ftruncate failed");
        }
    }
}

impl Drop for MemoryManager {
    fn drop(&mut self) {
        let misses_by_path = self.misses_by_path.borrow();
        println!("MemoryManager misses (consider extending MemoryManager to remap regions with a high miss count)");
        for (path, count) in misses_by_path.iter() {
            println!("\t{} in {}", count, path);
        }
        drop(misses_by_path);
        self.reset_and_unmap_all();
    }
}

fn get_regions(pid: libc::pid_t) -> IntervalMap<Region> {
    let mut regions = IntervalMap::new();
    for mapping in proc_maps::mappings_for_pid(pid).unwrap() {
        let mutations = regions.insert(
            mapping.begin..mapping.end,
            Region {
                shadow_base: std::ptr::null_mut(),
                read: mapping.read,
                write: mapping.write,
                execute: mapping.execute,
                sharing: mapping.sharing,
                original_path: mapping.path,
            },
        );
        // Regions shouldn't overlap.
        assert_eq!(mutations.len(), 0);
    }
    // Useful for debugging.
    /*
    println!("regions");
    for (interval, mapping) in regions.iter() {
        println!("{:?} {:?}", interval, mapping);
    }
    */
    regions
}

// Find the heap range, and map it if non-empty.
fn get_heap(
    shm_file: &ShmFile,
    thread: &impl Thread,
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
    let heap_size = heap_interval.end - heap_interval.start;

    shm_file.alloc(&heap_interval);
    let mut heap_region = heap_region.clone();
    heap_region.shadow_base = shm_file.mmap_into_shadow(&heap_interval, HEAP_PROT);
    shm_file.copy_into_file(thread, &heap_interval, &heap_region, &heap_interval);
    shm_file.mmap_into_plugin(thread, &heap_interval, HEAP_PROT);

    {
        let mutations = regions.insert(heap_interval.clone(), heap_region);
        // Should have overwritten the old heap region and not affected any others.
        assert!(mutations.len() == 1);
    }

    heap_interval
}

// Finds where the stack is located and reserves space in shadow's address space for the maximum
// size to which the stack can grow. *Doesn't* reserve space in the shared memory file; this is
// done on-demand as it grows and is accessed.
fn map_stack(shm_file: &ShmFile, regions: &mut IntervalMap<Region>) -> usize {
    // Find the current stack region. There should be exactly one.
    let mut iter = regions
        .iter()
        .filter(|(i, r)| r.original_path == Some(MappingPath::InitialStack));
    // Get the stack region, panicking if none.
    let (interval, region) = iter.next().unwrap();
    // Panic if there's more than one.
    assert!(iter.next().is_none());

    // TODO: get actual max stack limit via getrlimit.
    // This is the lowest address where there *could eventually* be stack allocated.
    // We reserve enough address-space in Shadow to accomodate it so that we don't have to worry
    // about resizing/moving it later, but we don't actually allocate any memory in the shared mem
    // file yet.
    let max_stack_size: usize = 8 * (1 << 20); // 8 MB.

    let stack_end = interval.end;
    let stack_begin = stack_end - max_stack_size;
    let stack_bounds = stack_begin..stack_end;
    let mut region = region.clone();
    region.shadow_base = shm_file.mmap_into_shadow(&stack_bounds, STACK_PROT);

    {
        let mutations = regions.insert(stack_begin..stack_begin + max_stack_size, region);
        // Should have overwritten the old stack region and not affected any others.
        assert!(mutations.len() == 1);
    }

    stack_end
}

impl MemoryManager {
    pub fn new(thread: &mut impl Thread, host_id: u32, pid: u32) -> MemoryManager {
        let system_pid = thread.get_system_pid();

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

        let path_buf_len = page_size();
        let path_buf_plugin_ptr: PluginPtr = thread.malloc_plugin_ptr(path_buf_len).unwrap();

        let shm_plugin_fd = {
            let path_buf_len = shm_path.len() + 1;
            let path_buf_raw: *mut c_void = unsafe {
                thread.get_writeable_ptr(
                    path_buf_plugin_ptr,
                    std::mem::size_of::<u8>() * path_buf_len,
                )
            }
            .unwrap();
            let path_buf: &mut [u8] =
                unsafe { std::slice::from_raw_parts_mut(path_buf_raw as *mut u8, path_buf_len) };
            path_buf[..shm_path.len()].copy_from_slice(shm_path.as_bytes());
            path_buf[shm_path.len()] = '\0' as u8;
            thread.flush();
            thread
                .native_open(path_buf_plugin_ptr, libc::O_RDWR, 0)
                .unwrap()
        };

        let shm_file = ShmFile {
            shm_path,
            shm_file,
            shm_plugin_fd,
        };
        let mut regions = get_regions(system_pid);
        let heap = get_heap(&shm_file, thread, &mut regions);
        let stack_end = map_stack(&shm_file, &mut regions);

        MemoryManager {
            shm_file: RefCell::new(shm_file),
            regions: RefCell::new(regions),
            host_id,
            pid,
            misses_by_path: RefCell::new(HashMap::new()),
            need_post_exec_cleanup: Cell::new(false),
            heap: RefCell::new(heap),
            stack_copied: RefCell::new(stack_end..stack_end),
        }
    }

    // Clears all mappings, unmapping them from shadow's address space and recovering space from
    // the memory file. e.g. called after execve and in Drop.
    fn reset_and_unmap_all(&self) {
        // Mappings are no longer valid. Clear out our map, and unmap those regions.
        let mutations = self
            .regions
            .borrow_mut()
            .clear(std::usize::MIN..std::usize::MAX);
        for m in mutations {
            match m {
                Mutation::Removed(interval, region) => {
                    if region.shadow_base != std::ptr::null_mut() {
                        let res = unsafe {
                            libc::munmap(region.shadow_base, interval.end - interval.start)
                        };
                        if res != 0 {
                            println!("Warning: munmap failed");
                        }
                    }
                }
                _ => (),
            }
        }
        self.shm_file.borrow_mut().truncate();
    }

    /// Should be called by shadow's SysCallHandler before allowing the plugin to execute the exec
    /// syscall.
    pub fn post_exec_hook(&mut self, thread: &impl Thread) {
        self.need_post_exec_cleanup.set(true);
    }

    // Called internally on the next usage *after* execve syscall has executed. Re-initializes as
    // needed.
    fn post_exec_cleanup_if_needed(&self, thread: &impl Thread) {
        if !self.need_post_exec_cleanup.get() {
            return;
        }
        self.reset_and_unmap_all();
        let shm_file = self.shm_file.borrow();
        *self.regions.borrow_mut() = get_regions(thread.get_system_pid());
        *self.heap.borrow_mut() = get_heap(&*shm_file, thread, &mut *self.regions.borrow_mut());
        let stack_end = map_stack(&*shm_file, &mut *self.regions.borrow_mut());
        *self.stack_copied.borrow_mut() = stack_end..stack_end;
        self.need_post_exec_cleanup.set(false);
    }

    /// Gets a reference to an object in the plugin's memory.
    /// SAFETY
    /// * The pointer must point to readable value of type T.
    /// * Returned ref mustn't be accessed after Thread runs again or flush is called.
    pub unsafe fn get_ref<T>(&self, thread: &impl Thread, src: PluginPtr) -> Result<&T, i32> {
        let raw = self.get_readable_ptr(thread, src, std::mem::size_of::<T>())?;
        Ok(&*(raw as *const T))
    }

    /// Gets a mutable reference to an object in the plugin's memory.
    /// SAFETY
    /// * The pointer must point to a writeable value of type T.
    /// * Returned ref mustn't be accessed after Thread runs again or flush is called.
    pub unsafe fn get_mut_ref<T>(
        &self,
        thread: &impl Thread,
        src: PluginPtr,
    ) -> Result<&mut T, i32> {
        let raw = self.get_writeable_ptr(thread, src, std::mem::size_of::<T>())?;
        Ok(&mut *(raw as *mut T))
    }

    /// Gets a slice of the plugin's memory.
    /// SAFETY
    /// * The pointer must point to a readable array of type T and at least size `len`.
    /// * Returned slice mustn't be accessed after Thread runs again or flush is called.
    unsafe fn get_slice<T>(
        &self,
        thread: &impl Thread,
        src: PluginPtr,
        len: usize,
    ) -> Result<&[T], i32> {
        let raw = self.get_readable_ptr(thread, src, std::mem::size_of::<T>() * len)?;
        Ok(std::slice::from_raw_parts(raw as *const T, len))
    }

    /// Gets a mutable slice of the plugin's memory.
    /// SAFETY
    /// * The pointer must point to a writeable array of type T and at least size `len`.
    /// * Returned slice mustn't be accessed after Thread runs again or flush is called.
    unsafe fn get_mut_slice<T>(
        &self,
        thread: &impl Thread,
        src: PluginPtr,
        len: usize,
    ) -> Result<&mut [T], i32> {
        let raw = self.get_writeable_ptr(thread, src, std::mem::size_of::<T>() * len)?;
        Ok(std::slice::from_raw_parts_mut(raw as *mut T, len))
    }

    /// We take a mutable reference here to ensure there are no outstanding borrowed
    /// references/pointers, which might otherwise become invalidated if shadow's mapping of the
    /// heap region gets moved.
    pub fn handle_brk(
        &mut self,
        thread: &mut impl Thread,
        ptr: PluginPtr,
    ) -> Result<PluginPtr, i32> {
        self.post_exec_cleanup_if_needed(thread);
        let mut heap = self.heap.borrow_mut();
        let mut regions = self.regions.borrow_mut();
        let requested_brk = usize::from(ptr);

        // On error, brk syscall returns current brk (end of heap). The only errors we specifically
        // handle is trying to set the end of heap before the start. In practice this case is
        // generally triggered with a NULL argument to get the current brk value.
        if requested_brk < heap.start {
            return Ok(PluginPtr::from(heap.end));
        }

        // Unclear how to handle a non-page-size increment. panic for now.
        assert!(requested_brk % page_size() == 0);

        // Not aware of this happening in practice, but handle this case specifically so we can
        // assume it's not the case below.
        if requested_brk == heap.end {
            return Ok(ptr);
        }

        let opt_heap_interval_and_region = regions.get(heap.start);
        let new_heap = heap.start..requested_brk;

        // handle growth
        if requested_brk > heap.end {
            // Grow the heap.
            let shadow_base = match opt_heap_interval_and_region {
                None => {
                    // Initialize heap region.
                    assert_eq!(heap.start, heap.end);
                    self.shm_file.borrow().alloc(&new_heap);
                    let shadow_base = self
                        .shm_file
                        .borrow()
                        .mmap_into_shadow(&new_heap, HEAP_PROT);
                    // No data to copy.
                    self.shm_file
                        .borrow()
                        .mmap_into_plugin(thread, &new_heap, HEAP_PROT);
                    shadow_base
                }
                Some((heap_interval, heap_region)) => {
                    // Grow heap region.
                    self.shm_file.borrow().alloc(&heap);
                    // mremap in plugin, enforcing that base stays the same.
                    unsafe {
                        let res = thread.native_mremap(
                            /* old_addr: */ PluginPtr::from(heap.start),
                            /* old_len: */ heap.end - heap.start,
                            /* new_len: */ new_heap.end - new_heap.start,
                            /* flags: */ 0,
                            /* new_addr: */ PluginPtr::from(0usize),
                        );
                        assert!(res.is_ok());
                    };
                    // mremap in shadow, allowing mapping to move if needed.
                    let shadow_base = unsafe {
                        libc::mremap(
                            /* old_addr: */ heap_region.shadow_base,
                            /* old_len: */ heap.end - heap.start,
                            /* new_len: */ new_heap.end - new_heap.start,
                            /* flags: */ libc::MREMAP_MAYMOVE,
                        )
                    };
                    assert_ne!(shadow_base as i32, -1);
                    shadow_base
                }
            };
            regions.insert(
                new_heap.clone(),
                Region {
                    shadow_base,
                    read: true,
                    write: true,
                    execute: false,
                    sharing: Sharing::Private,
                    original_path: Some(MappingPath::Heap),
                },
            );
        } else {
            if new_heap.start == new_heap.end {
                // Reducing heap to size zero unhandled.
                unimplemented!();
            }
            // handle shrink
            let (heap_interval, heap_region) = opt_heap_interval_and_region.unwrap();

            // mremap in plugin, enforcing that base stays the same.
            unsafe {
                let res = thread.native_mremap(
                    /* old_addr: */ PluginPtr::from(heap.start),
                    /* old_len: */ heap.end - heap.start,
                    /* new_len: */ new_heap.end - new_heap.start,
                    /* flags: */ 0,
                    /* new_addr: */ PluginPtr::from(0usize),
                );
                assert!(res.is_ok());
            };
            // mremap in shadow, assuming no need to move.
            let shadow_base = unsafe {
                libc::mremap(
                    /* old_addr: */ heap_region.shadow_base,
                    /* old_len: */ heap.end - heap.start,
                    /* new_len: */ new_heap.end - new_heap.start,
                    /* flags: */ 0,
                )
            };
            assert_eq!(shadow_base, heap_region.shadow_base);
            regions.clear(new_heap.end..heap.end);
            self.shm_file.borrow().dealloc(&(new_heap.end..heap.end));
        }
        *heap = new_heap;

        Ok(PluginPtr::from(requested_brk))
    }

    // Extend the portion of the stack that we've mapped downward to include `src`.
    fn extend_stack(&self, thread: &impl Thread, src: usize) {
        let regions = self.regions.borrow();
        let mut stack_copied = self.stack_copied.borrow_mut();

        let start = page_of(src);
        let end = stack_copied.end;
        let stack_extension = start..stack_copied.start;
        //println!("jdn extending stack from {:x} to {:x}", stack_copied.start, start);

        let (mapped_stack_interval, mapped_stack_region) =
            regions.get(stack_copied.start - 1).unwrap();
        assert!(mapped_stack_interval.contains(&src));

        self.shm_file.borrow().alloc(&stack_extension);
        self.shm_file.borrow().copy_into_file(
            thread,
            &mapped_stack_interval,
            &mapped_stack_region,
            &stack_extension,
        );

        // update stack bounds
        stack_copied.start = start;

        // Map into the Plugin's space, overwriting any previous mapping.
        self.shm_file
            .borrow()
            .mmap_into_plugin(thread, &stack_copied, STACK_PROT);
    }

    // Get a raw pointer to the plugin's memory, if we have it mapped (or can do so now).
    fn get_mapped_ptr(
        &self,
        thread: &impl Thread,
        src: PluginPtr,
        n: usize,
    ) -> Option<*mut c_void> {
        self.post_exec_cleanup_if_needed(thread);
        if n == 0 {
            // Length zero pointer should never be deref'd. Just return null.
            // TODO: warn?
            return Some(std::ptr::null_mut());
        }

        let src = usize::from(src);
        let regions = self.regions.borrow();
        let opt_interval_and_region = regions.get(src.into());
        if opt_interval_and_region.is_none() {
            // src isn't in any mapped region. TODO: warn?
            return None;
        }
        let (interval, region) = opt_interval_and_region.unwrap();
        if region.shadow_base == std::ptr::null_mut() {
            // region isn't mapped into shadow
            return None;
        }
        if !interval.contains(&(src + n - 1)) {
            // End isn't in the region.
            return None;
        }
        if region.original_path == Some(MappingPath::InitialStack)
            && !self.stack_copied.borrow().contains(&src)
        {
            // src is in the stack, but in the portion we haven't mapped into shadow yet. Extend
            // it.
            self.extend_stack(thread, src);
        }
        let offset = src - interval.start;
        Some((region.shadow_base as usize + offset) as *mut c_void)
    }

    // Counts accesses where we had to fall back to the thread's (slow) apis.
    fn inc_misses(&self, thread: &impl Thread, addr: PluginPtr) {
        let key = match self.regions.borrow().get(usize::from(addr)) {
            Some((_, original_path)) => format!("{:?}", original_path),
            None => "not found".to_string(),
        };
        let mut misses_by_path = self.misses_by_path.borrow_mut();
        let counter = misses_by_path.entry(key).or_insert(0);
        *counter += 1;
    }

    // Get a readable pointer to the plugin's memory via mapping, or via the thread APIs.
    unsafe fn get_readable_ptr(
        &self,
        thread: &impl Thread,
        plugin_src: PluginPtr,
        n: usize,
    ) -> Result<*const c_void, i32> {
        if let Some(p) = self.get_mapped_ptr(thread, plugin_src, n) {
            Ok(p)
        } else {
            // Fall back to reading via the thread.
            self.inc_misses(thread, plugin_src);
            thread.get_readable_ptr(plugin_src, n)
        }
    }

    // Get a writeable pointer to the plugin's memory via mapping, or via the thread APIs.
    unsafe fn get_writeable_ptr(
        &self,
        thread: &impl Thread,
        plugin_src: PluginPtr,
        n: usize,
    ) -> Result<*mut c_void, i32> {
        if let Some(p) = self.get_mapped_ptr(thread, plugin_src, n) {
            Ok(p)
        } else {
            self.inc_misses(thread, plugin_src);
            thread.get_writeable_ptr(plugin_src, n)
        }
    }
}

mod export {
    use super::*;

    #[no_mangle]
    pub extern "C" fn memorymanager_new(
        thread: *mut c::Thread,
        host_id: u32,
        process_id: u32,
    ) -> *mut MemoryManager {
        Box::into_raw(Box::new(MemoryManager::new(
            &mut CThread::new(thread),
            host_id,
            process_id,
        )))
    }

    #[no_mangle]
    pub extern "C" fn memorymanager_free(mm: *mut MemoryManager) {
        if mm.is_null() {
            return;
        }
        unsafe { Box::from_raw(mm) };
    }

    /// Get a readable pointer to the plugin's memory via mapping, or via the thread APIs.
    #[no_mangle]
    pub extern "C" fn memorymanager_getReadablePtr(
        memory_manager: *mut MemoryManager,
        thread: *mut c::Thread,
        plugin_src: c::PluginPtr,
        n: usize,
    ) -> *const c_void {
        let thread = CThread::new(thread);
        let memory_manager = unsafe { &*memory_manager };
        let plugin_src: PluginPtr = plugin_src.into();
        unsafe { memory_manager.get_readable_ptr(&thread, plugin_src, n) }.unwrap()
    }

    /// Get a writeagble pointer to the plugin's memory via mapping, or via the thread APIs.
    #[no_mangle]
    pub extern "C" fn memorymanager_getWriteablePtr(
        memory_manager: *mut MemoryManager,
        thread: *mut c::Thread,
        plugin_src: c::PluginPtr,
        n: usize,
    ) -> *mut c_void {
        let thread = CThread::new(thread);
        let memory_manager = unsafe { &*memory_manager };
        let plugin_src: PluginPtr = plugin_src.into();
        unsafe { memory_manager.get_writeable_ptr(&thread, plugin_src, n) }.unwrap()
    }

    /// Notifies memorymanager that plugin is about to call execve.
    #[no_mangle]
    pub extern "C" fn memorymanager_postExecHook(
        memory_manager: *mut MemoryManager,
        thread: *mut c::Thread,
    ) {
        let memory_manager = unsafe { &mut *memory_manager };
        let thread = CThread::new(thread);
        memory_manager.post_exec_hook(&thread);
    }

    /// Fully handles the `brk` syscall, keeping the "heap" mapped in our shared mem file.
    #[no_mangle]
    pub extern "C" fn memorymanager_handleBrk(
        memory_manager: *mut MemoryManager,
        thread: *mut c::Thread,
        plugin_src: c::PluginPtr,
    ) -> c::SysCallReg {
        let memory_manager = unsafe { &mut *memory_manager };
        let mut thread = CThread::new(thread);
        c::SysCallReg::from(
            match memory_manager.handle_brk(&mut thread, PluginPtr::from(plugin_src)) {
                Ok(p) => SysCallReg::from(p),
                // negative errno
                Err(e) => SysCallReg::from(-e),
            },
        )
    }
}
