use std::net::SocketAddrV4;
use std::path::PathBuf;

use crate::core::support::configuration::QDiscMode;
use crate::cshadow as c;
use crate::host::host::Host;
use crate::utility::{self, HostTreePointer};
use shadow_shim_helper_rs::HostId;

/// Represents a network device that can send and receive packets. All accesses
/// to the internal C implementation should be done through this module.
pub struct NetworkInterface {
    c_ptr: HostTreePointer<c::NetworkInterface>,
}

impl NetworkInterface {
    /// Create a new network interface for `host_id` with the assigned `addr`.
    ///
    /// # Safety
    ///
    /// This function will trigger undefined behavior if `addr` is
    /// invalid. The reference count of `addr` will be increased by one using
    /// `address_ref()`, so the caller should call `address_unref()` on it to
    /// drop their reference when they no longer need it.
    pub unsafe fn new(
        host_id: HostId,
        addr: *mut c::Address,
        pcap_options: Option<PcapOptions>,
        qdisc: QDiscMode,
        uses_router: bool,
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

        let c_ptr = unsafe {
            c::networkinterface_new(addr, pcap_dir_cptr, pcap_capture_size, qdisc, uses_router)
        };

        NetworkInterface {
            c_ptr: HostTreePointer::new_for_host(host_id, c_ptr),
        }
    }

    pub fn associate(
        &self,
        socket_ptr: *const c::CompatSocket,
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

    pub fn is_associated(&self, protocol: c::ProtocolType, port: u16, peer: SocketAddrV4) -> bool {
        let port = port.to_be();
        let peer_ip = u32::from(*peer.ip()).to_be();
        let peer_port = peer.port().to_be();

        (unsafe {
            c::networkinterface_isAssociated(self.c_ptr.ptr(), protocol, port, peer_ip, peer_port)
        }) != 0
    }

    pub fn start_refilling_token_buckets(&self, bw_down_kibps: u64, bw_up_kibps: u64) {
        unsafe {
            c::networkinterface_startRefillingTokenBuckets(
                self.c_ptr.ptr(),
                bw_down_kibps,
                bw_up_kibps,
            )
        };
    }

    pub fn wants_send(&self, socket_ptr: &c::CompatSocket, host: &Host) {
        unsafe {
            c::networkinterface_wantsSend(
                self.c_ptr.ptr(),
                host as *const Host,
                socket_ptr as *const c::CompatSocket,
            )
        };
    }

    pub fn receive_packets(&self, host: &Host) {
        unsafe { c::networkinterface_receivePackets(self.c_ptr.ptr(), host as *const Host) };
    }

    /// Returns a pointer to our internal C network interface object so that
    /// network interface functions can be called outside of the rust API.
    ///
    /// # Safety
    ///
    /// The returned pointer is only valid until the reference to this
    /// `NetworkInterface` object is dropped.
    // TODO: Remove this function to remove unsafe access to internals.
    pub unsafe fn borrow_inner(&self) -> *mut c::NetworkInterface {
        unsafe { self.c_ptr.ptr() }
    }
}

impl Drop for NetworkInterface {
    fn drop(&mut self) {
        // don't check the active host since we're in the middle of dropping the host
        unsafe { c::networkinterface_free(self.c_ptr.ptr_unchecked()) };
    }
}

#[derive(Debug, Clone)]
pub struct PcapOptions {
    pub path: PathBuf,
    pub capture_size_bytes: u32,
}
