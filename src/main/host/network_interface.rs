use std::path::PathBuf;

use libc::{in_addr_t, in_port_t};

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
    /// SAFETY: This function will trigger undefined behavior if `addr` is
    /// invalid. The reference count of `addr` will be increased by one using
    /// `address_ref()`, so the caller should call `address_unref()` on it to
    /// drop their reference when they no longer need it.
    pub unsafe fn new(
        host_id: HostId,
        addr: *mut c::Address,
        maybe_pcap_dir: Option<PathBuf>,
        pcap_capture_size: u32,
        qdisc: QDiscMode,
        uses_router: bool,
    ) -> NetworkInterface {
        let maybe_pcap_dir = maybe_pcap_dir.map(|p| utility::pathbuf_to_nul_term_cstring(p));
        let pcap_dir_cptr = maybe_pcap_dir
            .as_ref()
            .map_or(std::ptr::null(), |p| p.as_ptr());

        let c_ptr = unsafe {
            c::networkinterface_new(addr, pcap_dir_cptr, pcap_capture_size, qdisc, uses_router)
        };

        NetworkInterface {
            c_ptr: HostTreePointer::new_for_host(host_id, c_ptr),
        }
    }

    pub fn associate(&self, socket_ptr: *const c::CompatSocket) {
        unsafe { c::networkinterface_associate(self.c_ptr.ptr(), socket_ptr) };
    }

    pub fn disassociate(&self, socket_ptr: *const c::CompatSocket) {
        unsafe { c::networkinterface_disassociate(self.c_ptr.ptr(), socket_ptr) };
    }

    pub fn is_associated(
        &self,
        protocol: c::ProtocolType,
        port: in_port_t,
        peer_addr: in_addr_t,
        peer_port: in_port_t,
    ) -> bool {
        unsafe {
            c::networkinterface_isAssociated(self.c_ptr.ptr(), protocol, port, peer_addr, peer_port)
                != 0
        }
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
    /// SAFETY: The returned pointer is only valid until the reference to this
    /// `NetworkInterface` object is dropped.
    // TODO: Remove this function to remove unsafe access to internals.
    pub unsafe fn borrow_inner(&self) -> *mut c::NetworkInterface {
        unsafe { self.c_ptr.ptr() }
    }
}

impl Drop for NetworkInterface {
    fn drop(&mut self) {
        unsafe { c::networkinterface_free(self.c_ptr.ptr()) };
    }
}
