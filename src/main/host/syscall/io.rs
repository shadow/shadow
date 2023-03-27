use std::mem::MaybeUninit;
use std::ops::Deref;

use crate::host::memory_manager::MemoryManager;
use crate::host::syscall_types::{SyscallError, TypedPluginPtr};
use crate::utility::sockaddr::SockaddrStorage;
use crate::utility::{pod, NoTypeInference};
use shadow_shim_helper_rs::syscall_types::PluginPtr;

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

/// Analogous to [`libc::iovec`].
#[derive(Copy, Clone, PartialEq, Eq)]
pub struct IoVec {
    pub base: PluginPtr,
    pub len: libc::size_t,
}

impl From<IoVec> for TypedPluginPtr<u8> {
    fn from(iov: IoVec) -> Self {
        Self::new::<u8>(iov.base, iov.len)
    }
}

impl From<TypedPluginPtr<u8>> for IoVec {
    fn from(ptr: TypedPluginPtr<u8>) -> Self {
        IoVec {
            base: ptr.ptr(),
            len: ptr.len(),
        }
    }
}

/// A reader which reads data from [`IoVec`] buffers of plugin memory. If an error occurs while
/// reading (for example if an `IoVec` points to an invalid memory address), the error will be
/// returned only if no bytes have yet been read. If an error occurs after some bytes have already
/// been read, the [`Read::read`](std::io::Read::read) will return how many bytes have been read.
///
/// In the future we may want to merge this with
/// [`MemoryReaderCursor`](crate::host::memory_manager::MemoryReaderCursor).
pub struct IoVecReader<'a, I> {
    iovs: I,
    mem: &'a MemoryManager,
    /// A plugin pointer for the current iov.
    current_src: Option<TypedPluginPtr<u8>>,
}

impl<'a, I> IoVecReader<'a, I> {
    pub fn new<'b>(
        iovs: impl IntoIterator<Item = &'b IoVec, IntoIter = I>,
        mem: &'a MemoryManager,
    ) -> Self {
        Self {
            iovs: iovs.into_iter(),
            mem,
            current_src: None,
        }
    }
}

impl<'a, I: Iterator<Item = &'a IoVec>> std::io::Read for IoVecReader<'a, I> {
    fn read(&mut self, mut buf: &mut [u8]) -> std::io::Result<usize> {
        let mut bytes_read = 0;

        loop {
            // we filled the buffer
            if buf.is_empty() {
                break;
            }

            if let Some(ref mut src) = self.current_src {
                let num_to_read = std::cmp::min(src.len(), buf.len());
                let result = self
                    .mem
                    .copy_from_ptr(&mut buf[..num_to_read], src.slice(..num_to_read));

                match (result, bytes_read) {
                    // we successfully read the bytes
                    (Ok(()), _) => {}
                    // we haven't yet read any bytes, so return the error
                    (Err(e), 0) => return Err(e.into()),
                    // return how many bytes we've read
                    (Err(_), _) => break,
                }

                bytes_read += num_to_read;
                buf = &mut buf[num_to_read..];
                *src = src.slice(num_to_read..);

                if src.is_empty() {
                    // no bytes remaining in this iov
                    self.current_src = None;
                }
            } else {
                let Some(next_iov) = self.iovs.next() else {
                    // no iovs remaining
                    break;
                };
                self.current_src = Some((*next_iov).into());
            }
        }

        Ok(bytes_read)
    }
}

/// A writer which writes data to [`IoVec`] buffers of plugin memory. If an error occurs while
/// writing (for example if an `IoVec` points to an invalid memory address), the error will be
/// returned only if no bytes have yet been written. If an error occurs after some bytes have
/// already been written, the [`Write::write`](std::io::Write::write) will return how many bytes
/// have been written.
///
/// In the future we may want to merge this with
/// [`MemoryWriterCursor`](crate::host::memory_manager::MemoryWriterCursor).
pub struct IoVecWriter<'a, I> {
    iovs: I,
    mem: &'a mut MemoryManager,
    /// A plugin pointer for the current iov.
    current_dst: Option<TypedPluginPtr<u8>>,
}

impl<'a, I> IoVecWriter<'a, I> {
    pub fn new<'b>(
        iovs: impl IntoIterator<Item = &'b IoVec, IntoIter = I>,
        mem: &'a mut MemoryManager,
    ) -> Self {
        Self {
            iovs: iovs.into_iter(),
            mem,
            current_dst: None,
        }
    }
}

impl<'a, I: Iterator<Item = &'a IoVec>> std::io::Write for IoVecWriter<'a, I> {
    fn write(&mut self, mut buf: &[u8]) -> std::io::Result<usize> {
        let mut bytes_written = 0;

        loop {
            // no bytes left to write
            if buf.is_empty() {
                break;
            }

            if let Some(ref mut dst) = self.current_dst {
                let num_to_write = std::cmp::min(dst.len(), buf.len());
                let result = self
                    .mem
                    .copy_to_ptr(dst.slice(..num_to_write), &buf[..num_to_write]);

                match (result, bytes_written) {
                    // we successfully wrote the bytes
                    (Ok(()), _) => {}
                    // we haven't yet written any bytes, so return the error
                    (Err(e), 0) => return Err(e.into()),
                    // return how many bytes we've written
                    (Err(_), _) => break,
                }

                bytes_written += num_to_write;
                buf = &buf[num_to_write..];
                *dst = dst.slice(num_to_write..);

                if dst.is_empty() {
                    // no space remaining in this iov
                    self.current_dst = None;
                }
            } else {
                let Some(next_iov) = self.iovs.next() else {
                    // no iovs remaining
                    break;
                };
                self.current_dst = Some((*next_iov).into());
            }
        }

        Ok(bytes_written)
    }

    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

/// Read a plugin's array of [`libc::iovec`] into a [`Vec<IoVec>`].
pub fn read_iovecs(
    mem: &MemoryManager,
    iov_ptr: PluginPtr,
    count: usize,
) -> Result<Vec<IoVec>, Errno> {
    if count > libc::UIO_MAXIOV.try_into().unwrap() {
        return Err(Errno::EINVAL);
    }

    let mut iovs = Vec::with_capacity(count);

    let iov_ptr = TypedPluginPtr::new::<libc::iovec>(iov_ptr, count);
    let mem_ref = mem.memory_ref(iov_ptr)?;
    let plugin_iovs = mem_ref.deref();

    for plugin_iov in plugin_iovs {
        iovs.push(IoVec {
            base: PluginPtr::from_raw_ptr(plugin_iov.iov_base),
            len: plugin_iov.iov_len,
        });
    }

    Ok(iovs)
}
