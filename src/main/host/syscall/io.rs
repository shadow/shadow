use std::mem::MaybeUninit;

use crate::host::memory_manager::MemoryManager;
use crate::host::syscall_types::{PluginPtr, SyscallError, TypedPluginPtr};
use crate::utility::sockaddr::SockaddrStorage;
use crate::utility::{pod, NoTypeInference};

use nix::errno::Errno;

pub fn write_sockaddr(
    mem: &mut MemoryManager,
    addr: Option<&SockaddrStorage>,
    plugin_addr: PluginPtr,
    plugin_addr_len: TypedPluginPtr<libc::socklen_t>,
) -> Result<(), SyscallError> {
    let addr = match addr {
        Some(x) => x,
        None => {
            mem.copy_to_ptr(plugin_addr_len, &[0])?;
            return Ok(());
        }
    };

    let from_addr_slice = addr.as_slice();
    let from_len: u32 = from_addr_slice.len().try_into().unwrap();

    // get the provided address buffer length, and overwrite it with the real address length
    let plugin_addr_len = {
        let mut plugin_addr_len = mem.memory_ref_mut(plugin_addr_len)?;
        let plugin_addr_len_value = plugin_addr_len.get_mut(0).unwrap();

        // keep a copy before we change it
        let plugin_addr_len_copy = *plugin_addr_len_value;

        *plugin_addr_len_value = from_len;

        plugin_addr_len.flush()?;
        plugin_addr_len_copy
    };

    // return early if the address length is 0
    if plugin_addr_len == 0 {
        return Ok(());
    }

    // the minimum of the given address buffer length and the real address length
    let len_to_copy = std::cmp::min(from_len, plugin_addr_len).try_into().unwrap();

    let plugin_addr = TypedPluginPtr::new::<MaybeUninit<u8>>(plugin_addr, len_to_copy);
    mem.copy_to_ptr(plugin_addr, &from_addr_slice[..len_to_copy])?;

    Ok(())
}

pub fn read_sockaddr(
    mem: &MemoryManager,
    addr_ptr: PluginPtr,
    addr_len: libc::socklen_t,
) -> Result<Option<SockaddrStorage>, SyscallError> {
    if addr_ptr.is_null() {
        return Ok(None);
    }

    let addr_len_usize: usize = addr_len.try_into().unwrap();

    // this won't have the correct alignment, but that's fine since `SockaddrStorage::from_bytes()`
    // doesn't require alignment
    let mut addr_buf = [MaybeUninit::new(0u8); std::mem::size_of::<libc::sockaddr_storage>()];

    // make sure we will not lose data when we copy
    if addr_len_usize > std::mem::size_of_val(&addr_buf) {
        log::warn!(
            "Shadow does not support the address length {}, which is larger than {}",
            addr_len,
            std::mem::size_of_val(&addr_buf),
        );
        return Err(Errno::EINVAL.into());
    }

    let addr_buf = &mut addr_buf[..addr_len_usize];

    mem.copy_from_ptr(
        addr_buf,
        TypedPluginPtr::new::<MaybeUninit<u8>>(addr_ptr, addr_len_usize),
    )?;

    let addr = unsafe { SockaddrStorage::from_bytes(addr_buf).ok_or(Errno::EINVAL)? };

    Ok(Some(addr))
}

/// Writes `val` to `val_ptr`, but will only write a partial value if `val_len` is smaller than the
/// size of `val`. Returns the number of bytes written.
///
/// The generic type must be given explicitly to prevent accidentally writing the wrong type.
///
/// ```ignore
/// let bytes_written = write_partial::<i32, _>(mem, foo(), ptr, len)?;
/// ```
pub fn write_partial<U: NoTypeInference<This = T>, T: pod::Pod>(
    mem: &mut MemoryManager,
    val: &T,
    val_ptr: PluginPtr,
    val_len: usize,
) -> Result<usize, SyscallError> {
    let val_len = std::cmp::min(val_len, std::mem::size_of_val(val));

    let val = &pod::as_u8_slice(val)[..val_len];
    let val_ptr = TypedPluginPtr::new::<MaybeUninit<u8>>(val_ptr, val_len);

    mem.copy_to_ptr(val_ptr, val)?;

    Ok(val_len)
}
