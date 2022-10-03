use crate::cshadow as c;
use crate::host::host::Host;
use crate::utility::{Magic, ObjectCounter};

use self::codel_queue::CoDelQueue;
use super::packet::Packet;

mod codel_queue;

pub struct Router {
    magic: Magic<Self>,
    _counter: ObjectCounter,
    inbound_packets: CoDelQueue,
}

impl Router {
    fn new() -> Router {
        Router {
            magic: Magic::new(),
            _counter: ObjectCounter::new("Router"),
            inbound_packets: CoDelQueue::new(),
        }
    }

    // Return true if the router changed from empty to non-empty.
    fn push(&mut self, packet: Packet) -> bool {
        self.magic.debug_check();
        let was_empty = self.inbound_packets.len() == 0;
        self.inbound_packets.push(packet);
        was_empty && self.inbound_packets.len() > 0
    }

    fn peek(&self) -> Option<&Packet> {
        self.magic.debug_check();
        self.inbound_packets.peek()
    }

    fn pop(&mut self) -> Option<Packet> {
        self.magic.debug_check();
        self.inbound_packets.pop()
    }

    fn _route_outgoing_packet(_src_host: &Host, _packet: Packet) {
        // TODO: move worker_sendPacket to here
        todo!()
    }

    fn _route_incoming_packet(_dst_host: &Host, _packet: Packet) {
        // TODO: move _worker_runDeliverPacketTask to here
        todo!()
    }
}

mod export {
    use super::*;

    #[no_mangle]
    pub extern "C" fn router_new() -> *mut Router {
        Box::into_raw(Box::new(Router::new()))
    }

    #[no_mangle]
    pub extern "C" fn router_peek(router_ptr: *mut Router) -> *mut c::Packet {
        let router = unsafe { router_ptr.as_mut() }.unwrap();
        match router.peek() {
            Some(packet) => packet.borrow_inner(),
            None => std::ptr::null_mut(),
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

    #[no_mangle]
    pub extern "C" fn router_enqueue(router_ptr: *mut Router, packet_ptr: *mut c::Packet) -> bool {
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

    #[test]
    fn empty() {
        let mut router = Router::new();
        assert!(router.pop().is_none());
        assert!(router.peek().is_none());
    }
}
