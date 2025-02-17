//! An active queue management (AQM) algorithm implementing CoDel.
//! <https://tools.ietf.org/html/rfc8289>
//!
//!  The "Flow Queue" variant is not implemented.
//!  <https://tools.ietf.org/html/rfc8290>
//!
//!  More info:
//!   - <https://en.wikipedia.org/wiki/CoDel>
//!   - <http://man7.org/linux/man-pages/man8/tc-codel.8.html>
//!   - <https://queue.acm.org/detail.cfm?id=2209336>
//!   - <https://queue.acm.org/appendices/codel.html>

use std::{collections::VecDeque, time::Duration};

use shadow_shim_helper_rs::{emulated_time::EmulatedTime, simulation_time::SimulationTime};

use crate::cshadow as c;
use crate::network::packet::{PacketRc, PacketStatus};

/// The target minimum standing queue delay time, corresponding to the "TARGET"
/// parameter in the RFC. This is recommended to be set to 5 milliseconds in
/// internet routers, but in Shadow we increase it to 10 milliseconds.
const TARGET: SimulationTime = SimulationTime::from_duration(Duration::from_millis(10));

/// The most recent time interval over which the standing delay is computed,
/// corresponding to the "INTERVAL" parameter in the RFC. This is recommended to
/// be set to 100 milliseconds in internet routers.
const INTERVAL: SimulationTime = SimulationTime::from_duration(Duration::from_millis(100));

/// The maximum number of packets we will store, corresponding to the "limit"
/// parameter in the codel man page. This is recommended to be 1000 in internet
/// routers, but in Shadow we don't enforce a limit due to our batched sending.
const LIMIT: usize = usize::MAX;

/// Encodes if CoDel determines that the next available packet can be dropped.
struct CoDelPopItem {
    packet: PacketRc,
    ok_to_drop: bool,
}

/// Represents the possible states of the CoDel algorithm.
#[derive(PartialEq, Debug)]
enum CoDelMode {
    /// Under good conditions, we store and forward packets
    Store,
    /// Under bad conditions, we occasionally drop packets
    Drop,
}

/// An entry in the CoDel queque.
struct CoDelElement {
    packet: PacketRc,
    enqueue_ts: EmulatedTime,
}

/// A packet queue implementing the CoDel active queue management (AQM)
/// algorithm, suitable for use in network routers.
///
/// Currently, the memory capacity of the queue for storing elements is
/// monitonically increasing since we do not shrink the queue's capacity on
/// `pop()` operations. We think this is OK since we only use one queue per
/// host. However, if memory overhead becomes problematic, we can consider
/// occasionally shrinking the queue's capacity or using a backing that is more
/// memory-efficient (e.g. a LinkedList).
pub struct CoDelQueue {
    /// A queue holding packets and insertion times.
    elements: VecDeque<CoDelElement>,
    /// The running sum of the sizes of packets stored in the queue.
    total_bytes_stored: usize,
    /// The state indicating if we are dropping or storing packets.
    mode: CoDelMode,
    /// If Some, this is an interval worth of time after which packet delays
    /// started to exceed the target delay.
    interval_end: Option<EmulatedTime>,
    /// If Some, the next time we should drop a packet.
    drop_next: Option<EmulatedTime>,
    /// The number of packets dropped since entering drop mode.
    current_drop_count: usize,
    /// The number of packets dropped the last time we were in drop mode.
    previous_drop_count: usize,
}

impl CoDelQueue {
    /// Creates a new empty packet queue.
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

    /// Returns the total number of packets stored in the queue.
    #[cfg(test)]
    pub fn len(&self) -> usize {
        self.elements.len()
    }

    /// Returns true if the queue is holding zero packets, false otherwise.
    #[cfg(test)]
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Returns the packet at the front of the queue, or None if the queue is
    /// empty. Note that there is no gurantee that a subsequent `pop()`
    /// operation will return the same packet, since it could be dropped by the
    /// queue between the `peek()` and `pop()` operations.
    #[cfg(test)]
    pub fn peek(&self) -> Option<&PacketRc> {
        self.elements.front().map(|x| &x.packet)
    }

    /// Returns the next packet in the queue that conforms to the CoDel standing
    /// delay requirements, or None if the queue is empty before a conforming
    /// packet is found. The CoDel packet dropping logic is applied during this
    /// operation, which could result in packets being dropped before a packet
    /// that conforms to the standing delay requirements is returned.
    /// Requires the current time as an argument to avoid calling into the
    /// worker module internally.
    pub fn pop(&mut self, now: EmulatedTime) -> Option<PacketRc> {
        let maybe_packet = match self.codel_pop(&now) {
            Some(item) => match item.ok_to_drop {
                true => match self.mode {
                    CoDelMode::Store => self.drop_from_store_mode(&now, item.packet),
                    CoDelMode::Drop => self.drop_from_drop_mode(&now, item.packet),
                },
                false => {
                    // Always set Store mode when standing delay below TARGET.
                    self.mode = CoDelMode::Store;
                    Some(item.packet)
                }
            },
            None => {
                // Always set Store mode when the queue is empty.
                self.mode = CoDelMode::Store;
                None
            }
        };

        maybe_packet.inspect(|p| {
            p.add_status(PacketStatus::RouterDequeued);
        })
    }

    fn drop_from_store_mode(&mut self, now: &EmulatedTime, packet: PacketRc) -> Option<PacketRc> {
        debug_assert_eq!(self.mode, CoDelMode::Store);

        // Drop one packet and move to drop mode.
        self.drop_packet(packet);
        let next_item = self.codel_pop(now);
        self.mode = CoDelMode::Drop;

        // Reset to the drop rate that was known to control the queue.
        let delta = self
            .current_drop_count
            .saturating_sub(self.previous_drop_count);
        self.current_drop_count = match self.was_dropping_recently(now) && delta > 1 {
            true => delta,
            false => 1,
        };
        self.drop_next = Some(CoDelQueue::apply_control_law(now, self.current_drop_count));
        self.previous_drop_count = self.current_drop_count;

        next_item.map(|x| x.packet)
    }

    fn drop_from_drop_mode(&mut self, now: &EmulatedTime, packet: PacketRc) -> Option<PacketRc> {
        debug_assert_eq!(self.mode, CoDelMode::Drop);

        let mut item = Some(CoDelPopItem {
            packet,
            ok_to_drop: true,
        });

        // Drop as many packets as the control law dictates.
        while item.is_some() && self.mode == CoDelMode::Drop && self.should_drop(now) {
            self.drop_packet(item.unwrap().packet);
            self.current_drop_count += 1;

            item = self.codel_pop(now);

            match item.as_ref().is_some_and(|x| x.ok_to_drop) {
                true => {
                    // Set the next drop time based on CoDel control law.
                    // `self.drop_next` is already set in `drop_from_store_mode()`
                    self.drop_next = Some(CoDelQueue::apply_control_law(
                        &self.drop_next.unwrap(),
                        self.current_drop_count,
                    ));
                }
                false => self.mode = CoDelMode::Store,
            }
        }

        item.map(|x| x.packet)
    }

    // Corresponds to the `dodequeue` function in the RFC.
    fn codel_pop(&mut self, now: &EmulatedTime) -> Option<CoDelPopItem> {
        match self.elements.pop_front() {
            Some(element) => {
                // Found a packet.
                debug_assert!(element.packet.len() <= self.total_bytes_stored);
                self.total_bytes_stored =
                    self.total_bytes_stored.saturating_sub(element.packet.len());

                debug_assert!(now >= &element.enqueue_ts);
                let standing_delay = now.saturating_duration_since(&element.enqueue_ts);
                let ok_to_drop = self.process_standing_delay(now, standing_delay);

                Some(CoDelPopItem {
                    packet: element.packet,
                    ok_to_drop,
                })
            }
            None => {
                // Queue is empty, so we cannot be above target.
                self.interval_end = None;
                None
            }
        }
    }

    /// Update our state based on the given standing delay. Returns true if the
    /// packet associated with this delay can be dropped, false otherwise.
    fn process_standing_delay(
        &mut self,
        now: &EmulatedTime,
        standing_delay: SimulationTime,
    ) -> bool {
        if standing_delay < TARGET || self.total_bytes_stored <= c::CONFIG_MTU.try_into().unwrap() {
            // We are in a good state, i.e., below the target delay. We reset
            // the interval expiration, so that we wait for at least one full
            // interval if the delay exceeds the target again.
            self.interval_end = None;
            false
        } else {
            // We are in a bad state, i.e., at or above the target delay.
            match self.interval_end {
                Some(end) => {
                    // We were already in a bad state, and now we stayed in it.

                    // if we have been in a bad state for a full interval worth
                    // of time, drop this packet.
                    now >= &end
                }
                None => {
                    // None means we were in a good state, but now we just
                    // entered a bad state. If we stay in the bad state for a
                    // full interval, we will need to enter drop mode later.
                    // Mark the end of the interval now so we can track it.
                    self.interval_end = Some(now.saturating_add(INTERVAL));
                    false
                }
            }
        }
    }

    /// Returns true if now exceeds our drop_next threshold, false otherwise.
    fn should_drop(&self, now: &EmulatedTime) -> bool {
        match self.drop_next {
            Some(next) => now >= &next,
            None => false, // Have not yet set the drop threshold
        }
    }

    /// Returns true if now is within 16 intervals of drop_next, false otherwise.
    fn was_dropping_recently(&self, now: &EmulatedTime) -> bool {
        match self.drop_next {
            Some(drop_next) => {
                // now < drop_next + interval*16
                now.saturating_duration_since(&drop_next) < INTERVAL.saturating_mul(16)
            }
            None => false, // Have not yet dropped a packet
        }
    }

    /// Apply the CoDel control law using the inverse sqrt of the drop count,
    /// i.e., `time + (INTERVAL / sqrt(count));`.
    fn apply_control_law(time: &EmulatedTime, count: usize) -> EmulatedTime {
        let increment = {
            let interval = INTERVAL.as_nanos_f64();
            let sqrt_count = match count {
                0 => 1f64,
                _ => (count as f64).sqrt(),
            };
            let div = interval / sqrt_count;
            SimulationTime::from_nanos(div.round() as u64)
        };
        let original = time.to_abs_simtime();
        let adjusted = original.saturating_add(increment);
        EmulatedTime::from_abs_simtime(adjusted)
    }

    /// Append a packet to the end of the queue.
    /// Requires the current time as an argument to avoid calling into the
    /// worker module internally.
    pub fn push(&mut self, packet: PacketRc, now: EmulatedTime) {
        if self.elements.len() < LIMIT {
            packet.add_status(PacketStatus::RouterEnqueued);
            self.total_bytes_stored += packet.len();
            self.elements.push_back(CoDelElement {
                packet,
                enqueue_ts: now,
            });
        } else {
            // Section 5.4 in the RFC notes that "packets arriving at a full
            // buffer will be dropped, but these drops are not counted towards
            // CoDel's computations".
            self.drop_packet(packet);
        }
    }

    fn drop_packet(&self, packet: PacketRc) {
        packet.add_status(PacketStatus::RouterDropped);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::network::tests::mock_time_millis;

    // Some of the tests here don't run in miri because they cause c::packet*
    // functions to be called during the test.

    #[test]
    fn empty() {
        let now = mock_time_millis(1000);
        let mut cdq = CoDelQueue::new();
        assert_eq!(cdq.len(), 0);
        assert!(cdq.is_empty());
        assert!(cdq.peek().is_none());
        assert!(cdq.pop(now).is_none());
    }

    #[test]
    #[cfg_attr(miri, ignore)]
    fn push_pop_simple() {
        let now = mock_time_millis(1000);
        let mut cdq = CoDelQueue::new();

        const N: usize = 10;

        for i in 1..=N {
            assert_eq!(cdq.len(), i - 1);
            cdq.push(PacketRc::new_ipv4_udp_mock(), now);
            assert_eq!(cdq.len(), i);
        }
        for i in 1..=N {
            assert_eq!(cdq.len(), N - i + 1);
            assert!(cdq.pop(now).is_some());
            assert_eq!(cdq.len(), N - i);
        }
        assert_eq!(cdq.len(), 0);
        assert!(cdq.is_empty());
        assert!(cdq.pop(now).is_none());
    }

    #[test]
    fn control_law() {
        let now = mock_time_millis(1000);

        // The increment should be a full interval.
        for i in 0..2 {
            assert_eq!(
                CoDelQueue::apply_control_law(&now, i).duration_since(&now),
                INTERVAL
            );
        }

        // The increment should reduce exponentially.
        for i in 2..20 {
            assert_eq!(
                CoDelQueue::apply_control_law(&now, i).duration_since(&now),
                SimulationTime::from_nanos(
                    (INTERVAL.as_nanos_f64() / (i as f64).sqrt()).round() as u64
                )
            );
        }
    }

    #[test]
    #[cfg_attr(miri, ignore)]
    fn interval() {
        let one = SimulationTime::try_from_millis(1).unwrap();

        let start = mock_time_millis(1000);

        let mut cdq = CoDelQueue::new();
        for _ in 0..5 {
            cdq.push(PacketRc::new_ipv4_udp_mock(), start);
        }
        assert!(cdq.total_bytes_stored > c::CONFIG_MTU.try_into().unwrap());

        // Standing delays must remain above the TARGET for INTERVAL amount of
        // time in order to enter the drop state.

        // Not above target so interval is not set.
        assert!(cdq.interval_end.is_none());
        let now = start + TARGET - one;
        assert!(!cdq.process_standing_delay(&now, TARGET - one));
        assert!(cdq.interval_end.is_none());

        // Reached target, interval is set but still not ok to drop.
        let now = start + TARGET;
        assert!(!cdq.process_standing_delay(&now, TARGET));
        assert!(cdq.interval_end.is_some());
        assert_eq!(cdq.interval_end.unwrap(), start + TARGET + INTERVAL);

        // Now we exceed target+interval, so ok to drop.
        let now = start + TARGET + INTERVAL;
        assert!(cdq.process_standing_delay(&now, TARGET + INTERVAL));
        assert!(cdq.interval_end.is_some());
        assert_eq!(cdq.interval_end.unwrap(), start + TARGET + INTERVAL);

        let now = start + TARGET + INTERVAL * 2u32;
        assert!(cdq.process_standing_delay(&now, TARGET + INTERVAL * 2u32));
        assert!(cdq.interval_end.is_some());
        assert_eq!(cdq.interval_end.unwrap(), start + TARGET + INTERVAL);

        // Delay back to low, interval resets, not ok to drop.
        let now = start + TARGET + INTERVAL * 2u32;
        assert!(!cdq.process_standing_delay(&now, one));
        assert!(cdq.interval_end.is_none());
    }

    #[test]
    #[cfg_attr(miri, ignore)]
    fn mode() {
        let one = SimulationTime::try_from_millis(1).unwrap();

        let start = mock_time_millis(1000);

        let mut cdq = CoDelQueue::new();
        const N: usize = 6;
        for _ in 0..N {
            cdq.push(PacketRc::new_ipv4_udp_mock(), start);
        }
        assert!(cdq.total_bytes_stored > c::CONFIG_MTU.try_into().unwrap());
        assert_eq!(cdq.len(), N);
        assert_eq!(cdq.mode, CoDelMode::Store);

        // Standing delays must remain above the TARGET for INTERVAL amount of
        // time in order to enter the drop state.

        // We didn't reach target yet.
        cdq.pop(start + TARGET - one);
        assert_eq!(cdq.len(), N - 1);
        assert_eq!(cdq.mode, CoDelMode::Store);

        // We now reached target.
        cdq.pop(start + TARGET);
        assert_eq!(cdq.len(), N - 2);
        assert_eq!(cdq.mode, CoDelMode::Store);

        // Still not above target for a full interval.
        cdq.pop(start + TARGET + INTERVAL - one);
        assert_eq!(cdq.len(), N - 3);
        assert_eq!(cdq.mode, CoDelMode::Store);

        // Now above target for interval, should enter drop mode and drop one packet.
        cdq.pop(start + TARGET + INTERVAL);
        assert_eq!(cdq.len(), N - 5);
        assert_eq!(cdq.mode, CoDelMode::Drop);

        // Now if we wait another interval, we get another drop and then
        // low-delay packets should put us back into store mode.
        for _ in 0..3 {
            // Add some low-delay packets
            cdq.push(
                PacketRc::new_ipv4_udp_mock(),
                start + TARGET + INTERVAL * 2u32 - one,
            );
        }
        cdq.pop(start + TARGET + INTERVAL * 2u32);
        assert_eq!(cdq.mode, CoDelMode::Store);
    }

    #[test]
    fn drop_empty() {
        let start = mock_time_millis(1000);
        let mut cdq = CoDelQueue::new();
        cdq.mode = CoDelMode::Drop;
        cdq.pop(start);
        assert_eq!(cdq.mode, CoDelMode::Store);
    }

    #[test]
    #[cfg_attr(miri, ignore)]
    fn drop_many() {
        let start = mock_time_millis(1000);
        let end = mock_time_millis(1000000);

        let mut cdq = CoDelQueue::new();
        const N: usize = 20;
        for _ in 0..N {
            cdq.push(PacketRc::new_ipv4_udp_mock(), start);
        }
        assert_eq!(cdq.len(), N);

        // Start in Store mode.
        assert_eq!(cdq.mode, CoDelMode::Store);

        // Sets the interval.
        cdq.pop(start + TARGET);
        assert_eq!(cdq.len(), N - 1);
        assert_eq!(cdq.current_drop_count, 0);
        assert_eq!(cdq.previous_drop_count, 0);
        assert!(!cdq.was_dropping_recently(&(start + TARGET)));
        assert_eq!(cdq.mode, CoDelMode::Store);

        // Enters Drop mode, drops 1 packet and sets drop_next
        cdq.pop(start + TARGET + INTERVAL);
        assert_eq!(cdq.len(), N - 3);
        assert_eq!(cdq.current_drop_count, 1);
        assert_eq!(cdq.previous_drop_count, 1);
        assert!(cdq.drop_next.is_some());
        assert!(cdq.was_dropping_recently(&(start + TARGET + INTERVAL)));
        assert_eq!(cdq.mode, CoDelMode::Drop);

        // In Drop mode, we have repeated drops, but then it leaves Drop mode
        // before the queue is empty.
        assert!(cdq.should_drop(&end));
        cdq.pop(end);
        assert_eq!(cdq.len(), 1);
        assert_eq!(cdq.current_drop_count, N - 4);
        assert_eq!(cdq.mode, CoDelMode::Store);
    }
}
