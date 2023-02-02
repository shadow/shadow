use std::cell::{Cell, RefCell};
use std::ffi::CString;
use std::net::{Ipv4Addr, SocketAddrV4};
use std::num::NonZeroU8;
use std::ops::{Deref, DerefMut};
use std::sync::Arc;

use atomic_refcell::AtomicRefCell;
use shadow_shim_helper_rs::HostId;

use crate::core::support::configuration::QDiscMode;
use crate::cshadow;
use crate::host::descriptor::socket::abstract_unix_ns::AbstractUnixNamespace;
use crate::host::network_interface::{NetworkInterface, PcapOptions};
use crate::utility::SyncSendPointer;

// The start of our random port range in host order, used if application doesn't
// specify the port it wants to bind to, and for client connections.
const MIN_RANDOM_PORT: u16 = 10000;

/// Represents a network namespace. Can be thought of as roughly equivalent to a Linux `struct net`.
/// Shadow doesn't support multiple network namespaces, but this `NetworkNamespace` allows us to
/// consolidate the host's networking objects, and hopefully might make it easier to support
/// multiple network namespaces if we want to in the future.
pub struct NetworkNamespace {
    // map abstract socket addresses to unix sockets
    pub unix: Arc<AtomicRefCell<AbstractUnixNamespace>>,

    pub localhost: RefCell<NetworkInterface>,
    pub internet: RefCell<NetworkInterface>,

    // TODO: use a Rust address type
    pub default_address: SyncSendPointer<cshadow::Address>,
    pub default_ip: Ipv4Addr,

    // used for debugging to make sure we've cleaned up before being dropped
    has_run_cleanup: Cell<bool>,
}

impl NetworkNamespace {
    /// # Safety
    ///
    /// `dns` must be a valid pointer.
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
                    hostname,
                    ip: public_ip,
                    pcap,
                    qdisc,
                },
                dns,
            )
        };

        Self {
            unix: Arc::new(AtomicRefCell::new(AbstractUnixNamespace::new())),
            localhost: RefCell::new(localhost),
            internet: RefCell::new(internet),
            default_address: unsafe { SyncSendPointer::new(public_addr) },
            default_ip: public_ip,
            has_run_cleanup: Cell::new(false),
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
            NetworkInterface::new(options.host_id, addr, options.pcap.clone(), options.qdisc)
        };

        (interface, addr)
    }

    /// Clean up the network namespace. This should be called while `Worker` has the active host
    /// set. The `dns` object should be the same object that was originally provided to
    /// [`Self::new`].
    pub fn cleanup(&self, dns: &cshadow::DNS) {
        assert!(!self.has_run_cleanup.get());

        let dns = dns as *const cshadow::DNS;
        // deregistering localhost is a no-op, so we skip it
        unsafe {
            cshadow::dns_deregister(dns.cast_mut(), self.default_address.ptr());
        }

        // we need to unref all sockets and free them before we drop the host, otherwise they'll try
        // to access the global host and panic since there is no host
        self.localhost.borrow().remove_all_sockets();
        self.internet.borrow().remove_all_sockets();

        self.has_run_cleanup.set(true);
    }

    /// Returns `None` if there is no such interface.
    #[track_caller]
    pub fn interface_borrow(
        &self,
        addr: Ipv4Addr,
    ) -> Option<impl Deref<Target = NetworkInterface> + '_> {
        if addr.is_loopback() {
            Some(self.localhost.borrow())
        } else if addr == self.default_ip {
            Some(self.internet.borrow())
        } else {
            None
        }
    }

    /// Returns `None` if there is no such interface.
    #[track_caller]
    pub fn interface_borrow_mut(
        &self,
        addr: Ipv4Addr,
    ) -> Option<impl Deref<Target = NetworkInterface> + DerefMut + '_> {
        if addr.is_loopback() {
            Some(self.localhost.borrow_mut())
        } else if addr == self.default_ip {
            Some(self.internet.borrow_mut())
        } else {
            None
        }
    }

    pub fn is_interface_available(
        &self,
        protocol_type: cshadow::ProtocolType,
        src: SocketAddrV4,
        dst: SocketAddrV4,
    ) -> bool {
        if src.ip().is_unspecified() {
            // Check that all interfaces are available.
            !self
                .localhost
                .borrow()
                .is_associated(protocol_type, src.port(), dst)
                && !self
                    .internet
                    .borrow()
                    .is_associated(protocol_type, src.port(), dst)
        } else {
            // The interface is not available if it does not exist.
            match self.interface_borrow(*src.ip()) {
                Some(i) => !i.is_associated(protocol_type, src.port(), dst),
                None => false,
            }
        }
    }

    /// Returns a random port in host byte order.
    pub fn get_random_free_port(
        &self,
        protocol_type: cshadow::ProtocolType,
        interface_ip: Ipv4Addr,
        peer: SocketAddrV4,
        mut rng: impl rand::Rng,
    ) -> Option<u16> {
        // we need a random port that is free everywhere we need it to be.
        // we have two modes here: first we just try grabbing a random port until we
        // get a free one. if we cannot find one fast enough, then as a fallback we
        // do an inefficient linear search that is guaranteed to succeed or fail.

        // if choosing randomly doesn't succeed within 10 tries, then we have already
        // allocated a lot of ports (>90% on average). then we fall back to linear search.
        for _ in 0..10 {
            let random_port = rng.gen_range(MIN_RANDOM_PORT..=u16::MAX);

            // this will check all interfaces in the case of INADDR_ANY
            if self.is_interface_available(
                protocol_type,
                SocketAddrV4::new(interface_ip, random_port),
                peer,
            ) {
                return Some(random_port);
            }
        }

        // now if we tried too many times and still don't have a port, fall back
        // to a linear search to make sure we get a free port if we have one.
        // but start from a random port instead of the min.
        let start = rng.gen_range(MIN_RANDOM_PORT..=u16::MAX);
        for port in (start..=u16::MAX).chain(MIN_RANDOM_PORT..start) {
            if self.is_interface_available(
                protocol_type,
                SocketAddrV4::new(interface_ip, port),
                peer,
            ) {
                return Some(port);
            }
        }

        log::warn!("unable to find free ephemeral port for {protocol_type} peer {peer}");
        None
    }

    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    pub unsafe fn associate_interface(
        &self,
        socket: *const cshadow::CompatSocket,
        protocol: cshadow::ProtocolType,
        bind_addr: SocketAddrV4,
        peer_addr: SocketAddrV4,
    ) {
        if bind_addr.ip().is_unspecified() {
            // need to associate all interfaces
            self.localhost
                .borrow()
                .associate(socket, protocol, bind_addr.port(), peer_addr);
            self.internet
                .borrow()
                .associate(socket, protocol, bind_addr.port(), peer_addr);
        } else {
            // TODO: return error if interface does not exist
            if let Some(iface) = self.interface_borrow(*bind_addr.ip()) {
                iface.associate(socket, protocol, bind_addr.port(), peer_addr);
            }
        }
    }

    pub fn disassociate_interface(
        &self,
        protocol: cshadow::ProtocolType,
        bind_addr: SocketAddrV4,
        peer_addr: SocketAddrV4,
    ) {
        if bind_addr.ip().is_unspecified() {
            // need to disassociate all interfaces
            self.localhost
                .borrow()
                .disassociate(protocol, bind_addr.port(), peer_addr);

            self.internet
                .borrow()
                .disassociate(protocol, bind_addr.port(), peer_addr);
        } else {
            // TODO: return error if interface does not exist
            if let Some(iface) = self.interface_borrow(*bind_addr.ip()) {
                iface.disassociate(protocol, bind_addr.port(), peer_addr);
            }
        }
    }
}

impl std::ops::Drop for NetworkNamespace {
    fn drop(&mut self) {
        unsafe { cshadow::address_unref(self.default_address.ptr()) };

        if !self.has_run_cleanup.get() && !std::thread::panicking() {
            debug_panic!("Dropped the network namespace before it has been cleaned up");
        }
    }
}

struct InterfaceOptions {
    pub host_id: HostId,
    pub hostname: Vec<NonZeroU8>,
    pub ip: Ipv4Addr,
    pub pcap: Option<PcapOptions>,
    pub qdisc: QDiscMode,
}
