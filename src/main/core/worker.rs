use crate::cshadow;
use crate::host::syscall_types::PluginPtr;
use crate::utility::pod::Pod;
use std::cell::RefCell;
use std::mem::MaybeUninit;

// Worker context, capturing e.g. the current Process and Thread.
// This is currently just a marker, since we actually access the context through
// the C Worker APIs. Eventually it'll store the Worker and/or its stored context, though.
pub struct Context {}

std::thread_local! {
    static CURRENT_CONTEXT: RefCell<Context> = RefCell::new(Context { });
}

impl Context {
    pub fn with_current<F, R>(f: F) -> R
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
        // FIXME: Make this actually safe by using the MemoryManager to
        // validate bounds, even if the MemoryManager hasn't remapped the pointer.
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
        // FIXME: Make this actually safe by using the MemoryManager to
        // validate bounds, even if the MemoryManager hasn't remapped the pointer.
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

    pub fn get_ref<T: Pod>(&self, src: PluginPtr) -> nix::Result<&T> {
        let raw = self.get_readable_ptr(src, std::mem::size_of::<T>())?;
        // SAFETY: `get_readable_ptr` already checked bounds, and since T
        // is Pod, any data is a valid T.
        Ok(unsafe { &*(raw as *const T) })
    }

    pub fn get_mut_ref<T: Pod>(&mut self, src: PluginPtr) -> nix::Result<&mut T> {
        let raw = self.get_mutable_ptr(src, std::mem::size_of::<T>())?;
        // SAFETY: `get_mutable_ptr` already checked bounds, and since T
        // is Pod, any data is a valid T.
        Ok(unsafe { &mut *(raw as *mut T) })
    }

    pub fn get_writable_ref<T: Pod>(&mut self, src: PluginPtr) -> nix::Result<&mut T> {
        let raw = self.get_writable_ptr(src, std::mem::size_of::<T>())?;
        // SAFETY: `get_writable_ptr` already checked bounds, and since T
        // is Pod, any data is a valid T. (Even though the data there isn't
        // necessarily even what was at *src)
        Ok(unsafe { &mut *(raw as *mut T) })
    }

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
