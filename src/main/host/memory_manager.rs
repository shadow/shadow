use super::syscall_types::{PluginPtr, SysCallReg};
use super::thread::{CThread, Thread};
use crate::cbindings as c;
use crate::utility::interval_map::{Interval, IntervalMap, Mutation};
use crate::utility::proc_maps;
use crate::utility::proc_maps::{MappingPath, Sharing};
use log::{debug, info, warn};
use nix::sys::mman;
use std::collections::HashMap;
use std::fs::File;
use std::fs::OpenOptions;
use std::os::raw::c_void;
use std::os::unix::io::AsRawFd;
use std::path::PathBuf;
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
    prot: i32,
    sharing: proc_maps::Sharing,
    // The *original* path. Not the path to our mem file.
    original_path: Option<proc_maps::MappingPath>,
}

/// Manages the address-space for a plugin process.
pub struct MemoryManager {
    shm_file: ShmFile,
    regions: IntervalMap<Region>,

    misses_by_path: HashMap<String, u32>,

    // The part of the stack that we've already remapped in the plugin.
    // We initially mmap enough *address space* in Shadow to accomodate a large stack, but we only
    // lazily allocate it. This is both to prevent wasting memory, and to handle that we can't map
    // the current "working area" of the stack in thread-preload.
    stack_copied: Interval,

    // The bounds of the heap. Note that before the plugin's first `brk` syscall this will be a
    // zero-sized interval (though in the case of thread-preload that'll have already happened
    // before we get control).
    heap: Interval,
}

// Shared memory file into which we relocate parts of the plugin's address space.
struct ShmFile {
    shm_file: File,
    shm_plugin_fd: i32,
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
        assert!(res != libc::MAP_FAILED);
        res
    }

    // Copy data from the plugin's address space into the file. `interval` must be contained within
    // `region_interval`. It can be the whole region, but notably for the stack we only copy in
    // parts of the region as needed.
    fn copy_into_file(
        &self,
        thread: &mut impl Thread,
        region_interval: &Interval,
        region: &Region,
        interval: &Interval,
    ) {
        assert!(!region.shadow_base.is_null());
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
    fn mmap_into_plugin(&self, thread: &mut impl Thread, interval: &Interval, prot: i32) {
        let res = thread.native_mmap(
            PluginPtr::from(interval.start),
            interval.end - interval.start,
            prot,
            libc::MAP_SHARED | libc::MAP_FIXED,
            self.shm_plugin_fd,
            interval.start as i64,
        );
        assert!(res.is_ok());
    }
}

fn get_regions(pid: libc::pid_t) -> IntervalMap<Region> {
    let mut regions = IntervalMap::new();
    for mapping in proc_maps::mappings_for_pid(pid).unwrap() {
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
    thread: &mut impl Thread,
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
        .filter(|(_i, r)| r.original_path == Some(MappingPath::InitialStack));
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

impl Drop for MemoryManager {
    fn drop(&mut self) {
        {
            info!("MemoryManager misses (consider extending MemoryManager to remap regions with a high miss count)");
            for (path, count) in self.misses_by_path.iter() {
                info!("\t{} in {}", count, path);
            }
        }
        // Useful for debugging
        /*
        println!("MemoryManager regions");
        let regions = self.regions.borrow();
        for (interval, mapping) in regions.iter() {
            println!("{:?} {:?}", interval, mapping);
        }
        drop(regions);
        */

        // Mappings are no longer valid. Clear out our map, and unmap those regions.
        let mutations = self.regions.clear(std::usize::MIN..std::usize::MAX);
        for m in mutations {
            if let Mutation::Removed(interval, region) = m {
                if !region.shadow_base.is_null() {
                    let res =
                        unsafe { libc::munmap(region.shadow_base, interval.end - interval.start) };
                    if res != 0 {
                        warn!("munmap failed");
                    }
                }
            }
        }
    }
}

impl MemoryManager {
    pub fn new(thread: &mut impl Thread) -> MemoryManager {
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

        let shm_plugin_fd = {
            let path_buf_len = shm_path.len() + 1;
            let path_buf_plugin_ptr: PluginPtr = thread.malloc_plugin_ptr(path_buf_len).unwrap();
            let path_buf_raw: *mut c_void = thread
                .get_writeable_ptr(
                    path_buf_plugin_ptr,
                    std::mem::size_of::<u8>() * path_buf_len,
                )
                .unwrap();
            let path_buf: &mut [u8] =
                unsafe { std::slice::from_raw_parts_mut(path_buf_raw as *mut u8, path_buf_len) };
            path_buf[..shm_path.len()].copy_from_slice(shm_path.as_bytes());
            path_buf[shm_path.len()] = b'\0';
            thread.flush();
            let shm_plugin_fd = thread
                .native_open(path_buf_plugin_ptr, libc::O_RDWR | libc::O_CLOEXEC, 0)
                .unwrap();
            thread
                .free_plugin_ptr(path_buf_plugin_ptr, path_buf_len)
                .unwrap();
            shm_plugin_fd
        };

        // We don't need the file anymore in the file system.
        match std::fs::remove_file(&shm_path) {
            Ok(_) => (),
            Err(e) => warn!("removing '{}': {}", shm_path, e),
        }

        let shm_file = ShmFile {
            shm_file,
            shm_plugin_fd,
        };
        let mut regions = get_regions(system_pid);
        let heap = get_heap(&shm_file, thread, &mut regions);
        let stack_end = map_stack(&shm_file, &mut regions);

        MemoryManager {
            shm_file,
            regions,
            misses_by_path: HashMap::new(),
            heap,
            stack_copied: stack_end..stack_end,
        }
    }

    // Processes the mutations returned by an IntervalMap::insert or IntervalMap::clear operation.
    // Each mutation describes a mapping that has been partly or completely overwritten (in the
    // case of an insert) or cleared (in the case of clear).
    //
    // Potentially:
    // * Updates `shadow_base` on affected regions.
    // * Deallocates space from shm_file.
    // * Reclaims Shadow's address space via unmap.
    //
    // When used on mutations after an insert, if the inserted region is to be mapped into shadow,
    // be sure to call this *before* doing that mapping; otherwise we'll end up some or all of the
    // space in that new mapping.
    fn handle_mutations(&mut self, mutations: Vec<Mutation<Region>>) {
        for mutation in mutations {
            match mutation {
                Mutation::ModifiedBegin(interval, new_start) => {
                    let (_, region) = self.regions.get_mut(new_start).unwrap();
                    if region.shadow_base.is_null() {
                        continue;
                    }
                    let removed_range = interval.start..new_start;
                    let removed_size = removed_range.end - removed_range.start;

                    // Deallocate
                    self.shm_file.dealloc(&removed_range);

                    // Unmap range from Shadow's address space.
                    unsafe { libc::munmap(region.shadow_base, removed_size) };

                    // Adjust base
                    region.shadow_base =
                        ((region.shadow_base as usize) + removed_size) as *mut c_void;
                }
                Mutation::ModifiedEnd(interval, new_end) => {
                    let (_, region) = self.regions.get(interval.start).unwrap();
                    if region.shadow_base.is_null() {
                        continue;
                    }
                    let removed_range = new_end..interval.end;
                    let removed_size = removed_range.end - removed_range.start;

                    // Deallocate
                    self.shm_file.dealloc(&removed_range);

                    // Unmap range from Shadow's address space.
                    unsafe { libc::munmap(region.shadow_base, removed_size) };
                }
                Mutation::Split(_original, left, right) => {
                    let (_, left_region) = self.regions.get(left.start).unwrap();
                    let (_, right_region) = self.regions.get(right.start).unwrap();
                    debug_assert_eq!(left_region.shadow_base, right_region.shadow_base);
                    if left_region.shadow_base.is_null() {
                        continue;
                    }
                    let removed_range = left.end..right.start;
                    let removed_size = removed_range.end - removed_range.start;

                    // Deallocate
                    self.shm_file.dealloc(&removed_range);

                    // Unmap range from Shadow's address space.
                    unsafe {
                        libc::munmap(
                            (left_region.shadow_base as usize + left.end) as *mut c_void,
                            removed_size,
                        )
                    };

                    // Adjust start of right region.
                    let (_, right_region) = self.regions.get_mut(right.start).unwrap();
                    right_region.shadow_base = ((right_region.shadow_base as usize)
                        + (right.start - left.start))
                        as *mut c_void;
                }
                Mutation::Removed(interval, region) => {
                    if region.shadow_base.is_null() {
                        continue;
                    }

                    // Deallocate
                    self.shm_file.dealloc(&interval);

                    // Unmap range from Shadow's address space.
                    unsafe { libc::munmap(region.shadow_base, interval.end - interval.start) };
                }
            }
        }
    }

    /// Gets a reference to an object in the plugin's memory.
    /// SAFETY
    /// * The pointer must point to readable value of type T.
    /// * Returned ref mustn't be accessed after Thread runs again or flush is called.
    #[allow(dead_code)]
    pub unsafe fn get_ref<T>(
        &mut self,
        thread: &mut impl Thread,
        src: PluginPtr,
    ) -> Result<&T, i32> {
        let raw = self.get_readable_ptr(thread, src, std::mem::size_of::<T>())?;
        Ok(&*(raw as *const T))
    }

    /// Gets a mutable reference to an object in the plugin's memory.
    /// SAFETY
    /// * The pointer must point to a writeable value of type T.
    /// * Returned ref mustn't be accessed after Thread runs again or flush is called.
    // TODO: Consider tracking borrowed memory ranges so that we can safely allow mutable borrowed
    // references, with a run-time check to validate that they don't overlap with other borrows.
    // OTOH doing so would require mutating another interval map on every borrow and return, so
    // it could be a performance hit.
    #[allow(dead_code)]
    pub unsafe fn get_mut_ref<T>(
        &mut self,
        thread: &mut impl Thread,
        src: PluginPtr,
    ) -> Result<&mut T, i32> {
        let raw = self.get_writeable_ptr(thread, src, std::mem::size_of::<T>())?;
        Ok(&mut *(raw as *mut T))
    }

    /// Gets a slice of the plugin's memory.
    /// SAFETY
    /// * The pointer must point to a readable array of type T and at least size `len`.
    /// * Returned slice mustn't be accessed after Thread runs again or flush is called.
    #[allow(dead_code)]
    pub unsafe fn get_slice<T>(
        &mut self,
        thread: &mut impl Thread,
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
    // TODO: Consider tracking borrowed memory ranges so that we can safely allow mutable borrowed
    // references, with a run-time check to validate that they don't overlap with other borrows.
    // OTOH doing so would require mutating another interval map on every borrow and return, so
    // it could be a performance hit.
    #[allow(dead_code)]
    pub unsafe fn get_mut_slice<T>(
        &mut self,
        thread: &mut impl Thread,
        src: PluginPtr,
        len: usize,
    ) -> Result<&mut [T], i32> {
        let raw = self.get_writeable_ptr(thread, src, std::mem::size_of::<T>() * len)?;
        Ok(std::slice::from_raw_parts_mut(raw as *mut T, len))
    }

    #[allow(clippy::too_many_arguments)]
    pub fn handle_mmap(
        &mut self,
        thread: &mut impl Thread,
        addr: PluginPtr,
        length: usize,
        prot: i32,
        flags: i32,
        fd: i32,
        offset: i64,
    ) -> Result<PluginPtr, i32> {
        let result = thread.native_mmap(addr, length, prot, flags, fd, offset)?;
        if length == 0 {
            return Ok(result);
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
        self.handle_mutations(mutations);

        if is_anonymous && sharing == Sharing::Private {
            // Overwrite the freshly mapped region with a region from the shared mem file and map
            // it. In principle we might be able to avoid doing the first mmap above in this case,
            // but doing so lets the OS decide if it's a legal mapping, and where to put it.
            self.shm_file.alloc(&interval);
            region.shadow_base = self.shm_file.mmap_into_shadow(&interval, prot);
            self.shm_file.mmap_into_plugin(thread, &interval, prot);
        }

        // TODO: We *could* handle file mappings and some shared mappings as well. Doesn't make
        // sense to add that complexity until if/when we see a lot of misses in such regions,
        // though.

        {
            // There shouldn't be any mutations here; we already cleared a hole above.
            let mutations = self.regions.insert(interval, region);
            assert!(mutations.is_empty());
        }

        Ok(result)
    }

    pub fn handle_munmap(
        &mut self,
        thread: &mut impl Thread,
        addr: PluginPtr,
        length: usize,
    ) -> Result<(), i32> {
        thread.native_munmap(addr, length)?;
        if length == 0 {
            return Ok(());
        }

        // Clear out metadata and mappings for anything unmapped.
        let start = usize::from(addr);
        let end = start + length;
        let mutations = self.regions.clear(start..end);
        self.handle_mutations(mutations);

        Ok(())
    }

    pub fn handle_mremap(
        &mut self,
        thread: &mut impl Thread,
        old_address: PluginPtr,
        old_size: usize,
        new_size: usize,
        flags: i32,
        new_address: PluginPtr,
    ) -> Result<PluginPtr, i32> {
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
            self.handle_mutations(mutations);
            return Ok(new_address);
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
                panic!(format!("Unexpected mutation {:?}", mutations[0]))
            }
        };

        // Clear any mappings that are about to be overwritten by the new mapping. We have to do
        // this *before* potentially mapping the new region into Shadow, so that we don't end up
        // freeing space for that new mapping.
        {
            let mutations = self.regions.clear(new_interval.clone());
            self.handle_mutations(mutations);
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
                self.shm_file
                    .mmap_into_plugin(thread, &new_interval, region.prot);

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
                assert_eq!(unsafe { libc::munmap(region.shadow_base, old_size) }, 0);

                // Update the region metadata.
                region.shadow_base = new_shadow_base;

                // Deallocate the old location.
                self.shm_file.dealloc(&old_interval);
            } else if new_size < old_size {
                // Deallocate the part no longer in use.
                self.shm_file.dealloc(&(new_interval.end..old_interval.end));

                // Shrink Shadow's mapping.
                assert_ne!(
                    unsafe { libc::mremap(region.shadow_base, old_size, new_size, 0) },
                    libc::MAP_FAILED
                );
            } else if new_size > old_size {
                // Allocate space in the file.
                self.shm_file.alloc(&new_interval);

                // Grow Shadow's mapping into the memory file, allowing the mapping to move if
                // needed.
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

        Ok(new_address)
    }

    /// Execute the requested `brk` and update our mappings accordingly. May invalidate outstanding
    /// pointers. (Rust won't allow mutable methods such as this one to be called with outstanding
    /// borrowed references).
    pub fn handle_brk(
        &mut self,
        thread: &mut impl Thread,
        ptr: PluginPtr,
    ) -> Result<PluginPtr, i32> {
        let requested_brk = usize::from(ptr);

        // On error, brk syscall returns current brk (end of heap). The only errors we specifically
        // handle is trying to set the end of heap before the start. In practice this case is
        // generally triggered with a NULL argument to get the current brk value.
        if requested_brk < self.heap.start {
            return Ok(PluginPtr::from(self.heap.end));
        }

        // Unclear how to handle a non-page-size increment. panic for now.
        assert!(requested_brk % page_size() == 0);

        // Not aware of this happening in practice, but handle this case specifically so we can
        // assume it's not the case below.
        if requested_brk == self.heap.end {
            return Ok(ptr);
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
                    self.shm_file.mmap_into_plugin(thread, &new_heap, HEAP_PROT);
                    shadow_base
                }
                Some((_, heap_region)) => {
                    // Grow heap region.
                    self.shm_file.alloc(&self.heap);
                    // mremap in plugin, enforcing that base stays the same.
                    let res = thread.native_mremap(
                        /* old_addr: */ PluginPtr::from(self.heap.start),
                        /* old_len: */ self.heap.end - self.heap.start,
                        /* new_len: */ new_heap.end - new_heap.start,
                        /* flags: */ 0,
                        /* new_addr: */ PluginPtr::from(0usize),
                    );
                    assert!(res.is_ok());
                    // mremap in shadow, allowing mapping to move if needed.
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
            let res = thread.native_mremap(
                /* old_addr: */ PluginPtr::from(self.heap.start),
                /* old_len: */ self.heap.end - self.heap.start,
                /* new_len: */ new_heap.end - new_heap.start,
                /* flags: */ 0,
                /* new_addr: */ PluginPtr::from(0usize),
            );
            assert!(res.is_ok());
            // mremap in shadow, assuming no need to move.
            let shadow_base = unsafe {
                libc::mremap(
                    /* old_addr: */ heap_region.shadow_base,
                    /* old_len: */ self.heap.end - self.heap.start,
                    /* new_len: */ new_heap.end - new_heap.start,
                    /* flags: */ 0,
                )
            };
            assert_eq!(shadow_base, heap_region.shadow_base);
            self.regions.clear(new_heap.end..self.heap.end);
            self.shm_file.dealloc(&(new_heap.end..self.heap.end));
        }
        self.heap = new_heap;

        Ok(PluginPtr::from(requested_brk))
    }

    pub fn handle_mprotect(
        &mut self,
        thread: &mut impl Thread,
        addr: PluginPtr,
        size: usize,
        prot: i32,
    ) -> Result<(), i32> {
        thread.native_mprotect(addr, size, prot)?;
        let protflags = mman::ProtFlags::from_bits(prot).unwrap();

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
                        extant_region.shadow_base = ((extant_region.shadow_base as usize)
                            + modified_interval.len())
                            as *mut c_void;
                        unsafe {
                            mman::mprotect(
                                modified_region.shadow_base,
                                modified_interval.len(),
                                protflags,
                            )
                        }
                        .unwrap_or_else(|e| warn!("mprotect: {}", e));
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
                        modified_region.shadow_base = ((modified_region.shadow_base as usize)
                            + extant_interval.len())
                            as *mut c_void;
                        unsafe {
                            mman::mprotect(
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
                        modified_region.shadow_base = ((modified_region.shadow_base as usize)
                            + left_interval.len())
                            as *mut c_void;
                        right_region.shadow_base = ((right_region.shadow_base as usize)
                            + left_interval.len()
                            + modified_interval.len())
                            as *mut c_void;
                        unsafe {
                            mman::mprotect(
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
                            mman::mprotect(
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
        Ok(())
    }

    // Extend the portion of the stack that we've mapped downward to include `src`.  This is
    // carefuly designed *not* to invalidate any outstanding borrowed references or pointers, since
    // otherwise a caller trying to marshall multiple syscall arguments might invalidate the first
    // argument when marshalling the second.
    fn extend_stack(&mut self, thread: &mut impl Thread, src: usize) {
        let start = page_of(src);
        let stack_extension = start..self.stack_copied.start;
        debug!(
            "extending stack from {:x} to {:x}",
            stack_extension.end, stack_extension.start
        );

        let (mapped_stack_interval, mapped_stack_region) =
            self.regions.get(self.stack_copied.start - 1).unwrap();
        assert!(mapped_stack_interval.contains(&src));

        self.shm_file.alloc(&stack_extension);
        self.shm_file.copy_into_file(
            thread,
            &mapped_stack_interval,
            &mapped_stack_region,
            &stack_extension,
        );

        // update stack bounds
        self.stack_copied.start = start;

        // Map into the Plugin's space, overwriting any previous mapping.
        self.shm_file
            .mmap_into_plugin(thread, &self.stack_copied, STACK_PROT);
    }

    // Get a raw pointer to the plugin's memory, if we have it mapped (or can do so now).
    fn get_mapped_ptr(
        &mut self,
        thread: &mut impl Thread,
        src: PluginPtr,
        n: usize,
    ) -> Option<*mut c_void> {
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

        if region.original_path == Some(MappingPath::InitialStack)
            && !self.stack_copied.contains(&src)
        {
            // src is in the stack, but in the portion we haven't mapped into shadow yet. Extend
            // it. Note that `ptr` calculated above will still be correct, since `extend_stack`
            // never moves shadow's mapping of the stack.
            self.extend_stack(thread, src);
        }

        Some(ptr)
    }

    // Counts accesses where we had to fall back to the thread's (slow) apis.
    fn inc_misses(&mut self, addr: PluginPtr) {
        let key = match self.regions.get(usize::from(addr)) {
            Some((_, original_path)) => format!("{:?}", original_path),
            None => "not found".to_string(),
        };
        let counter = self.misses_by_path.entry(key).or_insert(0);
        *counter += 1;
    }

    // Get a readable pointer to the plugin's memory via mapping, or via the thread APIs.
    // Never returns NULL.
    unsafe fn get_readable_ptr(
        &mut self,
        thread: &mut impl Thread,
        plugin_src: PluginPtr,
        n: usize,
    ) -> Result<*const c_void, i32> {
        let p = if let Some(p) = self.get_mapped_ptr(thread, plugin_src, n) {
            p
        } else {
            // Fall back to reading via the thread.
            self.inc_misses(plugin_src);
            thread.get_readable_ptr(plugin_src, n)?
        };
        if p.is_null() {
            Err(libc::EFAULT)
        } else {
            Ok(p)
        }
    }

    // Get a writeable pointer to the plugin's memory via mapping, or via the thread APIs.
    // Never returns NULL.
    unsafe fn get_writeable_ptr(
        &mut self,
        thread: &mut impl Thread,
        plugin_src: PluginPtr,
        n: usize,
    ) -> Result<*mut c_void, i32> {
        let p = if let Some(p) = self.get_mapped_ptr(thread, plugin_src, n) {
            p
        } else {
            // Fall back to reading via the thread.
            self.inc_misses(plugin_src);
            thread.get_writeable_ptr(plugin_src, n)?
        };
        if p.is_null() {
            Err(libc::EFAULT)
        } else {
            Ok(p)
        }
    }

    // Get a mutable pointer to the plugin's memory via mapping, or via the thread APIs.
    // Never returns NULL.
    unsafe fn get_mutable_ptr(
        &mut self,
        thread: &mut impl Thread,
        plugin_src: PluginPtr,
        n: usize,
    ) -> Result<*mut c_void, i32> {
        let p = if let Some(p) = self.get_mapped_ptr(thread, plugin_src, n) {
            p
        } else {
            // Fall back to reading via the thread.
            self.inc_misses(plugin_src);
            thread.get_mutable_ptr(plugin_src, n)?
        };
        if p.is_null() {
            Err(libc::EFAULT)
        } else {
            Ok(p)
        }
    }
}

mod export {
    use super::*;

    /// # Safety
    /// * `thread` must point to a valid object.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_new(thread: *mut c::Thread) -> *mut MemoryManager {
        Box::into_raw(Box::new(MemoryManager::new(&mut CThread::new(thread))))
    }

    /// # Safety
    /// * `mm` must point to a valid object.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_free(mm: *mut MemoryManager) {
        mm.as_mut().map(|mm| Box::from_raw(mm));
    }

    /// Get a readable pointer to the plugin's memory via mapping, or via the thread APIs.
    /// # Safety
    /// * `mm` and `thread` must point to valid objects.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_getReadablePtr(
        memory_manager: *mut MemoryManager,
        thread: *mut c::Thread,
        plugin_src: c::PluginPtr,
        n: usize,
    ) -> *const c_void {
        let mut thread = CThread::new(thread);
        let memory_manager = memory_manager.as_mut().unwrap();
        let plugin_src: PluginPtr = plugin_src.into();
        memory_manager
            .get_readable_ptr(&mut thread, plugin_src, n)
            .unwrap()
    }

    /// Get a writeagble pointer to the plugin's memory via mapping, or via the thread APIs.
    /// # Safety
    /// * `mm` and `thread` must point to valid objects.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_getWriteablePtr(
        memory_manager: *mut MemoryManager,
        thread: *mut c::Thread,
        plugin_src: c::PluginPtr,
        n: usize,
    ) -> *mut c_void {
        let mut thread = CThread::new(thread);
        let memory_manager = memory_manager.as_mut().unwrap();
        let plugin_src: PluginPtr = plugin_src.into();
        memory_manager
            .get_writeable_ptr(&mut thread, plugin_src, n)
            .unwrap()
    }

    /// Get a mutable pointer to the plugin's memory via mapping, or via the thread APIs.
    /// # Safety
    /// * `mm` and `thread` must point to valid objects.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_getMutablePtr(
        memory_manager: *mut MemoryManager,
        thread: *mut c::Thread,
        plugin_src: c::PluginPtr,
        n: usize,
    ) -> *mut c_void {
        let mut thread = CThread::new(thread);
        let memory_manager = memory_manager.as_mut().unwrap();
        let plugin_src: PluginPtr = plugin_src.into();
        memory_manager
            .get_mutable_ptr(&mut thread, plugin_src, n)
            .unwrap()
    }

    /// Fully handles the `brk` syscall, keeping the "heap" mapped in our shared mem file.
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_handleBrk(
        memory_manager: *mut MemoryManager,
        thread: *mut c::Thread,
        plugin_src: c::PluginPtr,
    ) -> c::SysCallReg {
        let memory_manager = memory_manager.as_mut().unwrap();
        let mut thread = CThread::new(thread);
        c::SysCallReg::from(
            match memory_manager.handle_brk(&mut thread, PluginPtr::from(plugin_src)) {
                Ok(p) => SysCallReg::from(p),
                // negative errno
                Err(e) => SysCallReg::from(-e),
            },
        )
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
    ) -> c::SysCallReg {
        let memory_manager = memory_manager.as_mut().unwrap();
        let mut thread = CThread::new(thread);
        c::SysCallReg::from(
            match memory_manager.handle_mmap(
                &mut thread,
                PluginPtr::from(addr),
                len,
                prot,
                flags,
                fd,
                offset,
            ) {
                Ok(p) => SysCallReg::from(p),
                // negative errno
                Err(e) => SysCallReg::from(-e),
            },
        )
    }

    /// Fully handles the `munmap` syscall
    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_handleMunmap(
        memory_manager: *mut MemoryManager,
        thread: *mut c::Thread,
        addr: c::PluginPtr,
        len: usize,
    ) -> c::SysCallReg {
        let memory_manager = memory_manager.as_mut().unwrap();
        let mut thread = CThread::new(thread);
        c::SysCallReg::from(
            match memory_manager.handle_munmap(&mut thread, PluginPtr::from(addr), len) {
                Ok(()) => SysCallReg::from(0),
                // negative errno
                Err(e) => SysCallReg::from(-e),
            },
        )
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
    ) -> c::SysCallReg {
        let memory_manager = memory_manager.as_mut().unwrap();
        let mut thread = CThread::new(thread);
        c::SysCallReg::from(
            match memory_manager.handle_mremap(
                &mut thread,
                PluginPtr::from(old_addr),
                old_size,
                new_size,
                flags,
                PluginPtr::from(new_addr),
            ) {
                Ok(p) => SysCallReg::from(p),
                // negative errno
                Err(e) => SysCallReg::from(-e),
            },
        )
    }

    #[no_mangle]
    pub unsafe extern "C" fn memorymanager_handleMprotect(
        memory_manager: *mut MemoryManager,
        thread: *mut c::Thread,
        addr: c::PluginPtr,
        size: usize,
        prot: i32,
    ) -> c::SysCallReg {
        let memory_manager = memory_manager.as_mut().unwrap();
        let mut thread = CThread::new(thread);
        c::SysCallReg::from(
            match memory_manager.handle_mprotect(&mut thread, PluginPtr::from(addr), size, prot) {
                Ok(()) => SysCallReg::from(0),
                // negative errno
                Err(e) => SysCallReg::from(-e),
            },
        )
    }
}
