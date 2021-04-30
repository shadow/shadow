/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

/*!
A shared buffer that is composed of several chunks. The buffer can be read
and written and guarantees it will not allow reading more than was written.
Its basically a linked queue that is written (and grows) at the front and
read (and shrinks) from the back. As data is written, new chunks are created
automatically. As data is read, old chunks are freed automatically.
*/

use std::collections::LinkedList;
use std::io::Read;

struct ByteChunk {
    buf: Vec<u8>,
}

/// A queue of byte chunks.
pub struct ByteQueue {
    chunks: LinkedList<ByteChunk>,
    tail_read_offset: usize,
    length: usize,
    chunk_capacity: usize,
}

impl ByteChunk {
    fn new(capacity: usize) -> ByteChunk {
        ByteChunk {
            buf: Vec::with_capacity(capacity),
        }
    }
}

impl ByteQueue {
    pub fn new(chunk_capacity: usize) -> ByteQueue {
        ByteQueue {
            chunks: LinkedList::new(),
            tail_read_offset: 0,
            length: 0,
            chunk_capacity,
        }
    }

    /// Number of bytes in the queue.
    pub fn len(&self) -> usize {
        self.length
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Push bytes to the head of the queue.
    /// Returns an error iff `src` returns an error; i.e. this is infallible for
    /// an infallible `src` such as a slice.
    pub fn push<R: Read>(&mut self, src: R) -> std::io::Result<usize> {
        // create new buffer head lazily as opposed to proactively
        if self.chunks.is_empty() {
            self.create_new_head();
        }

        let mut total_written = 0;
        let mut src = src;

        // while there are bytes to copy
        loop {
            let head = &mut self.chunks.front_mut().unwrap().buf;
            let head_space = head.capacity() - head.len();

            // if we have no space, allocate a new chunk at head
            if head_space == 0 {
                self.create_new_head();
                continue;
            }

            let written = src.by_ref().take(head_space as u64).read_to_end(head)?;
            self.length += written;
            total_written += written;

            if written == 0 {
                // End of the reader
                if head.len() == 0 {
                    // We created an empty head but ended up not reading any
                    // data into it.
                    self.chunks.pop_front();
                }
                return Ok(total_written);
            }
        }
    }

    /// Dequeues data into `dst` until empty or `dst` returns EOF or error.
    /// Returns an error iff `dst` returns an error; i.e. this is infallible for
    /// an infallible `dst` such as a slice.
    pub fn pop<W: std::io::Write>(&mut self, dst: W) -> std::io::Result<usize> {
        let mut total_copied = 0;
        let mut dst = dst;

        loop {
            let back = match self.chunks.back() {
                Some(x) => x,
                None => {
                    // No more data to copy
                    return Ok(total_copied);
                }
            };
            debug_assert_ne!(self.get_available_bytes_tail(), 0);

            // copy bytes to dst
            let copied = dst.write(&back.buf[self.tail_read_offset..])?;
            self.tail_read_offset += copied;
            self.length -= copied;
            total_copied += copied;

            // proactively destroy old tail
            if self.get_available_bytes_tail() == 0 {
                self.destroy_old_tail();
            }

            if copied == 0 {
                // Writer EOF
                return Ok(total_copied);
            }
        }
    }

    fn create_new_head(&mut self) {
        if self.chunks.is_empty() {
            // this will also be the tail
            self.tail_read_offset = 0;
        }

        self.chunks.push_front(ByteChunk::new(self.chunk_capacity));
    }

    fn destroy_old_tail(&mut self) {
        self.chunks.pop_back();
        self.tail_read_offset = 0;
    }

    fn get_available_bytes_tail(&self) -> usize {
        match self.chunks.len() {
            0 => 0,
            1 => self.chunks.front().unwrap().buf.len() - self.tail_read_offset,
            _ => self.chunk_capacity - self.tail_read_offset,
        }
    }
}

mod export {
    use super::*;
    use std::slice;

    #[no_mangle]
    pub extern "C" fn bytequeue_new(chunk_size: libc::size_t) -> *mut ByteQueue {
        Box::into_raw(Box::new(ByteQueue::new(chunk_size)))
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
    pub extern "C" fn bytequeue_len(bq: *mut ByteQueue) -> libc::size_t {
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
    pub extern "C" fn bytequeue_push(
        bq: *mut ByteQueue,
        src: *const std::os::raw::c_uchar,
        len: libc::size_t,
    ) {
        assert!(!bq.is_null());
        assert!(!src.is_null());
        let bq = unsafe { &mut *bq };
        let src = unsafe { slice::from_raw_parts(src, len) };
        bq.push(src).unwrap();
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
        let dst = unsafe { slice::from_raw_parts_mut(dst, len) };
        bq.pop(dst).unwrap()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_bytequeue() {
        let chunk_size = 5;
        let mut bq = ByteQueue::new(chunk_size);

        let src1 = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13];
        let src2 = [51, 52, 53];
        let mut dst1 = [0; 8];
        let mut dst2 = [0; 10];

        bq.push(&src1[..]).unwrap();
        bq.push(&src2[..]).unwrap();

        // check the number of chunk sizes is correct (ceiling division)
        assert_eq!(
            bq.chunks.len(),
            (src1.len() + src2.len() - 1) / chunk_size + 1
        );

        assert_eq!(bq.length, src1.len() + src2.len());

        let mut count = 0;
        count += bq.pop(&mut dst1[..]).unwrap();
        count += bq.pop(&mut dst2[..]).unwrap();

        assert_eq!(count, src1.len() + src2.len());
        assert_eq!(dst1, [1, 2, 3, 4, 5, 6, 7, 8]);
        assert_eq!(dst2, [9, 10, 11, 12, 13, 51, 52, 53, 0, 0]);
        assert_eq!(bq.length, 0);
    }
}
