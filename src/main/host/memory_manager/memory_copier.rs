use crate::core::worker::Worker;
use crate::host::memory_manager::page_size;
use crate::host::syscall_types::TypedPluginPtr;
use crate::utility::pod;
use crate::utility::pod::Pod;
use log::*;
use nix::{errno::Errno, unistd::Pid};
use std::fmt::Debug;

/// A utility for copying data to and from a process's memory.
#[derive(Debug, Clone)]
pub struct MemoryCopier {
    pid: Pid,
}

impl MemoryCopier {
    pub fn new(pid: Pid) -> Self {
        Self { pid }
    }

    /// Copy the region.
    /// SAFETY: A mutable reference to the process memory must not exist.
    pub unsafe fn clone_mem<T: Pod + Debug>(
        &self,
        ptr: TypedPluginPtr<T>,
    ) -> Result<Vec<T>, Errno> {
        let mut v = Vec::with_capacity(ptr.len());
        unsafe { v.set_len(v.capacity()) };
        unsafe { self.copy_from_ptr(&mut v, ptr)? };
        Ok(v)
    }

    /// Copy the readable prefix of the region.
    /// SAFETY: A mutable reference to the process memory must not exist.
    pub unsafe fn clone_mem_prefix<T: Pod + Debug>(
        &self,
        ptr: TypedPluginPtr<T>,
    ) -> Result<Vec<T>, Errno> {
        let mut v = Vec::with_capacity(ptr.len());
        unsafe { v.set_len(v.capacity()) };
        let copied = unsafe { self.copy_prefix_from_ptr(&mut v, ptr)? };
        v.truncate(copied);
        Ok(v)
    }

    // Read as much of `ptr` as is accessible into `dst`.
    /// SAFETY: A mutable reference to the process memory must not exist.
    pub unsafe fn copy_prefix_from_ptr<T>(
        &self,
        dst: &mut [T],
        src: TypedPluginPtr<T>,
    ) -> Result<usize, Errno>
    where
        T: Pod + Debug,
    {
        // Convert to u8
        let mut buf = pod::to_u8_slice_mut(dst);
        let ptr = src.cast_u8();

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
        let bytes_read = unsafe { self.readv_ptrs(&mut slices, &[ptr])? };
        Ok(bytes_read / std::mem::size_of::<T>())
    }

    // Copy `dst` into `src`.
    /// SAFETY: A mutable reference to the process memory must not exist.
    pub unsafe fn copy_from_ptr<T: Pod + Debug>(
        &self,
        dst: &mut [T],
        src: TypedPluginPtr<T>,
    ) -> Result<(), Errno> {
        assert_eq!(dst.len(), src.len());
        let buf = pod::to_u8_slice_mut(dst);
        let ptr = src.cast_u8();
        let bytes_read = unsafe { self.readv_ptrs(&mut [buf], &[ptr])? };
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
    /// SAFETY: A mutable reference to the process memory must not exist.
    unsafe fn readv_ptrs(
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

        unsafe { self.readv_iovecs(&dsts, &srcs) }
    }

    // Low level helper for reading directly from `srcs` to `dsts`.
    // Returns the number of bytes read. Panics if the
    // MemoryManager's process isn't currently active.
    /// SAFETY: A mutable reference to the process memory must not exist.
    unsafe fn readv_iovecs(
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
        let active_tid = Worker::active_thread_native_tid().unwrap();
        let active_pid = Worker::active_process_native_pid().unwrap();

        // Don't access another process's memory.
        assert_eq!(active_pid, self.pid);

        let nread = nix::sys::uio::process_vm_readv(active_tid, &dsts, &srcs)?;

        Ok(nread)
    }

    // Low level helper for writing directly to `dst`. Panics if the
    // MemoryManager's process isn't currently active.
    /// SAFETY: A reference to the process memory must not exist.
    pub unsafe fn copy_to_ptr<T: Pod + Debug>(
        &self,
        dst: TypedPluginPtr<T>,
        src: &[T],
    ) -> Result<(), Errno> {
        let dst = dst.cast_u8();
        let src = pod::to_u8_slice(src);
        assert_eq!(src.len(), dst.len());

        let towrite = src.len();
        trace!("write_ptr writing {} bytes", towrite);
        let local = [nix::sys::uio::IoVec::from_slice(src)];
        let remote = [nix::sys::uio::RemoteIoVec {
            base: usize::from(dst.ptr()),
            len: towrite,
        }];

        // While the documentation for process_vm_writev says to use the pid, in
        // practice it needs to be the tid of a still-running thread. i.e. using the
        // pid after the thread group leader has exited will fail.
        let active_tid = Worker::active_thread_native_tid().unwrap();
        let active_pid = Worker::active_process_native_pid().unwrap();

        // Don't access another process's memory.
        assert_eq!(active_pid, self.pid);

        let nwritten = nix::sys::uio::process_vm_writev(active_tid, &local, &remote)?;
        // There shouldn't be any partial writes with a single remote iovec.
        assert_eq!(nwritten, towrite);
        Ok(())
    }
}
