use crate::cshadow;
use crate::host::syscall_types::PluginPtr;
use crate::utility::pod::Pod;
use std::mem::MaybeUninit;

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

    /// Read data from `src` in the current Process into a potentially-uninitialized `dst`.
    pub fn read_ptr_into_slice<T: Pod>(&self, dst: &mut [T], src: PluginPtr) -> nix::Result<()> {
        let status = unsafe {
            cshadow::process_readPtr(
                self.cprocess,
                dst.as_mut_ptr() as *mut std::os::raw::c_void,
                src.into(),
                (dst.len() * std::mem::size_of::<T>()) as u64,
            )
        };
        if status == 0 {
            Ok(())
        } else {
            Err(nix::Error::from_errno(nix::errno::from_i32(status as i32)))
        }
    }

    /// Read data from `src` in the current Process into `dst`.
    pub fn read_ptr_into_val<'a, T: Pod>(&self, dst: &mut T, src: PluginPtr) -> nix::Result<()> {
        let mut slice = std::slice::from_mut(dst);
        self.read_ptr_into_slice(&mut slice, src)?;
        Ok(())
    }

    /// Interpret `src` as a pointer to `T` in the current Process in `dst` and return its value.
    pub fn read_ptr_as_val<T: Pod>(&self, src: PluginPtr) -> nix::Result<T> {
        // SAFETY: Since `T` is Pod, any bit pattern is a valid `T`.
        let mut val: T = unsafe { MaybeUninit::uninit().assume_init() };
        self.read_ptr_into_val(&mut val, src)?;
        Ok(val)
    }

    /// Write `src` into `dst` in the current Process.
    pub fn write_ptr_from_slice<T: Copy>(&mut self, dst: PluginPtr, src: &[T]) -> nix::Result<()> {
        let status = unsafe {
            cshadow::process_writePtr(
                self.cprocess,
                dst.into(),
                src.as_ptr() as *const std::os::raw::c_void,
                (src.len() * std::mem::size_of::<T>()) as u64,
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

    // For simplicity, never returns NULL, even for s == 0.
    fn get_readable_ptr(
        &self,
        p: PluginPtr,
        s: usize,
    ) -> nix::Result<*const ::std::os::raw::c_void> {
        // SAFETY: This never returns an invalid pointer. Passing a `p` outside
        // of the plugin's addressable memory will return a NULL pointer.
        let ptr = unsafe { cshadow::process_getReadablePtr(self.cprocess, p.into(), s as u64) };
        if ptr.is_null() {
            Err(nix::Error::from_errno(nix::errno::Errno::EFAULT))
        } else {
            Ok(ptr)
        }
    }

    // For simplicity, never returns NULL, even for s == 0.
    fn get_writable_ptr(
        &mut self,
        p: PluginPtr,
        s: usize,
    ) -> nix::Result<*mut ::std::os::raw::c_void> {
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
        let ptr = unsafe { cshadow::process_getWriteablePtr(self.cprocess, p.into(), s as u64) };
        if ptr.is_null() {
            Err(nix::Error::from_errno(nix::errno::Errno::EFAULT))
        } else {
            Ok(ptr)
        }
    }

    fn get_mutable_ptr(
        &mut self,
        p: PluginPtr,
        s: usize,
    ) -> nix::Result<*mut ::std::os::raw::c_void> {
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
        let ptr = unsafe { cshadow::process_getMutablePtr(self.cprocess, p.into(), s as u64) };
        if ptr.is_null() {
            Err(nix::Error::from_errno(nix::errno::Errno::EFAULT))
        } else {
            Ok(ptr)
        }
    }

    /// Get a read-only reference to `src`. Returns an error if the memory can't
    /// be accessed.
    ///
    /// Prefer the `read_ptr_*` methods for small objects or when the data must
    /// be copied anyway.
    pub fn get_ref<T: Pod>(&self, src: PluginPtr) -> nix::Result<&T> {
        let raw = self.get_readable_ptr(src, std::mem::size_of::<T>())?;
        // SAFETY: `get_readable_ptr` already checked bounds, and since T
        // is Pod, any data is a valid T.
        Ok(unsafe { &*(raw as *const T) })
    }

    /// Get a mutable reference to `src`. May return an error if the memory can't
    /// be accessed. In some cases the returned reference is to local memory that
    /// will be flushed later, in which case any errors will be deferred until
    /// then.
    ///
    /// Prefer the `write_ptr_*` methods when feasible, and `get_writable_ref` when
    /// you don't need the original value stored at `src`.
    pub fn get_mut_ref<T: Pod>(&mut self, src: PluginPtr) -> nix::Result<&mut T> {
        let raw = self.get_mutable_ptr(src, std::mem::size_of::<T>())?;
        // SAFETY: `get_mutable_ptr` already checked bounds, and since T
        // is Pod, any data is a valid T.
        Ok(unsafe { &mut *(raw as *mut T) })
    }

    /// As `get_mut_ref`, but the initial value of the reference is unspecified.
    ///
    /// Prefer the `write_ptr_*` methods.
    pub fn get_writable_ref<T: Pod>(&mut self, src: PluginPtr) -> nix::Result<&mut T> {
        let raw = self.get_writable_ptr(src, std::mem::size_of::<T>())?;
        // SAFETY: `get_writable_ptr` already checked bounds, and since T
        // is Pod, any data is a valid T. (Even though the data there isn't
        // necessarily even what was at *src)
        Ok(unsafe { &mut *(raw as *mut T) })
    }

    /// Get a read-only slice at `src`. Returns an error if the memory can't
    /// be accessed.
    ///
    /// Prefer the `read_ptr_*` methods for small objects or when the data must
    /// be copied anyway.
    pub fn get_slice<T>(&self, src: PluginPtr, len: usize) -> nix::Result<&[T]> {
        let raw = self.get_readable_ptr(src, std::mem::size_of::<T>() * len)?;
        // SAFETY: `get_readable_ptr` already checked bounds, and since T
        // is Pod, any data is a valid T.
        Ok(unsafe { std::slice::from_raw_parts(raw as *const T, len) })
    }

    /// Get a mutable slice to `src`. May return an error if the memory can't
    /// be accessed. In some cases the returned reference is to local memory that
    /// will be flushed later, in which case any errors will be deferred until
    /// then.
    ///
    /// Prefer the `write_ptr_*` methods when feasible, and `get_writable_ref` when
    /// you don't need the original value stored at `src`.
    pub fn get_mut_slice<T>(&mut self, src: PluginPtr, len: usize) -> nix::Result<&mut [T]> {
        let raw = self.get_mutable_ptr(src, std::mem::size_of::<T>() * len)?;
        // SAFETY: `get_mutable_ptr` already checked bounds, and since T
        // is Pod, any data is a valid T.
        Ok(unsafe { std::slice::from_raw_parts_mut(raw as *mut T, len) })
    }

    /// As `get_mut_slice`, but the initial value is unspecified.
    ///
    /// Prefer the `write_ptr_*` methods.
    pub fn get_writable_slice<T>(&mut self, src: PluginPtr, len: usize) -> nix::Result<&mut [T]> {
        let raw = self.get_writable_ptr(src, std::mem::size_of::<T>() * len)?;
        // SAFETY: `get_mutable_ptr` already checked bounds, and since T
        // is Pod, any data is a valid T.
        Ok(unsafe { std::slice::from_raw_parts_mut(raw as *mut T, len) })
    }
}
