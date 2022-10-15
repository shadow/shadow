//! An active queue management (AQM) algorithm implementing CoDel.
//! https://tools.ietf.org/html/rfc8289
//!
//!  The "Flow Queue" variant is not implemented.
//!  https://tools.ietf.org/html/rfc8290
//!
//!  More info:
//!   - https://en.wikipedia.org/wiki/CoDel
//!   - http://man7.org/linux/man-pages/man8/tc-codel.8.html
//!   - https://queue.acm.org/detail.cfm?id=2209336
//!   - https://queue.acm.org/appendices/codel.html

use std::collections::VecDeque;
use std::time::Duration;

use super::Packet;
use crate::core::worker::Worker;
use shadow_shim_helper_rs::emulated_time::EmulatedTime;

/// The target minimum standing queue delay time, corresponding to the "TARGET"
/// parameter in the RFC. This is recommended to be set to 5 milliseconds in
/// internet routers, but in Shadow we increase it to 10 milliseconds.
const TARGET: Duration = Duration::from_millis(10);

/// The most recent time interval over which the standing delay is computed,
/// corresponding to the "INTERVAL" parameter in the RFC. This is recommended to
/// be set to 100 milliseconds in internet routers.
const INTERVAL: Duration = Duration::from_millis(100);

/// The maximum number of packets we will store, corresponding to the "limit"
/// parameter in the codel man page. This is recommended to be 1000 in internet
/// routers, but in Shadow we don't enforce a limit due to our batched sending.
const LIMIT: usize = usize::MAX;

/// Represents the possible states of the CoDel algorithm.
enum CoDelMode {
    Store, // under good conditions, we store and forward packets
    Drop,  // under bad conditions, we occasionally drop packets
}

/// An entry in the CoDel quque.
struct CoDelElement {
    packet: Packet,
    enqueue_ts: EmulatedTime,
}

/// A packet queue implementing the CoDel active queue management (AQM)
/// algorithm, suitable for use in network routers.
pub struct CoDelQueue {
    // A queue holding packets and insertion times.
    elements: VecDeque<CoDelElement>,
    // The running sum of the sizes of packets stored in the queue.
    total_bytes_stored: usize,
    // The state indicating if we are dropping or storing packets.
    mode: CoDelMode,
    // If Some, this is an interval worth of time after which packet delays
    // started to exceed the target delay.
    interval_end: Option<EmulatedTime>,
    // If Some, the next time we should drop a packet.
    drop_next: Option<EmulatedTime>,
    // The number of packets dropped since entering drop mode.
    current_drop_count: usize,
    // The number of packets dropped the last time we were in drop mode.
    previous_drop_count: usize,
}

impl CoDelQueue {
    pub fn new() -> CoDelQueue {
        CoDelQueue {
            elements: VecDeque::new(),
            total_bytes_stored: 0,
            mode: CoDelMode::Store,
            interval_end: None,
            drop_next: None,
            current_drop_count: 0,
            previous_drop_count: 0,
        }
    }

    pub fn len(&self) -> usize {
        self.elements.len()
    }

    pub fn peek(&self) -> Option<&Packet> {
        match self.elements.front() {
            Some(element) => Some(&element.packet),
            None => None,
        }
    }

    pub fn pop(&mut self) -> Option<Packet> {
        match self.elements.pop_front() {
            Some(element) => Some(element.packet),
            None => None,
        }
    }

    pub fn push(&mut self, packet: Packet) {
        if self.elements.len() < LIMIT {
            self.total_bytes_stored += packet.size();
            self.elements.push_back(CoDelElement {
                packet,
                enqueue_ts: Worker::current_time().unwrap(),
            });
        } else {
            // Section 5.4 in the RFC notes that "packets arriving at a full
            // buffer will be dropped, but these drops are not counted towards
            // CoDel's computations".
            self.drop(packet);
        }
    }

    fn drop(&self, packet: Packet) {
        // packet_addDeliveryStatus(packet, PDS_ROUTER_DROPPED);
        // #ifdef DEBUG
        //     gchar* pString = packet_toString(packet);
        //     trace("Router dropped packet %s", pString);
        //     g_free(pString);
        // #endif
        //     packet_unref(packet);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test() {}
}
