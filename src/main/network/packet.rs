use std::io::Write;

use crate::cshadow as c;
use crate::utility::pcap_writer::PacketDisplay;

impl PacketDisplay for *const c::Packet {
    fn display_bytes(&self, mut writer: impl Write) -> std::io::Result<()> {
        assert!(!self.is_null());

        let header_len: u16 = unsafe { c::packet_getHeaderSize(*self) }
            .try_into()
            .unwrap();
        let payload_len: u16 = unsafe { c::packet_getPayloadLength(*self) }
            .try_into()
            .unwrap();
        let protocol = unsafe { c::packet_getProtocol(*self) };

        // write the IP header

        let version_and_header_length: u8 = 0x45;
        let fields: u8 = 0x0;
        let total_length: u16 = header_len + payload_len;
        let identification: u16 = 0x0;
        let flags_and_fragment: u16 = 0x4000;
        let time_to_live: u8 = 64;
        let iana_protocol: u8 = match protocol {
            c::_ProtocolType_PTCP => 6,
            c::_ProtocolType_PUDP => 17,
            _ => panic!("Unexpected packet protocol"),
        };
        let header_checksum: u16 = 0x0;
        let source_ip: [u8; 4] =
            u32::from_be(unsafe { c::packet_getSourceIP(*self) }).to_be_bytes();
        let dest_ip: [u8; 4] =
            u32::from_be(unsafe { c::packet_getDestinationIP(*self) }).to_be_bytes();

        // version and header length: 1 byte
        // DSCP + ECN: 1 byte
        writer.write_all(&[version_and_header_length, fields])?;
        // total length: 2 bytes
        writer.write_all(&total_length.to_be_bytes())?;
        // identification: 2 bytes
        writer.write_all(&identification.to_be_bytes())?;
        // flags + fragment offset: 2 bytes
        writer.write_all(&flags_and_fragment.to_be_bytes())?;
        // ttl: 1 byte
        // protocol: 1 byte
        writer.write_all(&[time_to_live, iana_protocol])?;
        // header checksum: 2 bytes
        writer.write_all(&header_checksum.to_be_bytes())?;
        // source IP: 4 bytes
        writer.write_all(&source_ip)?;
        // destination IP: 4 bytes
        writer.write_all(&dest_ip)?;

        // write protocol-specific data

        match protocol {
            c::_ProtocolType_PTCP => display_tcp_bytes(*self, &mut writer)?,
            c::_ProtocolType_PUDP => display_udp_bytes(*self, &mut writer)?,
            _ => panic!("Unexpected packet protocol"),
        }

        // write payload data

        if payload_len > 0 {
            // shadow's packet payloads are guarded by a mutex, so it's easiest to make a copy of them
            let mut payload_buf = vec![0u8; payload_len.try_into().unwrap()];
            let count = unsafe {
                c::packet_copyPayloadShadow(
                    *self,
                    0,
                    payload_buf.as_mut_ptr() as *mut libc::c_void,
                    payload_len.into(),
                )
            };
            assert_eq!(
                count,
                u32::from(payload_len),
                "Packet payload somehow changed size"
            );

            // packet payload: `payload_len` bytes
            writer.write_all(&payload_buf)?;
        }

        Ok(())
    }
}

/// Helper for writing the tcp bytes of the packet.
fn display_tcp_bytes(packet: *const c::Packet, mut writer: impl Write) -> std::io::Result<()> {
    assert_eq!(
        unsafe { c::packet_getProtocol(packet) },
        c::_ProtocolType_PTCP
    );

    let tcp_header = unsafe { c::packet_getTCPHeader(packet) };
    assert!(!tcp_header.is_null());
    assert_eq!(
        tcp_header as usize % std::mem::align_of::<c::PacketTCPHeader>(),
        0
    );
    let tcp_header = unsafe { tcp_header.as_ref() }.unwrap();

    // write the TCP header

    let source_port: [u8; 2] =
        u16::from_be(unsafe { c::packet_getSourcePort(packet) }).to_be_bytes();
    let dest_port: [u8; 2] =
        u16::from_be(unsafe { c::packet_getDestinationPort(packet) }).to_be_bytes();
    let sequence: [u8; 4] = tcp_header.sequence.to_be_bytes();
    let ack: [u8; 4] = if tcp_header.flags & c::ProtocolTCPFlags_PTCP_ACK != 0 {
        tcp_header.acknowledgment.to_be_bytes()
    } else {
        0u32.to_be_bytes()
    };
    let header_len: u8 = 0x80;
    let mut tcp_flags: u8 = 0;
    if tcp_header.flags & c::ProtocolTCPFlags_PTCP_RST != 0 {
        tcp_flags |= 0x04;
    }
    if tcp_header.flags & c::ProtocolTCPFlags_PTCP_SYN != 0 {
        tcp_flags |= 0x02;
    }
    if tcp_header.flags & c::ProtocolTCPFlags_PTCP_ACK != 0 {
        tcp_flags |= 0x10;
    }
    if tcp_header.flags & c::ProtocolTCPFlags_PTCP_FIN != 0 {
        tcp_flags |= 0x01;
    }
    let window: [u8; 2] = u16::try_from(tcp_header.window).unwrap().to_be_bytes();
    let checksum: u16 = 0x0;
    let options = [0u8; 14];

    // source port: 2 bytes
    writer.write_all(&source_port)?;
    // destination port: 2 bytes
    writer.write_all(&dest_port)?;
    // sequence number: 4 bytes
    writer.write_all(&sequence)?;
    // acknowledgement number: 4 bytes
    writer.write_all(&ack)?;
    // data offset + reserved + NS: 1 byte
    // flags: 1 byte
    writer.write_all(&[header_len, tcp_flags])?;
    // window size: 2 bytes
    writer.write_all(&window)?;
    // checksum: 2 bytes
    writer.write_all(&checksum.to_be_bytes())?;
    // options: number of bytes dependent on the earlier data offset
    writer.write_all(&options)?;

    Ok(())
}

/// Helper for writing the udp bytes of the packet.
fn display_udp_bytes(packet: *const c::Packet, mut writer: impl Write) -> std::io::Result<()> {
    assert_eq!(
        unsafe { c::packet_getProtocol(packet) },
        c::_ProtocolType_PUDP
    );

    // write the UDP header

    let source_port: [u8; 2] =
        u16::from_be(unsafe { c::packet_getSourcePort(packet) }).to_be_bytes();
    let dest_port: [u8; 2] =
        u16::from_be(unsafe { c::packet_getDestinationPort(packet) }).to_be_bytes();
    let udp_len: u16 = u16::try_from(unsafe { c::packet_getPayloadLength(packet) })
        .unwrap()
        .checked_add(8)
        .unwrap();
    let checksum: u16 = 0x0;

    // source port: 2 bytes
    writer.write_all(&source_port)?;
    // destination port: 2 bytes
    writer.write_all(&dest_port)?;
    // length: 2 bytes
    writer.write_all(&udp_len.to_be_bytes())?;
    // checksum: 2 bytes
    writer.write_all(&checksum.to_be_bytes())?;

    Ok(())
}
