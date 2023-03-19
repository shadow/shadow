use std::ops::{Deref, DerefMut};

use crate::host::memory_manager::{MemoryManager, MemoryReaderCursor, MemoryWriterCursor};
use crate::host::syscall_types::{PluginPtr, SyscallError, TypedPluginPtr};

use nix::errno::Errno;

pub trait LendingWriteIterator {
    type Item<'a>: std::io::Write
    where
        Self: 'a;

    fn next<'a>(&'a mut self) -> Option<Self::Item<'a>>;
}

impl<T: Iterator> LendingWriteIterator for T
where
    T::Item: std::io::Write,
{
    type Item<'a> = <T as Iterator>::Item where T: 'a;

    fn next<'a>(&'a mut self) -> Option<Self::Item<'a>> {
        self.next()
    }
}

pub trait LendingReadIterator {
    type Item<'a>: std::io::Read
    where
        Self: 'a;

    fn next<'a>(&'a mut self) -> Option<Self::Item<'a>>;

    /*
    fn take(self, limit: u64) -> LendingReadTake<Self>
    where
        Self: Sized,
    {
        LendingReadTake {
            iter: self,
            remaining: limit,
        }
    }
    */
}

impl<T: Iterator> LendingReadIterator for T
where
    T::Item: std::io::Read,
{
    type Item<'a> = <T as Iterator>::Item where T: 'a;

    fn next<'a>(&'a mut self) -> Option<Self::Item<'a>> {
        self.next()
    }
}

/*
pub struct LendingReadTake<I> {
    iter: I,
    remaining: u64,
}

impl<I> LendingReadIterator for LendingReadTake<I> {
    type Item<'a> = I::Item<'a>;

    fn next<'a>(&'a mut self) -> Option<Self::Item<'a>> {
        self.iter.next().take(self.remaining)

        /*
        if let Some(iov) = self.iovs.get(self.idx) {
            self.idx += 1;

            let buf_ptr = TypedPluginPtr::new::<u8>(iov.base, iov.len);
            let bytes = self.mem.reader(buf_ptr);

            return Some(bytes);
        }

        None
        */
    }
}
*/

/*
pub trait IntoLendingIterator {
    type Item<'a>: std::io::Write where Self::IntoIter: 'a;
    type IntoIter: LendingIterator;

    fn into_iter(self) -> Self::IntoIter;
}

impl<T: LendingIterator> IntoLendingIterator for T {
    type Item<'a> = T::Item<'a> where Self::IntoIter: 'a;
    type IntoIter = T;

    fn into_iter(self) -> Self::IntoIter {
        self
    }
}

impl<T: IntoIterator> IntoLendingIterator for T {
    type Item<'a> = T::Item where Self::IntoIter: 'a;
    type IntoIter = T::IntoIter;

    fn into_iter(self) -> Self::IntoIter {
        self.into_iter()
    }
}
*/

/*
pub trait IntoLendingIterator {
    type Item<'a> where Self::IntoIter: 'a;
    type IntoIter: for<'a> LendingIterator<Item<'a> = Self::Item<'a>>;

    fn into_iter(self) -> Self::IntoIter;
}

impl<T: LendingIterator> IntoLendingIterator for T {
    type Item<'a> = T::Item<'a> where Self::IntoIter: 'a;
    type IntoIter = T;

    fn into_iter(self) -> Self::IntoIter {
        self
    }
}

fn test() {
    fn foo(x: impl IntoLendingIterator) {
        let mut x = x.into_iter();
        x.next();
    }
}
*/

/*
pub trait IntoLendingIterator<'a> {
    type Item;
    type IntoIter: LendingIterator<Item<'a> = Self::Item> + 'a;

    fn into_iter(self) -> Self::IntoIter;
}

impl<'a, T: LendingIterator + 'a> IntoLendingIterator<'a> for T {
    type Item = T::Item<'a>;
    type IntoIter = T;

    fn into_iter(self) -> Self::IntoIter {
        self
    }
}

fn test() {
    fn foo<'a>(x: impl IntoLendingIterator<'a>) {
        let mut x = x.into_iter();
        x.next();
    }
}
*/

/*
pub trait IntoLendingIterator {
    type Item<'a>;
    type IntoIter<'a>: LendingIterator<Item<'a> = Self::Item<'a>> + 'a;

    fn into_iter<'a>(self) -> Self::IntoIter<'a>;
}

impl<T: LendingIterator> IntoLendingIterator for T {
    type Item<'a> = T::Item<'a>;
    type IntoIter<'a> = T;

    fn into_iter<'a>(self) -> Self::IntoIter<'a> {
        self
    }
}
*/

/*
impl<T: IntoIterator> IntoLendingIterator for T {
    type Item<'a> = <T as IntoIterator>::Item where T: 'a;
    type IntoIter<'a> = <T as IntoIterator>::IntoIter;

    fn into_iter<'a>(self) -> Self::IntoIter<'a> {
        self.into_iter()
    }
}
*/

/// Analogous to [`libc::mmsghdr`].
pub struct MmsgHdr {
    pub hdr: MsgHdr,
    pub len: libc::c_uint,
}

/// Analogous to [`libc::msghdr`].
pub struct MsgHdr {
    pub name: PluginPtr,
    pub name_len: libc::socklen_t,
    pub iovs: Vec<IoVec>,
    pub control: PluginPtr,
    pub control_len: libc::size_t,
    pub flags: libc::c_int,
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

pub struct IoVecIterMut<'a> {
    iovs: &'a [IoVec],
    mem: &'a mut MemoryManager,
    idx: usize,
}

impl<'a> IoVecIterMut<'a> {
    pub fn new(iovs: &'a [IoVec], mem: &'a mut MemoryManager) -> Self {
        Self { iovs, mem, idx: 0 }
    }
}

impl<'a> LendingWriteIterator for IoVecIterMut<'a> {
    type Item<'b> = MemoryWriterCursor<'b> where Self: 'b;

    fn next<'b>(&'b mut self) -> Option<Self::Item<'b>> {
        if let Some(iov) = self.iovs.get(self.idx) {
            self.idx += 1;

            let buf_ptr = TypedPluginPtr::new::<u8>(iov.base, iov.len);
            let bytes = self.mem.writer(buf_ptr);

            return Some(bytes);
        }

        None
    }
}

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

pub struct IoVecIter<'a> {
    iovs: &'a [IoVec],
    mem: &'a MemoryManager,
    idx: usize,
}

impl<'a> IoVecIter<'a> {
    pub fn new(iovs: &'a [IoVec], mem: &'a MemoryManager) -> Self {
        Self { iovs, mem, idx: 0 }
    }
}

impl<'a> LendingReadIterator for IoVecIter<'a> {
    type Item<'b> = MemoryReaderCursor<'b> where Self: 'b;

    fn next<'b>(&'b mut self) -> Option<Self::Item<'b>> {
        if let Some(iov) = self.iovs.get(self.idx) {
            self.idx += 1;

            let buf_ptr = TypedPluginPtr::new::<u8>(iov.base, iov.len);
            let bytes = self.mem.reader(buf_ptr);

            return Some(bytes);
        }

        None
    }
}

/*
struct LendingIter<I, F, T>
where
    I: Iterator,
    F: Fn(I::Item) -> T,
{
    iter: I,
    map: F,
}

impl<I, F> LendingIter<I, F, T>
where
    I: Iterator,
    F: Fn(I::Item) -> T,
{
    pub fn new(iter: I, map: F) -> Self {
        Self { iter, map }
    }

    pub fn next(&mut self) -> T {
        if let Some(x) = self.iter.next() {
            return Some((self.map)(x))
        }
    }
}
*/

/*
impl<'a, 'b> std::iter::Iterator for IoVecIterMut<'a, 'b> {
    type Item = MemoryWriterCursor<'b>;
    fn next(&'b mut self) -> Option<Self::Item> {
        if let Some(iov) = self.iovs.get(self.idx) {
            self.idx += 1;

            let buf_ptr = TypedPluginPtr::new::<u8>(iov.base, iov.len);
            let bytes = self.mem.writer(buf_ptr);

            return Some(bytes);
        }

        None
    }
}
*/

/// Read a plugin's array of [`libc::iovec`] into a [`Vec<IoVec>`].
pub fn read_iovecs(
    mem: &MemoryManager,
    iov_ptr: PluginPtr,
    count: usize,
) -> Result<Vec<IoVec>, SyscallError> {
    if count > libc::UIO_MAXIOV.try_into().unwrap() {
        return Err(Errno::EINVAL.into());
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

/// Read a plugin's [`libc::msghdr`] into a [`MsgHdr`].
pub fn read_msghdr(mem: &MemoryManager, msg_ptr: PluginPtr) -> Result<MsgHdr, SyscallError> {
    let msg_ptr = TypedPluginPtr::new::<libc::msghdr>(msg_ptr, 1);
    let mem_ref = mem.memory_ref(msg_ptr)?;
    let plugin_msg = mem_ref.deref()[0];

    msghdr_to_rust(&plugin_msg, mem)
}

/// Only writes the [`libc::msghdr`] `msg_namelen`, `msg_controllen`, and `msg_flags` fields.
pub fn write_msghdr(
    mem: &mut MemoryManager,
    msg_ptr: PluginPtr,
    msg: MsgHdr,
) -> Result<(), SyscallError> {
    let msg_ptr = TypedPluginPtr::new::<libc::msghdr>(msg_ptr, 1);
    let mut mem_ref = mem.memory_ref_mut(msg_ptr)?;
    let mut plugin_msg = &mut mem_ref.deref_mut()[0];

    // write only the msg fields that may have changed
    plugin_msg.msg_namelen = msg.name_len;
    plugin_msg.msg_controllen = msg.control_len;
    plugin_msg.msg_flags = msg.flags;

    // TODO: should flush be the default drop behaviour?
    mem_ref.flush()?;

    Ok(())
}

/// Read a plugin's array of [`libc::mmsghdr`] into a [`Vec<MmsgHdr>`].
pub fn read_mmsghdrs(
    mem: &MemoryManager,
    mmsg_ptr: PluginPtr,
    count: usize,
) -> Result<Vec<MmsgHdr>, SyscallError> {
    let mut mmsgs = Vec::with_capacity(count);

    let mmsg_ptr = TypedPluginPtr::new::<libc::mmsghdr>(mmsg_ptr, count);
    let mem_ref = mem.memory_ref(mmsg_ptr)?;
    let plugin_mmsgs = mem_ref.deref();

    for plugin_mmsg in plugin_mmsgs {
        mmsgs.push(MmsgHdr {
            hdr: msghdr_to_rust(&plugin_mmsg.msg_hdr, mem)?,
            len: plugin_mmsg.msg_len,
        });
    }

    Ok(mmsgs)
}

/// Only writes the [`libc::mmsghdr`] `msg_len` field, and the [`libc::msghdr`] `msg_namelen`,
/// `msg_controllen`, and `msg_flags` fields.
pub fn write_mmsghdrs(
    mem: &mut MemoryManager,
    mmsg_ptr: PluginPtr,
    mmsgs: &[MmsgHdr],
) -> Result<(), SyscallError> {
    let mmsg_ptr = TypedPluginPtr::new::<libc::mmsghdr>(mmsg_ptr, mmsgs.len());
    let mut mem_ref = mem.memory_ref_mut(mmsg_ptr)?;
    let plugin_mmsgs = mem_ref.deref_mut();

    for (mmsg, mut plugin_mmsg) in std::iter::zip(mmsgs.iter(), plugin_mmsgs) {
        // write only the mmsg fields that may have changed
        plugin_mmsg.msg_len = mmsg.len;

        // write only the msg fields that may have changed
        plugin_mmsg.msg_hdr.msg_namelen = mmsg.hdr.name_len;
        plugin_mmsg.msg_hdr.msg_controllen = mmsg.hdr.control_len;
        plugin_mmsg.msg_hdr.msg_flags = mmsg.hdr.flags;
    }

    mem_ref.flush()?;

    Ok(())
}

/// Helper to read a plugin's [`libc::msghdr`] into a [`MsgHdr`]. While `msg` is a local struct, it
/// should have been copied from plugin memory, meaning any pointers in the struct are pointers to
/// plugin memory, not local memory.
fn msghdr_to_rust(msg: &libc::msghdr, mem: &MemoryManager) -> Result<MsgHdr, SyscallError> {
    let iovs = read_iovecs(mem, PluginPtr::from_raw_ptr(msg.msg_iov), msg.msg_iovlen)?;
    assert_eq!(iovs.len(), msg.msg_iovlen);

    Ok(MsgHdr {
        name: PluginPtr::from_raw_ptr(msg.msg_name),
        name_len: msg.msg_namelen,
        iovs,
        control: PluginPtr::from_raw_ptr(msg.msg_control),
        control_len: msg.msg_controllen,
        flags: msg.msg_flags,
    })
}
