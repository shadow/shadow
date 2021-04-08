use crate::cshadow;
use crate::host::syscall_types::PluginPtr;
use crate::utility::pod::Pod;
use std::cell::RefCell;
use std::mem::MaybeUninit;

/// Worker context, capturing e.g. the current Process and Thread.
// This is currently just a marker, since we actually access the context through
// the C Worker APIs. Eventually it'll store the Worker and/or its stored
// context, though.
pub struct Context {}

std::thread_local! {
    static CURRENT_CONTEXT: RefCell<Context> = RefCell::new(Context { });
}

impl Context {
    /// Run `f` with a reference to the current context.
    pub fn with_current<F, R>(f: F) -> R
    where
        F: FnOnce(&Context) -> R,
    {
        CURRENT_CONTEXT.with(|c| f(&*c.borrow()))
    }

    /// Run `f` with a mut reference to the current context.
    pub fn with_current_mut<F, R>(f: F) -> R
    where
        F: FnOnce(&mut Context) -> R,
    {
        CURRENT_CONTEXT.with(|c| f(&mut *c.borrow_mut()))
    }

    /// Read data from `src` in the current Process into `dst`.
    pub fn read_ptr_into_slice<T: Pod>(
        &self,
        dst: &mut [MaybeUninit<T>],
        src: PluginPtr,
    ) -> nix::Result<()> {
        let status = unsafe {
            cshadow::worker_readPtr(
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
    pub fn read_ptr_into_val<T: Pod>(
        &self,
        dst: &mut MaybeUninit<T>,
        src: PluginPtr,
    ) -> nix::Result<()> {
        let mut s = std::slice::from_mut(dst);
        self.read_ptr_into_slice(&mut s, src)
    }

    /// Interpret `src` as a pointer to `T` in the current Process in `dst` and return its value.
    pub fn read_ptr_as_val<T: Pod>(&self, src: PluginPtr) -> nix::Result<T> {
        let mut val = MaybeUninit::uninit();
        self.read_ptr_into_val(&mut val, src)?;
        Ok(unsafe { val.assume_init() })
    }

    /// Write `src` into `dst` in the current Process.
    pub fn write_ptr_from_slice<T: Copy>(&mut self, dst: PluginPtr, src: &[T]) -> nix::Result<()> {
        let status = unsafe {
            cshadow::worker_writePtr(
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

    fn get_readable_ptr(
        &self,
        p: PluginPtr,
        s: usize,
    ) -> nix::Result<*const ::std::os::raw::c_void> {
        if s == 0 {
            return Ok(std::ptr::null());
        }
        // SAFETY: This never returns an invalid pointer. Passing a `p` outside
        // of the plugin's addressable memory will return a NULL pointer.
        Ok(unsafe { cshadow::worker_getReadablePtr(p.into(), s as u64) })
    }

    fn get_writable_ptr(
        &mut self,
        p: PluginPtr,
        s: usize,
    ) -> nix::Result<*mut ::std::os::raw::c_void> {
        if s == 0 {
            return Ok(std::ptr::null_mut());
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
        Ok(unsafe { cshadow::worker_getWritablePtr(p.into(), s as u64) })
    }

    fn get_mutable_ptr(
        &mut self,
        p: PluginPtr,
        s: usize,
    ) -> nix::Result<*mut ::std::os::raw::c_void> {
        if s == 0 {
            return Ok(std::ptr::null_mut());
        }
        // FIXME: Make this actually safe by using the MemoryManager to
        // validate bounds, even if the MemoryManager hasn't remapped the pointer.
        Ok(unsafe { cshadow::worker_getMutablePtr(p.into(), s as u64) })
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
        // Ideally we'd just return an empty slice, but it's impossible to construct
        // an empty one without a valid pointer on rust stable.
        if raw.is_null() && len == 0 {
            return Err(nix::Error::from(nix::errno::Errno::EINVAL));
        }
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
        // Ideally we'd just return an empty slice, but it's impossible to construct
        // an empty one without a valid pointer on rust stable.
        if raw.is_null() && len == 0 {
            return Err(nix::Error::from(nix::errno::Errno::EINVAL));
        }
        // SAFETY: `get_mutable_ptr` already checked bounds, and since T
        // is Pod, any data is a valid T.
        Ok(unsafe { std::slice::from_raw_parts_mut(raw as *mut T, len) })
    }

    /// As `get_mut_slice`, but the initial value is unspecified.
    ///
    /// Prefer the `write_ptr_*` methods.
    pub fn get_writable_slice<T>(&mut self, src: PluginPtr, len: usize) -> nix::Result<&mut [T]> {
        let raw = self.get_writable_ptr(src, std::mem::size_of::<T>() * len)?;
        // Ideally we'd just return an empty slice, but it's impossible to construct
        // an empty one without a valid pointer on rust stable.
        if raw.is_null() && len == 0 {
            return Err(nix::Error::from(nix::errno::Errno::EINVAL));
        }
        // SAFETY: `get_mutable_ptr` already checked bounds, and since T
        // is Pod, any data is a valid T.
        Ok(unsafe { std::slice::from_raw_parts_mut(raw as *mut T, len) })
    }
}
