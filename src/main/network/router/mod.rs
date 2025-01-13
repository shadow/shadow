use std::cell::RefCell;
use std::net::Ipv4Addr;

use self::codel_queue::CoDelQueue;
use crate::core::worker::Worker;
use crate::network::packet::PacketRc;
use crate::network::PacketDevice;
use crate::utility::{Magic, ObjectCounter};
mod codel_queue;

use shadow_shim_helper_rs::emulated_time::EmulatedTime;

/// A router assists with moving packets between hosts across the simulated
/// network.
pub struct Router {
    magic: Magic<Self>,
    _counter: ObjectCounter,
    address: Ipv4Addr,
    /// Packets inbound to the host from the simulated network.
    inbound_packets: RefCell<CoDelQueue>,
}

impl Router {
    /// Create a new router for a host that will help route packets between it
    /// and other hosts. The `address` must uniquely identify this router to the
    /// host that owns it.
    pub fn new(address: Ipv4Addr) -> Router {
        Router {
            magic: Magic::new(),
            address,
            _counter: ObjectCounter::new("Router"),
            inbound_packets: RefCell::new(CoDelQueue::new()),
        }
    }

    fn push_inner(&self, packet: PacketRc, now: EmulatedTime) {
        self.magic.debug_check();
        self.inbound_packets.borrow_mut().push(packet, now);
    }

    fn pop_inner(&self, now: EmulatedTime) -> Option<PacketRc> {
        self.magic.debug_check();
        self.inbound_packets.borrow_mut().pop(now)
    }

    /// Routes the packet from the source host through the virtual internet to
    /// the destination host.
    fn route_outgoing_packet(&self, packet: PacketRc) {
        // TODO: move Worker::send_packet to here?
        Worker::with_active_host(|src_host| Worker::send_packet(src_host, packet)).unwrap();
    }

    /// Routes the packet from the virtual internet into our CoDel queue, which
    /// can then be received by the destiantion host by calling pop().
    pub fn route_incoming_packet(&self, packet: PacketRc) {
        self.push_inner(packet, Worker::current_time().unwrap())
    }
}

impl PacketDevice for Router {
    fn get_address(&self) -> Ipv4Addr {
        self.address
    }

    fn pop(&self) -> Option<PacketRc> {
        // When the host calls pop, we provide the next packet from the CoDel queue.
        self.pop_inner(Worker::current_time().unwrap())
    }

    fn push(&self, packet: PacketRc) {
        // When the host calls push, we send to the virtual internet.
        self.route_outgoing_packet(packet);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::network::tests::mock_time_millis;

    #[test]
    fn empty() {
        let now = mock_time_millis(1000);
        let router = Router::new(Ipv4Addr::UNSPECIFIED);
        assert!(router.inbound_packets.borrow().peek().is_none());
        assert!(router.pop_inner(now).is_none());
    }

    #[test]
    // Ignore in miri for use of c::packet* functions.
    #[cfg_attr(miri, ignore)]
    fn push_pop_simple() {
        let now = mock_time_millis(1000);
        let router = Router::new(Ipv4Addr::UNSPECIFIED);

        const N: usize = 10;

        for _ in 1..=N {
            router.push_inner(PacketRc::mock_new(), now);
            assert!(router.inbound_packets.borrow().peek().is_some());
        }
        for _ in 1..=N {
            assert!(router.inbound_packets.borrow().peek().is_some());
            assert!(router.pop_inner(now).is_some());
        }

        assert!(router.inbound_packets.borrow().peek().is_none());
        assert!(router.pop_inner(now).is_none());
    }
}
