use std::io::{Seek, SeekFrom, Write};

use crate::utility::give::Give;

pub struct PcapWriter<W: Write> {
    writer: W,
    capture_len: u32,
}

impl<W: Write> PcapWriter<W> {
    /// A new packet capture writer. Each packet (header and payload) captured will be truncated to
    /// a length `capture_len`.
    pub fn new(writer: W, capture_len: u32) -> std::io::Result<Self> {
        let mut rv = PcapWriter {
            writer,
            capture_len,
        };

        rv.write_header()?;

        Ok(rv)
    }

    fn write_header(&mut self) -> std::io::Result<()> {
        // magic number to show endianness
        const MAGIC_NUMBER: u32 = 0xA1B2C3D4;
        const VERSION_MAJOR: u16 = 2;
        const VERSION_MINOR: u16 = 4;
        // GMT to local correction
        const THIS_ZONE: i32 = 0;
        // accuracy of timestamps
        const SIG_FLAGS: u32 = 0;
        // data link type (LINKTYPE_RAW)
        const NETWORK: u32 = 101;

        // magic number: 4 bytes
        self.writer.write_all(&MAGIC_NUMBER.to_ne_bytes())?;
        // major version: 2 bytes
        self.writer.write_all(&VERSION_MAJOR.to_ne_bytes())?;
        // minor version: 2 bytes
        self.writer.write_all(&VERSION_MINOR.to_ne_bytes())?;

        // GMT to local correction: 4 bytes
        self.writer.write_all(&THIS_ZONE.to_ne_bytes())?;
        // accuracy of timestamps: 4 bytes
        self.writer.write_all(&SIG_FLAGS.to_ne_bytes())?;
        // snapshot length: 4 bytes
        self.writer.write_all(&self.capture_len.to_ne_bytes())?;
        // link type: 4 bytes
        self.writer.write_all(&NETWORK.to_ne_bytes())?;

        Ok(())
    }

    /// Write a packet from a buffer.
    pub fn write_packet(
        &mut self,
        ts_sec: u32,
        ts_usec: u32,
        packet: &[u8],
    ) -> std::io::Result<()> {
        let packet_len = u32::try_from(packet.len()).unwrap();
        let packet_trunc_len = std::cmp::min(packet_len, self.capture_len);

        // timestamp (seconds): 4 bytes
        self.writer.write_all(&ts_sec.to_ne_bytes())?;
        // timestamp (microseconds): 4 bytes
        self.writer.write_all(&ts_usec.to_ne_bytes())?;

        // captured packet length: 4 bytes
        self.writer.write_all(&packet_trunc_len.to_ne_bytes())?;
        // original packet length: 4 bytes
        self.writer.write_all(&packet_len.to_ne_bytes())?;

        // packet data: `packet_trunc_len` bytes
        self.writer
            .write_all(&packet[..(packet_trunc_len.try_into().unwrap())])?;

        Ok(())
    }
}

impl<W: Write + Seek> PcapWriter<W> {
    /// Write a packet without requiring an intermediate buffer.
    pub fn write_packet_fmt(
        &mut self,
        ts_sec: u32,
        ts_usec: u32,
        packet_len: u32,
        write_packet_fn: impl FnOnce(&mut Give<&mut W>) -> std::io::Result<()>,
    ) -> std::io::Result<()> {
        // timestamp (seconds): 4 bytes
        self.writer.write_all(&ts_sec.to_ne_bytes())?;
        // timestamp (microseconds): 4 bytes
        self.writer.write_all(&ts_usec.to_ne_bytes())?;

        // position of the captured packet length field
        let pos_of_len = self.writer.stream_position()?;

        // captured packet length: 4 bytes
        // (write initially as 0, we'll update it later)
        self.writer.write_all(&0u32.to_ne_bytes())?;
        // original packet length: 4 bytes
        self.writer.write_all(&packet_len.to_ne_bytes())?;

        // position of the packet data
        let pos_before_packet_data = self.writer.stream_position()?;

        // packet data: a soft limit of `capture_len` bytes
        match write_packet_fn(&mut Give::new(&mut self.writer, self.capture_len as u64)) {
            Ok(()) => {}
            // this should mean that the entire packet couldn't be written, which is fine since
            // we'll use a smaller captured packet length value
            Err(e) if e.kind() == std::io::ErrorKind::WriteZero => {}
            Err(e) => return Err(e),
        }

        // position after the packet data
        let pos_after_packet_data = self.writer.stream_position()?;
        // the number of packet data bytes written
        let bytes_written = pos_after_packet_data - pos_before_packet_data;

        // it is still possible for 'write_payload_fn' to have written more bytes than it was
        // supposed to, so double check here
        if bytes_written > self.capture_len.into() {
            log::warn!(
                "Pcap writer wrote more bytes than intended: {bytes_written} > {}",
                self.capture_len
            );
            return Err(std::io::ErrorKind::InvalidData.into());
        }

        // go back and update the captured packet length
        let bytes_written = u32::try_from(bytes_written).unwrap();
        self.writer.seek(SeekFrom::Start(pos_of_len))?;
        // captured packet length: 4 bytes
        self.writer.write_all(&bytes_written.to_ne_bytes())?;
        self.writer.seek(SeekFrom::Start(pos_after_packet_data))?;

        Ok(())
    }
}

pub trait PacketDisplay {
    /// Write the packet bytes.
    fn display_bytes(&self, writer: impl Write) -> std::io::Result<()>;
}

#[cfg(test)]
mod tests {
    use std::io::Cursor;

    use super::*;

    #[test]
    fn test_empty_pcap_writer() {
        let mut buf = vec![];
        PcapWriter::new(&mut buf, 65535).unwrap();

        let expected_header = [
            0xD4, 0xC3, 0xB2, 0xA1, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x65, 0x00, 0x00, 0x00,
        ];

        assert_eq!(buf, expected_header);
    }

    #[test]
    fn test_write_packet() {
        let mut buf = vec![];
        let mut pcap = PcapWriter::new(&mut buf, 65535).unwrap();
        pcap.write_packet(32, 128, &[0x01, 0x02, 0x03]).unwrap();

        let expected_header = [
            0xD4, 0xC3, 0xB2, 0xA1, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x65, 0x00, 0x00, 0x00,
        ];
        let expected_packet_header = [
            0x20, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00,
            0x00, 0x00,
        ];
        let expected_payload = [0x01, 0x02, 0x03];

        assert_eq!(
            buf,
            [
                &expected_header[..],
                &expected_packet_header[..],
                &expected_payload[..]
            ]
            .concat()
        );
    }

    #[test]
    fn test_write_packet_fmt() {
        let mut buf = Cursor::new(vec![]);
        let mut pcap = PcapWriter::new(&mut buf, 65535).unwrap();
        pcap.write_packet_fmt(32, 128, 3, |writer| {
            writer.write_all(&[0x01])?;
            writer.write_all(&[0x02])?;
            writer.write_all(&[0x03])
        })
        .unwrap();

        let expected_header = [
            0xD4, 0xC3, 0xB2, 0xA1, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x65, 0x00, 0x00, 0x00,
        ];
        let expected_packet_header = [
            0x20, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00,
            0x00, 0x00,
        ];
        let expected_payload = [0x01, 0x02, 0x03];

        let buf = buf.into_inner();

        assert_eq!(
            buf,
            [
                &expected_header[..],
                &expected_packet_header[..],
                &expected_payload[..]
            ]
            .concat()
        );
    }
}
