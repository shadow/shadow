use std::collections::LinkedList;

use bytes::Bytes;

use crate::seq::{Seq, SeqRange};
use crate::util::time::Instant;

#[derive(Debug)]
pub(crate) struct SendQueue<T: Instant> {
    segments: LinkedList<SegmentMetadata<T>>,
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

    pub fn add_data(&mut self, data: Bytes) {
        self.add_segment(Segment::Data(data));
    }

    fn add_segment(&mut self, seg: Segment) {
        assert!(!self.fin_added);

        if matches!(seg, Segment::Fin) {
            self.fin_added = true;
        }

        if seg.len() == 0 {
            return;
        }

        let seg = SegmentMetadata::new(seg);

        self.end_seq += seg.seg.len();
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
            // this shouldn't panic due to the assertion above
            let front = self.segments.front_mut().unwrap();

            let advance_by = new_start - self.start_seq;

            // if the chunk is too small
            if front.seg.len() <= advance_by {
                self.start_seq += front.seg.len();
                self.segments.pop_front();
                continue;
            }

            let Segment::Data(data) = &mut front.seg else {
                unreachable!();
            };

            let advance_by_usize: usize = advance_by.try_into().unwrap();
            *data = data.slice(advance_by_usize..);

            assert!(!data.is_empty());

            self.start_seq = new_start;
        }
    }

    pub fn next_not_transmitted(&self) -> Option<(Seq, &SegmentMetadata<T>)> {
        let mut seq_cursor = self.start_seq;
        for seg in &self.segments {
            if seg.transmit_count == 0 {
                return Some((seq_cursor, seg));
            }

            seq_cursor += seg.seg.len();
        }

        None
    }

    pub fn mark_as_transmitted(&mut self, up_to: Seq, time: T) {
        let mut seq_cursor = self.start_seq;

        if up_to == seq_cursor {
            return;
        }

        for seg in &mut self.segments {
            let range = SeqRange::new(self.start_seq, seq_cursor + seg.seg.len());

            // we only support `up_to` values along a chunk boundary, so `up_to` must be >=
            // `range.end`
            // TODO: support arbitary positions that aren't aligned with chunks
            assert!(!range.contains(up_to));

            if seg.transmit_count == 0 {
                seg.transmit_count = 1;
                seg.original_transmit_time = Some(time);
            }

            if range.end == up_to {
                break;
            }

            seq_cursor = range.end;
        }
    }
}

#[derive(Debug)]
pub(crate) struct RecvQueue {
    segments: LinkedList<Bytes>,
    // inclusive
    start_seq: Seq,
    // exclusive
    end_seq: Seq,
}

impl RecvQueue {
    pub fn new(initial_seq: Seq) -> Self {
        Self {
            segments: LinkedList::new(),
            start_seq: initial_seq,
            end_seq: initial_seq,
        }
    }

    pub fn add(&mut self, data: Bytes) {
        // TODO: should take the sequence number, and can possibly discard data that's already
        // received or that is too far in the future

        let len: u32 = data.len().try_into().unwrap();

        if len == 0 {
            return;
        }

        self.end_seq += len;
        self.segments.push_back(data);
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
pub(crate) struct SegmentMetadata<T: Instant> {
    seg: Segment,
    transmit_count: u8,
    original_transmit_time: Option<T>,
}

impl<T: Instant> SegmentMetadata<T> {
    pub fn new(seg: Segment) -> Self {
        Self {
            seg,
            transmit_count: 0,
            original_transmit_time: None,
        }
    }

    pub fn segment(&self) -> &Segment {
        &self.seg
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
