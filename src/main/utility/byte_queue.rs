/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use std::collections::LinkedList;
use std::io::{ErrorKind, Read, Write};

use bytes::{Bytes, BytesMut};

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
    length: usize,
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

    /// The number of bytes in the queue. If the queue has 0 bytes, it does not mean that the queue
    /// is empty since there may be 0-length packets in the queue.
    pub fn num_bytes(&self) -> usize {
        self.length
    }

    /// Returns true if the queue has bytes.
    pub fn has_bytes(&self) -> bool {
        self.num_bytes() > 0
    }

    /// Returns true if the queue has data/chunks, which may include packets with 0 bytes.
    pub fn has_chunks(&self) -> bool {
        !self.bytes.is_empty()
    }

    #[must_use]
    fn alloc_zeroed_buffer(&mut self, size: usize) -> BytesMut {
        #[cfg(test)]
        {
            self.total_allocations += 1;
        }

        BytesMut::from_iter(std::iter::repeat(0).take(size))
    }

    /// Push stream data onto the queue. The data may be merged into the previous stream chunk.
    pub fn push_stream<R: Read>(&mut self, mut src: R) -> std::io::Result<usize> {
        let mut total_copied = 0;

        loop {
            let mut unused = match self.unused_buffer.take() {
                // we already have an allocated buffer
                Some(x) => x,
                // we need to allocate a new buffer
                None => self.alloc_zeroed_buffer(self.default_chunk_capacity),
            };
            assert_eq!(unused.len(), unused.capacity());

            let copied = src.read(&mut unused)?;
            let bytes = unused.split_to(copied);

            total_copied += bytes.len();

            if !unused.is_empty() {
                // restore the remaining unused buffer
                self.unused_buffer = Some(unused);
            }

            if bytes.is_empty() {
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
                            self.length += len;
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
    pub fn push_packet<R: Read>(&mut self, mut src: R, size: usize) -> std::io::Result<()> {
        let unused = match &mut self.unused_buffer {
            // if the existing 'unused_buffer' has enough space
            Some(buf) if buf.len() >= size => buf,
            // otherwise allocate a new buffer
            _ => &mut self.alloc_zeroed_buffer(size),
        };
        assert_eq!(unused.len(), unused.capacity());

        src.read_exact(&mut unused[..size])?;
        let bytes = unused.split_to(size);

        // we may have used up all of the space in 'unused_buffer'
        if let Some(ref unused_buffer) = self.unused_buffer {
            if unused_buffer.is_empty() {
                self.unused_buffer = None;
            }
        }

        self.push_chunk(bytes, ChunkType::Packet);

        Ok(())
    }

    /// Push a chunk of stream or packet data onto the queue.
    pub fn push_chunk(&mut self, data: impl Into<BytesWrapper>, chunk_type: ChunkType) -> usize {
        let data = data.into();
        let len = data.len();
        self.length += len;
        self.bytes.push_back(ByteChunk::new(data, chunk_type));
        len
    }

    /// Pop data from the queue. Only a single type of data will be popped per invocation. To read
    /// all data from the queue, you must call this method until the returned chunk type is `None`.
    /// Zero-length packets may be returned. If packet data is returned but `dst` did not have
    /// enough space, the remaining bytes in the packet will be dropped. Returns a tuple containing
    /// the number of bytes copied, the number of bytes removed from the queue (including dropped
    /// bytes), and the chunk type.
    pub fn pop<W: Write>(&mut self, dst: W) -> std::io::Result<Option<(usize, usize, ChunkType)>> {
        // peek the front to see what kind of data is next
        match self.bytes.front() {
            Some(x) => match x.chunk_type {
                ChunkType::Stream => {
                    let num_copied = self.pop_stream(dst)?;
                    Ok(Some((num_copied, num_copied, ChunkType::Stream)))
                }
                ChunkType::Packet => {
                    let (num_copied, num_removed_from_buf) = self.pop_packet(dst)?;
                    Ok(Some((num_copied, num_removed_from_buf, ChunkType::Packet)))
                }
            },
            None => Ok(None),
        }
    }

    fn pop_stream<W: Write>(&mut self, mut dst: W) -> std::io::Result<usize> {
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

            let copied = match dst.write(bytes.as_ref()) {
                Ok(x) => x,
                // may have been interrupted due to a signal, so try again
                Err(e) if e.kind() == ErrorKind::Interrupted => continue,
                Err(e) if e.kind() == ErrorKind::WouldBlock => {
                    // only return an error if no bytes have been copied yet
                    if total_copied == 0 {
                        return Err(e);
                    }
                    // no bytes could be written this iteration
                    0
                }
                // a partial write may have occurred in previous iterations
                Err(e) => return Err(e),
            };

            let _ = bytes.split_to(copied);

            if copied == 0 {
                break;
            }

            self.length -= copied;
            total_copied += copied;

            if bytes.is_empty() {
                self.bytes.pop_front();
            }
        }

        Ok(total_copied)
    }

    fn pop_packet<W: Write>(&mut self, mut dst: W) -> std::io::Result<(usize, usize)> {
        let mut chunk = self
            .bytes
            .pop_front()
            .expect("This function assumes there is a chunk");
        assert_eq!(chunk.chunk_type, ChunkType::Packet);
        let bytes = &mut chunk.data;

        let packet_len = bytes.len();

        // decrease the length now in case we return early
        self.length = self.length.checked_sub(packet_len).unwrap();

        let mut total_copied = 0;

        loop {
            let copied = match dst.write(bytes.as_ref()) {
                Ok(x) => x,
                // may have been interrupted due to a signal, so try again
                Err(e) if e.kind() == ErrorKind::Interrupted => continue,
                // `WouldBlock` typically means "try again later", but we don't support that
                // behaviour since a packet may have been partially copied already
                Err(e) if e.kind() == ErrorKind::WouldBlock => {
                    panic!("Non-blocking writers aren't supported for packets")
                }
                // a partial write may have occurred in previous iterations, and the remainder of
                // the packet will be dropped
                Err(e) => return Err(e),
            };

            let _ = bytes.split_to(copied);

            if copied == 0 {
                break;
            }

            total_copied += copied;
        }

        Ok((total_copied, packet_len))
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
                if chunk.data.is_empty() {
                    self.bytes.pop_front();
                }
                temp
            }
            ChunkType::Packet => self.bytes.pop_front().unwrap().data,
        };

        self.length -= bytes.len();

        Some((bytes.into(), chunk_type))
    }

    /// Peek data from the queue. Only a single type of data will be peeked per invocation.
    /// Zero-length packets may be returned. If packet data is returned but `dst` did not have
    /// enough space, the packet written to `dst` will be truncated. Returns a tuple containing the
    /// number of bytes copied, the number of bytes that would have been copied if `dst` had enough
    /// space (for packet chunks, the size of the packet), and the chunk type.
    pub fn peek<W: Write>(&self, dst: W) -> std::io::Result<Option<(usize, usize, ChunkType)>> {
        // peek the front to see what kind of data is next
        match self.bytes.front() {
            Some(x) => match x.chunk_type {
                ChunkType::Stream => {
                    let num_copied = self.peek_stream(dst)?;
                    Ok(Some((num_copied, num_copied, ChunkType::Stream)))
                }
                ChunkType::Packet => {
                    let (num_copied, size_of_packet) = self.peek_packet(dst)?;
                    Ok(Some((num_copied, size_of_packet, ChunkType::Packet)))
                }
            },
            None => Ok(None),
        }
    }

    fn peek_stream<W: Write>(&self, mut dst: W) -> std::io::Result<usize> {
        let mut total_copied = 0;
        assert_ne!(
            self.bytes.len(),
            0,
            "This function assumes there is a chunk"
        );

        for bytes in self.bytes.iter() {
            let mut bytes = match bytes {
                x if x.chunk_type != ChunkType::Stream => break,
                x => x.data.as_ref(),
            };

            loop {
                let copied = match dst.write(bytes) {
                    Ok(x) => x,
                    // may have been interrupted due to a signal, so try again
                    Err(e) if e.kind() == ErrorKind::Interrupted => continue,
                    Err(e) if e.kind() == ErrorKind::WouldBlock => {
                        // only return an error if no bytes have been copied yet
                        if total_copied == 0 {
                            return Err(e);
                        }
                        // no bytes could be written this iteration
                        0
                    }
                    // a partial write may have occurred in previous iterations
                    Err(e) => return Err(e),
                };

                bytes = &bytes[copied..];

                if copied == 0 {
                    break;
                }

                total_copied += copied;
            }
        }

        Ok(total_copied)
    }

    fn peek_packet<W: Write>(&self, mut dst: W) -> std::io::Result<(usize, usize)> {
        let chunk = self
            .bytes
            .front()
            .expect("This function assumes there is a chunk");

        assert_eq!(chunk.chunk_type, ChunkType::Packet);
        let mut bytes = chunk.data.as_ref();
        let packet_len = bytes.len();
        let mut total_copied = 0;

        loop {
            let copied = match dst.write(bytes) {
                Ok(x) => x,
                // may have been interrupted due to a signal, so try again
                Err(e) if e.kind() == ErrorKind::Interrupted => continue,
                // `WouldBlock` typically means "try again later", but we don't support that
                // behaviour since a packet may have been partially copied already
                Err(e) if e.kind() == ErrorKind::WouldBlock => {
                    panic!("Non-blocking writers aren't supported for packets")
                }
                // a partial write may have occurred in previous iterations, and the remainder of
                // the packet will be dropped
                Err(e) => return Err(e),
            };

            bytes = &bytes[copied..];

            if copied == 0 {
                break;
            }

            total_copied += copied;
        }

        Ok((total_copied, packet_len))
    }
}

// a sanity check only when using debug mode
#[cfg(debug_assertions)]
impl std::ops::Drop for ByteQueue {
    fn drop(&mut self) {
        // check that the length is consistent with the number of remaining bytes
        assert_eq!(
            self.num_bytes(),
            self.bytes.iter().map(|x| x.data.len()).sum::<usize>()
        );
    }
}

/// The types of data that are supported by the [`ByteQueue`].
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
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
            BytesWrapper::Mutable(x) => x,
            BytesWrapper::Immutable(x) => x,
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

#[cfg(test)]
mod tests {
    use rand::{Rng, RngCore};
    use rand_chacha::ChaCha20Rng;
    use rand_core::SeedableRng;

    use super::*;

    #[test]
    fn test_bytequeue_stream() {
        let chunk_size = 5;
        let mut bq = ByteQueue::new(chunk_size);

        let src1 = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13];
        let src2 = [51, 52, 53];
        let mut dst1 = [0; 8];
        let mut dst2 = [0; 10];

        bq.push_stream(&src1[..]).unwrap();
        bq.push_stream(&[][..]).unwrap();
        bq.push_stream(&src2[..]).unwrap();

        // test size and allocation count

        assert_eq!(bq.num_bytes(), src1.len() + src2.len());
        // ceiling division
        assert_eq!(
            bq.bytes.len(),
            (src1.len() + src2.len() - 1) / chunk_size + 1
        );
        assert_eq!(bq.total_allocations as usize, bq.bytes.len());

        // test peek()

        assert_eq!(8, bq.peek(&mut dst1[..]).unwrap().unwrap().0);
        assert_eq!(10, bq.peek(&mut dst2[..]).unwrap().unwrap().0);

        assert_eq!(dst1, [1, 2, 3, 4, 5, 6, 7, 8]);
        assert_eq!(dst2, [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]);
        assert_eq!(bq.num_bytes(), src1.len() + src2.len());

        // test pop()

        dst1.fill(0);
        dst2.fill(0);

        assert_eq!(8, bq.pop(&mut dst1[..]).unwrap().unwrap().0);
        assert_eq!(8, bq.pop(&mut dst2[..]).unwrap().unwrap().0);

        assert_eq!(dst1, [1, 2, 3, 4, 5, 6, 7, 8]);
        assert_eq!(dst2, [9, 10, 11, 12, 13, 51, 52, 53, 0, 0]);
        assert_eq!(bq.num_bytes(), 0);
    }

    #[test]
    fn test_bytequeue_packet() {
        let mut bq = ByteQueue::new(5);

        let src1 = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13];
        let src2 = [51, 52, 53];
        let mut dst1 = [0; 8];
        let mut dst2 = [0; 10];

        bq.push_packet(&src1[..], src1.len()).unwrap();
        bq.push_packet(&[][..], 0).unwrap();
        bq.push_packet(&src2[..], src2.len()).unwrap();

        // test size and allocation count

        assert_eq!(bq.num_bytes(), src1.len() + src2.len());
        assert_eq!(bq.bytes.len(), 3);
        assert_eq!(bq.total_allocations, 3);

        // test peek()

        assert_eq!(8, bq.peek(&mut dst1[..]).unwrap().unwrap().0);
        assert_eq!(10, bq.peek(&mut dst2[..]).unwrap().unwrap().0);
        assert_eq!(10, bq.peek(&mut dst2[..]).unwrap().unwrap().0);

        assert_eq!(dst1, [1, 2, 3, 4, 5, 6, 7, 8]);
        assert_eq!(dst2, [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]);
        assert_eq!(bq.num_bytes(), src1.len() + src2.len());

        // test pop()

        dst1.fill(0);
        dst2.fill(0);

        assert_eq!(8, bq.pop(&mut dst1[..]).unwrap().unwrap().0);
        assert_eq!(0, bq.pop(&mut dst2[..]).unwrap().unwrap().0);
        assert_eq!(3, bq.pop(&mut dst2[..]).unwrap().unwrap().0);

        assert_eq!(dst1, [1, 2, 3, 4, 5, 6, 7, 8]);
        assert_eq!(dst2, [51, 52, 53, 0, 0, 0, 0, 0, 0, 0]);
        assert_eq!(bq.num_bytes(), 0);
    }

    #[test]
    fn test_bytequeue_combined_1() {
        let mut bq = ByteQueue::new(10);

        bq.push_stream(&[1, 2, 3][..]).unwrap();
        bq.push_packet(&[4, 5, 6][..], 3).unwrap();
        bq.push_stream(&[7, 8, 9][..]).unwrap();

        assert_eq!(bq.num_bytes(), 9);
        assert_eq!(bq.bytes.len(), 3);
        assert_eq!(bq.total_allocations, 1);

        let mut buf = [0; 20];

        assert_eq!(
            bq.pop(&mut buf[..]).unwrap(),
            Some((3, 3, ChunkType::Stream))
        );
        assert_eq!(buf[..3], [1, 2, 3]);

        assert_eq!(
            bq.pop(&mut buf[..]).unwrap(),
            Some((3, 3, ChunkType::Packet))
        );
        assert_eq!(buf[..3], [4, 5, 6]);

        assert_eq!(
            bq.pop(&mut buf[..]).unwrap(),
            Some((3, 3, ChunkType::Stream))
        );
        assert_eq!(buf[..3], [7, 8, 9]);

        assert!(!bq.has_bytes());
    }

    #[test]
    fn test_bytequeue_combined_2() {
        let mut bq = ByteQueue::new(5);

        bq.push_stream(&[1, 2, 3, 4][..]).unwrap();
        bq.push_stream(&[5][..]).unwrap();
        bq.push_stream(&[6][..]).unwrap();
        bq.push_packet(&[7, 8, 9, 10, 11, 12, 13, 14][..], 8)
            .unwrap();
        bq.push_stream(&[15, 16, 17][..]).unwrap();
        bq.push_chunk(
            Bytes::from_static(&[100, 101, 102, 103, 104, 105]),
            ChunkType::Packet,
        );
        bq.push_packet(&[][..], 0).unwrap();
        bq.push_stream(&[18][..]).unwrap();
        bq.push_stream(&[19][..]).unwrap();
        bq.push_stream(&[20, 21][..]).unwrap();

        let mut buf = [0; 20];

        assert_eq!(
            bq.pop(&mut buf[..3]).unwrap(),
            Some((3, 3, ChunkType::Stream))
        );
        assert_eq!(buf[..3], [1, 2, 3]);

        assert_eq!(
            bq.pop(&mut buf[..5]).unwrap(),
            Some((3, 3, ChunkType::Stream))
        );
        assert_eq!(buf[..3], [4, 5, 6]);

        assert_eq!(
            bq.pop(&mut buf[..4]).unwrap(),
            Some((4, 8, ChunkType::Packet))
        );
        assert_eq!(buf[..4], [7, 8, 9, 10]);

        assert_eq!(
            bq.pop(&mut buf[..4]).unwrap(),
            Some((3, 3, ChunkType::Stream))
        );
        assert_eq!(buf[..3], [15, 16, 17]);

        assert_eq!(
            bq.pop(&mut buf[..4]).unwrap(),
            Some((4, 6, ChunkType::Packet))
        );
        assert_eq!(buf[..4], [100, 101, 102, 103]);

        assert_eq!(
            bq.pop(&mut buf[..4]).unwrap(),
            Some((0, 0, ChunkType::Packet))
        );

        assert_eq!(bq.pop_chunk(4), Some(([18][..].into(), ChunkType::Stream)));

        assert_eq!(
            bq.pop_chunk(4),
            Some(([19, 20, 21][..].into(), ChunkType::Stream))
        );

        assert_eq!(bq.pop_chunk(8), None);
        assert_eq!(bq.pop(&mut buf[..4]).unwrap(), None);
        assert!(!bq.has_bytes());
    }

    #[test]
    fn test_bytequeue_fallible_writer() {
        struct TestWriter;

        impl std::io::Write for TestWriter {
            fn write(&mut self, _buf: &[u8]) -> std::io::Result<usize> {
                Err(std::io::ErrorKind::BrokenPipe.into())
            }
            fn flush(&mut self) -> std::io::Result<()> {
                Ok(())
            }
        }

        let mut bq = ByteQueue::new(10);

        bq.push_packet(&[4, 5, 6][..], 3).unwrap();
        bq.push_stream(&[1, 2, 3][..]).unwrap();

        let mut writer = TestWriter {};

        // the remainder of the packet will be dropped, so length will decrease by 3 bytes
        bq.pop(&mut writer).unwrap_err();
        // no stream data will be dropped, so length will not decrease
        bq.pop(&mut writer).unwrap_err();

        assert_eq!(bq.num_bytes(), 3);
    }

    /// Test that the peek output always matches the pop output.
    #[test]
    fn test_bytequeue_peek() {
        let mut rng = ChaCha20Rng::seed_from_u64(1234);

        const PROB_PUSH: f64 = 0.8;
        const PROB_POP: f64 = 0.9;
        const PROB_STREAM: f64 = 0.5;
        const MAX_PUSH: usize = 20;
        const MAX_POP: usize = 30;

        // the bytequeue doesn't use any unsafe code, so we don't really need to worry about UB
        #[cfg(not(miri))]
        const NUM_ITER: usize = 5000;
        #[cfg(miri)]
        const NUM_ITER: usize = 10;

        // pop more bytes and chunks than we push so that we generally stay near an empty queue
        static_assertions::const_assert!(PROB_POP > PROB_PUSH);
        static_assertions::const_assert!(MAX_POP > MAX_PUSH);

        let mut bq = ByteQueue::new(10);

        for _ in 0..NUM_ITER {
            // push
            if rng.random_bool(PROB_PUSH) {
                let mut bytes = vec![0u8; rng.random_range(0..MAX_PUSH)];
                rng.fill_bytes(&mut bytes);

                if rng.random_bool(PROB_STREAM) {
                    bq.push_stream(&bytes[..]).unwrap();
                } else {
                    bq.push_packet(&bytes[..], bytes.len()).unwrap();
                }
            }

            let pop_size = rng.random_range(0..MAX_POP);

            // peek
            let mut peeked_bytes = vec![0u8; pop_size];
            let peek_rv = bq.peek(&mut peeked_bytes[..]).unwrap();

            // pop
            if rng.random_bool(PROB_POP) {
                let mut popped_bytes = vec![0u8; pop_size];
                let pop_rv = bq.pop(&mut popped_bytes[..]).unwrap();

                assert_eq!(peek_rv, pop_rv);
                assert_eq!(popped_bytes, peeked_bytes);
            }
        }
    }
}
