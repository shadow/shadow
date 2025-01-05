use std::io::Write;
use std::mem::MaybeUninit;
use std::net::{IpAddr, SocketAddrV4};
use std::sync::Arc;

use crate::cshadow as c;
use crate::host::network::interface::FifoPacketPriority;
use crate::utility::pcap_writer::PacketDisplay;
use crate::utility::ObjectCounter;

use atomic_refcell::AtomicRefCell;
use bytes::Bytes;
use shadow_shim_helper_rs::HostId;

/// Represents different checkpoints that a packet reaches as it is being moved around in Shadow.
#[derive(Copy, Clone, Debug)]
pub enum PacketStatus {
    SndCreated,
    SndTcpEnqueueThrottled,
    SndTcpEnqueueRetransmit,
    SndTcpDequeueRetransmit,
    SndTcpRetransmitted,
    SndSocketBuffered,
    SndInterfaceSent,
    InetSent,
    InetDropped,
    RouterEnqueued,
    RouterDequeued,
    RouterDropped,
    RcvInterfaceReceived,
    RcvInterfaceDropped,
    RcvSocketProcessed,
    RcvSocketDropped,
    RcvTcpEnqueueUnordered,
    RcvSocketBuffered,
    RcvSocketDelivered,
    Destroyed,
    RelayCached,
    RelayForwarded,
}

/// Official IANA-assigned protocols supported in our packets.
#[derive(Copy, Clone, Debug, PartialEq)]
pub enum IanaProtocol {
    Tcp,
    Udp,
}

impl IanaProtocol {
    /// The IANA-assigned protocol number. This value is guaranteed to be unique for distinct
    /// protocol variants.
    pub fn number(&self) -> u8 {
        // The specific values assigned here should not be changed since they are used to produce
        // correctly formatted pcap files.
        // https://www.iana.org/assignments/protocol-numbers/protocol-numbers.xhtml
        match self {
            IanaProtocol::Tcp => 6,
            IanaProtocol::Udp => 17,
        }
    }
}

/// A packet's type of service (TOS) indicates its desired queuing priority. This may be used by
/// Shadow's interface qdisc to prioritize packets that it sends out to the network. For example,
/// in the `pfifo_fast` qdisc, packets in lower priority bands would be sent ahead of others.
// https://lartc.org/howto/lartc.qdisc.classless.html
#[derive(Copy, Clone, Debug, PartialEq)]
pub enum TypeOfService {
    /// Corresponds to "Best Effort" priority (band 1).
    Normal,
    /// Corresponds to "Interactive" priority (band 0).
    MinimizeDelay,
    /// Corresponds to "Best Effort" priority (band 1).
    MaximizeReliability,
    /// Corresponds to "Bulk" priority (band 2).
    MaximizeThroughput,
}

/// A thread-safe shared reference to a `Packet`.
///
/// Although a `PacketRc` is thread-safe, a panic may occur if multiple threads concurrently write
/// `PacketStatus` metadata on the inner `Packet`. Thus, use `new_copy_inner()` to copy the inner
/// `Packet` and send the copy to the other thread rather than sharing it between threads.
///
/// A `PacketRc` is a wrapper around a `Packet` and allows us to support reference counting on
/// `Packet`s while safely sharing them across threads. A clone of a `PacketRc`s increments the
/// reference count of the shared `Packet` while dropping the `PacketRc` decrements the reference
/// count. The shared inner `Packet` is only dropped when all `PacketRc`s referring to it are also
/// dropped (i.e., the reference count reaches zero).
///
/// The `PartialEq` implementation on `PacketRc` compares the pointer values of the wrapped
/// `Packet`.
///
/// `PacketRc` implements the `Deref` trait so that all non-associated functions on `Packet` can be
/// accessed from instances of `PacketRc` too.
#[derive(Clone, Debug)]
pub struct PacketRc {
    inner: Arc<Packet>,
}

impl PacketRc {
    /// Creates a thread-safe shared reference to a new `Packet` using the provided information.
    /// Additional references to the `Packet` can be cheaply obtained by cloning the returned
    /// `PacketRc`. The `Packet` is dropped when its last `PacketRc` reference is dropped.
    ///
    /// See `Packet::new_ipv4_tcp()` for more details.
    pub fn new_ipv4_tcp(
        header: tcp::TcpHeader,
        payload: tcp::Payload,
        priority: FifoPacketPriority,
    ) -> Self {
        Self::from(Packet::new_ipv4_tcp(header, payload, priority))
    }

    /// Creates a thread-safe shared reference to a new `Packet` using the provided information.
    /// Additional references to the `Packet` can be cheaply obtained by cloning the returned
    /// `PacketRc`. The `Packet` is dropped when its last `PacketRc` reference is dropped.
    ///
    /// See `Packet::new_ipv4_udp()` for more details.
    pub fn new_ipv4_udp(
        src: SocketAddrV4,
        dst: SocketAddrV4,
        payload: Bytes,
        priority: FifoPacketPriority,
    ) -> Self {
        Self::from(Packet::new_ipv4_udp(src, dst, payload, priority))
    }

    /// Creates a thread-safe shared reference to a new `Packet` using the provided information.
    /// Additional references to the `Packet` can be cheaply obtained by cloning the returned
    /// `PacketRc`. The `Packet` is dropped when its last `PacketRc` reference is dropped.
    ///
    /// See `Packet::new_ipv4_udp_mock()` for more details.
    #[cfg(test)]
    pub fn new_ipv4_udp_mock() -> Self {
        Self::from(Packet::new_ipv4_udp_mock())
    }

    /// Creates a thread-safe shared reference to a new `Packet` that is created by performing a
    /// copy of the provided referenced `Packet`. This function copies all packet data _except_ the
    /// packet payload, which is shared by incrementing its reference count. Additional references
    /// to the `Packet` can be cheaply obtained by cloning the returned `PacketRc`. The `Packet` is
    /// dropped when its last `PacketRc` reference is dropped.
    pub fn new_copy_inner(&self) -> Self {
        // We want a copy of the inner packet, since cloning the `Arc` wrapper would just increase
        // the reference count.
        Self::from(self.inner.as_ref().clone())
    }

    /// Transfers ownership of the given packet_ptr reference into a new `PacketRc` object. The provided
    /// pointer must have been obtained from a call to the Rust function `PacketRc::into_raw()` or
    /// the C function `packet_new_tcp()`.
    ///
    /// Use `PacketRc::into_raw()` if the returned reference is to be handed back to C code.
    ///
    /// # Panics
    ///
    /// This function panics if the supplied packet pointer is NULL.
    ///
    /// # Deprecation
    ///
    /// This function provides compatibility with the legacy C TCP stack and should be considered
    /// deprecated and removed when the legacy C TCP stack is removed.
    pub fn from_raw(packet_ptr: *mut Packet) -> Self {
        assert!(!packet_ptr.is_null());
        Self {
            inner: unsafe { Arc::from_raw(packet_ptr) },
        }
    }

    /// Transfers ownership of the inner Arc reference to the caller while dropping the `PacketRc`.
    ///
    /// To avoid a memory leak, the returned pointer must be either reconstituted into a `PacketRc`
    /// using the Rust function `PacketRc::from_raw()`, or dropped using the C function
    /// `packet_unref()`.
    ///
    /// Although this returns a `*mut Packet` pointer, the packet is not actually mutuable. We
    /// return a `*mut Packet` pointer only to avoid having to change the instnaces of the pointers
    /// to const instances in the C network code.
    ///
    /// # Deprecation
    ///
    /// This function provides compatibility with the legacy C TCP stack and should be considered
    /// deprecated and removed when the legacy C TCP stack is removed.
    pub fn into_raw(self) -> *mut Packet {
        Arc::into_raw(self.inner).cast_mut()
    }

    /// Borrow a reference that is owned by the C code so that we can temporarily operate on the
    /// packet in a Rust context. The reference count of the underlying `Packet` will be the same
    /// after the returned `PacketRc` is dropped as it was before calling this function. The C code
    /// that owns the reference is still responsible for dropping it.
    ///
    /// # Panics
    ///
    /// This function panics if the supplied packet pointer is NULL.
    ///
    /// # Deprecation
    ///
    /// This function provides compatibility with the legacy C TCP stack and should be considered
    /// deprecated and removed when the legacy C TCP stack is removed.
    fn borrow_raw(packet_ptr: *const Packet) -> Self {
        assert!(!packet_ptr.is_null());
        unsafe { Arc::increment_strong_count(packet_ptr) };
        PacketRc::from_raw(packet_ptr.cast_mut())
    }

    /// The same as `PacketRc::borrow_raw()` but this version handles a `*mut` packet pointer.
    ///
    /// # Panics
    ///
    /// This function panics if the supplied packet pointer is NULL.
    ///
    /// # Deprecation
    ///
    /// This function provides compatibility with the legacy C TCP stack and should be considered
    /// deprecated and removed when the legacy C TCP stack is removed.
    fn borrow_raw_mut(packet_ptr: *mut Packet) -> Self {
        Self::borrow_raw(packet_ptr.cast_const())
    }

    // The following are deprecated API function names that will be dropped in a future commit.

    /// Deprecated, use `new_ipv4_udp_mock()` instead.
    #[cfg(test)]
    pub fn mock_new() -> Self {
        Self::new_ipv4_udp_mock()
    }

    /// Deprecated, use `new_copy_inner()` instead.
    pub fn copy(&self) -> Self {
        self.new_copy_inner()
    }

    /// Deprecated, use `len()` instead.
    pub fn total_size(&self) -> usize {
        self.len()
    }
}

impl PartialEq for PacketRc {
    /// Compares the pointer rather than the value of the inner `Arc<Packet>` object.
    fn eq(&self, other: &Self) -> bool {
        // Two `PacketRc`s are considered equal if they point to the same shared `Packet`.
        Arc::ptr_eq(&self.inner, &other.inner)
    }
}

impl Eq for PacketRc {}

impl From<Packet> for PacketRc {
    fn from(packet: Packet) -> Self {
        Self {
            inner: Arc::new(packet),
        }
    }
}

// Non-associated functions on `Packet` can be accessed from instances of `PacketRc`.
impl std::ops::Deref for PacketRc {
    type Target = Packet;
    fn deref(&self) -> &Self::Target {
        self.inner.as_ref()
    }
}

/// Holds networking information and payload data that assists in sending managed-process bytes
/// between different sockets and hosts over the simulation network.
///
/// The `Packet` is designed to separate the IP layer header from the transport layer data, to make
/// it easier to support new transport protocols in the future. Thus, the `Packet` has a `Header` to
/// store IP header information, and a `Data` to store transport-specific data (e.g., A TCP header
/// and payload for TCP, or a UDP header and payload for UDP).
///
/// The `Packet` is designed to be read-only after creation to simplify implementation and to reduce
/// the need for mutable borrows.
///
/// # Deprecation
///
/// As an exception to the read-only design, some legacy TCP data is mutable to support the legacy C
/// TCP stack. When the legacy TCP stack and the `Packet`'s C interface is removed, mutability will
/// also be removed.
#[derive(Clone, Debug)]
pub struct Packet {
    header: Header,
    data: Data,
    meta: Metadata,
    _counter: ObjectCounter,
}

impl Packet {
    /// Creates a new `Packet` with the pre-constructed objects.
    ///
    /// This function is private because we do not expose the inner `Packet` structures.
    fn new(header: Header, data: Data, meta: Metadata) -> Self {
        Self {
            header,
            data,
            meta,
            _counter: ObjectCounter::new("Packet"),
        }
    }

    /// Creates a new IPv4 TCP packet using the provided data.
    pub fn new_ipv4_tcp(
        header: tcp::TcpHeader,
        payload: tcp::Payload,
        priority: FifoPacketPriority,
    ) -> Self {
        let hdr = header;
        let header = Header::new(IpAddr::V4(hdr.ip.src), IpAddr::V4(hdr.ip.dst));

        let tcp_packet = TcpData::new(TcpHeader::from(hdr), payload.0);
        let data = Data::from(tcp_packet);

        let meta = Metadata::new(priority);

        Self::new(header, data, meta)
    }

    /// Creates a new IPv4 UDP packet using the provided data.
    pub fn new_ipv4_udp(
        src: SocketAddrV4,
        dst: SocketAddrV4,
        payload: Bytes,
        priority: FifoPacketPriority,
    ) -> Self {
        let header = Header::new(IpAddr::V4(*src.ip()), IpAddr::V4(*dst.ip()));

        let udp_header = UdpHeader::new(src.port(), dst.port());
        let udp_packet = UdpData::new(udp_header, payload);
        let data = Data::from(udp_packet);

        let meta = Metadata::new(priority);

        Self::new(header, data, meta)
    }

    /// Creates a new IPv4 UDP packet for unit tests with unspecified source and destination
    /// addresses and header information and a payload of 1_000 bytes.
    #[cfg(test)]
    pub fn new_ipv4_udp_mock() -> Self {
        let unspec = SocketAddrV4::new(std::net::Ipv4Addr::UNSPECIFIED, 0);
        // Some of our tests require packets with payloads.
        Self::new_ipv4_udp(unspec, unspec, Bytes::copy_from_slice(&[0; 1000]), 0)
    }

    /// If the packet is an IPv4 TCP packet, returns a copy of the TCP header in a format defined by
    /// the Rust TCP stack. Otherwise, returns `None`.
    ///
    /// Panics
    ///
    /// This function panics if the packet was created with `packet_new_tcp()` in the legacy C API.
    pub fn ipv4_tcp_header(&self) -> Option<tcp::TcpHeader> {
        let hdr = &self.header;

        let IpAddr::V4(src) = hdr.src else {
            return None;
        };
        let IpAddr::V4(dst) = hdr.dst else {
            return None;
        };

        let tcp_hdr = match &self.data {
            // The legacy TCP header is obtained with `packet_getTCPHeader()` in the legacy C API.
            Data::LegacyTcp(_) => unimplemented!(),
            Data::Tcp(tcp) => tcp.header.clone(),
            Data::Udp(_) => return None,
        };

        Some(tcp::TcpHeader {
            ip: tcp::Ipv4Header { src, dst },
            flags: tcp_hdr.flags,
            src_port: tcp_hdr.src_port,
            dst_port: tcp_hdr.dst_port,
            seq: tcp_hdr.sequence,
            ack: tcp_hdr.acknowledgement,
            window_size: tcp_hdr.window_size,
            selective_acks: tcp_hdr.selective_acks.map(|x| x.into()),
            window_scale: tcp_hdr.window_scale,
            timestamp: tcp_hdr.timestamp,
            timestamp_echo: tcp_hdr.timestamp_echo,
        })
    }

    /// Returns the packet's payload that was provided at packet creation time. This function
    /// allocates a new `Vec`, but is zero-copy with respect to the payload `Bytes`.
    ///
    /// This function may return a non-empty vector of zero-length `Bytes` object(s) if zero-length
    /// `Bytes` object(s) were provided at creation time. Thus, it may be helpful to check if the
    /// packet has useful payload using `payload_len()` before calling this function.
    //
    // TODO: after we remove the legacy C TCP stack, then all packet creation functions will require
    // a pre-set payload, and we do not add more bytes later. Thus, we can then store them as a
    // slice of `Bytes` rather than a `Vec<Bytes>`.
    pub fn payload(&self) -> Vec<Bytes> {
        match &self.data {
            Data::LegacyTcp(tcp_rc) => tcp_rc.borrow().payload.clone(),
            Data::Tcp(tcp) => tcp.payload.clone(),
            Data::Udp(udp) => vec![udp.payload.clone()],
        }
    }

    /// Returns the total simulated length of the packet, which is the sum of the emulated IP and
    /// transport header lengths and the payload length.
    #[allow(clippy::len_without_is_empty)]
    pub fn len(&self) -> usize {
        self.header.len().checked_add(self.data.len()).unwrap()
    }

    /// Returns the total number of payload bytes stored in the packet, which excludes IP and
    /// transport header lengths.
    pub fn payload_len(&self) -> usize {
        self.data.payload_len()
    }

    /// Appends the provided packet status to the list of the packet's status checkpoints.
    ///
    /// This function has no effect unless `log::Level::Trace` is enabled.
    pub fn add_status(&self, status: PacketStatus) {
        if log::log_enabled!(log::Level::Trace) {
            if let Some(vec) = self.meta.statuses.as_ref() {
                vec.borrow_mut().push(status);
            }
            log::trace!("[{status:?}] {self:?}");
        }
    }

    /// Returns the packet's IPv4 source address and source port.
    ///
    /// Panics
    ///
    /// This function panics if the source address is not an IPv4 address.
    pub fn src_address(&self) -> SocketAddrV4 {
        let IpAddr::V4(addr) = self.header.src else {
            unimplemented!()
        };

        let port = match &self.data {
            Data::LegacyTcp(tcp_rc) => tcp_rc.borrow().header.src_port,
            Data::Tcp(tcp) => tcp.header.src_port,
            Data::Udp(udp) => udp.header.src_port,
        };

        SocketAddrV4::new(addr, port)
    }

    /// Returns the packet's IPv4 destination address and destination port.
    ///
    /// Panics
    ///
    /// This function panics if the destination address is not an IPv4 address.
    pub fn dst_address(&self) -> SocketAddrV4 {
        let IpAddr::V4(addr) = self.header.dst else {
            unimplemented!()
        };

        let port = match &self.data {
            Data::LegacyTcp(tcp_rc) => tcp_rc.borrow().header.dst_port,
            Data::Tcp(tcp) => tcp.header.dst_port,
            Data::Udp(udp) => udp.header.dst_port,
        };

        SocketAddrV4::new(addr, port)
    }

    /// Returns the priority set at packet creation time.
    pub fn priority(&self) -> FifoPacketPriority {
        self.meta.priority
    }

    /// Deprecated: use `Packet::legacy_protocol()` or `Packet::iana_protocol()`.
    pub fn protocol(&self) -> c::ProtocolType {
        self.legacy_protocol()
    }

    /// Returns the packet's legacy protocol type.
    pub fn legacy_protocol(&self) -> c::ProtocolType {
        match self.data.iana_protocol() {
            IanaProtocol::Tcp => c::_ProtocolType_PTCP,
            IanaProtocol::Udp => c::_ProtocolType_PUDP,
        }
    }

    /// Returns the packet's iana-assigned protocol type.
    pub fn iana_protocol(&self) -> IanaProtocol {
        self.data.iana_protocol()
    }
}

/// Stores the IP header information.
#[derive(Clone, Debug)]
struct Header {
    src: IpAddr,
    dst: IpAddr,
    _tos: TypeOfService,
}

impl Header {
    pub fn new(src: IpAddr, dst: IpAddr) -> Self {
        // TODO: make TOS configurable, then the network queue can do pfifo properly.
        Self {
            src,
            dst,
            _tos: TypeOfService::Normal,
        }
    }

    pub fn len(&self) -> usize {
        match &self.dst {
            // 20 bytes without options: https://en.wikipedia.org/wiki/IPv4
            IpAddr::V4(_) => 20usize,
            // 40 bytes: https://en.wikipedia.org/wiki/IPv6
            IpAddr::V6(_) => 40usize,
        }
    }
}

/// Stores the data part of an IP packet. The data segment varies depending on the protocol being
/// carried by the packet.
#[derive(Clone, Debug)]
enum Data {
    // We need a mutable TCP packet to support the legacy TCP stack, which writes some header and
    // payload data after the packet was created.
    LegacyTcp(AtomicRefCell<TcpData>),
    Tcp(TcpData),
    Udp(UdpData),
}

impl Data {
    pub fn len(&self) -> usize {
        match self {
            Data::LegacyTcp(tcp_ref) => tcp_ref.borrow().len(),
            Data::Tcp(tcp) => tcp.len(),
            Data::Udp(udp) => udp.len(),
        }
    }

    pub fn payload_len(&self) -> usize {
        match self {
            Data::LegacyTcp(tcp_ref) => tcp_ref.borrow().payload_len(),
            Data::Tcp(tcp) => tcp.payload_len(),
            Data::Udp(udp) => udp.payload_len(),
        }
    }

    pub fn iana_protocol(&self) -> IanaProtocol {
        match self {
            Data::LegacyTcp(tcp_ref) => tcp_ref.borrow().iana_protocol(),
            Data::Tcp(tcp) => tcp.iana_protocol(),
            Data::Udp(udp) => udp.iana_protocol(),
        }
    }
}

impl From<UdpData> for Data {
    fn from(packet: UdpData) -> Self {
        Self::Udp(packet)
    }
}

impl From<TcpData> for Data {
    fn from(packet: TcpData) -> Self {
        Self::Tcp(packet)
    }
}

/// The data portion of an IP packet that contains TCP protocol information, including a TCP header
/// and payload.
#[derive(Clone, Debug)]
struct TcpData {
    // We don't use `tcp::TcpHeader` here because it includes non-transport layer data (IP headers).
    header: TcpHeader,
    // A vector allows us to store the payload in multiple separate chunks.
    // Consider using `SmallVec` instead. I'm not sure what the improvement would be, and it if is
    // worth adding in the extra dependency on the `SmallVec` code here.
    payload: Vec<Bytes>,
}

impl TcpData {
    pub fn new(header: TcpHeader, payload: Vec<Bytes>) -> Self {
        Self { header, payload }
    }

    pub fn len(&self) -> usize {
        self.header.len().checked_add(self.payload_len()).unwrap()
    }

    pub fn payload_len(&self) -> usize {
        self.payload
            .iter()
            // `fold` rather than `sum` so that we always panic on overflow
            .fold(0usize, |acc, x| acc.checked_add(x.len()).unwrap())
    }

    pub fn iana_protocol(&self) -> IanaProtocol {
        IanaProtocol::Tcp
    }
}

/// A TCP header contains protocol information including ports, sequence numbers, and other
/// protocol-specific information.
#[derive(Clone, Debug, PartialEq)]
struct TcpHeader {
    src_port: u16,
    dst_port: u16,
    flags: tcp::TcpFlags,
    sequence: u32,
    acknowledgement: u32,
    window_size: u16,
    selective_acks: Option<TcpSelectiveAcks>,
    window_scale: Option<u8>,
    timestamp: Option<u32>,
    timestamp_echo: Option<u32>,
}

impl TcpHeader {
    // Unused now, but want to allow its use in the future.
    #[allow(dead_code)]
    pub fn new(
        src_port: u16,
        dst_port: u16,
        flags: tcp::TcpFlags,
        sequence: u32,
        acknowledgement: u32,
        window_size: u16,
        selective_acks: Option<TcpSelectiveAcks>,
        window_scale: Option<u8>,
        timestamp: Option<u32>,
        timestamp_echo: Option<u32>,
    ) -> Self {
        Self {
            src_port,
            dst_port,
            sequence,
            flags,
            acknowledgement,
            window_size,
            selective_acks,
            window_scale,
            timestamp,
            timestamp_echo,
        }
    }

    // This function must be kept in sync with `display_bytes()`.
    // TODO: is there a better way of keeping this logic in sync with similar logic in
    // `display_bytes()`?
    pub fn len(&self) -> usize {
        // Base is 20 bytes: https://en.wikipedia.org/wiki/Transmission_Control_Protocol
        let mut len = 20usize;

        // TCP options use additional bytes.
        if self.window_scale.is_some() {
            // Window scale option is 3 bytes.
            len += 3;
        }

        // TODO: should we consider the length of the selective acks, if any exist?

        // Add padding bytes if needed.
        if (len % 4) != 0 {
            len += 4 - (len % 4);
        }

        len
    }
}

impl From<tcp::TcpHeader> for TcpHeader {
    fn from(hdr: tcp::TcpHeader) -> Self {
        TcpHeader {
            src_port: hdr.src_port,
            dst_port: hdr.dst_port,
            flags: hdr.flags,
            sequence: hdr.seq,
            acknowledgement: hdr.ack,
            window_size: hdr.window_size,
            selective_acks: hdr.selective_acks.map(|x| x.into()),
            window_scale: hdr.window_scale,
            timestamp: hdr.timestamp,
            timestamp_echo: hdr.timestamp_echo,
        }
    }
}

#[derive(Clone, Copy, Debug, Default)]
struct TcpSelectiveAcks {
    len: u8,
    // A TCP packet can only hold at most 4 selective ack blocks.
    ranges: [(u32, u32); 4],
}

impl From<tcp::util::SmallArrayBackedSlice<4, (u32, u32)>> for TcpSelectiveAcks {
    fn from(array: tcp::util::SmallArrayBackedSlice<4, (u32, u32)>) -> Self {
        let mut selective_acks = Self::default();

        for (i, sack) in array.as_ref().iter().enumerate() {
            selective_acks.ranges[i] = (sack.0, sack.1);
            selective_acks.len += 1;
            if selective_acks.len >= 4 {
                break;
            }
        }

        selective_acks
    }
}

impl From<TcpSelectiveAcks> for tcp::util::SmallArrayBackedSlice<4, (u32, u32)> {
    fn from(selective_acks: TcpSelectiveAcks) -> Self {
        assert!(selective_acks.len <= 4);
        Self::new(&selective_acks.ranges[0..(selective_acks.len as usize)]).unwrap()
    }
}

impl PartialEq for TcpSelectiveAcks {
    fn eq(&self, other: &Self) -> bool {
        if self.len != other.len {
            return false;
        }
        for i in 0..self.len as usize {
            if self.ranges[i] != other.ranges[i] {
                return false;
            }
        }
        true
    }
}

/// The data portion of an IP packet that contains UDP protocol information, including a UDP header
/// and payload.
#[derive(Clone, Debug)]
struct UdpData {
    header: UdpHeader,
    payload: Bytes,
}

impl UdpData {
    pub fn new(header: UdpHeader, payload: Bytes) -> Self {
        Self { header, payload }
    }

    pub fn len(&self) -> usize {
        self.header.len().checked_add(self.payload_len()).unwrap()
    }

    pub fn payload_len(&self) -> usize {
        self.payload.len()
    }

    pub fn iana_protocol(&self) -> IanaProtocol {
        IanaProtocol::Udp
    }
}

/// A UDP header consists of source and destination port information.
#[derive(Clone, Debug, PartialEq)]
struct UdpHeader {
    src_port: u16,
    dst_port: u16,
}

impl UdpHeader {
    pub fn new(src_port: u16, dst_port: u16) -> Self {
        Self { src_port, dst_port }
    }

    pub fn len(&self) -> usize {
        // 8 bytes: https://en.wikipedia.org/wiki/User_Datagram_Protocol
        8usize
    }
}

#[derive(Clone, Debug)]
struct Metadata {
    /// Tracks application priority so we flush packets from the interface to the wire in the order
    /// intended by the application. This is used in the default FIFO network interface scheduling
    /// discipline. Smaller values have greater priority.
    // TODO: this can be removed once we support the TOS field in the `Header` struct.
    priority: FifoPacketPriority,
    /// Tracks the sequence of operations that happen on this packet as is transits Shadow's
    /// network.
    statuses: Option<AtomicRefCell<Vec<PacketStatus>>>,
    /// The id of the host that created the packet.
    ///
    /// # Deprecation
    ///
    /// This is currently set by the legacy C TCP stack to help uniquely identify a packet while
    /// debugging. It can be removed when the legacy TCP stack and our C packet API is removed.
    _host_id: Option<HostId>,
    /// The id of the packet.
    ///
    /// # Deprecation
    ///
    /// This is currently set by the legacy C TCP stack to help uniquely identify a packet while
    /// debugging. It can be removed when the legacy TCP stack and our C packet API is removed.
    _packet_id: Option<u64>,
}

impl Metadata {
    pub fn new(priority: FifoPacketPriority) -> Self {
        Self {
            priority,
            _host_id: None,
            _packet_id: None,
            // For efficiency, we only store statuses when tracing is enabled because they are
            // logged at trace level and won't be displayed at other levels anyway.
            statuses: log::log_enabled!(log::Level::Trace).then(AtomicRefCell::default),
        }
    }

    /// Supports creation of legacy TCP stack metadata.
    ///
    /// # Deprecation
    ///
    /// This is currently used by the legacy C TCP stack and can be removed when the legacy TCP
    /// stack and our C packet API is removed.
    fn new_legacy(priority: FifoPacketPriority, host_id: HostId, packet_id: u64) -> Self {
        Self {
            priority,
            _host_id: Some(host_id),
            _packet_id: Some(packet_id),
            // For efficiency, we only store statuses when tracing is enabled because they are
            // logged at trace level and won't be displayed at other levels anyway.
            statuses: log::log_enabled!(log::Level::Trace).then(AtomicRefCell::default),
        }
    }
}

impl PacketDisplay for Packet {
    fn display_bytes(&self, mut writer: impl Write) -> std::io::Result<()> {
        // write the IP header

        let version_and_header_length: u8 = 0x45;
        let fields: u8 = 0x0;
        let total_length: u16 = self.len().try_into().unwrap();
        let identification: u16 = 0x0;
        let flags_and_fragment: u16 = 0x4000;
        let time_to_live: u8 = 64;
        let iana_protocol: u8 = self.data.iana_protocol().number();
        let header_checksum: u16 = 0x0;
        let source_ip: [u8; 4] = self.src_address().ip().to_bits().to_be_bytes();
        let dest_ip: [u8; 4] = self.dst_address().ip().to_bits().to_be_bytes();

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

        match &self.data {
            Data::LegacyTcp(tcp_ref) => write_tcpdata_bytes(&tcp_ref.borrow(), writer),
            Data::Tcp(tcp) => write_tcpdata_bytes(tcp, writer),
            Data::Udp(udp) => write_udpdata_bytes(udp, writer),
        }?;

        Ok(())
    }
}

fn write_tcpdata_bytes(data: &TcpData, mut writer: impl Write) -> std::io::Result<()> {
    // process TCP options

    let tcp_hdr = &data.header;

    // options can be a max of 40 bytes
    let mut options = [0u8; 40];
    let mut options_len = 0;

    if let Some(window_scale) = tcp_hdr.window_scale {
        // option-kind = 3, option-len = 3, option-data = window-scale
        options[options_len..][..3].copy_from_slice(&[3, 3, window_scale]);
        options_len += 3;
    }

    // TODO: do we want to include selective acks or timestamp options?

    if options_len % 4 != 0 {
        // need to add padding (our options array was already initialized with zeroes)
        let padding = 4 - (options_len % 4);
        options_len += padding;
    }

    let options = &options[..options_len];

    // process the TCP header

    let mut tcp_flags = tcp_hdr.flags;

    // Header length in bytes. This must be kept in sync with `header().len()`.
    let header_len: usize = 20usize.checked_add(options.len()).unwrap();
    assert_eq!(header_len, tcp_hdr.len());

    // Ultimately, TCP header len is represented in 32-bit words, so we divide by 4. The
    // left-shift of 4 is because the header len is represented in the top 4 bits.
    let mut header_len = u8::try_from(header_len).unwrap();
    header_len /= 4;
    header_len <<= 4;

    // Filter these two bits because we stuff non-TCP legacy flags here on legacy packets.
    // TODO: remove this filter when the C TCP stack and Data::LegacyTCP packets are removed.
    tcp_flags.remove(tcp::TcpFlags::ECE);
    tcp_flags.remove(tcp::TcpFlags::CWR);

    // write the header data

    // source port: 2 bytes
    writer.write_all(&tcp_hdr.src_port.to_be_bytes())?;
    // destination port: 2 bytes
    writer.write_all(&tcp_hdr.dst_port.to_be_bytes())?;
    // sequence number: 4 bytes
    writer.write_all(&tcp_hdr.sequence.to_be_bytes())?;
    // acknowledgement number: 4 bytes
    writer.write_all(&tcp_hdr.acknowledgement.to_be_bytes())?;
    // data offset + reserved + NS: 1 byte
    // flags: 1 byte
    writer.write_all(&[header_len, tcp_flags.bits()])?;
    // window size: 2 bytes
    writer.write_all(&tcp_hdr.window_size.to_be_bytes())?;
    // checksum: 2 bytes
    let checksum: u16 = 0u16;
    writer.write_all(&checksum.to_be_bytes())?;
    // urgent: 2 bytes
    let urgent_pointer: u16 = 0u16;
    writer.write_all(&urgent_pointer.to_be_bytes())?;

    writer.write_all(options)?;

    // write payload data

    for bytes in &data.payload {
        writer.write_all(bytes)?;
    }

    Ok(())
}

fn write_udpdata_bytes(data: &UdpData, mut writer: impl Write) -> std::io::Result<()> {
    // write the UDP header

    // source port: 2 bytes
    writer.write_all(&data.header.src_port.to_be_bytes())?;
    // destination port: 2 bytes
    writer.write_all(&data.header.dst_port.to_be_bytes())?;
    // length: 2 bytes
    let udp_len: u16 = u16::try_from(data.len()).unwrap();
    writer.write_all(&udp_len.to_be_bytes())?;
    // checksum: 2 bytes
    let checksum: u16 = 0x0;
    writer.write_all(&checksum.to_be_bytes())?;

    // write payload data

    writer.write_all(&data.payload)?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use std::net::Ipv4Addr;

    use super::*;

    #[test]
    fn ipv4_udp() {
        let src = SocketAddrV4::new(Ipv4Addr::new(192, 168, 1, 1), 10_000);
        let dst = SocketAddrV4::new(Ipv4Addr::new(192, 168, 1, 2), 80);
        let payload = Bytes::from_static(b"Hello World!");
        let priority = 123;

        let packetrc = PacketRc::new_ipv4_udp(src, dst, payload.clone(), priority);

        assert_eq!(src, packetrc.src_ipv4_address());
        assert_eq!(dst, packetrc.dst_ipv4_address());
        assert_eq!(priority, packetrc.priority());
        assert_eq!(IanaProtocol::Udp, packetrc.iana_protocol());

        assert_eq!(payload.len(), packetrc.payload_len());
        let chunks = packetrc.payload();
        assert_eq!(1, chunks.len());
        assert_eq!(payload, chunks.first().unwrap());
    }

    #[test]
    fn ipv4_udp_empty() {
        let src = SocketAddrV4::new(Ipv4Addr::new(192, 168, 1, 1), 10_000);
        let dst = SocketAddrV4::new(Ipv4Addr::new(192, 168, 1, 2), 80);
        let priority = 123;

        // Bytes object with no data inside.

        let payload = Bytes::new();
        let packetrc = PacketRc::new_ipv4_udp(src, dst, payload.clone(), priority);

        assert_eq!(0, packetrc.payload_len());
        assert_eq!(payload.len(), packetrc.payload_len());
        let chunks = packetrc.payload();
        assert_eq!(1, chunks.len());
        assert_eq!(0, chunks.first().unwrap().len());
    }

    fn make_tcp_header(src: SocketAddrV4, dst: SocketAddrV4) -> tcp::TcpHeader {
        // Selective acks with two ranges: [1-3) and [5-6).
        let sel_acks =
            tcp::util::SmallArrayBackedSlice::<4, (u32, u32)>::new(&[(1, 3), (5, 6)]).unwrap();

        tcp::TcpHeader {
            ip: tcp::Ipv4Header {
                src: *src.ip(),
                dst: *dst.ip(),
            },
            flags: tcp::TcpFlags::SYN,
            src_port: src.port(),
            dst_port: dst.port(),
            seq: 10,
            ack: 3,
            window_size: 25,
            selective_acks: Some(sel_acks),
            window_scale: Some(2),
            timestamp: Some(123456),
            timestamp_echo: Some(123450),
        }
    }

    #[test]
    fn ipv4_tcp() {
        let src = SocketAddrV4::new(Ipv4Addr::new(192, 168, 1, 1), 10_000);
        let dst = SocketAddrV4::new(Ipv4Addr::new(192, 168, 1, 2), 80);
        let priority = 123;
        let tcp_hdr = make_tcp_header(src, dst);
        let payload = tcp::Payload(vec![
            Bytes::from_static(b"Hello"),
            Bytes::from_static(b" World!"),
        ]);

        let packetrc = PacketRc::new_ipv4_tcp(tcp_hdr, payload.clone(), priority);

        assert_eq!(src, packetrc.src_ipv4_address());
        assert_eq!(dst, packetrc.dst_ipv4_address());
        assert_eq!(priority, packetrc.priority());
        assert_eq!(IanaProtocol::Tcp, packetrc.iana_protocol());
        assert_eq!(
            TcpHeader::from(tcp_hdr),
            TcpHeader::from(packetrc.ipv4_tcp_header().unwrap())
        );

        assert_eq!(payload.len() as usize, packetrc.payload_len());
        let chunks = packetrc.payload();
        assert_eq!(2, chunks.len());

        for (i, bytes) in chunks.iter().enumerate() {
            assert_eq!(payload.0[i], bytes);
        }
    }

    #[test]
    fn ipv4_tcp_empty() {
        let src = SocketAddrV4::new(Ipv4Addr::new(192, 168, 1, 1), 10_000);
        let dst = SocketAddrV4::new(Ipv4Addr::new(192, 168, 1, 2), 80);
        let priority = 123;
        let tcp_hdr = make_tcp_header(src, dst);

        // Empty chunks vec.

        let payload = tcp::Payload(vec![]);
        let packetrc = PacketRc::new_ipv4_tcp(tcp_hdr, payload, priority);

        assert_eq!(0, packetrc.payload_len());
        let chunks = packetrc.payload();
        assert_eq!(0, chunks.len());

        // Non-empty chunks vec with empty bytes objects.

        let payload = tcp::Payload(vec![Bytes::new(), Bytes::new()]);
        let packetrc = PacketRc::new_ipv4_tcp(tcp_hdr, payload, priority);

        assert_eq!(0, packetrc.payload_len());
        let chunks = packetrc.payload();
        assert_eq!(2, chunks.len());
        assert_eq!(0, chunks.first().unwrap().len());
        assert_eq!(0, chunks.last().unwrap().len());
    }
}

/// This module provides a C API to create and operate on packets.
///
/// # Deprecation
///
/// This module provides compatibility with the legacy C TCP stack and should be considered
/// deprecated and removed when the legacy C TCP stack is removed.
mod export {
    use std::cmp::Ordering;
    use std::io::Write;

    use shadow_shim_helper_rs::simulation_time::SimulationTime;
    use shadow_shim_helper_rs::syscall_types::UntypedForeignPtr;

    use crate::cshadow as c;
    use crate::host::memory_manager::MemoryManager;
    use crate::host::syscall::types::ForeignArrayPtr;

    use super::*;

    #[no_mangle]
    pub extern "C-unwind" fn packet_new_tcp(
        host_id: HostId,
        packet_id: u64,
        flags: c::ProtocolTCPFlags,
        src_ip: libc::in_addr_t,
        src_port: libc::in_port_t,
        dst_ip: libc::in_addr_t,
        dst_port: libc::in_port_t,
        seq: u32,
        priority: u64,
    ) -> *mut Packet {
        // First construct the internet-level header.
        let header = Header::new(
            IpAddr::V4(u32::from_be(src_ip).into()),
            IpAddr::V4(u32::from_be(dst_ip).into()),
        );

        // The transport header and payload are defined within the data field.
        let data = Data::LegacyTcp(AtomicRefCell::new(TcpData {
            header: TcpHeader {
                src_port: u16::from_be(src_port),
                dst_port: u16::from_be(dst_port),
                flags: legacy_flags_to_tcp_flags(flags),
                sequence: seq,
                acknowledgement: 0,
                window_size: 0,
                selective_acks: None,
                window_scale: None,
                timestamp: None,
                timestamp_echo: None,
            },
            payload: vec![],
        }));

        let meta = Metadata::new_legacy(priority, host_id, packet_id);
        let packet = Packet::new(header, data, meta);

        // Move ownership of the inner Arc reference to C (for now).
        PacketRc::from(packet).into_raw()
    }

    #[no_mangle]
    pub extern "C-unwind" fn packet_ref(packet_ptr: *mut Packet) {
        assert!(!packet_ptr.is_null());
        unsafe { Arc::increment_strong_count(packet_ptr) };
    }

    #[no_mangle]
    pub extern "C-unwind" fn packet_unref(packet_ptr: *mut Packet) {
        assert!(!packet_ptr.is_null());
        unsafe { Arc::decrement_strong_count(packet_ptr) };
    }

    #[no_mangle]
    pub extern "C-unwind" fn packet_updateTCP(
        packet_ptr: *mut Packet,
        ack: libc::c_uint,
        sel_acks: c::PacketSelectiveAcks,
        window_size: libc::c_uint,
        window_scale: libc::c_uchar,
        window_scale_set: bool,
        ts_val: c::CSimulationTime,
        ts_echo: c::CSimulationTime,
    ) {
        let packet = PacketRc::borrow_raw_mut(packet_ptr);

        let Data::LegacyTcp(tcp) = &packet.data else {
            unimplemented!()
        };

        let mut tcp = tcp.borrow_mut();

        tcp.header.acknowledgement = ack;
        tcp.header.selective_acks = Some(TcpSelectiveAcks::from(sel_acks));

        tcp.header.window_size = u16::try_from(window_size).unwrap_or(u16::MAX);
        if window_scale_set {
            tcp.header.window_scale = Some(window_scale);
        } else {
            tcp.header.window_scale = None;
        }

        // The TCP header supports 32-bit timestamps; since these are used for network latency
        // calculations, which are usually on the order of milliseconds, we trade off some precision
        // here for supporting longer simulations.
        tcp.header.timestamp = from_legacy_timestamp(ts_val);
        tcp.header.timestamp_echo = from_legacy_timestamp(ts_echo);
    }

    #[no_mangle]
    pub extern "C-unwind" fn packet_getTCPHeader(packet_ptr: *const Packet) -> c::PacketTCPHeader {
        let packet = PacketRc::borrow_raw(packet_ptr);

        let IpAddr::V4(src_ip) = packet.header.src else {
            unimplemented!()
        };
        let IpAddr::V4(dst_ip) = packet.header.dst else {
            unimplemented!()
        };
        let Data::LegacyTcp(tcp_rc) = &packet.data else {
            unimplemented!()
        };
        let tcp = tcp_rc.borrow();

        let mut c_hdr: c::PacketTCPHeader = unsafe { MaybeUninit::zeroed().assume_init() };

        c_hdr.flags = tcp_flags_to_legacy_flags(tcp.header.flags);
        c_hdr.sourceIP = u32::from(src_ip).to_be();
        c_hdr.sourcePort = tcp.header.src_port.to_be();
        c_hdr.destinationIP = u32::from(dst_ip).to_be();
        c_hdr.destinationPort = tcp.header.dst_port.to_be();
        c_hdr.sequence = tcp.header.sequence;
        c_hdr.acknowledgment = tcp.header.acknowledgement;
        c_hdr.selectiveACKs = to_legacy_sel_acks(tcp.header.selective_acks);
        c_hdr.window = u32::from(tcp.header.window_size);
        if let Some(scale) = tcp.header.window_scale {
            c_hdr.windowScale = scale;
            c_hdr.windowScaleSet = true;
        }
        c_hdr.timestampValue = to_legacy_timestamp(tcp.header.timestamp);
        c_hdr.timestampEcho = to_legacy_timestamp(tcp.header.timestamp_echo);

        c_hdr
    }

    #[no_mangle]
    pub extern "C-unwind" fn packet_appendPayloadWithMemoryManager(
        packet_ptr: *mut Packet,
        src: UntypedForeignPtr,
        src_len: u64,
        mem: *const MemoryManager,
    ) {
        // Read data from the managed process into the packet's payload buffer.
        let packet = PacketRc::borrow_raw_mut(packet_ptr);
        let mem = unsafe { mem.as_ref() }.unwrap();

        let Data::LegacyTcp(tcp) = &packet.data else {
            unimplemented!()
        };

        let len = usize::try_from(src_len).unwrap();
        let src = ForeignArrayPtr::new(src.cast::<MaybeUninit<u8>>(), len);

        // We want the dst buf on the heap so we don't have to copy it later. Uses
        // `new_uninit_slice` to avoid zero-filling the bytes in dst buffer that we are going to
        // copy over with the memory manager below anyway.
        let mut dst = Box::<[u8]>::new_uninit_slice(len);

        log::trace!(
            "Requested to read payload of len {len} from the managed process into the packet's \
            payload buffer",
        );

        // Copy from the managed process directly into the heap buffer. We want this to be the only
        // copy of the payload that occurs until the receiver host later copies it into their
        // managed process.
        if let Err(e) = mem.copy_from_ptr(&mut dst[..], src) {
            // Panic because the packet data will be corrupt (not what the application wrote).
            panic!(
                "Couldn't read managed process memory at {:?} into packet payload at {:?}: {:?}",
                src, dst, e
            );
        }

        let dst = unsafe { dst.assume_init() };

        log::trace!(
            "We read {} bytes from the managed process into the packet's payload",
            dst.len()
        );

        // Move the payload into the packet. Use Bytes (not BytesMut) to avoid copying the payload.
        tcp.borrow_mut().payload.push(Bytes::from(dst));
    }

    #[no_mangle]
    pub extern "C-unwind" fn packet_copyPayloadWithMemoryManager(
        packet_ptr: *const Packet,
        payload_offset: u64,
        dst: UntypedForeignPtr,
        dst_len: u64,
        mem: *mut MemoryManager,
    ) -> i64 {
        // Write the payload data from the packet into the managed process memory.
        let packet = PacketRc::borrow_raw(packet_ptr);
        let mem = unsafe { mem.as_mut() }.unwrap();

        let Data::LegacyTcp(tcp) = &packet.data else {
            unimplemented!()
        };

        if dst_len == 0 {
            return 0;
        }

        log::trace!(
            "Requested to write payload of len {} from offset {payload_offset} into managed \
            process buffer of len {dst_len}",
            packet.payload_len()
        );

        let dst_len = usize::try_from(dst_len).unwrap_or(usize::MAX);
        let dst = ForeignArrayPtr::new(dst.cast::<u8>(), dst_len);

        let mut dst_writer = mem.writer(dst);
        let mut dst_space = dst_len;
        let mut src_offset = usize::try_from(payload_offset).unwrap_or(usize::MAX);

        for bytes in &tcp.borrow().payload {
            // This also skips over empty Bytes objects.
            if src_offset >= bytes.len() {
                src_offset = src_offset.saturating_sub(bytes.len());
                continue;
            }

            let start = src_offset;
            let len = bytes.len().saturating_sub(start).min(dst_space);
            let end = start + len;
            assert!(start <= end);

            if len == 0 {
                break;
            }

            log::trace!("Writing {len} bytes into managed process");

            if let Err(e) = dst_writer.write_all(&bytes[start..end]) {
                log::warn!(
                    "Couldn't write managed process memory at {:?} from packet payload: {:?}",
                    dst,
                    e
                );
                // TODO: can we get memmgr errno here like we can with `copy_from_ptr()`?
                return linux_api::errno::Errno::EFAULT.to_negated_i64();
            }

            dst_space = dst_space.saturating_sub(len);
            src_offset = 0;
        }

        let tot_written = dst_len.saturating_sub(dst_space);

        if tot_written > 0 {
            if let Err(e) = dst_writer.flush() {
                log::warn!(
                    "Couldn't flush managed process writes from packet payload: {:?}",
                    e
                );
                // TODO: can we get memmgr errno here like we can with `copy_from_ptr()`?
                return linux_api::errno::Errno::EFAULT.to_negated_i64();
            }
        }

        log::trace!("We wrote {tot_written} bytes into managed process buffer of len {dst_len}");

        i64::try_from(tot_written).unwrap()
    }

    #[no_mangle]
    pub extern "C-unwind" fn packet_getPriority(packet_ptr: *const Packet) -> u64 {
        let packet = PacketRc::borrow_raw(packet_ptr);
        packet.priority()
    }

    #[no_mangle]
    pub extern "C-unwind" fn packet_getPayloadSize(packet_ptr: *const Packet) -> u64 {
        let packet = PacketRc::borrow_raw(packet_ptr);
        packet.payload_len().try_into().unwrap()
    }

    #[no_mangle]
    pub extern "C-unwind" fn packet_getDestinationIP(packet_ptr: *const Packet) -> libc::in_addr_t {
        let packet = PacketRc::borrow_raw(packet_ptr);
        u32::to_be((*packet.dst_address().ip()).into())
    }

    #[no_mangle]
    pub extern "C-unwind" fn packet_getDestinationPort(
        packet_ptr: *const Packet,
    ) -> libc::in_port_t {
        let packet = PacketRc::borrow_raw(packet_ptr);
        u16::to_be(packet.dst_address().port())
    }

    #[no_mangle]
    pub extern "C-unwind" fn packet_getSourceIP(packet_ptr: *const Packet) -> libc::in_addr_t {
        let packet = PacketRc::borrow_raw(packet_ptr);
        u32::to_be((*packet.src_address().ip()).into())
    }

    #[no_mangle]
    pub extern "C-unwind" fn packet_getSourcePort(packet_ptr: *const Packet) -> libc::in_port_t {
        let packet = PacketRc::borrow_raw(packet_ptr);
        u16::to_be(packet.src_address().port())
    }

    #[no_mangle]
    pub extern "C-unwind" fn packet_addDeliveryStatus(
        packet_ptr: *mut Packet,
        status: c::PacketDeliveryStatusFlags,
    ) {
        let packet = PacketRc::borrow_raw_mut(packet_ptr);
        packet.add_status(PacketStatus::from(status));
    }

    #[no_mangle]
    pub extern "C-unwind" fn packet_compareTCPSequence(
        packet_ptr1: *mut Packet,
        packet_ptr2: *mut Packet,
        _ptr: *mut libc::c_void,
    ) -> libc::c_int {
        let packet1 = PacketRc::borrow_raw_mut(packet_ptr1);
        let packet2 = PacketRc::borrow_raw_mut(packet_ptr2);

        let seq1 = get_sequence_number(&packet1);
        let seq2 = get_sequence_number(&packet2);

        // Translated from the C packet impl.
        match seq1.cmp(&seq2) {
            Ordering::Less => -1,
            Ordering::Equal => 0,
            Ordering::Greater => 1,
        }
    }

    fn get_sequence_number(packet: &PacketRc) -> u32 {
        let Data::LegacyTcp(tcp_ref) = &packet.data else {
            unimplemented!()
        };
        tcp_ref.borrow().header.sequence
    }

    fn legacy_flags_to_tcp_flags(legacy_flags: c::ProtocolTCPFlags) -> tcp::TcpFlags {
        // The legacy flags use the first 7 bits of an i32. We could just do:
        // `u8::try_from(flags).expect("Legacy TCP flags use < 8 bits")` but we have to map the
        // values to support PCAP correctly.
        let mut tcp_flags = tcp::TcpFlags::empty();

        if legacy_flags & c::_ProtocolTCPFlags_PTCP_FIN != 0 {
            tcp_flags.insert(tcp::TcpFlags::FIN);
        }
        if legacy_flags & c::_ProtocolTCPFlags_PTCP_SYN != 0 {
            tcp_flags.insert(tcp::TcpFlags::SYN);
        }
        if legacy_flags & c::_ProtocolTCPFlags_PTCP_RST != 0 {
            tcp_flags.insert(tcp::TcpFlags::RST);
        }
        if legacy_flags & c::_ProtocolTCPFlags_PTCP_ACK != 0 {
            tcp_flags.insert(tcp::TcpFlags::ACK);
        }
        // These legacy flags don't exist as real TCP flags, so we overload the two bits that are
        // not shown in the PCAP output.
        if legacy_flags & c::_ProtocolTCPFlags_PTCP_SACK != 0 {
            tcp_flags.insert(tcp::TcpFlags::ECE);
        }
        if legacy_flags & c::_ProtocolTCPFlags_PTCP_DUPACK != 0 {
            tcp_flags.insert(tcp::TcpFlags::CWR);
        }

        tcp_flags
    }

    fn tcp_flags_to_legacy_flags(tcp_flags: tcp::TcpFlags) -> c::ProtocolTCPFlags {
        // The u8 header flags fit within the legacy flags which are represented by an i32. We could
        // just do: `bits as c::ProtocolTCPFlags` but we have to map the values to support PCAP
        // correctly.
        let mut legacy_flags = c::_ProtocolTCPFlags_PTCP_NONE;

        if tcp_flags.contains(tcp::TcpFlags::FIN) {
            legacy_flags |= c::_ProtocolTCPFlags_PTCP_FIN;
        }
        if tcp_flags.contains(tcp::TcpFlags::SYN) {
            legacy_flags |= c::_ProtocolTCPFlags_PTCP_SYN;
        }
        if tcp_flags.contains(tcp::TcpFlags::RST) {
            legacy_flags |= c::_ProtocolTCPFlags_PTCP_RST;
        }
        if tcp_flags.contains(tcp::TcpFlags::ACK) {
            legacy_flags |= c::_ProtocolTCPFlags_PTCP_ACK;
        }
        // Extract these from the encoding used in `legacy_flags_to_tcp_flags()`.
        if tcp_flags.contains(tcp::TcpFlags::ECE) {
            legacy_flags |= c::_ProtocolTCPFlags_PTCP_SACK;
        }
        if tcp_flags.contains(tcp::TcpFlags::CWR) {
            legacy_flags |= c::_ProtocolTCPFlags_PTCP_DUPACK;
        }

        legacy_flags
    }

    fn from_legacy_timestamp(ts: c::CSimulationTime) -> Option<u32> {
        SimulationTime::from_c_simtime(ts).map(|x| u32::try_from(x.as_millis()).unwrap_or(u32::MAX))
    }

    fn to_legacy_timestamp(val: Option<u32>) -> c::CSimulationTime {
        SimulationTime::to_c_simtime(val.map(|x| SimulationTime::from_millis(x as u64)))
    }

    impl From<c::PacketSelectiveAcks> for TcpSelectiveAcks {
        fn from(c_sel_acks: c::PacketSelectiveAcks) -> Self {
            let mut selective_acks = TcpSelectiveAcks::default();

            assert!(c_sel_acks.len <= 4);

            for i in 0..(c_sel_acks.len as usize) {
                let start: u32 = c_sel_acks.ranges[i].start;
                let end: u32 = c_sel_acks.ranges[i].end;
                selective_acks.ranges[i] = (start, end);
                selective_acks.len += 1;
            }

            selective_acks
        }
    }

    fn to_legacy_sel_acks(selective_acks: Option<TcpSelectiveAcks>) -> c::PacketSelectiveAcks {
        let mut c_sel_acks: c::PacketSelectiveAcks = unsafe { MaybeUninit::zeroed().assume_init() };

        let Some(selective_acks) = selective_acks else {
            return c_sel_acks;
        };

        assert!(selective_acks.len <= 4);

        for i in 0..(selective_acks.len as usize) {
            let (start, end) = selective_acks.ranges[i];
            c_sel_acks.ranges[i].start = start;
            c_sel_acks.ranges[i].end = end;
            c_sel_acks.len += 1;
        }

        c_sel_acks
    }

    impl From<c::PacketDeliveryStatusFlags> for PacketStatus {
        fn from(legacy_status: c::PacketDeliveryStatusFlags) -> Self {
            match legacy_status {
                c::_PacketDeliveryStatusFlags_PDS_SND_CREATED => PacketStatus::SndCreated,
                c::_PacketDeliveryStatusFlags_PDS_SND_TCP_ENQUEUE_THROTTLED => {
                    PacketStatus::SndTcpEnqueueThrottled
                }
                c::_PacketDeliveryStatusFlags_PDS_SND_TCP_ENQUEUE_RETRANSMIT => {
                    PacketStatus::SndTcpEnqueueRetransmit
                }
                c::_PacketDeliveryStatusFlags_PDS_SND_TCP_DEQUEUE_RETRANSMIT => {
                    PacketStatus::SndTcpDequeueRetransmit
                }
                c::_PacketDeliveryStatusFlags_PDS_SND_TCP_RETRANSMITTED => {
                    PacketStatus::SndTcpRetransmitted
                }
                c::_PacketDeliveryStatusFlags_PDS_SND_SOCKET_BUFFERED => {
                    PacketStatus::SndSocketBuffered
                }
                c::_PacketDeliveryStatusFlags_PDS_SND_INTERFACE_SENT => {
                    PacketStatus::SndInterfaceSent
                }
                c::_PacketDeliveryStatusFlags_PDS_INET_SENT => PacketStatus::InetSent,
                c::_PacketDeliveryStatusFlags_PDS_INET_DROPPED => PacketStatus::InetDropped,
                c::_PacketDeliveryStatusFlags_PDS_ROUTER_ENQUEUED => PacketStatus::RouterEnqueued,
                c::_PacketDeliveryStatusFlags_PDS_ROUTER_DEQUEUED => PacketStatus::RouterDequeued,
                c::_PacketDeliveryStatusFlags_PDS_ROUTER_DROPPED => PacketStatus::RouterDropped,
                c::_PacketDeliveryStatusFlags_PDS_RCV_INTERFACE_RECEIVED => {
                    PacketStatus::RcvInterfaceReceived
                }
                c::_PacketDeliveryStatusFlags_PDS_RCV_INTERFACE_DROPPED => {
                    PacketStatus::RcvInterfaceDropped
                }
                c::_PacketDeliveryStatusFlags_PDS_RCV_SOCKET_PROCESSED => {
                    PacketStatus::RcvSocketProcessed
                }
                c::_PacketDeliveryStatusFlags_PDS_RCV_SOCKET_DROPPED => {
                    PacketStatus::RcvSocketDropped
                }
                c::_PacketDeliveryStatusFlags_PDS_RCV_TCP_ENQUEUE_UNORDERED => {
                    PacketStatus::RcvTcpEnqueueUnordered
                }
                c::_PacketDeliveryStatusFlags_PDS_RCV_SOCKET_BUFFERED => {
                    PacketStatus::RcvSocketBuffered
                }
                c::_PacketDeliveryStatusFlags_PDS_RCV_SOCKET_DELIVERED => {
                    PacketStatus::RcvSocketDelivered
                }
                c::_PacketDeliveryStatusFlags_PDS_DESTROYED => PacketStatus::Destroyed,
                c::_PacketDeliveryStatusFlags_PDS_RELAY_CACHED => PacketStatus::RelayCached,
                c::_PacketDeliveryStatusFlags_PDS_RELAY_FORWARDED => PacketStatus::RelayForwarded,
                _ => unimplemented!(),
            }
        }
    }
}
