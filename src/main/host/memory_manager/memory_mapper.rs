use std::cell::RefCell;
use std::collections::HashMap;
use std::ffi::CString;
use std::fmt::Debug;
use std::fs::File;
use std::os::raw::c_void;
use std::os::unix::io::AsRawFd;
use std::path::PathBuf;
use std::process;

use linux_api::errno::Errno;
use linux_api::mman::MapFlags;
use linux_api::mman::ProtFlags;
use linux_api::posix_types::Pid;
use log::*;
use rustix::fs::FallocateFlags;
use rustix::fs::MemfdFlags;
use shadow_pod::Pod;
use shadow_shim_helper_rs::notnull::*;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::host::context::ProcessContext;
use crate::host::context::ThreadContext;
use crate::host::memory_manager::{page_size, MemoryManager};
use crate::host::syscall::types::ForeignArrayPtr;
use crate::utility::interval_map::{Interval, IntervalMap, Mutation};
use crate::utility::proc_maps;
use crate::utility::proc_maps::{MappingPath, Sharing};

/// Used when mapping heap regions.
const HEAP_PROT: ProtFlags = ProtFlags::PROT_READ.union(ProtFlags::PROT_WRITE);
/// Used when mapping stack regions.
const STACK_PROT: ProtFlags = ProtFlags::PROT_READ.union(ProtFlags::PROT_WRITE);

// Represents a region of plugin memory.
#[derive(Clone, Debug, Eq, PartialEq)]
struct Region {
    // Where the region is mapped into shadow's address space, or NULL if it isn't.
    shadow_base: *mut c_void,
    prot: ProtFlags,
    sharing: proc_maps::Sharing,
    // The *original* path. Not the path to our mem file.
    original_path: Option<proc_maps::MappingPath>,
}

// Safety: The Region owns the shadow_base pointer, and the mapper enforces
// Rust's aliasing rules etc.
//
// TODO: Consider using something like SyncSendPointer for `shadow_base`
// instead of this explicit Send implementation. SyncSendPointer adds quite a
// lot of boiler-plate in this case, though.
unsafe impl Send for Region {}

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
pub struct MemoryMapper {
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
    len: usize,
}

impl ShmFile {
    /// Allocate space in the file for the given interval.
    fn alloc(&mut self, interval: &Interval) {
        let needed_len = interval.end;
        // Ensure that the file size extends through the end of the interval.
        // Unlike calling fallocate or posix_fallocate, this does not pre-reserve
        // any space. The OS will allocate the space on-demand as it's written.
        if needed_len > self.len {
            rustix::fs::ftruncate(&self.shm_file, u64::try_from(needed_len).unwrap()).unwrap();
            self.len = needed_len;
        }
    }

    /// De-allocate space in the file for the given interval.
    fn dealloc(&self, interval: &Interval) {
        trace!("dealloc {:?}", interval);
        rustix::fs::fallocate(
            &self.shm_file,
            FallocateFlags::PUNCH_HOLE | FallocateFlags::KEEP_SIZE,
            u64::try_from(interval.start).unwrap(),
            u64::try_from(interval.len()).unwrap(),
        )
        .unwrap();
    }

    /// Map the given interval of the file into shadow's address space.
    fn mmap_into_shadow(&self, interval: &Interval, prot: ProtFlags) -> *mut c_void {
        unsafe {
            linux_api::mman::mmap(
                std::ptr::null_mut(),
                interval.len(),
                prot,
                MapFlags::MAP_SHARED,
                self.shm_file.as_raw_fd(),
                interval.start,
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
        if interval.is_empty() {
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

        memory_manager
            .copy_from_ptr(
                dst,
                ForeignArrayPtr::new(
                    ForeignPtr::from(interval.start).cast::<u8>(),
                    interval.len(),
                ),
            )
            .unwrap()
    }

    /// Map the given range of the file into the plugin's address space.
    fn mmap_into_plugin(&self, ctx: &ThreadContext, interval: &Interval, prot: ProtFlags) {
        ctx.thread
            .native_mmap(
                &ProcessContext::new(ctx.host, ctx.process),
                ForeignPtr::from(interval.start).cast::<u8>(),
                interval.len(),
                prot,
                MapFlags::MAP_SHARED | MapFlags::MAP_FIXED,
                self.shm_plugin_fd,
                interval.start as i64,
            )
            .unwrap();
    }
}

/// Get the current mapped regions of the process.
fn get_regions(pid: Pid) -> IntervalMap<Region> {
    let mut regions = IntervalMap::new();
    for mapping in proc_maps::mappings_for_pid(pid.as_raw_nonzero().get()).unwrap() {
        let mut prot = ProtFlags::empty();
        if mapping.read {
            prot |= ProtFlags::PROT_READ;
        }
        if mapping.write {
            prot |= ProtFlags::PROT_WRITE;
        }
        if mapping.execute {
            prot |= ProtFlags::PROT_EXEC;
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
    ctx: &ThreadContext,
    shm_file: &mut ShmFile,
    memory_manager: &MemoryManager,
    regions: &mut IntervalMap<Region>,
) -> Interval {
    // If there's already a region labeled heap, we use those bounds.
    let heap_mapping = {
        let mut it = regions
            .iter()
            .filter(|m| m.1.original_path == Some(proc_maps::MappingPath::Heap));
        let heap_mapping = it.next();
        // There should only be one heap region.
        assert_eq!(it.fuse().next(), None);
        heap_mapping
    };
    if heap_mapping.is_none() {
        let (ctx, thread) = ctx.split_thread();
        // There's no heap region allocated yet. Get the address where it will be and return.
        let start = usize::from(thread.native_brk(&ctx, ForeignPtr::null()).unwrap());
        return start..start;
    }
    let (heap_interval, heap_region) = heap_mapping.unwrap();

    shm_file.alloc(&heap_interval);
    let mut heap_region = heap_region.clone();
    heap_region.shadow_base = shm_file.mmap_into_shadow(&heap_interval, HEAP_PROT);
    shm_file.copy_into_file(memory_manager, &heap_interval, &heap_region, &heap_interval);
    shm_file.mmap_into_plugin(ctx, &heap_interval, HEAP_PROT);

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
    ctx: &ThreadContext,
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

    shm_file.mmap_into_plugin(ctx, &remapped_stack_bounds, STACK_PROT);

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
        let mutations = self.regions.clear(usize::MIN..usize::MAX);
        for m in mutations {
            if let Mutation::Removed(interval, region) = m {
                if !region.shadow_base.is_null() {
                    unsafe { linux_api::mman::munmap(region.shadow_base, interval.len()) }
                        .unwrap_or_else(|e| warn!("munmap: {}", e));
                }
            }
        }
    }
}

/// Collapses adjacent regions with identical properties into a single region.
/// Panics if any regions have already been mapped into shadow.
///
/// This is primarily to work around some kernel version 6.1.6 reporting
/// multiple adjacent heap regions. See
/// https://github.com/shadow/shadow/issues/2692
fn coalesce_regions(regions: IntervalMap<Region>) -> IntervalMap<Region> {
    let mut out = IntervalMap::new();
    let mut agg_interval_region: Option<(Interval, Region)> = None;
    for (interval, region) in regions.iter() {
        // We don't handle already-mapped regions
        assert!(region.shadow_base.is_null());
        agg_interval_region = Some(
            if let Some((agg_interval, agg_region)) = agg_interval_region.take() {
                if interval.start == agg_interval.end && region == &agg_region {
                    // can be coalesced. do so.
                    (agg_interval.start..interval.end, agg_region)
                } else {
                    // Can't be coalesced; flush the current aggregate to `out`.
                    out.insert(agg_interval, agg_region);
                    (interval, region.clone())
                }
            } else {
                (interval, region.clone())
            },
        );
    }
    // Flush last region
    if let Some((current_interval, current_region)) = agg_interval_region.take() {
        out.insert(current_interval, current_region);
    }
    out
}

impl MemoryMapper {
    pub fn new(memory_manager: &mut MemoryManager, ctx: &ThreadContext) -> MemoryMapper {
        let shm_name = CString::new(format!(
            "shadow_memory_manager_{}_{:?}_{}",
            process::id(),
            ctx.thread.host_id(),
            u32::from(ctx.process.id())
        ))
        .unwrap();
        let raw_file = rustix::fs::memfd_create(&shm_name, MemfdFlags::CLOEXEC).unwrap();
        let shm_file = File::from(raw_file);

        // Other processes can open the file via /proc.
        let shm_path = format!("/proc/{}/fd/{}\0", process::id(), shm_file.as_raw_fd());

        let shm_plugin_fd = {
            let (ctx, thread) = ctx.split_thread();
            let path_buf_foreign_ptr = ForeignArrayPtr::new(
                thread.malloc_foreign_ptr(&ctx, shm_path.len()).unwrap(),
                shm_path.len(),
            );
            memory_manager
                .copy_to_ptr(path_buf_foreign_ptr, shm_path.as_bytes())
                .unwrap();
            let shm_plugin_fd = thread
                .native_open(
                    &ctx,
                    path_buf_foreign_ptr.ptr(),
                    libc::O_RDWR | libc::O_CLOEXEC,
                    0,
                )
                .unwrap();
            thread
                .free_foreign_ptr(&ctx, path_buf_foreign_ptr.ptr(), path_buf_foreign_ptr.len())
                .unwrap();
            shm_plugin_fd
        };

        let mut shm_file = ShmFile {
            shm_file,
            shm_plugin_fd,
            len: 0,
        };
        let regions = get_regions(memory_manager.pid);
        let mut regions = coalesce_regions(regions);
        let heap = get_heap(ctx, &mut shm_file, memory_manager, &mut regions);
        map_stack(memory_manager, ctx, &mut shm_file, &mut regions);

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
                    unsafe { linux_api::mman::munmap(region.shadow_base, removed_range.len()) }
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
                        linux_api::mman::munmap(
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
                        linux_api::mman::munmap(
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
                    unsafe { linux_api::mman::munmap(region.shadow_base, interval.len()) }
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
    pub fn handle_mmap_result(
        &mut self,
        ctx: &ThreadContext,
        ptr: ForeignArrayPtr<u8>,
        prot: ProtFlags,
        flags: MapFlags,
        fd: i32,
    ) {
        trace!(
            "Handling mmap result for {:x}..+{}",
            usize::from(ptr.ptr()),
            ptr.len()
        );
        if ptr.is_empty() {
            return;
        }
        let addr = usize::from(ptr.ptr());
        let interval = addr..(addr + ptr.len());
        let is_anonymous = flags.contains(MapFlags::MAP_ANONYMOUS);
        let sharing = if flags.contains(MapFlags::MAP_PRIVATE) {
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
                std::fs::read_link(format!(
                    "/proc/{}/fd/{}",
                    ctx.thread.native_pid().as_raw_nonzero().get(),
                    fd
                ))
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
            self.shm_file.mmap_into_plugin(ctx, &interval, prot);
        }

        // TODO: We *could* handle file mappings and some shared mappings as well. Doesn't make
        // sense to add that complexity until if/when we see a lot of misses in such regions,
        // though.

        {
            // There shouldn't be any mutations here; we already cleared a hole above.
            let mutations = self.regions.insert(interval, region);
            assert!(mutations.is_empty());
        }
    }

    /// Shadow should delegate a plugin's call to munmap to this method.
    ///
    /// Executes the actual mmap operation in the plugin, updates the MemoryManager's understanding of
    /// the plugin's address space, and unmaps the affected memory from Shadow if it was mapped in.
    pub fn handle_munmap_result(&mut self, addr: ForeignPtr<u8>, length: usize) {
        trace!("handle_munmap_result({:?}, {})", addr, length);
        if length == 0 {
            return;
        }

        // Clear out metadata and mappings for anything unmapped.
        let start = usize::from(addr);
        let end = start + length;
        let mutations = self.regions.clear(start..end);
        self.unmap_mutations(mutations);
    }

    /// Shadow should delegate a plugin's call to mremap to this method.
    ///
    /// Executes the actual mremap operation in the plugin, updates the MemoryManager's
    /// understanding of the plugin's address space, and updates Shadow's mappings of that region
    /// if applicable.
    pub fn handle_mremap(
        &mut self,
        ctx: &ThreadContext,
        old_address: ForeignPtr<u8>,
        old_size: usize,
        new_size: usize,
        flags: i32,
        new_address: ForeignPtr<u8>,
    ) -> Result<ForeignPtr<u8>, Errno> {
        let new_address = {
            let (ctx, thread) = ctx.split_thread();
            thread.native_mremap(&ctx, old_address, old_size, new_size, flags, new_address)?
        };
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
                self.shm_file
                    .mmap_into_plugin(ctx, &new_interval, region.prot);

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
                unsafe { linux_api::mman::munmap(region.shadow_base, old_size) }
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

        Ok(new_address)
    }

    /// Execute the requested `brk` and update our mappings accordingly. May invalidate outstanding
    /// pointers. (Rust won't allow mutable methods such as this one to be called with outstanding
    /// borrowed references).
    pub fn handle_brk(
        &mut self,
        ctx: &ThreadContext,
        ptr: ForeignPtr<u8>,
    ) -> Result<ForeignPtr<u8>, Errno> {
        let requested_brk = usize::from(ptr);

        // On error, brk syscall returns current brk (end of heap). The only errors we specifically
        // handle is trying to set the end of heap before the start. In practice this case is
        // generally triggered with a NULL argument to get the current brk value.
        if requested_brk < self.heap.start {
            return Ok(ForeignPtr::from(self.heap.end).cast::<u8>());
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
                    self.shm_file.mmap_into_plugin(ctx, &new_heap, HEAP_PROT);
                    shadow_base
                }
                Some((_, heap_region)) => {
                    // Grow heap region.
                    self.shm_file.alloc(&self.heap);
                    // mremap in plugin, enforcing that base stays the same.
                    let (ctx, thread) = ctx.split_thread();
                    thread
                        .native_mremap(
                            &ctx,
                            /* old_addr: */
                            ForeignPtr::from(self.heap.start).cast::<u8>(),
                            /* old_len: */ self.heap.end - self.heap.start,
                            /* new_len: */ new_heap.end - new_heap.start,
                            /* flags: */ 0,
                            /* new_addr: */ ForeignPtr::null(),
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
            let (ctx, thread) = ctx.split_thread();
            thread
                .native_mremap(
                    &ctx,
                    /* old_addr: */ ForeignPtr::from(self.heap.start).cast::<u8>(),
                    /* old_len: */ self.heap.len(),
                    /* new_len: */ new_heap.len(),
                    /* flags: */ 0,
                    /* new_addr: */ ForeignPtr::null(),
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

        Ok(ForeignPtr::from(requested_brk).cast::<u8>())
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
    pub fn handle_mprotect(
        &mut self,
        ctx: &ThreadContext,
        addr: ForeignPtr<u8>,
        size: usize,
        prot: ProtFlags,
    ) -> Result<(), Errno> {
        let (ctx, thread) = ctx.split_thread();
        trace!("mprotect({:?}, {}, {:?})", addr, size, prot);
        thread.native_mprotect(&ctx, addr, size, prot)?;

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
                            linux_api::mman::mprotect(
                                modified_region.shadow_base,
                                modified_interval.len(),
                                prot,
                            )
                        }
                        .unwrap_or_else(|e| {
                            warn!(
                                "mprotect({:?}, {:?}, {:?}): {}",
                                modified_region.shadow_base,
                                modified_interval.len(),
                                prot,
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
                            linux_api::mman::mprotect(
                                modified_region.shadow_base,
                                modified_interval.len(),
                                prot,
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
                            linux_api::mman::mprotect(
                                modified_region.shadow_base,
                                modified_interval.len(),
                                prot,
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
                            linux_api::mman::mprotect(
                                modified_region.shadow_base,
                                modified_interval.len(),
                                prot,
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

    // Get a raw pointer to the plugin's memory, if it's been remapped into Shadow.
    // Panics if called with zero-length `src`.
    fn get_mapped_ptr<T: Pod + Debug>(&self, src: ForeignArrayPtr<T>) -> Option<*mut T> {
        assert!(!src.is_empty());

        if usize::from(src.ptr()) % std::mem::align_of::<T>() != 0 {
            // Creating a reference from an unaligned pointer is undefined
            // behavior in Rust.  Instead of accessing such pointers directly,
            // we fall back the memory *copier*, which will use a safely aligned
            // intermediate buffer.
            trace!("Can't map unaligned pointer {:?}", src);
            return None;
        }

        let (interval, region) = match self.regions.get(usize::from(src.ptr())) {
            Some((i, r)) => (i, r),
            None => {
                if !src.ptr().is_null() {
                    warn!("src {:?} isn't in any mapped region", src);
                }
                return None;
            }
        };
        let shadow_base = if region.shadow_base.is_null() {
            trace!("src {:?} isn't mapped into Shadow", src);
            return None;
        } else {
            region.shadow_base
        };

        if !interval.contains(&(usize::from(src.slice(src.len()..src.len()).ptr()) - 1)) {
            // End isn't in the region.
            trace!(
                "src {:?} mapped into Shadow, but extends beyond mapped region.",
                src
            );
            return None;
        }

        let offset = usize::from(src.ptr()) - interval.start;
        // Base pointer + offset won't wrap around, by construction.
        let ptr = unsafe { shadow_base.add(offset) } as *mut T;

        Some(ptr)
    }

    fn get_mapped_ptr_and_count<T: Pod + Debug>(&self, src: ForeignArrayPtr<T>) -> Option<*mut T> {
        let res = self.get_mapped_ptr(src);
        if res.is_none() {
            self.inc_misses(src);
        }
        res
    }

    pub unsafe fn get_ref<T: Debug + Pod>(&self, src: ForeignArrayPtr<T>) -> Option<&[T]> {
        if src.is_empty() {
            return Some(&[]);
        }
        let ptr = self.get_mapped_ptr_and_count(src)?;
        Some(unsafe { std::slice::from_raw_parts(notnull_debug(ptr), src.len()) })
    }

    pub unsafe fn get_mut<T: Debug + Pod>(&self, src: ForeignArrayPtr<T>) -> Option<&mut [T]> {
        if src.is_empty() {
            return Some(&mut []);
        }
        let ptr = self.get_mapped_ptr_and_count(src)?;
        Some(unsafe { std::slice::from_raw_parts_mut(notnull_mut_debug(ptr), src.len()) })
    }

    /// Counts accesses where we had to fall back to the thread's (slow) apis.
    fn inc_misses<T: Debug + Pod>(&self, src: ForeignArrayPtr<T>) {
        let key = match self.regions.get(usize::from(src.ptr())) {
            Some((_, original_path)) => format!("{:?}", original_path),
            None => "not found".to_string(),
        };
        let mut misses = self.misses_by_path.borrow_mut();
        let counter = misses.entry(key).or_insert(0);
        *counter += 1;
    }
}

#[cfg(test)]
#[test]
/// We assume throughout that we can do arithmetic on void pointers as if the size of "void" was 1.
/// While this seems like a reasonable assumption, it doesn't seem to be documented or guaranteed
/// anywhere, so we validate it:
fn test_validate_void_size() {
    assert_eq!(std::mem::size_of::<c_void>(), 1);
}
