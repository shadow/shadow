use crate::core::worker::Worker;
use crate::cshadow as c;
use crate::host::host::Host;
use crate::network::packet::Packet;
use crate::utility::{Magic, ObjectCounter};

use self::codel_queue::CoDelQueue;
mod codel_queue;

use shadow_shim_helper_rs::emulated_time::EmulatedTime;

// temporary re-export of the C wrapper function
pub use export::router_enqueue;

/// A router assists with moving packets between hosts across the simulated
/// network.
pub struct Router {
    magic: Magic<Self>,
    _counter: ObjectCounter,
    /// Packets inbound to the host from the simulated network.
    inbound_packets: CoDelQueue,
}

impl Router {
    #[allow(clippy::new_without_default)]
    pub fn new() -> Router {
        Router {
            magic: Magic::new(),
            _counter: ObjectCounter::new("Router"),
            inbound_packets: CoDelQueue::new(),
        }
    }

    // Return true if the router changed from empty to non-empty.
    // TODO: This will eventually not return anything once we have
    // PacketDevice signaling implemented in rust.
    // Rob: Coming in future PR.
    pub fn push(&mut self, packet: Packet) -> bool {
        self.push_inner(packet, Worker::current_time().unwrap())
    }

    fn push_inner(&mut self, packet: Packet, now: EmulatedTime) -> bool {
        self.magic.debug_check();
        let was_empty = self.inbound_packets.is_empty();
        self.inbound_packets.push(packet, now);
        was_empty && !self.inbound_packets.is_empty()
    }

    pub fn peek(&self) -> Option<&Packet> {
        self.magic.debug_check();
        self.inbound_packets.peek()
    }

    pub fn pop(&mut self) -> Option<Packet> {
        self.pop_inner(Worker::current_time().unwrap())
    }

    fn pop_inner(&mut self, now: EmulatedTime) -> Option<Packet> {
        self.magic.debug_check();
        self.inbound_packets.pop(now)
    }

    fn _route_outgoing_packet(_src_host: &Host, _packet: Packet) {
        // TODO: move worker_sendPacket to here.
        // Rob: Coming in future PR.
        todo!()
    }

    fn _route_incoming_packet(_dst_host: &Host, _packet: Packet) {
        // TODO: move _worker_runDeliverPacketTask to here.
        // Rob: Coming in future PR.
        todo!()
    }
}

mod export {
    use super::*;

    #[no_mangle]
    pub extern "C" fn router_new() -> *mut Router {
        Box::into_raw(Box::new(Router::new()))
    }

    /// The returned `c::Packet` must not live longer than the next time the router is modified;
    /// when the router is modified, the returned packet pointer becomes invalid/dangling.
    #[no_mangle]
    pub extern "C" fn router_peek(router_ptr: *const Router) -> *const c::Packet {
        let router = unsafe { router_ptr.as_ref() }.unwrap();
        match router.peek() {
            Some(packet) => packet.borrow_inner(),
            None => std::ptr::null(),
        }
    }

    #[no_mangle]
    pub extern "C" fn router_dequeue(router_ptr: *mut Router) -> *mut c::Packet {
        let router = unsafe { router_ptr.as_mut() }.unwrap();
        match router.pop() {
            Some(packet) => packet.into_inner(),
            None => std::ptr::null_mut(),
        }
    }

    /// Ownership of the `c::Packet` passed to this function transfers to the router. The caller
    /// should not use the packet after calling this function, and should not call `packet_unref`.
    #[no_mangle]
    pub unsafe extern "C" fn router_enqueue(
        router_ptr: *mut Router,
        packet_ptr: *mut c::Packet,
    ) -> bool {
        let router = unsafe { router_ptr.as_mut() }.unwrap();
        let packet = Packet::from_raw(packet_ptr);
        router.push(packet)
    }

    #[no_mangle]
    pub extern "C" fn router_free(router_ptr: *mut Router) {
        if router_ptr.is_null() {
            return;
        }
        unsafe {
            drop(Box::from_raw(router_ptr));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::network::tests::mock_time_millis;

    #[test]
    fn empty() {
        let now = mock_time_millis(1000);
        let mut router = Router::new();
        assert!(router.peek().is_none());
        assert!(router.pop_inner(now).is_none());
    }

    #[test]
    // Ignore in miri for use of c::packet* functions.
    #[cfg_attr(miri, ignore)]
    fn push_pop_simple() {
        let now = mock_time_millis(1000);
        let mut router = Router::new();

        const N: usize = 10;

        for i in 1..=N {
            assert_eq!(router.push_inner(Packet::mock_new(), now), i == 1);
            assert!(router.peek().is_some());
        }
        for _ in 1..=N {
            assert!(router.peek().is_some());
            assert!(router.pop_inner(now).is_some());
        }

        assert!(router.peek().is_none());
        assert!(router.pop_inner(now).is_none());
    }
}
