use std::cell::{Cell, RefCell};
use std::net::{Ipv4Addr, SocketAddrV4};
use std::ops::{Deref, DerefMut};
use std::sync::Arc;

use atomic_refcell::AtomicRefCell;

use crate::core::configuration::QDiscMode;
use crate::core::worker::Worker;
use crate::host::descriptor::socket::abstract_unix_ns::AbstractUnixNamespace;
use crate::host::descriptor::socket::inet::InetSocket;
use crate::host::network::interface::{NetworkInterface, PcapOptions};
use crate::network::packet::IanaProtocol;

// The start of our random port range in host order, used if application doesn't
// specify the port it wants to bind to, and for client connections.
const MIN_RANDOM_PORT: u16 = 10000;

/// Represents a network namespace.
///
/// Can be thought of as roughly equivalent to a Linux `struct net`. Shadow doesn't support multiple
/// network namespaces, but this `NetworkNamespace` allows us to consolidate the host's networking
/// objects, and hopefully might make it easier to support multiple network namespaces if we want to
/// in the future.
pub struct NetworkNamespace {
    // map abstract socket addresses to unix sockets
    pub unix: Arc<AtomicRefCell<AbstractUnixNamespace>>,

    pub localhost: RefCell<NetworkInterface>,
    pub internet: RefCell<NetworkInterface>,

    pub default_ip: Ipv4Addr,

    // used for debugging to make sure we've cleaned up before being dropped
    has_run_cleanup: Cell<bool>,
}

impl NetworkNamespace {
    pub fn new(public_ip: Ipv4Addr, pcap: Option<PcapOptions>, qdisc: QDiscMode) -> Self {
        let localhost = NetworkInterface::new("lo", Ipv4Addr::LOCALHOST, pcap.clone(), qdisc);

        let internet = NetworkInterface::new("eth0", public_ip, pcap, qdisc);

        Self {
            unix: Arc::new(AtomicRefCell::new(AbstractUnixNamespace::new())),
            localhost: RefCell::new(localhost),
            internet: RefCell::new(internet),
            default_ip: public_ip,
            has_run_cleanup: Cell::new(false),
        }
    }

    /// Clean up the network namespace. This should be called while `Worker` has the active host
    /// set.
    pub fn cleanup(&self) {
        assert!(!self.has_run_cleanup.get());

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
        // Notes:
        // - The `is_loopback` matches all loopback addresses, but shadow will only work correctly
        //   with 127.0.0.1. Using any other loopback address will lead to problems.
        // - If the address is 0.0.0.0, we return the `internet` interface. This is not ideal if a
        //   socket bound to 0.0.0.0 is trying to send a localhost packet and uses this method to
        //   get the network interface, since the packet will be sent on the internet interface
        //   instead of loopback. It's not clear if this will lead to bugs.
        if addr.is_loopback() {
            Some(self.localhost.borrow())
        } else if addr == self.default_ip || addr.is_unspecified() {
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
    ) -> Option<impl DerefMut<Target = NetworkInterface> + '_> {
        // Notes:
        // - The `is_loopback` matches all loopback addresses, but shadow will only work correctly
        //   with 127.0.0.1. Using any other loopback address will lead to problems.
        // - If the address is 0.0.0.0, we return the `internet` interface. This is not ideal if a
        //   socket bound to 0.0.0.0 is trying to send a localhost packet and uses this method to
        //   get the network interface, since the packet will be sent on the internet interface
        //   instead of loopback. It's not clear if this will lead to bugs.
        if addr.is_loopback() {
            Some(self.localhost.borrow_mut())
        } else if addr == self.default_ip || addr.is_unspecified() {
            Some(self.internet.borrow_mut())
        } else {
            None
        }
    }

    pub fn is_addr_in_use(
        &self,
        protocol_type: IanaProtocol,
        src: SocketAddrV4,
        dst: SocketAddrV4,
    ) -> Result<bool, NoInterface> {
        if src.ip().is_unspecified() {
            Ok(self
                .localhost
                .borrow()
                .is_addr_in_use(protocol_type, src.port(), dst)
                || self
                    .internet
                    .borrow()
                    .is_addr_in_use(protocol_type, src.port(), dst))
        } else {
            match self.interface_borrow(*src.ip()) {
                Some(i) => Ok(i.is_addr_in_use(protocol_type, src.port(), dst)),
                None => Err(NoInterface),
            }
        }
    }

    /// Returns a random port in host byte order.
    pub fn get_random_free_port(
        &self,
        protocol_type: IanaProtocol,
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
            let random_port = rng.random_range(MIN_RANDOM_PORT..=u16::MAX);

            // `is_addr_in_use` will check all interfaces in the case of INADDR_ANY
            let specific_in_use = self
                .is_addr_in_use(
                    protocol_type,
                    SocketAddrV4::new(interface_ip, random_port),
                    peer,
                )
                .unwrap_or(true);
            let generic_in_use = self
                .is_addr_in_use(
                    protocol_type,
                    SocketAddrV4::new(interface_ip, random_port),
                    SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, 0),
                )
                .unwrap_or(true);
            if !specific_in_use && !generic_in_use {
                return Some(random_port);
            }
        }

        // now if we tried too many times and still don't have a port, fall back
        // to a linear search to make sure we get a free port if we have one.
        // but start from a random port instead of the min.
        let start = rng.random_range(MIN_RANDOM_PORT..=u16::MAX);
        for port in (start..=u16::MAX).chain(MIN_RANDOM_PORT..start) {
            let specific_in_use = self
                .is_addr_in_use(protocol_type, SocketAddrV4::new(interface_ip, port), peer)
                .unwrap_or(true);
            let generic_in_use = self
                .is_addr_in_use(
                    protocol_type,
                    SocketAddrV4::new(interface_ip, port),
                    SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, 0),
                )
                .unwrap_or(true);
            if !specific_in_use && !generic_in_use {
                return Some(port);
            }
        }

        log::warn!("unable to find free ephemeral port for {protocol_type:?} peer {peer}");
        None
    }

    /// Associate the socket with any applicable network interfaces. The socket will be
    /// automatically disassociated when the returned handle is dropped.
    ///
    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    pub unsafe fn associate_interface(
        &self,
        socket: &InetSocket,
        protocol: IanaProtocol,
        bind_addr: SocketAddrV4,
        peer_addr: SocketAddrV4,
    ) -> AssociationHandle {
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

        AssociationHandle {
            protocol,
            local_addr: bind_addr,
            remote_addr: peer_addr,
        }
    }

    /// Disassociate the socket associated using the local and remote addresses from all network
    /// interfaces.
    ///
    /// Is only public so that it can be called from `host_disassociateInterface`. Normally this
    /// should only be called from the [`AssociationHandle`].
    pub fn disassociate_interface(
        &self,
        protocol: IanaProtocol,
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
        if !self.has_run_cleanup.get() && !std::thread::panicking() {
            debug_panic!("Dropped the network namespace before it has been cleaned up");
        }
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct NoInterface;

impl std::fmt::Display for NoInterface {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "No interface available")
    }
}

impl std::error::Error for NoInterface {}

/// A handle for a socket association with a network interface(s).
///
/// The network association will be dissolved when this handle is dropped (similar to
/// [`callback_queue::Handle`](crate::utility::callback_queue::Handle)).
#[derive(Debug)]
pub struct AssociationHandle {
    protocol: IanaProtocol,
    local_addr: SocketAddrV4,
    remote_addr: SocketAddrV4,
}

impl AssociationHandle {
    pub fn local_addr(&self) -> SocketAddrV4 {
        self.local_addr
    }

    pub fn remote_addr(&self) -> SocketAddrV4 {
        self.remote_addr
    }
}

impl std::ops::Drop for AssociationHandle {
    fn drop(&mut self) {
        Worker::with_active_host(|host| {
            host.network_namespace_borrow().disassociate_interface(
                self.protocol,
                self.local_addr,
                self.remote_addr,
            );
        })
        .unwrap();
    }
}
