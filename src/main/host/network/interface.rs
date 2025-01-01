use std::cell::RefCell;
use std::collections::hash_map::Entry;
use std::collections::HashMap;
use std::fs::File;
use std::io::BufWriter;
use std::net::{Ipv4Addr, SocketAddrV4};
use std::path::PathBuf;

use crate::core::configuration::QDiscMode;
use crate::core::worker::Worker;
use crate::cshadow as c;
use crate::host::descriptor::socket::inet::InetSocket;
use crate::host::network::queuing::{NetworkQueue, NetworkQueueKind};
use crate::network::packet::{PacketRc, PacketStatus};
use crate::network::PacketDevice;
use crate::utility::callback_queue::CallbackQueue;
use crate::utility::pcap_writer::{PacketDisplay, PcapWriter};
use crate::utility::ObjectCounter;

/// The priority used by the fifo qdisc to choose the next socket to send a packet from.
pub type FifoPacketPriority = u64;

#[derive(Debug, Clone)]
pub struct PcapOptions {
    pub path: PathBuf,
    pub capture_size_bytes: u32,
}

#[derive(Clone, Debug, Eq, Hash, PartialEq)]
struct AssociatedSocketKey {
    protocol: c::ProtocolType,
    local: SocketAddrV4,
    remote: SocketAddrV4,
}

impl AssociatedSocketKey {
    fn new(
        protocol: c::ProtocolType,
        local_addr: Ipv4Addr,
        local_port: u16,
        remote: SocketAddrV4,
    ) -> Self {
        Self {
            protocol,
            local: SocketAddrV4::new(local_addr, local_port),
            remote,
        }
    }
}

/// Represents a network device that can send and receive packets.
pub struct NetworkInterface {
    _counter: ObjectCounter,
    addr: Ipv4Addr,
    send_sockets: RefCell<NetworkQueue<InetSocket>>,
    recv_sockets: RefCell<HashMap<AssociatedSocketKey, InetSocket>>,
    pcap: RefCell<Option<PcapWriter<BufWriter<File>>>>,
    cleanup_in_progress: RefCell<bool>,
}

impl NetworkInterface {
    /// Create a new network interface for the assigned `addr`.
    pub fn new(
        name: &str,
        addr: Ipv4Addr,
        pcap_options: Option<PcapOptions>,
        qdisc: QDiscMode,
    ) -> Self {
        // Try to set up the pcap writer if configured.
        let pcap = pcap_options.and_then(|opt| {
            File::create(opt.path.join(format!("{name}.pcap"))).map_or(None, |f| {
                PcapWriter::new(BufWriter::new(f), opt.capture_size_bytes).ok()
            })
        });

        log::debug!(
            "Bringing up network interface '{name}' at '{addr}' using {:?}",
            qdisc
        );

        let queue_kind = match qdisc {
            // A packet fifo is realized using a min-heap over monitonically increasing priority
            // values, which encodes the sequence in which the packets became ready to be sent.
            // A socket's priority is that of its next sendable packet. This is equivalent to a
            // pfifo, and close to the default Linux qdisc.
            // https://tldp.org/HOWTO/Traffic-Control-HOWTO/classless-qdiscs.html
            QDiscMode::Fifo => NetworkQueueKind::MinPriority,
            // We use a round-robin policy to select the next socket, send a packet from that
            // socket, and repeat. We realize this using a fifo queue of sockets that we repeatedly
            // push() and pop(). A better name for this qdisc is probably 'StochasticFairQueuing':
            // https://tldp.org/HOWTO/Traffic-Control-HOWTO/classless-qdiscs.html
            QDiscMode::RoundRobin => NetworkQueueKind::FirstInFirstOut,
        };

        Self {
            _counter: ObjectCounter::new("NetworkInterface"),
            addr,
            send_sockets: RefCell::new(NetworkQueue::new(queue_kind)),
            recv_sockets: RefCell::new(HashMap::new()),
            pcap: RefCell::new(pcap),
            cleanup_in_progress: RefCell::new(false),
        }
    }

    pub fn associate(
        &self,
        socket: &InetSocket,
        protocol: c::ProtocolType,
        port: u16,
        peer: SocketAddrV4,
    ) {
        let key = AssociatedSocketKey::new(protocol, self.addr, port, peer);
        log::trace!("Associating socket key {key:?}");

        if let Entry::Vacant(entry) = self.recv_sockets.borrow_mut().entry(key) {
            entry.insert(socket.clone());
        } else {
            // TODO: Return an error if the association fails.
            debug_panic!("Entry is unexpectedly occupied");
        }
    }

    pub fn disassociate(&self, protocol: c::ProtocolType, port: u16, peer: SocketAddrV4) {
        if *self.cleanup_in_progress.borrow() {
            return;
        }

        let key = AssociatedSocketKey::new(protocol, self.addr, port, peer);
        log::trace!("Disassociating socket key {key:?}");

        // TODO: Return an error if the disassociation fails. Generally the calling code should only
        // try to disassociate a socket if it thinks that the socket is actually associated with
        // this interface, and if it's not, then it's probably an error. But TCP sockets will
        // disassociate all sockets (including ones that have never been associated) and will try to
        // disassociate the same socket multiple times, so we can't just add an assert here.
        if self.recv_sockets.borrow_mut().remove(&key).is_none() {
            warn_once_then_trace!("Attempted to disassociate a vacant socket key");
        }
    }

    pub fn is_addr_in_use(&self, protocol: c::ProtocolType, port: u16, peer: SocketAddrV4) -> bool {
        let key = AssociatedSocketKey::new(protocol, self.addr, port, peer);
        self.recv_sockets.borrow().contains_key(&key)
    }

    // Add the socket to the list of sockets that have data ready for us to send out to the network.
    pub fn add_data_source(&self, socket: &InetSocket) {
        if socket.borrow().has_data_to_send() && !self.send_sockets.borrow().contains(socket) {
            self.send_sockets
                .borrow_mut()
                .push(socket.clone(), socket.borrow().peek_next_packet_priority());
        } else {
            warn_once_then_trace!("Socket wants to send, but no packets available");
        }
    }

    /// Disassociate all bound sockets and remove sockets from the sending queue. This should be
    /// called as part of the host's cleanup procedure.
    pub fn remove_all_sockets(&self) {
        *self.cleanup_in_progress.borrow_mut() = true;
        self.recv_sockets.borrow_mut().clear();
        self.send_sockets.borrow_mut().clear();
        *self.cleanup_in_progress.borrow_mut() = false;
    }

    fn capture_if_configured(&self, packet: &PacketRc) {
        // Avoid double mutable borrow of pcap.
        let mut pcap_borrowed = self.pcap.borrow_mut();

        if let Some(pcap) = pcap_borrowed.as_mut() {
            let now = Worker::current_time().unwrap().to_abs_simtime();

            let ts_sec: u32 = now.as_secs().try_into().unwrap_or(u32::MAX);
            let ts_usec: u32 = now.subsec_micros();
            let packet_len: u32 = packet.total_size().try_into().unwrap_or(u32::MAX);

            if let Err(e) = pcap.write_packet_fmt(ts_sec, ts_usec, packet_len, |writer| {
                packet.display_bytes(writer)
            }) {
                // There was a non-recoverable error.
                log::warn!("Unable to write packet to pcap output: {}", e);
                log::warn!(
                    "Fatal pcap logging error; stopping pcap logging for interface '{}'.",
                    self.addr
                );
                pcap_borrowed.take();
            }
        }
    }
}

impl PacketDevice for NetworkInterface {
    fn get_address(&self) -> Ipv4Addr {
        self.addr
    }

    // Pops a packet from the interface to send over the simulated network.
    fn pop(&self) -> Option<PacketRc> {
        while !self.send_sockets.borrow().is_empty() {
            let Some(socket) = self.send_sockets.borrow_mut().pop() else {
                // TODO: a non-empty queue that doesn't return an item is an error.
                warn_once_then_trace!("A a non-empty queue has no sockets.");
                // Continuing here (as the legacy C code did) could cause an infinite loop.
                return None;
            };

            let Some(mut packet) = CallbackQueue::queue_and_run_with_legacy(|cb_queue| {
                socket.borrow_mut().pull_out_packet(cb_queue)
            }) else {
                // TODO: A sendable socket that does not have packets should be an error.
                warn_once_then_trace!("A sendable socket has no available packets.");
                // But dropping the socket may allow us to recover.
                continue;
            };

            // If socket has more packets, keep tracking it for future sends. Note that it is
            // possible that the socket was already re-added to the send queue above by calling
            // `add_data_source()` during the call to `pull_out_packet()`, so we check contains()
            // before pushing to avoid an AlreadyQueued error.
            if socket.borrow().has_data_to_send() && !self.send_sockets.borrow().contains(&socket) {
                let priority = socket.borrow().peek_next_packet_priority();
                self.send_sockets.borrow_mut().push(socket, priority);
            }

            packet.add_status(PacketStatus::SndInterfaceSent);
            self.capture_if_configured(&packet);

            return Some(packet);
        }
        // We don't have any sockets that have packets right now.
        None
    }

    // Pushes a packet from the simulated network into the interface.
    fn push(&self, mut packet: PacketRc) {
        // The packet is successfully received by this interface.
        packet.add_status(PacketStatus::RcvInterfaceReceived);

        // Record the packet before we process it, otherwise we may send more packets before we
        // record this one and the order will be incorrect.
        self.capture_if_configured(&packet);

        // Find the socket that should process the packet.
        let protocol = packet.protocol();
        let local = packet.dst_address();
        let peer = packet.src_address();
        let key = AssociatedSocketKey::new(protocol, self.addr, local.port(), peer);

        // First check for a socket with the specific association.
        log::trace!("Looking for socket associated with specific key {key:?}");
        let maybe_socket = {
            let associated = self.recv_sockets.borrow();
            associated
                .get(&key)
                .or_else(|| {
                    // Then fall back to checking for the wildcard association.
                    let wildcard = SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, 0);
                    let key = AssociatedSocketKey::new(protocol, self.addr, local.port(), wildcard);
                    log::trace!("Looking for socket associated with general key {key:?}");
                    associated.get(&key)
                })
                // Pushing a packet to the socket may cause the socket to be disassociated and freed
                // and cause our socket pointer to become dangling while we're using it, so we need
                // to temporarily increase its ref count.
                .cloned()
        };

        if let Some(socket) = maybe_socket {
            let recv_time = Worker::current_time().unwrap();
            CallbackQueue::queue_and_run_with_legacy(|cb_queue| {
                socket
                    .borrow_mut()
                    .push_in_packet(packet, cb_queue, recv_time);
            });
        } else {
            packet.add_status(PacketStatus::RcvInterfaceDropped);
        }
    }
}
