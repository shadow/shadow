use std::ffi::CString;
use std::mem::MaybeUninit;
use std::ops::{Deref, DerefMut};

use linux_api::errno::Errno;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::host::memory_manager::MemoryManager;
use crate::host::syscall::types::ForeignArrayPtr;
use crate::utility::sockaddr::SockaddrStorage;

/// Writes the socket address into a buffer at `plugin_addr` with length `plugin_addr_len`, and
/// writes the socket address length into `plugin_addr_len`.
///
/// The `plugin_addr_len` pointer is a value-result argument, so it should be initialized with the
/// size of the `plugin_addr` buffer. If the original value of `plugin_addr_len` is smaller than the
/// socket address' length, then the written socket address will be truncated. In this case the
/// value written to `plugin_addr_len` will be larger than its original value.
pub fn write_sockaddr_and_len(
    mem: &mut MemoryManager,
    addr: Option<&SockaddrStorage>,
    plugin_addr: ForeignPtr<u8>,
    plugin_addr_len: ForeignPtr<libc::socklen_t>,
) -> Result<(), Errno> {
    let addr = match addr {
        Some(x) => x,
        None => {
            mem.write(plugin_addr_len, &0)?;
            return Ok(());
        }
    };

    let from_addr_slice = addr.as_slice();
    let from_len: u32 = from_addr_slice.len().try_into().unwrap();

    // get the provided address buffer length, and overwrite it with the real address length
    let plugin_addr_len = {
        let mut plugin_addr_len = mem.memory_ref_mut(ForeignArrayPtr::new(plugin_addr_len, 1))?;
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

    let plugin_addr = ForeignArrayPtr::new(plugin_addr.cast::<MaybeUninit<u8>>(), len_to_copy);
    mem.copy_to_ptr(plugin_addr, &from_addr_slice[..len_to_copy])?;

    Ok(())
}

/// Writes the socket address into a buffer at `plugin_addr` with length `plugin_addr_len`.
///
/// If the buffer length is smaller than the socket address length, the written address will be
/// truncated. The length of the socket address is returned.
pub fn write_sockaddr(
    mem: &mut MemoryManager,
    addr: &SockaddrStorage,
    plugin_addr: ForeignPtr<u8>,
    plugin_addr_len: libc::socklen_t,
) -> Result<libc::socklen_t, Errno> {
    let from_addr_slice = addr.as_slice();
    let from_len: u32 = from_addr_slice.len().try_into().unwrap();

    // return early if the address length is 0
    if plugin_addr_len == 0 {
        return Ok(from_len);
    }

    // the minimum of the given address buffer length and the real address length
    let len_to_copy = std::cmp::min(from_len, plugin_addr_len).try_into().unwrap();

    let plugin_addr = ForeignArrayPtr::new(plugin_addr.cast::<MaybeUninit<u8>>(), len_to_copy);
    mem.copy_to_ptr(plugin_addr, &from_addr_slice[..len_to_copy])?;

    Ok(from_len)
}

pub fn read_sockaddr(
    mem: &MemoryManager,
    addr_ptr: ForeignPtr<u8>,
    addr_len: libc::socklen_t,
) -> Result<Option<SockaddrStorage>, Errno> {
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
        return Err(Errno::EINVAL);
    }

    let addr_buf = &mut addr_buf[..addr_len_usize];

    mem.copy_from_ptr(
        addr_buf,
        ForeignArrayPtr::new(addr_ptr.cast::<MaybeUninit<u8>>(), addr_len_usize),
    )?;

    let addr = unsafe { SockaddrStorage::from_bytes(addr_buf).ok_or(Errno::EINVAL)? };

    Ok(Some(addr))
}

/// Writes `val` to `val_ptr`, but will only write a partial value if `val_len_bytes` is smaller
/// than the size of `val`. Returns the number of bytes written.
///
/// ```no_run
/// # use shadow_rs::host::memory_manager::MemoryManager;
/// # use shadow_rs::host::syscall::io::write_partial;
/// # use shadow_shim_helper_rs::syscall_types::ForeignPtr;
/// # fn foo() -> anyhow::Result<()> {
/// # let memory_manager: &mut MemoryManager = todo!();
/// let ptr: ForeignPtr<u32> = todo!();
/// let val: u32 = 0xAABBCCDD;
/// // write a single byte of `val` (0xDD on little-endian) to `ptr`
/// let bytes_written = write_partial(memory_manager, &val, ptr, 1)?;
/// assert_eq!(bytes_written, 1);
/// # Ok(())
/// # }
/// ```
pub fn write_partial<T: shadow_pod::Pod>(
    mem: &mut MemoryManager,
    val: &T,
    val_ptr: ForeignPtr<T>,
    val_len_bytes: usize,
) -> Result<usize, Errno> {
    let val_len_bytes = std::cmp::min(val_len_bytes, std::mem::size_of_val(val));

    let val = &shadow_pod::as_u8_slice(val)[..val_len_bytes];

    let val_ptr = val_ptr.cast::<MaybeUninit<u8>>();
    let val_ptr = ForeignArrayPtr::new(val_ptr, val_len_bytes);

    mem.copy_to_ptr(val_ptr, val)?;

    Ok(val_len_bytes)
}

/// Analogous to [`libc::msghdr`].
pub struct MsgHdr {
    pub name: ForeignPtr<u8>,
    pub name_len: libc::socklen_t,
    pub iovs: Vec<IoVec>,
    pub control: ForeignPtr<u8>,
    pub control_len: libc::size_t,
    pub flags: std::ffi::c_int,
}

/// Analogous to [`libc::iovec`].
#[derive(Copy, Clone, PartialEq, Eq)]
pub struct IoVec {
    pub base: ForeignPtr<u8>,
    pub len: libc::size_t,
}

impl From<IoVec> for ForeignArrayPtr<u8> {
    fn from(iov: IoVec) -> Self {
        Self::new(iov.base, iov.len)
    }
}

impl From<ForeignArrayPtr<u8>> for IoVec {
    fn from(ptr: ForeignArrayPtr<u8>) -> Self {
        IoVec {
            base: ptr.ptr(),
            len: ptr.len(),
        }
    }
}

/// A reader which reads data from [`IoVec`] buffers of plugin memory.
///
/// If an error occurs while reading (for example if an `IoVec` points to an invalid memory
/// address), the error will be returned only if no bytes have yet been read. If an error occurs
/// after some bytes have already been read, the [`Read::read`](std::io::Read::read) will return how
/// many bytes have been read.
///
/// In the future we may want to merge this with
/// [`MemoryReaderCursor`](crate::host::memory_manager::MemoryReaderCursor).
pub struct IoVecReader<'a, I> {
    iovs: I,
    mem: &'a MemoryManager,
    /// A foreign pointer for the current iov.
    current_src: Option<ForeignArrayPtr<u8>>,
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

/// A writer which writes data to [`IoVec`] buffers of plugin memory.
///
/// If an error occurs while writing (for example if an `IoVec` points to an invalid memory
/// address), the error will be returned only if no bytes have yet been written. If an error occurs
/// after some bytes have already been written, the [`Write::write`](std::io::Write::write) will
/// return how many bytes have been written.
///
/// In the future we may want to merge this with
/// [`MemoryWriterCursor`](crate::host::memory_manager::MemoryWriterCursor).
pub struct IoVecWriter<'a, I> {
    iovs: I,
    mem: &'a mut MemoryManager,
    /// A foreign pointer for the current iov.
    current_dst: Option<ForeignArrayPtr<u8>>,
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
    iov_ptr: ForeignPtr<libc::iovec>,
    count: usize,
) -> Result<Vec<IoVec>, Errno> {
    if count > libc::UIO_MAXIOV.try_into().unwrap() {
        return Err(Errno::EINVAL);
    }

    let mut iovs = Vec::with_capacity(count);

    let iov_ptr = ForeignArrayPtr::new(iov_ptr, count);
    let mem_ref = mem.memory_ref(iov_ptr)?;
    let plugin_iovs = mem_ref.deref();

    for plugin_iov in plugin_iovs {
        iovs.push(IoVec {
            base: ForeignPtr::from_raw_ptr(plugin_iov.iov_base as *mut u8),
            len: plugin_iov.iov_len,
        });
    }

    Ok(iovs)
}

/// Read a plugin's [`libc::msghdr`] into a [`MsgHdr`].
pub fn read_msghdr(
    mem: &MemoryManager,
    msg_ptr: ForeignPtr<libc::msghdr>,
) -> Result<MsgHdr, Errno> {
    let msg_ptr = ForeignArrayPtr::new(msg_ptr, 1);
    let mem_ref = mem.memory_ref(msg_ptr)?;
    let plugin_msg = mem_ref.deref()[0];

    msghdr_to_rust(&plugin_msg, mem)
}

/// Used to update a `libc::msghdr`. Only writes the [`libc::msghdr`] `msg_namelen`,
/// `msg_controllen`, and `msg_flags` fields, which are the only fields that can be changed by
/// `recvmsg()`.
pub fn update_msghdr(
    mem: &mut MemoryManager,
    msg_ptr: ForeignPtr<libc::msghdr>,
    msg: MsgHdr,
) -> Result<(), Errno> {
    let msg_ptr = ForeignArrayPtr::new(msg_ptr, 1);
    let mut mem_ref = mem.memory_ref_mut(msg_ptr)?;
    let plugin_msg = &mut mem_ref.deref_mut()[0];

    // write only the msg fields that may have changed
    plugin_msg.msg_namelen = msg.name_len;
    plugin_msg.msg_controllen = msg.control_len;
    plugin_msg.msg_flags = msg.flags;

    mem_ref.flush()?;

    Ok(())
}

/// Helper to read a plugin's [`libc::msghdr`] into a [`MsgHdr`]. While `msg` is a local struct, it
/// should have been copied from plugin memory, meaning any pointers in the struct are pointers to
/// plugin memory, not local memory.
fn msghdr_to_rust(msg: &libc::msghdr, mem: &MemoryManager) -> Result<MsgHdr, Errno> {
    let iovs = read_iovecs(mem, ForeignPtr::from_raw_ptr(msg.msg_iov), msg.msg_iovlen)?;
    assert_eq!(iovs.len(), msg.msg_iovlen);

    Ok(MsgHdr {
        name: ForeignPtr::from_raw_ptr(msg.msg_name as *mut u8),
        name_len: msg.msg_namelen,
        iovs,
        control: ForeignPtr::from_raw_ptr(msg.msg_control as *mut u8),
        control_len: msg.msg_controllen,
        flags: msg.msg_flags,
    })
}

/// Read an array of strings, each of which with max length
/// `linux_api::limits::ARG_MAX`.  e.g. suitable for `execve`'s argument and
/// environment string lists.
pub fn read_cstring_vec(
    mem: &MemoryManager,
    mut ptr_ptr: ForeignPtr<ForeignPtr<i8>>,
) -> Result<Vec<CString>, Errno> {
    let mut res = Vec::new();

    // `execve(2)`: Most UNIX implementations impose some limit on the
    // total size of the command-line  argument  (argv)  and
    // environment  (envp) strings that may be passed to a new program.
    // POSIX.1 allows an implementation to advertise this limit using
    // the ARG_MAX constant
    let mut arg_buf = [0; linux_api::limits::ARG_MAX];

    loop {
        let ptr = mem.read(ptr_ptr)?;
        ptr_ptr = ptr_ptr.add(1);
        if ptr.is_null() {
            break;
        }
        let cstr = mem.copy_str_from_ptr(
            &mut arg_buf,
            ForeignArrayPtr::new(ptr.cast::<u8>(), linux_api::limits::ARG_MAX),
        )?;
        res.push(cstr.to_owned());
    }
    Ok(res)
}
