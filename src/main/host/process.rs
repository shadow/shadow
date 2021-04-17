use crate::cshadow;
use crate::host::syscall_types::TypedPluginPtr;
use crate::utility::pod::Pod;
use log::*;
use nix::errno::Errno;
use std::io::{Read, Seek, SeekFrom, Write};
use std::mem::align_of;

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

    /// Create an object to read `count` elements of type `T` from the specified
    /// pointer.
    pub fn reader<T>(&self, dst: TypedPluginPtr<T>) -> ProcessMemoryReader<T> {
        ProcessMemoryReader::<T> {
            process: self,
            ptr: dst,
        }
    }

    /// Create an object to write `count` elements of type `T` to the specified
    /// pointer.
    pub fn writer<T>(&mut self, src: TypedPluginPtr<T>) -> ProcessMemoryWriter<T> {
        ProcessMemoryWriter::<T> {
            process: self,
            ptr: src,
        }
    }
}

pub struct ProcessMemoryReader<'a, T> {
    process: &'a Process,
    ptr: TypedPluginPtr<T>,
}

impl<'a, T> ProcessMemoryReader<'a, T>
where
    T: Pod,
{
    /// Get a read-only slice.
    ///
    /// This *may* have to copy the data. If you need a copy anyway use the
    /// `std::io::Read` trait implementation avoid an extra copy.
    ///
    /// Fails if the source pointer is unaligned.
    ///
    /// Result's lifetime is intentionally bound to that of the underlying
    /// `Process` reference rather than the lifetime of this object. That allows
    /// this object to be discarded. e.g.:
    ///
    /// ```
    /// let tv : &timeval = process.reader(ptr, 1).as_ref().unwrap()[0];
    /// ```
    pub fn as_ref(&self) -> Result<&'a [T], Errno> {
        if self.ptr.items_remaining()? == 0 {
            return Ok(&[][..]);
        }
        // SAFETY: This never returns an invalid pointer. Passing a `p` outside
        // of the plugin's addressable memory will return a NULL pointer.
        let ptr = unsafe {
            cshadow::process_getReadablePtr(
                self.process.cprocess,
                self.ptr.ptr()?.into(),
                self.ptr.bytes_remaining()? as u64,
            )
        } as *const T;

        if ptr.is_null() {
            let e = Errno::EFAULT;
            warn!("Reading {:?}: {}", self.ptr, e);
            return Err(e);
        }
        if ptr.align_offset(align_of::<T>()) != 0 {
            warn!("Unaligned pointer {:?}", ptr);
            return Err(Errno::EFAULT);
        }

        // SAFETY: Any values are ok because T is Pod. We've validated the
        // pointer is accessible and aligned.
        Ok(unsafe { std::slice::from_raw_parts(ptr, self.ptr.items_remaining()?) })
    }
}

impl<'a> Seek for ProcessMemoryReader<'a, u8> {
    fn seek(&mut self, pos: SeekFrom) -> std::io::Result<u64> {
        self.ptr.seek(pos)
    }
}

impl<'a> Read for ProcessMemoryReader<'a, u8> {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        let toread = std::cmp::min(buf.len(), self.ptr.bytes_remaining()?);
        debug!(
            "Preparing to read {:?} into buffer of size {}",
            self.ptr,
            buf.len()
        );

        if toread == 0 {
            return Ok(0);
        }

        let status = unsafe {
            cshadow::process_readPtr(
                self.process.cprocess,
                buf.as_mut_ptr() as *mut std::os::raw::c_void,
                self.ptr.ptr()?.into(),
                toread as u64,
            )
        };
        if status != 0 {
            let e = std::io::Error::from_raw_os_error(status);
            warn!("Reading {} bytes from {:?}: {}", toread, self.ptr, e);
            return Err(e);
        }
        self.ptr.seek(SeekFrom::Current(toread as i64)).unwrap();

        Ok(toread)
    }
}

pub struct ProcessMemoryWriter<'a, T> {
    process: &'a mut Process,
    ptr: TypedPluginPtr<T>,
}

impl<'a> Seek for ProcessMemoryWriter<'a, u8> {
    fn seek(&mut self, pos: SeekFrom) -> std::io::Result<u64> {
        self.ptr.seek(pos)
    }
}

impl<'a, T> ProcessMemoryWriter<'a, T> {
    pub fn flush(&mut self) -> Result<(), Errno> {
        // TODO: Change process_flushPtrs to propagate errors.
        unsafe { cshadow::process_flushPtrs(self.process.cprocess) };
        Ok(())
    }

    /// Get a mutable slice to `src`. May return an error if the memory can't
    /// be accessed. In some cases the returned reference is to local memory that
    /// will be flushed later, in which case any errors will be deferred until
    /// then. Returns an empty slice for len == 0.
    ///
    /// The initial contents of the returned slice is unspecified.
    ///
    /// *May* return a temporary buffer that will later be written back into the
    /// process. Use the `std::io::Write` trait when feasible to avoid this
    /// potential extra copy.
    ///
    /// Unlike `ProcessMemoryReader::as_ref`, the result's lifetime is bound to
    /// that of *this* object, so that we can ensure writes are flushed.
    pub fn as_mut_uninit(&mut self) -> Result<&mut [T], Errno>
    where
        T: Pod,
    {
        if self.ptr.bytes_remaining()? == 0 {
            debug!("as_mut_init returning empty slice");
            return Ok(&mut [][..]);
        }

        // SAFETY: This never returns an invalid pointer. Passing a `p` outside
        // of the plugin's addressable memory will return a NULL pointer.
        let ptr = unsafe {
            cshadow::process_getWriteablePtr(
                self.process.cprocess,
                self.ptr.ptr()?.into(),
                self.ptr.bytes_remaining()? as u64,
            )
        } as *mut T;

        if ptr.is_null() {
            warn!("Couldn't write {:?}", self.ptr);
            return Err(Errno::EFAULT);
        }
        if ptr.align_offset(align_of::<T>()) != 0 {
            warn!("Unaligned pointer {:?}", ptr);
            return Err(Errno::EFAULT);
        }

        debug!("as_mut_uninit got {:?}", ptr);

        // SAFETY: Any values are ok because T is Pod. We've validated the
        // pointer is accessible and aligned.
        Ok(unsafe { std::slice::from_raw_parts_mut(ptr, self.ptr.items_remaining()?) })
    }

    /// Get a mutable slice to `src`. Returns an error if the memory can't
    /// be accessed. Returns an empty slice for len == 0.
    ///
    /// *May* return a temporary buffer that will later be written back into the
    /// process. Use the `std::io::Write` trait when feasible to avoid this
    /// potential extra copy.
    ///
    /// When a temporary buffer is needed, also needs to copy the original
    /// contents of the specified memory. Prefer `as_mut_uninit` if you don't
    /// need the initial state.
    ///
    /// Unlike `ProcessMemoryReader::as_ref`, the result's lifetime is bound to
    /// that of *this* object, so that we can ensure writes are flushed.
    pub fn as_mut(&mut self) -> Result<&mut [T], Errno>
    where
        T: Pod,
    {
        if self.ptr.items_remaining()? == 0 {
            return Ok(&mut [][..]);
        }

        // SAFETY: This never returns an invalid pointer. Passing a `p` outside
        // of the plugin's addressable memory will return a NULL pointer.
        let ptr = unsafe {
            cshadow::process_getMutablePtr(
                self.process.cprocess,
                self.ptr.ptr()?.into(),
                self.ptr.bytes_remaining()? as u64,
            )
        } as *mut T;

        if ptr.is_null() {
            warn!("Couldn't write {:?}", ptr);
            return Err(Errno::EFAULT);
        }
        if ptr.align_offset(align_of::<T>()) != 0 {
            warn!("Unaligned pointer {:?}", ptr);
            return Err(Errno::EFAULT);
        }

        // SAFETY: Any values are ok because T is Pod. We've validated the
        // pointer is accessible and aligned.
        Ok(unsafe { std::slice::from_raw_parts_mut(ptr, self.ptr.items_remaining()?) })
    }
}

impl<'a> Write for ProcessMemoryWriter<'a, u8> {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        // Don't write more than we have
        let towrite = std::cmp::min(buf.len(), self.ptr.bytes_remaining()?);
        debug!("Preparing to write {} bytes into {:?}", towrite, self.ptr);
        if towrite == 0 {
            return Ok(0);
        }

        let status = unsafe {
            cshadow::process_writePtr(
                self.process.cprocess,
                self.ptr.ptr()?.into(),
                buf.as_ptr() as *const std::os::raw::c_void,
                towrite as u64,
            )
        };
        if status != 0 {
            let e = std::io::Error::from_raw_os_error(status);
            warn!("Writing {} bytes to {:?}: {}", towrite, self.ptr, e);
            return Err(std::io::Error::from_raw_os_error(status));
        }
        self.ptr.seek(SeekFrom::Current(towrite as i64)).unwrap();
        Ok(towrite)
    }

    fn flush(&mut self) -> std::io::Result<()> {
        ProcessMemoryWriter::flush(self).map_err(|e| std::io::Error::from_raw_os_error(e as i32))
    }
}

impl<'a, T> Drop for ProcessMemoryWriter<'a, T> {
    fn drop(&mut self) {
        // Avoid syscall handlers needing to flush explicitly.
        self.flush().unwrap()
    }
}
