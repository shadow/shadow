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
use crate::cbindings;

struct ByteChunk {
    buf: Vec<u8>,
}

/// A queue of byte chunks.
pub struct ByteQueue {
    chunks: LinkedList<ByteChunk>,
    tail_read_offset: usize,
    head_write_offset: usize,
    length: usize,
    chunk_capacity: usize,
}

impl ByteChunk {
    fn new(capacity: usize) -> ByteChunk {
        let _ = cbindings::SysCallArgs{number: 2, args: [cbindings::SysCallReg{as_i64:0}; 6]};
        ByteChunk {
            buf: vec![0; capacity],
        }
    }
}

impl ByteQueue {
    pub fn new(chunk_capacity: usize) -> ByteQueue {
        ByteQueue {
            chunks: LinkedList::new(),
            tail_read_offset: 0,
            head_write_offset: 0,
            length: 0,
            chunk_capacity: chunk_capacity,
        }
    }

    /// Push bytes to the head of the queue.
    pub fn push(&mut self, src: &[u8]) {
        // create new buffer head lazily as opposed to proactively
        if self.chunks.is_empty() {
            self.create_new_head();
        }

        let src_len = src.len();
        let mut bytes_copied = 0;

        // while there are bytes to copy
        while bytes_copied < src_len {
            let head_space = self.chunks.front().unwrap().buf.len() - self.head_write_offset;

            // if we have no space, allocate a new chunk at head
            if head_space <= 0 {
                self.create_new_head();
                continue;
            }

            // how much we actually write in this iteration
            let src_remaining = src_len - bytes_copied;
            let num_write = if src_remaining < head_space {
                src_remaining
            } else {
                head_space
            };

            // copy bytes from src
            self.copy_to_head(&src[bytes_copied..(bytes_copied + num_write)]);
            bytes_copied += num_write;
        }
    }

    /// Pop bytes from the end of the queue.
    pub fn pop(&mut self, dst: &mut [u8]) -> usize {
        let dst_len = dst.len();
        let mut bytes_copied = 0;

        // while there are bytes to copy
        while bytes_copied < dst_len && !self.chunks.is_empty() {
            let tail_avail = self.get_available_bytes_tail();

            /* if we have nothing to read, destroy old tail
             * this *should* never happen since we destroy tails proactively
             * but i'm leaving it in for safety
             */
            if tail_avail <= 0 {
                self.destroy_old_tail();
                continue;
            }

            // how much we actually read in this iteration
            let dst_remaining = dst_len - bytes_copied;
            let num_read = if dst_remaining < tail_avail {
                dst_remaining
            } else {
                tail_avail
            };

            // copy bytes to dst
            self.copy_from_tail(&mut dst[bytes_copied..(bytes_copied + num_read)]);
            bytes_copied += num_read;

            // proactively destroy old tail
            let tail_avail = self.get_available_bytes_tail();
            if tail_avail <= 0 || self.length == 0 {
                self.destroy_old_tail();
            }
        }

        bytes_copied
    }

    fn create_new_head(&mut self) {
        if self.chunks.is_empty() {
            // this will also be the tail
            self.tail_read_offset = 0;
        }

        self.head_write_offset = 0;
        self.chunks.push_front(ByteChunk::new(self.chunk_capacity));
    }

    fn destroy_old_tail(&mut self) {
        self.chunks.pop_back();
        self.tail_read_offset = 0;

        if self.chunks.is_empty() {
            // this was also the head
            self.head_write_offset = 0;
        }
    }

    fn get_available_bytes_tail(&self) -> usize {
        match self.chunks.len() {
            0 => 0,
            1 => self.head_write_offset - self.tail_read_offset,
            _ => self.chunks.back().unwrap().buf.len() - self.tail_read_offset,
        }
    }

    /// Copy bytes from a slice. Assumes that the head exists, and that the
    /// slice will not overflow the head.
    fn copy_to_head(&mut self, src: &[u8]) {
        let len = src.len();
        self.chunks.front_mut().unwrap().buf
            [self.head_write_offset..(self.head_write_offset + len)]
            .copy_from_slice(&src);

        self.head_write_offset += len;
        self.length += len;
    }

    /// Copy bytes to a slice. Assumes that the tail exists, and that the
    /// slice does not require more bytes than exist in the tail.
    fn copy_from_tail(&mut self, dst: &mut [u8]) {
        let len = dst.len();
        dst.copy_from_slice(
            &self.chunks.back().unwrap().buf[self.tail_read_offset..(self.tail_read_offset + len)],
        );

        self.tail_read_offset += len;
        self.length += len;
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
    pub extern "C" fn bytequeue_push(
        bq: *mut ByteQueue,
        src: *const std::os::raw::c_uchar,
        len: libc::size_t,
    ) {
        assert!(!bq.is_null());
        assert!(!src.is_null());
        let bq = unsafe { &mut *bq };
        let src = unsafe { slice::from_raw_parts(src, len) };
        bq.push(src)
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
        bq.pop(dst)
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

        bq.push(&src1);
        bq.push(&src2);

        // check the number of chunk sizes is correct (ceiling division)
        assert_eq!(
            bq.chunks.len(),
            (src1.len() + src2.len() - 1) / chunk_size + 1
        );

        let mut count = 0;
        count += bq.pop(&mut dst1);
        count += bq.pop(&mut dst2);

        assert_eq!(count, src1.len() + src2.len());
        assert_eq!(dst1, [1, 2, 3, 4, 5, 6, 7, 8]);
        assert_eq!(dst2, [9, 10, 11, 12, 13, 51, 52, 53, 0, 0]);
    }
}
