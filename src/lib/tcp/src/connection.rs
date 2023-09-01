use bytes::{Buf, Bytes, BytesMut};
use std::io::{Read, Write};
use std::net::SocketAddrV4;

use crate::buffer::Segment;
use crate::seq::{Seq, SeqRange};
use crate::util::time::Instant;
use crate::window_scaling::WindowScaling;
use crate::{
    Ipv4Header, PopPacketError, PushPacketError, RecvError, SendError, TcpConfig, TcpFlags,
    TcpHeader,
};

/// Information for a TCP connection. Equivalent to the Transmission Control Block (TCB).
#[derive(Debug)]
pub(crate) struct Connection<I: Instant> {
    pub(crate) config: TcpConfig,
    pub(crate) local_addr: SocketAddrV4,
    pub(crate) remote_addr: SocketAddrV4,
    pub(crate) send: ConnectionSend<I>,
    pub(crate) recv: Option<ConnectionRecv>,
    pub(crate) need_to_ack: bool,
    pub(crate) last_advertised_window: Option<u32>,
    pub(crate) window_scaling: WindowScaling,
}

impl<I: Instant> Connection<I> {
    /// The max number of bytes allowed in the send and receive buffers. These should be made
    /// dynamic in the future.
    const SEND_BUF_MAX: usize = 100_000;
    const RECV_BUF_MAX: u32 = 100_000;

    pub fn new(
        local_addr: SocketAddrV4,
        remote_addr: SocketAddrV4,
        send_initial_seq: Seq,
        config: TcpConfig,
    ) -> Self {
        let mut rv = Self {
            config,
            local_addr,
            remote_addr,
            send: ConnectionSend::new(send_initial_seq),
            recv: None,
            need_to_ack: true,
            last_advertised_window: None,
            window_scaling: WindowScaling::new(),
        };

        // disable window scaling if it's disabled in the config
        if !rv.config.window_scaling_enabled {
            rv.window_scaling.disable();
        }

        rv
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
        let recv = self.recv.as_mut().unwrap();

        let has_data = !recv.buffer.is_empty();

        if !has_data {
            return Err(RecvError::Empty);
        }

        while bytes_copied < len {
            let remaining = len - bytes_copied;
            let remaining_u32 = remaining.try_into().unwrap_or(u32::MAX);

            let Some((_seq, data)) = recv.buffer.pop(remaining_u32) else {
                break;
            };

            assert!(data.len() <= remaining);

            if let Err(e) = writer.write_all(&data) {
                // TODO: the stream will lose partial data; is this fine?
                return Err(RecvError::Io(e));
            }

            bytes_copied += data.len();
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
            let seq = Seq::new(header.seq);
            self.recv = Some(ConnectionRecv::new(seq));

            self.window_scaling.received_syn(header.window_scale);
        }

        // We need to keep track of if the original packet had the SYN flag set, even if we trim a
        // old retransmitted SYN flag from the packet below. If it was sent with a SYN flag, then we
        // must not apply the window scale to the window size in the packet, even if the SYN's
        // sequence number isn't within the receive window.
        //
        // RFC 7323 2.2.:
        // > The window field in a segment where the SYN bit is set (i.e., a <SYN> or <SYN,ACK>)
        // > MUST NOT be scaled.
        //
        // TODO: be careful about this if we support a reassembly queue in the future
        let original_packet_had_syn = header.flags.contains(TcpFlags::SYN);

        let payload = payload.into();

        let recv_window = self.recv_window().unwrap();
        let Some((header, payload)) = trim_segment(header, payload, &recv_window) else {
            // the sequence range of the segment does not overlap with the receive window, so we
            // must drop the packet and send an ACK
            self.need_to_ack = true;

            return Ok(());
        };

        let Some(recv) = self.recv.as_mut() else {
            // we received a non-SYN packet before the first SYN packet

            if !header.flags.contains(TcpFlags::RST) {
                // TODO: send a RST packet
            }

            // TODO: move to closed state

            return Ok(());
        };

        // the receive buffer's initial next sequence number; useful so we can check if we need to
        // acknowledge or not
        let initial_seq = recv.buffer.next_seq();

        if !recv.is_closed {
            if header.flags.contains(TcpFlags::SYN) {
                if recv.buffer.syn_added() {
                    // this is the second SYN we've received

                    // TODO: We can follow RFC 793 or RFC 5961 here. 793 is probably easiest, and we
                    // should send an RST and move to the "closed" state.

                    return Ok(());
                }

                recv.buffer.add_syn();
            }

            let syn_len = if header.flags.contains(TcpFlags::SYN) {
                1
            } else {
                0
            };

            let payload_len: u32 = payload.len().try_into().unwrap();
            let payload_seq = (payload_len != 0).then_some(Seq::new(header.seq) + syn_len);
            let fin_seq = header
                .flags
                .contains(TcpFlags::FIN)
                .then_some(Seq::new(header.seq) + syn_len + payload_len);

            if let Some(payload_seq) = payload_seq {
                if payload_seq == recv.buffer.next_seq() {
                    recv.buffer.add(payload);
                } else {
                    // TODO: store (truncated?) out-of-order packet
                }
            }

            if let Some(fin_seq) = fin_seq {
                if fin_seq == recv.buffer.next_seq() {
                    recv.buffer.add_fin();
                    recv.is_closed = true;
                } else {
                    // TODO: store (truncated?) out of order packet
                }
            }
        }

        // we've added to the receive buffer (payload, syn, or fin), so we need to send an
        // acknowledgement
        if recv.buffer.next_seq() != initial_seq {
            self.need_to_ack = true;
        }

        // update the send window, applying the window scale shift only if it wasn't a SYN packet
        // TODO: should we still update the window if the ACK was not in the valid range?
        if original_packet_had_syn {
            self.send.window = u32::try_from(header.window_size).unwrap();
        } else {
            self.send.window = u32::try_from(header.window_size).unwrap()
                << self.window_scaling.send_window_scale_shift();
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

        let header_ack = if let Some(recv) = self.recv.as_ref() {
            // we've received a SYN packet (either now or in the past), so should always acknowledge
            flags.insert(TcpFlags::ACK);
            recv.buffer.next_seq()
        } else {
            // not setting the ACK flag, so this can probably be anything
            Seq::new(0)
        };

        let header_window_size;
        let header_window_scale;

        if flags.contains(TcpFlags::SYN) {
            if self.window_scaling.can_send_window_scale() {
                // The receive buffer capacity at the time the SYN is sent decides the window
                // scaling to use. This effectively limits future receive buffer capacity increases
                // since the receive window will forever have a ceiling set here by the window
                // scale.
                let shift = WindowScaling::scale_shift_for_max_window(self.recv_buffer_capacity());
                header_window_scale = Some(shift);
            } else {
                header_window_scale = None;
            }

            // don't actually apply this window scale in the SYN packet
            //
            // RFC 7323 2.2.:
            // > The window field in a segment where the SYN bit is set (i.e., a <SYN> or <SYN,ACK>)
            // > MUST NOT be scaled.
            header_window_size = self.recv_window_len();
            self.last_advertised_window = Some(header_window_size);

            // Make sure we're sending a valid 2-byte window size. We haven't called
            // `WindowScaling::sent_syn()` yet, so `Self::recv_window_len()` should not have
            // returned a window size larger than `u16::MAX`.
            debug_assert!(header_window_size <= u16::MAX as u32);

            self.window_scaling.sent_syn(header_window_scale);
        } else {
            // don't send a window scale
            //
            // RFC 7323 2.1.:
            // > The exponent of the scale factor is carried in a TCP option, Window Scale. This
            // > option is sent only in a <SYN> segment (a segment with the SYN bit on), [...]
            header_window_scale = None;

            let shift = self.window_scaling.recv_window_scale_shift();
            header_window_size = self.recv_window_len() >> shift;

            // this is the value the peer will see (precision is intentionally lost due to bit-shift)
            self.last_advertised_window = Some(header_window_size << shift);
        }

        let header = TcpHeader {
            ip: Ipv4Header {
                src: *self.local_addr.ip(),
                dst: *self.remote_addr.ip(),
            },
            flags,
            src_port: self.local_addr.port(),
            dst_port: self.remote_addr.port(),
            seq: seq_range.start.into(),
            ack: header_ack.into(),
            window_size: header_window_size.try_into().unwrap(),
            selective_acks: None,
            window_scale: header_window_scale,
            timestamp: None,
            timestamp_echo: None,
        };

        // we're sending the most up-to-date acknowledgement
        self.need_to_ack = false;

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
        let (seq_range, syn_fin_flags, payload) = 'packet: {
            // do we have a syn/fin/payload packet to send?
            if let Some((seq, metadata)) = self.send.buffer.next_not_transmitted() {
                let send_window = self.send_window();

                // does the segment fit in the send window?
                if send_window.contains(seq + metadata.segment().len()) {
                    debug_assert!(send_window.contains(seq));
                    let (syn_fin_flags, payload) = match metadata.segment() {
                        Segment::Syn => (TcpFlags::SYN, Bytes::new()),
                        Segment::Fin => (TcpFlags::FIN, Bytes::new()),
                        Segment::Data(bytes) => (TcpFlags::empty(), bytes.clone()),
                    };

                    let seq_range = SeqRange::new(seq, seq + metadata.segment().len());
                    break 'packet (seq_range, syn_fin_flags, payload);
                }
            }

            let mut send_empty_packet = false;

            // do we need to send an acknowledgement?
            if self.need_to_ack {
                send_empty_packet = true;
            }

            // do we need to send a window update?
            if let Some(window) = self.recv_window().map(|x| x.len()) {
                let window_scale = self.window_scaling.recv_window_scale_shift();

                let apparent_window = window >> window_scale << window_scale;

                if self.last_advertised_window != Some(apparent_window) {
                    send_empty_packet = true;
                }
            }

            if send_empty_packet {
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

        // if not sending a SYN packet and window scaling isn't yet confirmed
        if !syn_fin_flags.contains(TcpFlags::SYN) && !self.window_scaling.is_configured() {
            // we cannot send a non-SYN packet since non-SYN packets must apply window scaling, but
            // we haven't yet confirmed if we're using window scaling or not
            return None;
        }

        Some((seq_range, syn_fin_flags, payload))
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

    pub(crate) fn send_window(&self) -> SeqRange {
        // the buffer stores unsent/unacked data, so the buffer starts at the lowest unacked
        // sequence number
        let window_left = self.send.buffer.start_seq();
        SeqRange::new(window_left, window_left + self.send.window)
    }

    /// Returns the size of the receive window. This is useful when we only need the size of the
    /// window and we may not have received the SYN packet yet, so cannot construct the range.
    pub(crate) fn recv_window_len(&self) -> u32 {
        if let Some(recv_window) = self.recv_window() {
            return recv_window.len();
        }

        let window_max = self.window_scaling.recv_window_max();
        std::cmp::min(self.recv_buffer_capacity(), window_max)
    }

    /// Returns the receive window if we've received a SYN packet.
    pub(crate) fn recv_window(&self) -> Option<SeqRange> {
        let recv = self.recv.as_ref()?;
        let window_left = recv.buffer.next_seq();
        let window_max = self.window_scaling.recv_window_max();
        let window_len = self
            .recv_buffer_capacity()
            .saturating_sub(recv.buffer.len());
        let window_len = std::cmp::min(window_len, window_max);
        Some(SeqRange::new(window_left, window_left + window_len))
    }

    /// The total capacity of the receive buffer.
    fn recv_buffer_capacity(&self) -> u32 {
        Self::RECV_BUF_MAX
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
            // we don't know the peer's receive window, so choose something conservative
            window: 2048,
            is_closed: false,
            syn_acked: false,
        }
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
    let new_payload = match trim_chunk(payload_seq, payload, range) {
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

/// Trims `chunk`, which starts at a given `seq` number, such that only bytes in the sequence
/// `range` remain.
///
/// If the two ranges do not intersect `None` will be returned. A `None` is also returned if the
/// range intersects the chunk twice, for example if the chunk covers the range 100..200 and the
/// given range covers 180..120, but this shouldn't occur for reasonable TCP sequence number ranges.
/// The returned chunk may be empty if the original `chunk` was empty or the `range` was empty, but
/// they still intersect according to [`SeqRange::intersection`].
fn trim_chunk(seq: Seq, mut chunk: Bytes, range: &SeqRange) -> Option<(Seq, Bytes)> {
    let chunk_range = SeqRange::new(seq, seq + chunk.len().try_into().unwrap());

    let intersection = chunk_range.intersection(range)?;

    let new_offset = intersection.start - seq;
    let new_len = intersection.len();

    let new_offset: usize = new_offset.try_into().unwrap();
    let new_len: usize = new_len.try_into().unwrap();

    // update the existing `Bytes` object rather than using `slice()` to avoid an atomic operation
    chunk.advance(new_offset);
    chunk.truncate(new_len);

    Some((intersection.start, chunk))
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
    fn test_trim_chunk() {
        fn test_trim(seq: Seq, chunk: Bytes, range: SeqRange) -> Option<(Seq, Bytes)> {
            trim_chunk(seq, chunk, &range)
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
