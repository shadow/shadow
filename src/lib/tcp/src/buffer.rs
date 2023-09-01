use std::collections::LinkedList;
use std::io::Read;

use bytes::{Buf, Bytes, BytesMut};

use crate::seq::{Seq, SeqRange};
use crate::util::time::Instant;

#[derive(Debug)]
pub(crate) struct SendQueue<T: Instant> {
    segments: LinkedList<Segment>,
    time_last_segment_sent: Option<T>,
    // exclusive
    transmitted_up_to: Seq,
    // inclusive
    start_seq: Seq,
    // exclusive
    end_seq: Seq,
    fin_added: bool,
}

impl<T: Instant> SendQueue<T> {
    pub fn new(initial_seq: Seq) -> Self {
        let mut queue = Self {
            segments: LinkedList::new(),
            time_last_segment_sent: None,
            transmitted_up_to: initial_seq,
            start_seq: initial_seq,
            end_seq: initial_seq,
            fin_added: false,
        };

        queue.add_syn();

        queue
    }

    fn add_syn(&mut self) {
        self.add_segment(Segment::Syn);
    }

    pub fn add_fin(&mut self) {
        self.add_segment(Segment::Fin);
    }

    pub fn add_data(
        &mut self,
        mut reader: impl Read,
        mut len: usize,
    ) -> Result<(), std::io::Error> {
        // this value shouldn't affect the tcp behaviour, only how the underlying bytes are
        // allocated
        const MAX_BYTES_PER_CHUNK: usize = 10_000;

        while len > 0 {
            let to_read = std::cmp::min(len, MAX_BYTES_PER_CHUNK);

            let mut data = BytesMut::from_iter(std::iter::repeat(0).take(to_read));
            reader.read_exact(&mut data[..])?;

            self.add_segment(Segment::Data(data.into()));

            len -= to_read;
        }

        Ok(())
    }

    fn add_segment(&mut self, seg: Segment) {
        assert!(!self.fin_added);

        if matches!(seg, Segment::Fin) {
            self.fin_added = true;
        }

        if seg.len() == 0 {
            return;
        }

        self.end_seq += seg.len();
        self.segments.push_back(seg);
    }

    pub fn start_seq(&self) -> Seq {
        self.start_seq
    }

    pub fn next_seq(&self) -> Seq {
        self.end_seq
    }

    pub fn contains(&self, seq: Seq) -> bool {
        SeqRange::new(self.start_seq, self.end_seq).contains(seq)
    }

    pub fn len(&self) -> u32 {
        self.end_seq - self.start_seq
    }

    pub fn advance_start(&mut self, new_start: Seq) {
        assert!(self.contains(new_start) || new_start == self.end_seq);

        while self.start_seq != new_start {
            let advance_by = new_start - self.start_seq;

            // this shouldn't panic due to the assertion above
            let front = self.segments.front_mut().unwrap();

            // if the chunk would be completely removed
            if front.len() <= advance_by {
                self.start_seq += front.len();
                self.segments.pop_front();
                continue;
            }

            let Segment::Data(data) = front else {
                // syn and fin segments have a length of only 1 byte, so they should have been
                // popped by the check above
                unreachable!();
            };

            // update the existing `Bytes` object rather than using `slice()` to avoid an atomic
            // operation
            data.advance(advance_by.try_into().unwrap());
            assert!(!data.is_empty());

            self.start_seq += advance_by;
        }
    }

    /// Get the next segment that has not yet been transmitted. The `offset` argument can be used to
    /// return the next segment starting at `offset` bytes from the next non-transmitted segment.
    // TODO: this is slow and is called often
    pub fn next_not_transmitted(&self, offset: u32) -> Option<(Seq, Segment)> {
        // the sequence number of the segment we want to return
        let target_seq = self.transmitted_up_to + offset;

        // check if we've already transmitted everything in the buffer
        if !self.contains(target_seq) {
            return None;
        }

        let mut seq_cursor = self.start_seq;
        for seg in &self.segments {
            let len = seg.len();

            // if the target sequence number is within this segment
            if SeqRange::new(seq_cursor, seq_cursor + len).contains(target_seq) {
                let new_segment = match seg {
                    Segment::Syn => Segment::Syn,
                    Segment::Fin => Segment::Fin,
                    Segment::Data(chunk) => {
                        // the target sequence number might be somewhere within this chunk, so we
                        // need to trim any bytes with a lower sequence number
                        let chunk_offset = target_seq - seq_cursor;
                        let chunk_offset: usize = chunk_offset.try_into().unwrap();
                        Segment::Data(chunk.slice(chunk_offset..))
                    }
                };

                return Some((target_seq, new_segment));
            }

            seq_cursor += len;
        }

        // we confirmed above that the target sequence number is contained within the buffer, but we
        // looped over all segments in the buffer and didn't find it
        unreachable!();
    }

    pub fn mark_as_transmitted(&mut self, up_to: Seq, time: T) {
        assert!(self.contains(up_to) || up_to == self.end_seq);

        if up_to != self.transmitted_up_to {
            self.time_last_segment_sent = Some(time);
        }

        self.transmitted_up_to = up_to;
    }
}

#[derive(Debug)]
pub(crate) struct RecvQueue {
    segments: LinkedList<Bytes>,
    // inclusive
    start_seq: Seq,
    // exclusive
    end_seq: Seq,
    syn_added: bool,
    fin_added: bool,
}

impl RecvQueue {
    pub fn new(initial_seq: Seq) -> Self {
        Self {
            segments: LinkedList::new(),
            start_seq: initial_seq,
            end_seq: initial_seq,
            syn_added: false,
            fin_added: false,
        }
    }

    pub fn add_syn(&mut self) {
        assert!(!self.syn_added);
        self.syn_added = true;

        self.start_seq += 1;
        self.end_seq += 1;
    }

    pub fn add_fin(&mut self) {
        assert!(self.syn_added);
        assert!(!self.fin_added);
        self.fin_added = true;

        self.start_seq += 1;
        self.end_seq += 1;
    }

    pub fn add(&mut self, data: Bytes) {
        assert!(self.syn_added);
        assert!(!self.fin_added);

        let len: u32 = data.len().try_into().unwrap();

        if len == 0 {
            return;
        }

        self.end_seq += len;
        self.segments.push_back(data);
    }

    pub fn syn_added(&self) -> bool {
        self.syn_added
    }

    pub fn len(&self) -> u32 {
        self.end_seq - self.start_seq
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    pub fn next_seq(&self) -> Seq {
        self.end_seq
    }

    pub fn pop(&mut self, len: u32) -> Option<(Seq, Bytes)> {
        let seq = self.start_seq;

        let chunk_len: u32 = self.segments.front()?.len().try_into().unwrap();

        let segment = if len < chunk_len {
            // want fewer bytes than the size of the next chunk, so need to split the chunk
            self.segments
                .front_mut()
                .unwrap()
                .split_to(len.try_into().unwrap())
        } else {
            // want more bytes than the size of the next chunk, so return as much as we can in a
            // single chunk
            self.segments.pop_front().unwrap()
        };

        // only return an empty chunk if len was 0
        assert!(!segment.is_empty() || len == 0);

        let advance_by: u32 = segment.len().try_into().unwrap();
        self.start_seq += advance_by;

        Some((seq, segment))
    }
}

#[derive(Debug)]
pub(crate) enum Segment {
    Data(Bytes),
    Syn,
    Fin,
}

impl Segment {
    pub fn len(&self) -> u32 {
        match self {
            Segment::Syn | Segment::Fin => 1,
            Segment::Data(ref data) => data.len().try_into().unwrap(),
        }
    }
}
