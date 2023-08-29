use bytes::{Buf, Bytes, BytesMut};
use std::io::{Read, Write};
use std::net::SocketAddrV4;

use crate::buffer::Segment;
use crate::seq::{Seq, SeqRange};
use crate::util::time::Instant;
use crate::{
    Ipv4Header, PopPacketError, PushPacketError, RecvError, SendError, TcpConfig, TcpFlags,
    TcpHeader,
};

/// Information for a TCP connection. Equivalent to the Transmission Control Block (TCB).
#[derive(Debug)]
pub(crate) struct Connection<I: Instant> {
    pub(crate) _config: TcpConfig,
    pub(crate) local_addr: SocketAddrV4,
    pub(crate) remote_addr: SocketAddrV4,
    pub(crate) send: ConnectionSend<I>,
    pub(crate) recv: Option<ConnectionRecv>,
    pub(crate) need_to_ack: bool,
    pub(crate) need_to_send_window_update: bool,
}

impl<I: Instant> Connection<I> {
    /// The max number of bytes allowed in the send buffer. This is an arbitrary number used
    /// temporarily until we implement a proper TCP queue.
    const SEND_BUF_MAX: usize = 100_000;

    pub fn new(
        local_addr: SocketAddrV4,
        remote_addr: SocketAddrV4,
        send_initial_seq: Seq,
        config: TcpConfig,
    ) -> Self {
        Self {
            _config: config,
            local_addr,
            remote_addr,
            send: ConnectionSend::new(send_initial_seq),
            recv: None,
            need_to_ack: true,
            need_to_send_window_update: false,
        }
    }

    /// Returns `true` if the packet header src/dst addresses match this connection.
    pub fn packet_addrs_match(&self, header: &TcpHeader) -> bool {
        header.src() == self.remote_addr && header.dst() == self.local_addr
    }

    pub fn send_fin(&mut self) {
        self.send.buffer.add_fin();
        self.send.is_closed = true;
    }

    pub fn send(&mut self, mut reader: impl Read, len: usize) -> Result<usize, SendError> {
        // if the buffer is full
        if !self.send_buf_has_space() {
            return Err(SendError::Full);
        }

        let send_buffer_len = self.send.buffer.len() as usize;
        let send_buffer_space = Self::SEND_BUF_MAX.saturating_sub(send_buffer_len);

        let len = std::cmp::min(len, send_buffer_space);
        let mut remaining = len;

        // The max number of payload bytes allowed per packet. This is an arbitrary number used
        // temporarily until we add code that considers the MSS.
        const MAX_PER_PACKET: usize = 1500;

        while remaining > 0 {
            let to_read = std::cmp::min(remaining, MAX_PER_PACKET);
            let mut payload = BytesMut::from_iter(std::iter::repeat(0).take(to_read));
            if let Err(e) = reader.read_exact(&mut payload[..]) {
                return Err(SendError::Io(e));
            }

            let payload = payload.into();
            self.send.buffer.add_data(payload);

            remaining -= to_read;
        }

        Ok(len)
    }

    pub fn recv(&mut self, mut writer: impl Write, len: usize) -> Result<usize, RecvError> {
        let mut bytes_copied = 0;
        let has_data = !self.recv.as_ref().unwrap().buffer.is_empty();

        if !has_data {
            return Err(RecvError::Empty);
        }

        while bytes_copied < len {
            let remaining = len - bytes_copied;
            let remaining_u32 = remaining.try_into().unwrap_or(u32::MAX);

            let Some((_seq, data)) = self.recv.as_mut().unwrap().buffer.pop(remaining_u32) else {
                break;
            };

            assert!(data.len() <= remaining);

            if let Err(e) = writer.write_all(&data) {
                // TODO: the stream will lose partial data; is this fine?
                return Err(RecvError::Io(e));
            }

            bytes_copied += data.len();
        }

        if bytes_copied > 0 {
            self.need_to_send_window_update = true;
        }

        Ok(bytes_copied)
    }

    pub fn push_packet(
        &mut self,
        header: &TcpHeader,
        payload: impl Into<Bytes>,
    ) -> Result<(), PushPacketError> {
        if self.recv.is_none() && header.flags.contains(TcpFlags::SYN) {
            // we needed to know the sender's initial sequence number before we could initialize the
            // receiving part of the connection
            self.recv = Some(ConnectionRecv::new(Seq::new(header.seq) + 1));
            self.need_to_ack = true;

            // remove the SYN flag, increase the sequence number, and restart
            let mut header = *header;
            header.flags.remove(TcpFlags::SYN);
            header.seq = (Seq::new(header.seq) + 1).into();
            return self.push_packet(&header, payload);
        }

        let Some(recv) = self.recv.as_mut() else {
            // we received a non-SYN packet before the first SYN packet

            if !header.flags.contains(TcpFlags::RST) {
                // TODO: send a RST packet
            }

            // TODO: move to closed state

            return Ok(());
        };

        let payload = payload.into();

        let Some((header, payload)) = trim_segment(header, payload, &recv.window_range()) else {
            // the sequence range of the segment does not overlap with the receive window, so we
            // must drop the packet and send an ACK
            self.need_to_ack = true;

            return Ok(());
        };

        if !recv.is_closed {
            if header.flags.contains(TcpFlags::SYN) {
                // this is the second SYN we've received

                // TODO: We can follow RFC 793 or RFC 5961 here. 793 is probably easiest, and we
                // should send an RST and move to the "closed" state.

                return Ok(());
            }

            let payload_len: u32 = payload.len().try_into().unwrap();
            let payload_seq = (payload_len != 0).then_some(Seq::new(header.seq));
            let fin_seq = header
                .flags
                .contains(TcpFlags::FIN)
                .then_some(Seq::new(header.seq) + payload_len);

            if let Some(payload_seq) = payload_seq {
                if payload_seq == recv.buffer.next_seq() {
                    recv.buffer.add(payload);
                    self.need_to_ack = true;
                } else {
                    // TODO: store (truncated?) out-of-order packet
                }
            }

            if let Some(fin_seq) = fin_seq {
                if fin_seq == recv.buffer.next_seq() {
                    recv.is_closed = true;
                    self.need_to_ack = true;
                } else {
                    // TODO: store (truncated?) out of order packet
                }
            }

            self.send.window = header.window_size.try_into().unwrap();
        }

        if header.flags.contains(TcpFlags::ACK) {
            let valid_ack_range = SeqRange::new(
                self.send.buffer.start_seq() + 1,
                self.send.buffer.next_seq() + 1,
            );

            if valid_ack_range.contains(Seq::new(header.ack)) {
                // the SYN is always first, so if a new sequence number has been acknowledged, then
                // either it's acknowledging the SYN, or the SYN has been acknowledged in the past
                if Seq::new(header.ack) != self.send.buffer.start_seq() {
                    self.send.syn_acked = true;
                }

                self.send.buffer.advance_start(Seq::new(header.ack));
            }
        }

        Ok(())
    }

    pub fn pop_packet(&mut self, now: I) -> Result<(TcpHeader, Bytes), PopPacketError> {
        let (seq_range, mut flags, payload) =
            self.next_segment().ok_or(PopPacketError::NoPacket)?;

        // After this point we must always send a packet. If we don't, then we're not being
        // consistent with `self.wants_to_send()`.
        debug_assert!(self.wants_to_send());

        let ack = if let Some(recv) = self.recv.as_ref() {
            // we've received a SYN packet, so should always acknowledge
            flags.insert(TcpFlags::ACK);

            // if we received a FIN, we need to ack the FIN as well
            if recv.is_closed {
                recv.buffer.next_seq() + 1
            } else {
                recv.buffer.next_seq()
            }
        } else {
            // not setting the ACK flag, so this can probably be anything
            Seq::new(0)
        };

        // TODO: what default to use here?
        let window_size = self
            .recv
            .as_ref()
            .map(|x| x.window_range().len())
            .unwrap_or(100)
            .try_into()
            .unwrap();

        let header = TcpHeader {
            ip: Ipv4Header {
                src: *self.local_addr.ip(),
                dst: *self.remote_addr.ip(),
            },
            flags,
            src_port: self.local_addr.port(),
            dst_port: self.remote_addr.port(),
            seq: seq_range.start.into(),
            ack: ack.into(),
            window_size,
            selective_acks: None,
            window_scale: None,
            timestamp: None,
            timestamp_echo: None,
        };

        // we're sending the most up-to-date acknowledgement and window size
        self.need_to_ack = false;
        self.need_to_send_window_update = false;

        // inform the buffer that we transmitted this segment
        self.send.buffer.mark_as_transmitted(seq_range.end, now);

        Ok((header, payload))
    }

    fn next_segment(&self) -> Option<(SeqRange, TcpFlags, Bytes)> {
        // should be inlined
        self._next_segment()
    }

    pub fn wants_to_send(&self) -> bool {
        // should be inlined
        self._next_segment().is_some()
    }

    /// Do not call directly. Use either `next_segment()` or `wants_to_send()`.
    ///
    /// Since `wants_to_send()` is only interested in whether the result is `Some`, by inlining this
    /// function the compiler should hopefully optimize it to remove unnecessary values that will be
    /// immediately discarded. I'm uncertain whether there's really much that can be optimized here
    /// though, but splitting it into functions will at least help us notice if either function is
    /// showing up in a profile/heatmap. Since the function is large and is `inline(always)` we only
    /// call it from two functions, `next_segment()` and `wants_to_send()`.
    #[inline(always)]
    fn _next_segment(&self) -> Option<(SeqRange, TcpFlags, Bytes)> {
        let (seq_range, flags, payload) = 'packet: {
            let send_window = self.send.window_range();

            if let Some((seq, metadata)) = self.send.buffer.next_not_transmitted() {
                // if segment fits in the send window
                if send_window.contains(seq + metadata.segment().len()) {
                    let (flags, payload) = match metadata.segment() {
                        Segment::Syn => (TcpFlags::SYN, Bytes::new()),
                        Segment::Fin => (TcpFlags::FIN, Bytes::new()),
                        Segment::Data(bytes) => (TcpFlags::empty(), bytes.clone()),
                    };

                    let seq_range = SeqRange::new(seq, seq + metadata.segment().len());
                    break 'packet (seq_range, flags, payload);
                }
            }

            if self.need_to_ack || self.need_to_send_window_update {
                // use the sequence number of the next unsent message if we have one buffered,
                // otherwise get the next sequence number from the buffer
                let seq = self
                    .send
                    .buffer
                    .next_not_transmitted()
                    .map(|x| x.0)
                    .unwrap_or(self.send.buffer.next_seq());

                let seq_range = SeqRange::new(seq, seq);
                break 'packet (seq_range, TcpFlags::empty(), Bytes::new());
            }

            return None;
        };

        Some((seq_range, flags, payload))
    }

    /// Returns true if we received a SYN packet from the peer.
    pub fn received_syn(&self) -> bool {
        // we don't construct the receive part of the connection until we've received the SYN
        self.recv.is_some()
    }

    /// Returns true if we received a FIN packet from the peer.
    pub fn received_fin(&self) -> bool {
        self.recv.as_ref().map(|x| x.is_closed).unwrap_or(false)
    }

    /// Returns true if the peer acknowledged the SYN packet we sent.
    pub fn syn_was_acked(&self) -> bool {
        self.send.syn_acked
    }

    /// Returns true if the peer acknowledged the FIN packet we sent.
    pub fn fin_was_acked(&self) -> bool {
        self.send.is_closed && self.send.buffer.start_seq() == self.send.buffer.next_seq()
    }

    /// Returns true if the send buffer has space available. Does not consider whether the
    /// connection is open/closed, either due to FIN packets or `shutdown()`.
    pub fn send_buf_has_space(&self) -> bool {
        let send_buffer_len = self.send.buffer.len() as usize;

        send_buffer_len < Self::SEND_BUF_MAX
    }

    /// Returns true if the recv buffer has data to read. Does not consider whether the connection
    /// is open/closed, either due to FIN packets or `shutdown()`.
    pub fn recv_buf_has_data(&self) -> bool {
        let is_empty = self
            .recv
            .as_ref()
            .map(|x| x.buffer.is_empty())
            .unwrap_or(true);
        !is_empty
    }
}

#[derive(Debug)]
pub(crate) struct ConnectionSend<I: Instant> {
    pub(crate) buffer: super::buffer::SendQueue<I>,
    pub(crate) window: u32,
    pub(crate) is_closed: bool,
    pub(crate) syn_acked: bool,
}

impl<I: Instant> ConnectionSend<I> {
    pub fn new(initial_seq: Seq) -> Self {
        Self {
            buffer: super::buffer::SendQueue::new(initial_seq),
            window: 5000,
            is_closed: false,
            syn_acked: false,
        }
    }

    pub fn window_range(&self) -> SeqRange {
        // the buffer stores unsent/unacked data, so the buffer starts at the lowest unacked
        // sequence number
        let window_left = self.buffer.start_seq();
        SeqRange::new(window_left, window_left + self.window)
    }
}

#[derive(Debug)]
pub(crate) struct ConnectionRecv {
    pub(crate) buffer: super::buffer::RecvQueue,
    pub(crate) is_closed: bool,
}

impl ConnectionRecv {
    pub fn new(initial_seq: Seq) -> Self {
        Self {
            buffer: super::buffer::RecvQueue::new(initial_seq),
            is_closed: false,
        }
    }

    pub fn window_range(&self) -> SeqRange {
        const MAX_BUFSIZE: u32 = 10000;
        let window_left = self.buffer.next_seq();
        let window_len = MAX_BUFSIZE.saturating_sub(self.buffer.len());
        SeqRange::new(window_left, window_left + window_len)
    }
}

fn trim_segment(
    header: &TcpHeader,
    payload: Bytes,
    range: &SeqRange,
) -> Option<(TcpHeader, Bytes)> {
    let seq = Seq::new(header.seq);
    let syn_len = if header.flags.contains(TcpFlags::SYN) {
        1
    } else {
        0
    };
    let fin_len = if header.flags.contains(TcpFlags::FIN) {
        1
    } else {
        0
    };
    let payload_len = payload.len().try_into().unwrap();
    let header_range = SeqRange::new(seq, seq + syn_len + payload_len + fin_len);

    let include_syn = syn_len == 1 && range.contains(header_range.start);
    let include_fin = fin_len == 1 && range.contains(header_range.end - 1);

    let intersection = header_range.intersection(range)?;

    let payload_seq = seq + syn_len;
    let new_payload = match trim_payload(payload_seq, payload, range) {
        Some((new_seq, new_payload)) => {
            assert_eq!(
                new_seq,
                intersection.start + if include_syn { 1 } else { 0 }
            );
            new_payload
        }
        None => Bytes::new(),
    };

    let mut new_flags = header.flags;
    new_flags.set(TcpFlags::SYN, include_syn);
    new_flags.set(TcpFlags::FIN, include_fin);

    let new_header = TcpHeader {
        seq: intersection.start.into(),
        flags: new_flags,
        ..*header
    };

    Some((new_header, new_payload))
}

/// Trims `payload`, which starts at a given `seq` number, such that only bytes in the sequence
/// `range` remain.
///
/// If the two ranges do not intersect `None` will be returned. A `None` is also returned if the
/// range intersects the payload twice, for example if the payload covers the range 100..200 and the
/// given range covers 180..120, but this shouldn't occur for reasonable TCP sequence number ranges.
/// The returned payload may be empty if the original `payload` was empty or the `range` was empty,
/// but they still intersect according to [`SeqRange::intersection`].
fn trim_payload(seq: Seq, mut payload: Bytes, range: &SeqRange) -> Option<(Seq, Bytes)> {
    let payload_range = SeqRange::new(seq, seq + payload.len().try_into().unwrap());

    let intersection = payload_range.intersection(range)?;

    let new_offset = intersection.start - seq;
    let new_len = intersection.len();

    let new_offset: usize = new_offset.try_into().unwrap();
    let new_len: usize = new_len.try_into().unwrap();

    // update the existing `Bytes` object rather than using `slice()` to avoid an atomic operation
    payload.advance(new_offset);
    payload.truncate(new_len);

    Some((intersection.start, payload))
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::net::Ipv4Addr;

    // helper to make the tests fit on a single line
    fn range(start: u32, end: u32) -> SeqRange {
        SeqRange::new(Seq::new(start), Seq::new(end))
    }

    // helper to make the tests fit on a single line
    fn seq(val: u32) -> Seq {
        Seq::new(val)
    }

    // helper to make the tests fit on a single line
    fn bytes<const N: usize>(x: &[u8; N]) -> Bytes {
        Box::<[u8]>::from(x.as_slice()).into()
    }

    #[test]
    fn test_trim_segment() {
        fn test_trim(
            flags: TcpFlags,
            seq: Seq,
            payload: impl Into<Bytes>,
            range: SeqRange,
        ) -> Option<(TcpFlags, Seq, Bytes)> {
            let header = TcpHeader {
                ip: Ipv4Header {
                    src: Ipv4Addr::UNSPECIFIED,
                    dst: Ipv4Addr::UNSPECIFIED,
                },
                flags,
                src_port: 0,
                dst_port: 0,
                seq: seq.into(),
                ack: 0,
                window_size: 0,
                selective_acks: None,
                window_scale: None,
                timestamp: None,
                timestamp_echo: None,
            };

            let (header, payload) = trim_segment(&header, payload.into(), &range)?;

            Some((header.flags, Seq::new(header.seq), payload))
        }

        const SYN: TcpFlags = TcpFlags::SYN;
        const FIN: TcpFlags = TcpFlags::FIN;
        const ACK: TcpFlags = TcpFlags::ACK;
        const EMPTY: TcpFlags = TcpFlags::empty();

        assert_eq!(test_trim(EMPTY, seq(0), bytes(b""), range(1, 1)), None);
        assert_eq!(test_trim(EMPTY, seq(1), bytes(b""), range(0, 1)), None);
        assert_eq!(
            test_trim(EMPTY, seq(0), bytes(b""), range(0, 0)),
            Some((EMPTY, seq(0), bytes(b""))),
        );
        assert_eq!(
            test_trim(ACK, seq(0), bytes(b""), range(0, 0)),
            Some((ACK, seq(0), bytes(b""))),
        );
        assert_eq!(
            test_trim(EMPTY, seq(0), bytes(b"123"), range(0, 0)),
            Some((EMPTY, seq(0), bytes(b""))),
        );
        assert_eq!(
            test_trim(SYN, seq(0), bytes(b""), range(0, 0)),
            Some((EMPTY, seq(0), bytes(b""))),
        );
        assert_eq!(
            test_trim(FIN, seq(0), bytes(b""), range(0, 0)),
            Some((EMPTY, seq(0), bytes(b""))),
        );
        assert_eq!(
            test_trim(EMPTY, seq(0), bytes(b""), range(0, 2)),
            Some((EMPTY, seq(0), bytes(b""))),
        );
        assert_eq!(
            test_trim(EMPTY, seq(0), bytes(b"123"), range(0, 2)),
            Some((EMPTY, seq(0), bytes(b"12"))),
        );
        assert_eq!(
            test_trim(SYN, seq(0), bytes(b"123"), range(0, 2)),
            Some((SYN, seq(0), bytes(b"1"))),
        );
        assert_eq!(
            test_trim(SYN | FIN, seq(0), bytes(b"123"), range(0, 2)),
            Some((SYN, seq(0), bytes(b"1"))),
        );
        assert_eq!(
            test_trim(SYN | FIN, seq(0), bytes(b"123"), range(1, 2)),
            Some((EMPTY, seq(1), bytes(b"1"))),
        );
        assert_eq!(
            test_trim(SYN | FIN, seq(0), bytes(b"123"), range(1, 5)),
            Some((FIN, seq(1), bytes(b"123"))),
        );
        assert_eq!(
            test_trim(SYN | FIN, seq(0), bytes(b"123"), range(0, 1)),
            Some((SYN, seq(0), bytes(b""))),
        );
        assert_eq!(
            test_trim(SYN | FIN, seq(4), bytes(b"123"), range(0, 5)),
            Some((SYN, seq(4), bytes(b""))),
        );
        assert_eq!(
            test_trim(SYN | FIN | ACK, seq(3), bytes(b"123"), range(0, 5)),
            Some((SYN | ACK, seq(3), bytes(b"1"))),
        );
    }

    #[test]
    fn test_trim_payload() {
        fn test_trim(seq: Seq, payload: impl Into<Bytes>, range: SeqRange) -> Option<(Seq, Bytes)> {
            trim_payload(seq, payload.into(), &range)
        }

        assert_eq!(
            test_trim(seq(0), bytes(b""), range(0, 0)),
            Some((seq(0), bytes(b""))),
        );
        assert_eq!(test_trim(seq(1), bytes(b""), range(0, 0)), None);
        assert_eq!(test_trim(seq(1), bytes(b""), range(0, 1)), None);
        assert_eq!(
            test_trim(seq(1), bytes(b""), range(0, 2)),
            Some((seq(1), bytes(b""))),
        );
        assert_eq!(
            test_trim(seq(0), bytes(b"a"), range(0, 0)),
            Some((seq(0), bytes(b""))),
        );
        assert_eq!(
            test_trim(seq(0), bytes(b"a"), range(0, 1)),
            Some((seq(0), bytes(b"a"))),
        );
        assert_eq!(
            test_trim(seq(0), bytes(b"ab"), range(0, 1)),
            Some((seq(0), bytes(b"a"))),
        );
        assert_eq!(
            test_trim(seq(0), bytes(b"abcdefg"), range(2, 4)),
            Some((seq(2), bytes(b"cd"))),
        );
        assert_eq!(
            test_trim(seq(3), bytes(b"abcdefg"), range(2, 4)),
            Some((seq(3), bytes(b"a"))),
        );
        assert_eq!(
            test_trim(seq(3), bytes(b"abcdefg"), range(2, 20)),
            Some((seq(3), bytes(b"abcdefg"))),
        );
        assert_eq!(
            test_trim(seq(3), bytes(b"abcdefg"), range(9, 20)),
            Some((seq(9), bytes(b"g"))),
        );
        assert_eq!(test_trim(seq(3), bytes(b"abcdefg"), range(10, 20)), None);

        // second test intersects twice, so returns `None`
        assert_eq!(
            test_trim(seq(5), bytes(b"abcdefg"), range(8, 5)),
            Some((seq(8), bytes(b"defg"))),
        );
        assert_eq!(test_trim(seq(5), bytes(b"abcdefg"), range(8, 7)), None);
    }
}
