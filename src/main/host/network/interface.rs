use std::ffi::{CString, OsStr};
use std::net::{Ipv4Addr, SocketAddrV4};
use std::os::unix::ffi::OsStrExt;
use std::path::PathBuf;

use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::HostId;

use crate::core::configuration::QDiscMode;
use crate::core::worker::Worker;
use crate::cshadow as c;
use crate::host::descriptor::socket::inet::InetSocket;
use crate::network::packet::PacketRc;
use crate::network::PacketDevice;
use crate::utility::{self, HostTreePointer};

/// The priority used by the fifo qdisc to choose the next socket to send a packet from.
pub type FifoPacketPriority = u64;

#[derive(Debug, Clone)]
pub struct PcapOptions {
    pub path: PathBuf,
    pub capture_size_bytes: u32,
}

/// Represents a network device that can send and receive packets. All accesses
/// to the internal C implementation should be done through this module.
pub struct NetworkInterface {
    c_ptr: HostTreePointer<c::NetworkInterface>,
    addr: Ipv4Addr,
}

impl NetworkInterface {
    /// Create a new network interface for `host_id` with the assigned `addr`.
    pub fn new(
        host_id: HostId,
        addr: Ipv4Addr,
        name: &OsStr,
        pcap_options: Option<PcapOptions>,
        qdisc: QDiscMode,
    ) -> NetworkInterface {
        let maybe_pcap_dir = pcap_options
            .as_ref()
            .map(|x| utility::pathbuf_to_nul_term_cstring(x.path.clone()));
        let pcap_dir_cptr = maybe_pcap_dir
            .as_ref()
            .map_or(std::ptr::null(), |p| p.as_ptr());

        let pcap_capture_size = pcap_options
            .as_ref()
            .map(|x| x.capture_size_bytes)
            .unwrap_or(0);

        let mut name = name.as_bytes().to_vec();
        name.push(0);
        let name = CString::from_vec_with_nul(name).unwrap();

        let net_addr = u32::from(addr).to_be();

        let c_ptr = unsafe {
            c::networkinterface_new(
                net_addr,
                name.as_ptr(),
                pcap_dir_cptr,
                pcap_capture_size,
                qdisc,
            )
        };

        NetworkInterface {
            c_ptr: HostTreePointer::new_for_host(host_id, c_ptr),
            addr,
        }
    }

    pub fn associate(
        &self,
        socket_ptr: &InetSocket,
        protocol_type: c::ProtocolType,
        port: u16,
        peer_addr: SocketAddrV4,
    ) {
        let port = port.to_be();
        let peer_ip = u32::from(*peer_addr.ip()).to_be();
        let peer_port = peer_addr.port().to_be();

        unsafe {
            c::networkinterface_associate(
                self.c_ptr.ptr(),
                socket_ptr,
                protocol_type,
                port,
                peer_ip,
                peer_port,
            )
        };
    }

    pub fn disassociate(&self, protocol_type: c::ProtocolType, port: u16, peer_addr: SocketAddrV4) {
        let port = port.to_be();
        let peer_ip = u32::from(*peer_addr.ip()).to_be();
        let peer_port = peer_addr.port().to_be();

        unsafe {
            c::networkinterface_disassociate(
                self.c_ptr.ptr(),
                protocol_type,
                port,
                peer_ip,
                peer_port,
            )
        };
    }

    pub fn is_addr_in_use(&self, protocol: c::ProtocolType, port: u16, peer: SocketAddrV4) -> bool {
        let port = port.to_be();
        let peer_ip = u32::from(*peer.ip()).to_be();
        let peer_port = peer.port().to_be();

        (unsafe {
            c::networkinterface_isAssociated(self.c_ptr.ptr(), protocol, port, peer_ip, peer_port)
        }) != 0
    }

    pub fn add_data_source(&self, socket: &InetSocket) {
        unsafe { c::networkinterface_wantsSend(self.c_ptr.ptr(), socket) };
    }

    /// Disassociate all bound sockets and remove sockets from the sending queue. This should be
    /// called as part of the host's cleanup procedure.
    pub fn remove_all_sockets(&self) {
        unsafe { c::networkinterface_removeAllSockets(self.c_ptr.ptr()) };
    }
}

impl Drop for NetworkInterface {
    fn drop(&mut self) {
        // don't check the active host since we're in the middle of dropping the host
        unsafe { c::networkinterface_free(self.c_ptr.ptr_unchecked()) };
    }
}

impl PacketDevice for NetworkInterface {
    fn get_address(&self) -> Ipv4Addr {
        self.addr
    }

    fn pop(&self) -> Option<PacketRc> {
        let packet_ptr = unsafe { c::networkinterface_pop(self.c_ptr.ptr()) };
        match packet_ptr.is_null() {
            true => None,
            false => Some(PacketRc::from_raw(packet_ptr)),
        }
    }

    fn push(&self, packet: PacketRc) {
        let packet_ptr = packet.into_inner();
        let current_time = Worker::current_time().unwrap();
        unsafe {
            c::networkinterface_push(
                self.c_ptr.ptr(),
                packet_ptr,
                EmulatedTime::to_c_emutime(Some(current_time)),
            )
        };
        unsafe { c::packet_unref(packet_ptr) };
    }
}
