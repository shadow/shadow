use std::convert::TryInto;
use std::sync::Arc;

use atomic_refcell::AtomicRefCell;

use crate::cshadow as c;
use crate::host::descriptor::{CompatDescriptor, FileStatus, PosixFile};

pub mod unistd;

struct Trigger(c::Trigger);

impl From<c::Trigger> for Trigger {
    fn from(trigger: c::Trigger) -> Self {
        Self(trigger)
    }
}

impl From<Trigger> for c::Trigger {
    fn from(trigger: Trigger) -> Self {
        trigger.0
    }
}

impl Trigger {
    pub fn from_posix_file(file: &Arc<AtomicRefCell<PosixFile>>, status: FileStatus) -> Self {
        let file_ptr = Arc::into_raw(Arc::clone(file));

        Self(c::Trigger {
            type_: c::_TriggerType_TRIGGER_POSIX_FILE,
            object: c::TriggerObject {
                as_file: file_ptr as *const c::PosixFileArc,
            },
            status: status.into(),
        })
    }
}

impl c::SysCallReturn {
    pub fn from_errno(errno: nix::errno::Errno) -> Self {
        Self {
            state: c::SysCallReturnState_SYSCALL_DONE,
            retval: c::SysCallReg {
                as_i64: -(errno as i64),
            },
            cond: std::ptr::null_mut(),
        }
    }

    pub fn from_int(int: i64) -> Self {
        Self {
            state: c::SysCallReturnState_SYSCALL_DONE,
            retval: c::SysCallReg { as_i64: int },
            cond: std::ptr::null_mut(),
        }
    }
}

/// Returns a pointer to the `CompatDescriptor` for the fd. The pointer will never be NULL.
pub fn get_descriptor(
    fd: libc::c_int,
    process: *mut c::Process,
) -> Result<*mut CompatDescriptor, nix::errno::Errno> {
    // check that fd is within bounds
    if fd < 0 {
        return Err(nix::errno::Errno::EBADF);
    }

    // check that the fd exists
    let desc = unsafe { c::process_getRegisteredCompatDescriptor(process, fd) };
    if desc.is_null() {
        return Err(nix::errno::Errno::EBADF);
    }

    Ok(desc)
}

pub enum PluginPtrError {
    /// Was `NULL` inside the plugin.
    Null,
    /// Length was 0.
    ZeroLen,
    /// Length was too small to represent the generic type `T`.
    LenTooSmall,
}

pub fn get_readable_ptr<T>(
    process: *mut c::Process,
    thread: *mut c::Thread,
    plugin_ptr: c::PluginPtr,
    num_bytes: usize,
) -> Result<*const T, PluginPtrError> {
    if plugin_ptr.val == 0 {
        return Err(PluginPtrError::Null);
    }

    if num_bytes == 0 {
        return Err(PluginPtrError::ZeroLen);
    }

    if num_bytes < std::mem::size_of::<T>() {
        return Err(PluginPtrError::LenTooSmall);
    }

    let num_bytes = num_bytes.try_into().unwrap();

    let ptr = unsafe { c::process_getReadablePtr(process, thread, plugin_ptr, num_bytes) };
    let ptr = ptr as *const T;
    assert!(!ptr.is_null());

    // check pointer alignment
    assert_eq!((ptr as usize) % std::mem::align_of::<T>(), 0);

    Ok(ptr)
}

pub fn get_writable_ptr<T>(
    process: *mut c::Process,
    thread: *mut c::Thread,
    plugin_ptr: c::PluginPtr,
    num_bytes: usize,
) -> Result<*mut T, PluginPtrError> {
    if plugin_ptr.val == 0 {
        return Err(PluginPtrError::Null);
    }

    if num_bytes == 0 {
        return Err(PluginPtrError::ZeroLen);
    }

    if num_bytes < std::mem::size_of::<T>() {
        return Err(PluginPtrError::LenTooSmall);
    }

    let num_bytes = num_bytes.try_into().unwrap();

    let ptr = unsafe { c::process_getWriteablePtr(process, thread, plugin_ptr, num_bytes) };
    let ptr = ptr as *mut T;
    assert!(!ptr.is_null());

    // check pointer alignment
    assert_eq!((ptr as usize) % std::mem::align_of::<T>(), 0);

    Ok(ptr)
}

pub fn get_mutable_ptr<T>(
    process: *mut c::Process,
    thread: *mut c::Thread,
    plugin_ptr: c::PluginPtr,
    num_bytes: usize,
) -> Result<*mut T, PluginPtrError> {
    if plugin_ptr.val == 0 {
        return Err(PluginPtrError::Null);
    }

    if num_bytes == 0 {
        return Err(PluginPtrError::ZeroLen);
    }

    if num_bytes < std::mem::size_of::<T>() {
        return Err(PluginPtrError::LenTooSmall);
    }

    let num_bytes = num_bytes.try_into().unwrap();

    let ptr = unsafe { c::process_getMutablePtr(process, thread, plugin_ptr, num_bytes) };
    let ptr = ptr as *mut T;
    assert!(!ptr.is_null());

    // check pointer alignment
    assert_eq!((ptr as usize) % std::mem::align_of::<T>(), 0);

    Ok(ptr)
}
