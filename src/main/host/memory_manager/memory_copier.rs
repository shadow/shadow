use std::fmt::Debug;

use linux_api::errno::Errno;
use linux_api::posix_types::Pid;
use log::*;
use shadow_pod::Pod;

use crate::core::worker::Worker;
use crate::host::memory_manager::page_size;
use crate::host::syscall::types::ForeignArrayPtr;

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
    #[allow(clippy::uninit_vec)]
    pub unsafe fn clone_mem<T: Pod + Debug>(
        &self,
        ptr: ForeignArrayPtr<T>,
    ) -> Result<Vec<T>, Errno> {
        let mut v = Vec::with_capacity(ptr.len());
        unsafe { v.set_len(v.capacity()) };
        unsafe { self.copy_from_ptr(&mut v, ptr)? };
        Ok(v)
    }

    /// Copy the readable prefix of the region.
    /// SAFETY: A mutable reference to the process memory must not exist.
    #[allow(clippy::uninit_vec)]
    pub unsafe fn clone_mem_prefix<T: Pod + Debug>(
        &self,
        ptr: ForeignArrayPtr<T>,
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
        src: ForeignArrayPtr<T>,
    ) -> Result<usize, Errno>
    where
        T: Pod + Debug,
    {
        // Convert to u8
        // SAFETY: We do not write uninitialized data into `buf`.
        let buf: &mut [std::mem::MaybeUninit<u8>] = unsafe { shadow_pod::to_u8_slice_mut(dst) };
        // SAFETY: this buffer is write-only.
        // TODO: Fix or move away from nix's process_vm_readv wrapper so that we
        // don't need to construct this slice, and can instead only ever operate
        // on the pointer itself.
        let mut buf: &mut [u8] =
            unsafe { std::slice::from_raw_parts_mut(buf.as_mut_ptr() as *mut u8, buf.len()) };

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
        src: ForeignArrayPtr<T>,
    ) -> Result<(), Errno> {
        assert_eq!(dst.len(), src.len());
        // SAFETY: We do not write uninitialized data into `buf`.
        let buf = unsafe { shadow_pod::to_u8_slice_mut(dst) };
        // SAFETY: this buffer is write-only.
        // TODO: Fix or move away from nix's process_vm_readv wrapper so that we
        // don't need to construct this slice, and can instead only ever operate
        // on the pointer itself.
        let buf: &mut [u8] =
            unsafe { std::slice::from_raw_parts_mut(buf.as_mut_ptr() as *mut u8, buf.len()) };
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
        srcs: &[ForeignArrayPtr<u8>],
    ) -> Result<usize, Errno> {
        let srcs: Vec<_> = srcs
            .iter()
            .map(|src| nix::sys::uio::RemoteIoVec {
                base: usize::from(src.ptr()),
                len: src.len(),
            })
            .collect();
        let mut dsts: Vec<_> = dsts
            .iter_mut()
            .map(|dst: &mut &mut [u8]| -> std::io::IoSliceMut { std::io::IoSliceMut::new(dst) })
            .collect();

        unsafe { self.readv_iovecs(&mut dsts, &srcs) }
    }

    // Low level helper for reading directly from `srcs` to `dsts`.
    // Returns the number of bytes read. Panics if the
    // MemoryManager's process isn't currently active.
    /// SAFETY: A mutable reference to the process memory must not exist.
    unsafe fn readv_iovecs(
        &self,
        dsts: &mut [std::io::IoSliceMut],
        srcs: &[nix::sys::uio::RemoteIoVec],
    ) -> Result<usize, Errno> {
        trace!(
            "Reading from srcs of len {}",
            srcs.iter().map(|s| s.len).sum::<usize>()
        );
        trace!(
            "Reading to dsts of len {}",
            dsts.iter().map(|d| d.len()).sum::<usize>()
        );

        // While the documentation for process_vm_readv says to use the pid, in
        // practice it needs to be the tid of a still-running thread. i.e. using the
        // pid after the thread group leader has exited will fail.
        let tid = Worker::with_active_host(|host| {
            Worker::with_active_process(|process| {
                // Don't access another process's memory.
                assert_eq!(process.native_pid(), self.pid);
                let thread = process.first_live_thread_borrow(host.root()).unwrap();
                let thread = thread.borrow(host.root());
                thread.native_tid()
            })
            .unwrap()
        })
        .unwrap();

        let nread = nix::sys::uio::process_vm_readv(
            nix::unistd::Pid::from_raw(tid.as_raw_nonzero().get()),
            dsts,
            srcs,
        )
        .map_err(|e| Errno::try_from(e as i32).unwrap())?;

        Ok(nread)
    }

    // Low level helper for writing directly to `dst`. Panics if the
    // MemoryManager's process isn't currently active.
    /// SAFETY: A reference to the process memory must not exist.
    pub unsafe fn copy_to_ptr<T: Pod + Debug>(
        &self,
        dst: ForeignArrayPtr<T>,
        src: &[T],
    ) -> Result<(), Errno> {
        let dst = dst.cast_u8();
        let src: &[std::mem::MaybeUninit<u8>] = shadow_pod::to_u8_slice(src);
        // SAFETY: We *should* never actually read from this buffer in this process;
        // ultimately its pointer will be passed to the process_vm_writev syscall,
        // for which unitialized data is ok.
        // TODO: Fix or move away from nix's process_vm_writev wrapper so that we
        // don't need to construct this slice, and can instead only ever operate
        // on the pointer itself.
        let src: &[u8] =
            unsafe { std::slice::from_raw_parts(src.as_ptr() as *const u8, src.len()) };
        assert_eq!(src.len(), dst.len());

        let towrite = src.len();
        trace!("write_ptr writing {} bytes", towrite);
        let local = [std::io::IoSlice::new(src)];
        let remote = [nix::sys::uio::RemoteIoVec {
            base: usize::from(dst.ptr()),
            len: towrite,
        }];

        // While the documentation for process_vm_writev says to use the pid, in
        // practice it needs to be the tid of a still-running thread. i.e. using the
        // pid after the thread group leader has exited will fail.
        //
        // TODO: get this explicitly from the caller instead of reaching out to
        // the global Worker.
        let tid = Worker::with_active_host(|host| {
            Worker::with_active_process(|process| {
                if process.native_pid() != self.pid {
                    // This currently only happens in the clone syscall handler
                    // if we need to write to the child's memory, and the child
                    // is a new process. In this case the thread group leader should
                    // be alive, so the pid will work.
                    //
                    // TODO: as above, this hack can be avoided by getting a live tid
                    // explicitly from the caller.
                    self.pid
                } else {
                    let thread = process.first_live_thread_borrow(host.root()).unwrap();
                    let thread = thread.borrow(host.root());
                    thread.native_tid()
                }
            })
            .unwrap()
        })
        .unwrap();

        let nwritten = nix::sys::uio::process_vm_writev(
            nix::unistd::Pid::from_raw(tid.as_raw_nonzero().get()),
            &local,
            &remote,
        )
        .map_err(|e| Errno::try_from(e as i32).unwrap())?;
        // There shouldn't be any partial writes with a single remote iovec.
        assert_eq!(nwritten, towrite);
        Ok(())
    }
}
