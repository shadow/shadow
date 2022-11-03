use std::io::Write;
use std::net::Ipv4Addr;

use crate::cshadow as c;
use crate::utility::pcap_writer::PacketDisplay;
use crate::utility::SyncSendPointer;

pub enum PacketStatus {
    RouterEnqueued = c::_PacketDeliveryStatusFlags_PDS_ROUTER_ENQUEUED as isize,
    RouterDequeued = c::_PacketDeliveryStatusFlags_PDS_ROUTER_DEQUEUED as isize,
    RouterDropped = c::_PacketDeliveryStatusFlags_PDS_ROUTER_DROPPED as isize,
    RelayCached = c::_PacketDeliveryStatusFlags_PDS_RELAY_CACHED as isize,
    RelayForwarded = c::_PacketDeliveryStatusFlags_PDS_RELAY_FORWARDED as isize,
}

pub struct Packet {
    c_ptr: SyncSendPointer<c::Packet>,
}

impl std::fmt::Debug for Packet {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Packet").finish_non_exhaustive()
    }
}

impl PartialEq for Packet {
    fn eq(&self, other: &Self) -> bool {
        self.c_ptr.ptr() == other.c_ptr.ptr()
    }
}

impl Eq for Packet {}

impl Packet {
    #[cfg(test)]
    /// Creates an empty packet for unit tests.
    pub fn mock_new() -> Packet {
        let c_ptr = unsafe { c::packet_new_inner(1, 1) };
        unsafe { c::packet_setMock(c_ptr) };
        Packet::from_raw(c_ptr)
    }

    pub fn size(&self) -> usize {
        assert!(!self.c_ptr.ptr().is_null());
        let sz = unsafe { c::packet_getTotalSize(self.c_ptr.ptr()) };
        sz as usize
    }

    fn _header_size(&self) -> usize {
        assert!(!self.c_ptr.ptr().is_null());
        let sz = unsafe { c::packet_getHeaderSize(self.c_ptr.ptr()) };
        sz as usize
    }

    fn _payload_size(&self) -> usize {
        assert!(!self.c_ptr.ptr().is_null());
        let sz = unsafe { c::packet_getPayloadSize(self.c_ptr.ptr()) };
        sz as usize
    }

    pub fn add_status(&mut self, status: PacketStatus) {
        assert!(!self.c_ptr.ptr().is_null());
        let status_flag = status as c::PacketDeliveryStatusFlags;
        unsafe { c::packet_addDeliveryStatus(self.c_ptr.ptr(), status_flag) };
    }

    pub fn dst_address(&self) -> Ipv4Addr {
        Ipv4Addr::from(u32::from_be(unsafe {
            c::packet_getDestinationIP(self.c_ptr.ptr())
        }))
    }

    /// Transfers ownership of the given c_ptr reference into a new rust packet
    /// object.
    pub fn from_raw(c_ptr: *mut c::Packet) -> Self {
        assert!(!c_ptr.is_null());
        Self {
            c_ptr: unsafe { SyncSendPointer::new(c_ptr) },
        }
    }

    /// Transfers ownership of the inner c_ptr reference to the caller while
    /// dropping the rust packet object.
    pub fn into_inner(mut self) -> *mut c::Packet {
        // We want to keep the c ref when the rust packet is dropped.
        let c_ptr = self.c_ptr.ptr();
        self.c_ptr = unsafe { SyncSendPointer::new(std::ptr::null_mut()) };
        c_ptr
    }

    pub fn borrow_inner(&self) -> *mut c::Packet {
        self.c_ptr.ptr()
    }
}

impl Drop for Packet {
    fn drop(&mut self) {
        if !self.c_ptr.ptr().is_null() {
            // If the rust packet is dropped before into_inner() is called,
            // we also drop the c packet ref to free it.
            unsafe { c::packet_unref(self.c_ptr.ptr()) }
        }
    }
}

impl PacketDisplay for *const c::Packet {
    fn display_bytes(&self, mut writer: impl Write) -> std::io::Result<()> {
        assert!(!self.is_null());

        let header_len: u16 = unsafe { c::packet_getHeaderSize(*self) }
            .try_into()
            .unwrap();
        let payload_len: u16 = unsafe { c::packet_getPayloadSize(*self) }
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

    // c::CONFIG_HEADER_SIZE is in bytes. Ultimately, TCP header len is represented in 32-bit
    // words, so we divide by 4. The left-shift of 4 is because the header len is represented
    // in the top 4 bits.
    let mut header_len: u8 = c::CONFIG_HEADER_SIZE_TCP.try_into().unwrap();
    header_len /= 4;
    header_len <<= 4;

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
    let urgent_pointer: u16 = 0x0;

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

    writer.write_all(&urgent_pointer.to_be_bytes())?;

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
    let udp_len: u16 = u16::try_from(unsafe { c::packet_getPayloadSize(packet) })
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
