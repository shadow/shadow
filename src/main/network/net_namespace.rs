use std::ffi::CString;
use std::net::Ipv4Addr;
use std::num::NonZeroU8;
use std::sync::Arc;

use atomic_refcell::AtomicRefCell;
use shadow_shim_helper_rs::HostId;

use crate::core::support::configuration::QDiscMode;
use crate::core::worker::Worker;
use crate::cshadow;
use crate::host::descriptor::socket::abstract_unix_ns::AbstractUnixNamespace;
use crate::host::network_interface::{NetworkInterface, PcapOptions};
use crate::utility::SyncSendPointer;

/// Represents a network namespace. Can be thought of as roughly equivalent to a Linux `struct net`.
/// Shadow doesn't support multiple network namespaces, but this `NetworkNamespace` allows us to
/// consolidate the host's networking objects, and hopefully might make it easier to support
/// multiple network namespaces if we want to in the future.
pub struct NetworkNamespace {
    // map abstract socket addresses to unix sockets
    pub unix: Arc<AtomicRefCell<AbstractUnixNamespace>>,

    pub localhost: NetworkInterface,
    pub internet: NetworkInterface,

    // TODO: use a Rust address type
    pub default_address: SyncSendPointer<cshadow::Address>,
}

impl NetworkNamespace {
    /// SAFETY: `dns` must be a valid pointer.
    pub unsafe fn new(
        host_id: HostId,
        hostname: Vec<NonZeroU8>,
        public_ip: Ipv4Addr,
        pcap: Option<PcapOptions>,
        qdisc: QDiscMode,
        dns: *mut cshadow::DNS,
    ) -> Self {
        let (localhost, local_addr) = unsafe {
            Self::setup_net_interface(
                &InterfaceOptions {
                    host_id,
                    hostname: hostname.clone(),
                    ip: Ipv4Addr::LOCALHOST,
                    uses_router: false,
                    pcap: pcap.clone(),
                    qdisc,
                },
                dns,
            )
        };

        unsafe { cshadow::address_unref(local_addr) };

        let (internet, public_addr) = unsafe {
            Self::setup_net_interface(
                &InterfaceOptions {
                    host_id,
                    hostname: hostname,
                    ip: public_ip,
                    uses_router: true,
                    pcap,
                    qdisc,
                },
                dns,
            )
        };

        Self {
            unix: Arc::new(AtomicRefCell::new(AbstractUnixNamespace::new())),
            localhost,
            internet,
            default_address: unsafe { SyncSendPointer::new(public_addr) },
        }
    }

    /// Must free the returned `*mut cshadow::Address` using [`cshadow::address_unref`].
    unsafe fn setup_net_interface(
        options: &InterfaceOptions,
        dns: *mut cshadow::DNS,
    ) -> (NetworkInterface, *mut cshadow::Address) {
        let ip = u32::from(options.ip).to_be();

        // hostname is shadowed so that we can't accidentally drop the CString before the end of the
        // scope
        let hostname: CString = options.hostname.clone().into();
        let hostname = hostname.as_ptr();

        let addr = unsafe { cshadow::dns_register(dns, options.host_id, hostname, ip) };
        assert!(!addr.is_null());

        let interface = unsafe {
            NetworkInterface::new(
                options.host_id,
                addr,
                options.pcap.clone(),
                options.qdisc,
                options.uses_router,
            )
        };

        (interface, addr)
    }
}

impl std::ops::Drop for NetworkNamespace {
    fn drop(&mut self) {
        // deregistering localhost is a no-op, so we skip it
        Worker::with_dns(|dns| unsafe {
            let dns = dns as *const cshadow::DNS;
            cshadow::dns_deregister(dns.cast_mut(), self.default_address.ptr())
        });

        unsafe { cshadow::address_unref(self.default_address.ptr()) };
    }
}

struct InterfaceOptions {
    pub host_id: HostId,
    pub hostname: Vec<NonZeroU8>,
    pub ip: Ipv4Addr,
    pub uses_router: bool,
    pub pcap: Option<PcapOptions>,
    pub qdisc: QDiscMode,
}
