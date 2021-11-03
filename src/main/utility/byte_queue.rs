/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use bytes::{Bytes, BytesMut};

use std::collections::LinkedList;
use std::convert::{TryFrom, TryInto};
use std::io::{Read, Write};
use std::iter::FromIterator;

/// A queue of bytes that supports reading and writing stream and/or packet data.
///
/// Both stream and packet data can be pushed onto the buffer and their order will be preserved.
/// Data is stored internally as a linked list of chunks. Each chunk stores either stream or packet
/// data. Consecutive stream data may be merged into a single chunk, but consecutive packets will
/// always be contained in their own chunks.
///
/// To avoid memory copies when moving bytes from one `ByteQueue` to another, you can use
/// `pop_chunk()` to remove a chunk from the queue, and use `push_chunk()` to add it to another
/// queue.
pub struct ByteQueue {
    /// The queued bytes.
    bytes: LinkedList<ByteChunk>,
    /// A pre-allocated buffer that can be used for new bytes.
    unused_buffer: Option<BytesMut>,
    /// The number of bytes in the queue.
    length: u64,
    /// The size of newly allocated chunks when storing stream data.
    default_chunk_capacity: usize,
    #[cfg(test)]
    /// An allocation counter for testing purposes.
    total_allocations: u64,
}

impl ByteQueue {
    pub fn new(default_chunk_capacity: usize) -> Self {
        Self {
            bytes: LinkedList::new(),
            unused_buffer: None,
            length: 0,
            default_chunk_capacity,
            #[cfg(test)]
            total_allocations: 0,
        }
    }

    pub fn len(&self) -> u64 {
        self.length
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    #[must_use]
    fn alloc_zeroed_buffer(&mut self, size: usize) -> BytesMut {
        #[cfg(test)]
        {
            self.total_allocations += 1;
        }

        BytesMut::from_iter(std::iter::repeat(0).take(size))
    }

    /// Push stream data onto the queue. The data may be merged into the previous stream chunk. To
    /// read the entirety of `src`, set `size` to `None`.
    pub fn push_stream(&mut self, src: &mut dyn Read, size: Option<usize>) -> std::io::Result<u64> {
        let mut total_copied = 0;

        loop {
            let mut unused = match self.unused_buffer.take() {
                // we already have an allocated buffer
                Some(x) => x,
                // we need to allocate a new buffer
                None => self.alloc_zeroed_buffer(self.default_chunk_capacity),
            };
            assert_eq!(unused.len(), unused.capacity());

            let max = match size {
                Some(x) => std::cmp::min(x - total_copied as usize, unused.len()),
                None => unused.len(),
            };
            let copied = src.read(&mut unused[..max])?;
            let bytes = unused.split_to(copied);

            total_copied += u64::try_from(bytes.len()).unwrap();

            if unused.len() != 0 {
                // restore the remaining unused buffer
                self.unused_buffer = Some(unused);
            }

            if bytes.len() == 0 {
                break;
            }

            let mut bytes = Some(bytes);

            // if there is some data chunk in the queue
            if let Some(last_chunk) = self.bytes.back_mut() {
                // if the last chunk was a stream chunk
                if last_chunk.chunk_type == ChunkType::Stream {
                    // if the last stream chunk is mutable
                    if let BytesWrapper::Mutable(last_chunk) = &mut last_chunk.data {
                        let len = bytes.as_ref().unwrap().len();
                        // try merging our new bytes into the last chunk, which will be
                        // successful if it doesn't require any memory copying
                        // (puts 'bytes' back if the merge was unsuccessful)
                        bytes = last_chunk.try_unsplit(bytes.take().unwrap()).err();
                        if bytes.is_none() {
                            // we were successful, so increase the queue's length manually
                            self.length += u64::try_from(len).unwrap();
                        }
                    }
                }
            }

            // if we didn't merge it into the previous chunk
            if let Some(bytes) = bytes {
                self.push_chunk(bytes, ChunkType::Stream);
            }
        }

        Ok(total_copied)
    }

    /// Push packet data onto the queue in a single chunk. Exactly `size` bytes will be read into
    /// the packet.
    pub fn push_packet(&mut self, src: &mut dyn Read, size: usize) -> std::io::Result<()> {
        // we may need somewhere to store a new buffer
        let mut new_buf;

        let unused = match &mut self.unused_buffer {
            // if the existing 'unused_buffer' has enough space
            Some(buf) if buf.len() >= size => buf,
            // otherwise allocate a new buffer
            _ => {
                new_buf = self.alloc_zeroed_buffer(size);
                &mut new_buf
            }
        };
        assert_eq!(unused.len(), unused.capacity());

        src.read_exact(&mut unused[..size])?;
        let bytes = unused.split_to(size);

        // we may have used up all of the space in 'unused_buffer'
        if let Some(ref unused_buffer) = self.unused_buffer {
            if unused_buffer.len() == 0 {
                self.unused_buffer = None;
            }
        }

        self.push_chunk(bytes, ChunkType::Packet);

        Ok(())
    }

    /// Push a chunk of stream or packet data onto the queue.
    pub fn push_chunk(&mut self, data: impl Into<BytesWrapper>, chunk_type: ChunkType) -> u64 {
        let data = data.into();
        let len = u64::try_from(data.len()).unwrap();
        self.length += len;
        self.bytes.push_back(ByteChunk::new(data, chunk_type));
        len
    }

    /// Pop data from the queue. Only a single type of data will be popped per invocation. To read
    /// all data from the queue, you must call this method until the returned chunk type is `None`.
    /// Zero-length packets may be returned. If packet data is returned but `dst` did not have
    /// enough space, the remaining bytes in the packet will be dropped.
    pub fn pop(&mut self, dst: &mut dyn Write) -> std::io::Result<(u64, Option<ChunkType>)> {
        // peek the front to see what kind of data is next
        match self.bytes.front() {
            Some(x) => match x.chunk_type {
                ChunkType::Stream => Ok((self.pop_stream(dst)?, Some(ChunkType::Stream))),
                ChunkType::Packet => Ok((self.pop_packet(dst)?, Some(ChunkType::Packet))),
            },
            None => Ok((0, None)),
        }
    }

    fn pop_stream(&mut self, dst: &mut dyn Write) -> std::io::Result<u64> {
        let mut total_copied = 0;
        assert_ne!(
            self.bytes.len(),
            0,
            "This function assumes there is a chunk"
        );

        loop {
            let bytes = match self.bytes.front_mut() {
                Some(x) if x.chunk_type != ChunkType::Stream => break,
                Some(x) => &mut x.data,
                None => break,
            };

            let copied = dst.write(bytes.as_ref())?;
            let _ = bytes.split_to(copied);

            if copied == 0 {
                break;
            }

            let copied = u64::try_from(copied).unwrap();
            self.length -= copied;
            total_copied += copied;

            if bytes.len() == 0 {
                self.bytes.pop_front();
            }
        }

        Ok(total_copied)
    }

    fn pop_packet(&mut self, dst: &mut dyn Write) -> std::io::Result<u64> {
        let mut chunk = self
            .bytes
            .pop_front()
            .expect("This function assumes there is a chunk");
        assert_eq!(chunk.chunk_type, ChunkType::Packet);
        let bytes = &mut chunk.data;

        let mut total_copied = 0u64;

        loop {
            let copied = dst.write(bytes.as_ref())?;
            let _ = bytes.split_to(copied);

            if copied == 0 {
                break;
            }

            total_copied += u64::try_from(copied).unwrap();
        }

        self.length -= total_copied + u64::try_from(bytes.len()).unwrap();
        Ok(total_copied)
    }

    /// Pop a single chunk of data from the queue. The `size_hint` argument is used to limit the
    /// number of bytes in the returned chunk iff the next chunk has stream data. If the returned
    /// chunk has packet data, the `size_hint` is ignored and the entire packet is returned.
    pub fn pop_chunk(&mut self, size_hint: usize) -> Option<(Bytes, ChunkType)> {
        let chunk = self.bytes.front_mut()?;
        let chunk_type = chunk.chunk_type;

        let bytes = match chunk_type {
            ChunkType::Stream => {
                let temp = chunk
                    .data
                    .split_to(std::cmp::min(chunk.data.len(), size_hint));
                if chunk.data.len() == 0 {
                    self.bytes.pop_front();
                }
                temp
            }
            ChunkType::Packet => self.bytes.pop_front().unwrap().data,
        };

        self.length -= u64::try_from(bytes.len()).unwrap();

        Some((bytes.into(), chunk_type))
    }
}

// a sanity check only when using debug mode
#[cfg(debug_assertions)]
impl std::ops::Drop for ByteQueue {
    fn drop(&mut self) {
        // check that the length is consistent with the number of remaining bytes
        assert_eq!(
            self.len(),
            self.bytes
                .iter()
                .map(|x| u64::try_from(x.data.len()).unwrap())
                .sum::<u64>()
        );
    }
}

/// The types of data that are supported by the [`ByteQueue`].
#[derive(Copy, Clone, Debug, PartialEq)]
pub enum ChunkType {
    Stream,
    Packet,
}

/// A wrapper type that holds either [`Bytes`] or [`BytesMut`].
pub enum BytesWrapper {
    Mutable(BytesMut),
    Immutable(Bytes),
}

impl From<BytesMut> for BytesWrapper {
    fn from(x: BytesMut) -> Self {
        BytesWrapper::Mutable(x)
    }
}

impl From<Bytes> for BytesWrapper {
    fn from(x: Bytes) -> Self {
        BytesWrapper::Immutable(x)
    }
}

impl From<BytesWrapper> for Bytes {
    fn from(x: BytesWrapper) -> Self {
        match x {
            BytesWrapper::Mutable(x) => x.freeze(),
            BytesWrapper::Immutable(x) => x,
        }
    }
}

impl std::convert::AsRef<[u8]> for BytesWrapper {
    fn as_ref(&self) -> &[u8] {
        match self {
            BytesWrapper::Mutable(x) => &x,
            BytesWrapper::Immutable(x) => &x,
        }
    }
}

impl std::borrow::Borrow<[u8]> for BytesWrapper {
    fn borrow(&self) -> &[u8] {
        self.as_ref()
    }
}

impl BytesWrapper {
    enum_passthrough!(self, (), Mutable, Immutable;
        pub fn len(&self) -> usize
    );
    enum_passthrough!(self, (), Mutable, Immutable;
        pub fn is_empty(&self) -> bool
    );
    enum_passthrough_into!(self, (at), Mutable, Immutable;
        pub fn split_to(&mut self, at: usize) -> BytesWrapper
    );
}

/// A chunk of bytes and its type.
struct ByteChunk {
    data: BytesWrapper,
    chunk_type: ChunkType,
}

impl ByteChunk {
    pub fn new(data: BytesWrapper, chunk_type: ChunkType) -> Self {
        Self { data, chunk_type }
    }
}

mod export {
    use super::*;
    use std::slice;

    #[no_mangle]
    pub extern "C" fn bytequeue_new(default_chunk_size: usize) -> *mut ByteQueue {
        Box::into_raw(Box::new(ByteQueue::new(default_chunk_size)))
    }

    #[no_mangle]
    pub extern "C" fn bytequeue_free(bq_ptr: *mut ByteQueue) {
        if bq_ptr.is_null() {
            return;
        }
        unsafe {
            Box::from_raw(bq_ptr);
        }
    }

    #[no_mangle]
    pub extern "C" fn bytequeue_len(bq: *mut ByteQueue) -> u64 {
        assert!(!bq.is_null());
        let bq = unsafe { &mut *bq };
        bq.len()
    }

    #[no_mangle]
    pub extern "C" fn bytequeue_isEmpty(bq: *mut ByteQueue) -> bool {
        assert!(!bq.is_null());
        let bq = unsafe { &mut *bq };
        bq.is_empty()
    }

    #[no_mangle]
    pub extern "C" fn bytequeue_pushStream(
        bq: *mut ByteQueue,
        src: *const std::os::raw::c_uchar,
        len: libc::size_t,
    ) {
        assert!(!bq.is_null());
        assert!(!src.is_null());
        let bq = unsafe { &mut *bq };
        let mut src = unsafe { slice::from_raw_parts(src, len) };
        bq.push_stream(&mut src, Some(len)).unwrap();
    }

    #[no_mangle]
    pub extern "C" fn bytequeue_pushPacket(
        bq: *mut ByteQueue,
        src: *const std::os::raw::c_uchar,
        len: libc::size_t,
    ) {
        assert!(!bq.is_null());
        assert!(!src.is_null());
        let bq = unsafe { &mut *bq };
        let mut src = unsafe { slice::from_raw_parts(src, len) };
        let len = src.len();
        bq.push_packet(&mut src, len).unwrap();
    }

    #[no_mangle]
    pub extern "C" fn bytequeue_pop(
        bq: *mut ByteQueue,
        dst: *mut std::os::raw::c_uchar,
        len: libc::size_t,
    ) -> libc::size_t {
        assert!(!bq.is_null());
        assert!(!dst.is_null());
        let bq = unsafe { &mut *bq };
        let mut dst = unsafe { slice::from_raw_parts_mut(dst, len) };
        let (count, _chunk_type) = bq.pop(&mut dst).unwrap();
        count.try_into().unwrap()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_bytequeue_stream() {
        let chunk_size = 5;
        let mut bq = ByteQueue::new(chunk_size);

        let src1 = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13];
        let src2 = [51, 52, 53];
        let mut dst1 = [0; 8];
        let mut dst2 = [0; 10];

        bq.push_stream(&mut &src1[..], None).unwrap();
        bq.push_stream(&mut &[][..], None).unwrap();
        bq.push_stream(&mut &src2[..], None).unwrap();

        assert_eq!(bq.len() as usize, src1.len() + src2.len());
        // ceiling division
        assert_eq!(
            bq.bytes.len(),
            (src1.len() + src2.len() - 1) / chunk_size + 1
        );
        assert_eq!(bq.total_allocations as usize, bq.bytes.len());

        assert_eq!(8, bq.pop(&mut &mut dst1[..]).unwrap().0);
        assert_eq!(8, bq.pop(&mut &mut dst2[..]).unwrap().0);

        assert_eq!(dst1, [1, 2, 3, 4, 5, 6, 7, 8]);
        assert_eq!(dst2, [9, 10, 11, 12, 13, 51, 52, 53, 0, 0]);
        assert_eq!(bq.len(), 0);
    }

    #[test]
    fn test_bytequeue_packet() {
        let mut bq = ByteQueue::new(5);

        let src1 = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13];
        let src2 = [51, 52, 53];
        let mut dst1 = [0; 8];
        let mut dst2 = [0; 10];

        bq.push_packet(&mut &src1[..], src1.len()).unwrap();
        bq.push_packet(&mut &[][..], 0).unwrap();
        bq.push_packet(&mut &src2[..], src2.len()).unwrap();

        assert_eq!(bq.len() as usize, src1.len() + src2.len());
        assert_eq!(bq.bytes.len(), 3);
        assert_eq!(bq.total_allocations, 3);

        assert_eq!(8, bq.pop(&mut &mut dst1[..]).unwrap().0);
        assert_eq!(0, bq.pop(&mut &mut dst2[..]).unwrap().0);
        assert_eq!(3, bq.pop(&mut &mut dst2[..]).unwrap().0);

        assert_eq!(dst1, [1, 2, 3, 4, 5, 6, 7, 8]);
        assert_eq!(dst2, [51, 52, 53, 0, 0, 0, 0, 0, 0, 0]);
        assert_eq!(bq.len(), 0);
    }

    #[test]
    fn test_bytequeue_combined_1() {
        let mut bq = ByteQueue::new(10);

        bq.push_stream(&mut &[1, 2, 3][..], None).unwrap();
        bq.push_packet(&mut &[4, 5, 6][..], 3).unwrap();
        bq.push_stream(&mut &[7, 8, 9][..], None).unwrap();

        assert_eq!(bq.len() as usize, 9);
        assert_eq!(bq.bytes.len(), 3);
        assert_eq!(bq.total_allocations, 1);

        let mut buf = [0; 20];

        assert_eq!(
            bq.pop(&mut &mut buf[..]).unwrap(),
            (3, Some(ChunkType::Stream))
        );
        assert_eq!(buf[..3], [1, 2, 3]);

        assert_eq!(
            bq.pop(&mut &mut buf[..]).unwrap(),
            (3, Some(ChunkType::Packet))
        );
        assert_eq!(buf[..3], [4, 5, 6]);

        assert_eq!(
            bq.pop(&mut &mut buf[..]).unwrap(),
            (3, Some(ChunkType::Stream))
        );
        assert_eq!(buf[..3], [7, 8, 9]);

        assert!(bq.is_empty());
    }

    #[test]
    fn test_bytequeue_combined_2() {
        let mut bq = ByteQueue::new(5);

        bq.push_stream(&mut &[1, 2, 3, 4][..], None).unwrap();
        bq.push_stream(&mut &[5][..], None).unwrap();
        bq.push_stream(&mut &[6][..], None).unwrap();
        bq.push_packet(&mut &[7, 8, 9, 10, 11, 12, 13, 14][..], 8)
            .unwrap();
        bq.push_stream(&mut &[15, 16, 17][..], None).unwrap();
        bq.push_chunk(
            Bytes::from_static(&[100, 101, 102, 103, 104, 105]),
            ChunkType::Packet,
        );
        bq.push_packet(&mut &[][..], 0).unwrap();
        bq.push_stream(&mut &[18][..], None).unwrap();
        bq.push_stream(&mut &[19][..], None).unwrap();
        bq.push_stream(&mut &[20, 21][..], None).unwrap();

        let mut buf = [0; 20];

        assert_eq!(
            bq.pop(&mut &mut buf[..3]).unwrap(),
            (3, Some(ChunkType::Stream))
        );
        assert_eq!(buf[..3], [1, 2, 3]);

        assert_eq!(
            bq.pop(&mut &mut buf[..5]).unwrap(),
            (3, Some(ChunkType::Stream))
        );
        assert_eq!(buf[..3], [4, 5, 6]);

        assert_eq!(
            bq.pop(&mut &mut buf[..4]).unwrap(),
            (4, Some(ChunkType::Packet))
        );
        assert_eq!(buf[..4], [7, 8, 9, 10]);

        assert_eq!(
            bq.pop(&mut &mut buf[..4]).unwrap(),
            (3, Some(ChunkType::Stream))
        );
        assert_eq!(buf[..3], [15, 16, 17]);

        assert_eq!(
            bq.pop(&mut &mut buf[..4]).unwrap(),
            (4, Some(ChunkType::Packet))
        );
        assert_eq!(buf[..4], [100, 101, 102, 103]);

        assert_eq!(
            bq.pop(&mut &mut buf[..4]).unwrap(),
            (0, Some(ChunkType::Packet))
        );

        assert_eq!(bq.pop_chunk(4), Some(([18][..].into(), ChunkType::Stream)));

        assert_eq!(
            bq.pop_chunk(4),
            Some(([19, 20, 21][..].into(), ChunkType::Stream))
        );

        assert_eq!(bq.pop_chunk(8), None);
        assert_eq!(bq.pop(&mut &mut buf[..4]).unwrap(), (0, None));
        assert!(bq.is_empty());
    }
}
