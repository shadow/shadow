use crate::cshadow;
use crate::host::syscall_types::PluginPtr;
use crate::utility::pod::Pod;
use log::*;
use std::mem::{align_of, size_of, MaybeUninit};

pub struct Process {
    cprocess: *mut cshadow::Process,
}

impl Process {
    /// For now, this should only be called via Worker, to borrow the current
    /// Process. This will ensure there is only one reference to a given Process
    /// in Rust.
    ///
    /// SAFETY: `p` must point to a valid c::Process, to which this Process will
    /// have exclusive access over its lifetime. `p` must outlive the returned object.
    pub unsafe fn borrow_from_c(p: *mut cshadow::Process) -> Self {
        assert!(!p.is_null());
        Process { cprocess: p }
    }

    /// Read data from `src` into `dst`.
    /// Always succeeds for zero-size `dst`.
    ///
    /// Use when:
    /// * We need to copy the data into a Shadow-owned buffer (such as a packet).
    /// * Or: when the cost of copying the data is small (e.g. < 1kB).
    pub fn read_ptr_into_slice<T: Pod>(&self, dst: &mut [T], src: PluginPtr) -> nix::Result<()> {
        let status = unsafe {
            cshadow::process_readPtr(
                self.cprocess,
                dst.as_mut_ptr() as *mut std::os::raw::c_void,
                src.into(),
                (dst.len() * size_of::<T>()) as u64,
            )
        };
        if status == 0 {
            Ok(())
        } else {
            Err(nix::Error::from_errno(nix::errno::from_i32(status as i32)))
        }
    }

    /// Read data from `src` into `dst`.
    ///
    /// Use when:
    /// * We need to copy the data into a Shadow-owned buffer (such as a packet).
    /// * Or: when the cost of copying the data is small (e.g. < 1kB).
    pub fn read_ptr_into_val<'a, T: Pod>(&self, dst: &mut T, src: PluginPtr) -> nix::Result<()> {
        let mut slice = std::slice::from_mut(dst);
        self.read_ptr_into_slice(&mut slice, src)?;
        Ok(())
    }

    /// Convenience wrapper around `read_ptr_into_val`.
    pub fn read_ptr_as_val<T: Pod>(&self, src: PluginPtr) -> nix::Result<T> {
        // SAFETY: Since `T` is Pod, any bit pattern is a valid `T`.
        let mut val: T = unsafe { MaybeUninit::uninit().assume_init() };
        self.read_ptr_into_val(&mut val, src)?;
        Ok(val)
    }

    /// Write data from `src` into `dst`.
    /// Always succeeds for zero-size `dst`.
    pub fn write_ptr_from_slice<T: Copy>(&mut self, dst: PluginPtr, src: &[T]) -> nix::Result<()> {
        let status = unsafe {
            cshadow::process_writePtr(
                self.cprocess,
                dst.into(),
                src.as_ptr() as *const std::os::raw::c_void,
                (src.len() * size_of::<T>()) as u64,
            )
        };
        if status == 0 {
            Ok(())
        } else {
            Err(nix::Error::from_errno(nix::errno::from_i32(status as i32)))
        }
    }

    /// Write `src` into `dst` in the current Process.
    pub fn write_ptr_from_val<T: Copy>(&mut self, dst: PluginPtr, src: &T) -> nix::Result<()> {
        let src = std::slice::from_ref(src);
        self.write_ptr_from_slice(dst, src)
    }

    /// Get a read-only slice at `src`. If memory can't be accessed, may return
    /// an error, or may cause a Shadow error later when flushing.
    ///
    /// Prefer the `read_ptr_*` methods unless you specifically need a slice, and
    /// the data is of non-trivial size.
    pub fn get_slice<T>(&self, src: PluginPtr, len: usize) -> nix::Result<&[T]> {
        if len == 0 {
            return Ok(&[][..]);
        }

        // SAFETY: This never returns an invalid pointer. Passing a `p` outside
        // of the plugin's addressable memory will return a NULL pointer.
        let ptr = unsafe {
            cshadow::process_getReadablePtr(
                self.cprocess,
                src.into(),
                (size_of::<T>() * len) as u64,
            )
        } as *const T;

        if ptr.is_null() {
            return Err(nix::Error::from_errno(nix::errno::Errno::EFAULT));
        }

        if ptr.align_offset(align_of::<T>()) != 0 {
            debug!("Unaligned pointer {:?}", ptr);
            return Err(nix::Error::from_errno(nix::errno::Errno::EFAULT));
        }

        // SAFETY: `process_getReadablePtr` already checked bounds; we've
        // checked alignment, and since T is Pod, any data is a valid T.
        Ok(unsafe { std::slice::from_raw_parts(ptr, len) })
    }

    /// Get a mutable slice to `src`. May return an error if the memory can't
    /// be accessed. In some cases the returned reference is to local memory that
    /// will be flushed later, in which case any errors will be deferred until
    /// then. Returns an empty slice for len == 0.
    ///
    /// Prefer the `write_ptr_*` methods unless you need a mutable slice to pass
    /// to another API, and prefer `get_writable_slice` when you don't need the
    /// original value stored at `src`.
    pub fn get_mut_slice<T>(&mut self, src: PluginPtr, len: usize) -> nix::Result<&mut [T]> {
        if len == 0 {
            return Ok(unsafe {
                std::slice::from_raw_parts_mut(std::ptr::NonNull::dangling().as_ptr(), 0)
            });
        }

        // SAFETY: This never returns an invalid pointer. Passing a `p` outside
        // of the plugin's addressable memory will either result in returning a
        // NULL pointer (if we end up going through the MemoryManager), or
        // result in a failure later when the thread attempts to flush the
        // write. Such a failure is undesirable, but should at least fail in a
        // well-defined way.
        //
        // TODO: When the MemoryManager is available, use it to validate the
        // bounds of the pointer even if it doesn't have the pointer mmap'd into
        // Shadow.
        let ptr = unsafe {
            cshadow::process_getMutablePtr(self.cprocess, src.into(), (size_of::<T>() * len) as u64)
        } as *mut T;

        if ptr.is_null() {
            return Err(nix::Error::from_errno(nix::errno::Errno::EFAULT));
        }

        if ptr.align_offset(align_of::<T>()) != 0 {
            debug!("Unaligned pointer {:?}", ptr);
            return Err(nix::Error::from_errno(nix::errno::Errno::EFAULT));
        }

        // SAFETY: `process_getMutablePtr` already checked bounds; we've checked
        // alignment, and since T is Pod, any data is a valid T.
        Ok(unsafe { std::slice::from_raw_parts_mut(ptr, len) })
    }

    /// As `get_mut_slice`, but the initial value is unspecified.
    ///
    /// Prefer the `write_ptr_*` methods unless you need a mutable slice to pass
    /// to another API.
    pub fn get_writable_slice<T>(&mut self, src: PluginPtr, len: usize) -> nix::Result<&mut [T]> {
        if len == 0 {
            return Ok(unsafe {
                std::slice::from_raw_parts_mut(std::ptr::NonNull::dangling().as_ptr(), 0)
            });
        }

        // SAFETY: This never returns an invalid pointer. Passing a `p` outside
        // of the plugin's addressable memory will either result in returning a
        // NULL pointer (if we end up going through the MemoryManager), or
        // result in a failure later when the thread attempts to flush the
        // write. Such a failure is undesirable, but should at least fail in a
        // well-defined way.
        //
        // TODO: When the MemoryManager is available, use it to validate the
        // bounds of the pointer even if it doesn't have the pointer mmap'd into
        // Shadow.
        let ptr = unsafe {
            cshadow::process_getWriteablePtr(
                self.cprocess,
                src.into(),
                (size_of::<T>() * len) as u64,
            )
        } as *mut T;

        if ptr.is_null() {
            return Err(nix::Error::from_errno(nix::errno::Errno::EFAULT));
        }

        if ptr.align_offset(align_of::<T>()) != 0 {
            debug!("Unaligned pointer {:?}", ptr);
            return Err(nix::Error::from_errno(nix::errno::Errno::EFAULT));
        }

        // SAFETY: `process_getWriteablePtr` already checked bounds; we've checked
        // alignment, and since T is Pod, any data is a valid T.
        Ok(unsafe { std::slice::from_raw_parts_mut(ptr, len) })
    }
}
