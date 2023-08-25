use std::collections::LinkedList;
use std::io::{Read, Write};
use std::net::SocketAddrV4;

use bytes::{Bytes, BytesMut};

use crate::{Ipv4Header, RecvError, SendError, TcpFlags, TcpHeader};

/// Information for a TCP connection.
#[derive(Debug)]
pub(crate) struct Connection {
    pub(crate) local_addr: SocketAddrV4,
    pub(crate) remote_addr: SocketAddrV4,
    pub(crate) send_buffer: LinkedList<(TcpHeader, Bytes)>,
    pub(crate) recv_buffer: LinkedList<(TcpHeader, Bytes)>,
    // this should also contain types for tracking window sizes, sequence numbers, etc
}

impl Connection {
    /// The max number of bytes allowed in the send buffer. This is an arbitrary number used
    /// temporarily until we implement a proper TCP queue.
    const SEND_BUF_MAX: usize = 100_000;

    /// Returns `true` if the packet header src/dst addresses match this connection.
    pub fn packet_addrs_match(&self, header: &TcpHeader) -> bool {
        header.src() == self.remote_addr && header.dst() == self.local_addr
    }

    pub fn send(&mut self, mut reader: impl Read, len: usize) -> Result<usize, SendError> {
        // if the buffer is full
        if !self.send_buf_has_space() {
            return Err(SendError::Full);
        }

        let send_buffer_len: usize = self
            .send_buffer
            .iter()
            .map(|(_, payload)| payload.len())
            .sum();
        let send_buffer_space = Self::SEND_BUF_MAX.saturating_sub(send_buffer_len);

        let len = std::cmp::min(len, send_buffer_space);
        let mut remaining = len;

        // The max number of payload bytes allowed per packet. This is an arbitrary number used
        // temporarily until we add code that considers the MSS.
        const MAX_PER_PACKET: usize = 1024;

        while remaining > 0 {
            let to_read = std::cmp::min(remaining, MAX_PER_PACKET);
            let mut payload = BytesMut::from_iter(std::iter::repeat(0).take(to_read));
            if let Err(e) = reader.read_exact(&mut payload[..]) {
                return Err(SendError::Io(e));
            }

            let header = TcpHeader {
                ip: Ipv4Header {
                    src: *self.local_addr.ip(),
                    dst: *self.remote_addr.ip(),
                },
                flags: TcpFlags::empty(),
                src_port: self.local_addr.port(),
                dst_port: self.remote_addr.port(),
                seq: 0,
                ack: 0,
                window_size: 0,
                selective_acks: None,
                window_scale: None,
                timestamp: None,
                timestamp_echo: None,
            };

            self.send_buffer.push_back((header, payload.into()));

            remaining -= to_read;
        }

        Ok(len)
    }

    pub fn recv(&mut self, mut writer: impl Write, len: usize) -> Result<usize, RecvError> {
        let mut bytes_copied = 0;
        let has_data = self
            .recv_buffer
            .iter()
            .any(|(_, payload)| !payload.is_empty());

        if !has_data {
            return Err(RecvError::Empty);
        }

        while bytes_copied < len {
            let Some((_header, payload)) = self.recv_buffer.front_mut() else {
                return Ok(bytes_copied);
            };

            let remaining = len - bytes_copied;
            let to_write = std::cmp::min(payload.len(), remaining);
            if let Err(e) = writer.write_all(&payload[..to_write]) {
                // TODO: the stream will lose partial data; is this fine?
                return Err(RecvError::Io(e));
            }

            bytes_copied += to_write;

            if to_write == payload.len() {
                // copied all bytes, so remove chunk from the buffer
                self.recv_buffer.pop_front();
            } else {
                // copied some bytes, so shrink chunk in the buffer
                let _ = payload.split_to(to_write);
            }
        }

        Ok(bytes_copied)
    }

    /// Returns true if the send buffer has space available. Does not consider whether the
    /// connection is open/closed, either due to FIN packets or `shutdown()`.
    pub fn send_buf_has_space(&self) -> bool {
        let send_buffer_len: usize = self
            .send_buffer
            .iter()
            .map(|(_, payload)| payload.len())
            .sum();

        send_buffer_len < Self::SEND_BUF_MAX
    }

    /// Returns true if the recv buffer has data to read. Does not consider whether the connection
    /// is open/closed, either due to FIN packets or `shutdown()`.
    pub fn recv_buf_has_data(&self) -> bool {
        !self.recv_buffer.is_empty()
    }
}
