use shadow_shim_helper_rs::HostId;
use shadow_shim_helper_rs::emulated_time::EmulatedTime;

use super::task::TaskRef;
use crate::host::host::Host;
use crate::network::packet::PacketRc;
use crate::utility::{Magic, ObjectCounter};

#[derive(Debug)]
pub struct Event {
    magic: Magic<Self>,
    time: EmulatedTime,
    data: EventData,
    _counter: ObjectCounter,
}

impl Event {
    /// A new packet event, which is an event for packets arriving from the Internet. Packet events
    /// do not include packets on localhost.
    pub fn new_packet(packet: PacketRc, time: EmulatedTime, src_host: &Host) -> Self {
        Self {
            magic: Magic::new(),
            time,
            data: EventData::Packet(PacketEventData {
                packet,
                src_host_id: src_host.id(),
                src_host_event_id: src_host.get_new_event_id(),
            }),
            _counter: ObjectCounter::new("Event"),
        }
    }

    /// A new local event, which is an event that was generated locally by the host itself (timers,
    /// localhost packets, etc).
    pub fn new_local(task: TaskRef, time: EmulatedTime, host: &Host) -> Self {
        Self {
            magic: Magic::new(),
            time,
            data: EventData::Local(LocalEventData {
                task,
                event_id: host.get_new_event_id(),
            }),
            _counter: ObjectCounter::new("Event"),
        }
    }

    pub fn time(&self) -> EmulatedTime {
        self.magic.debug_check();
        self.time
    }

    pub fn set_time(&mut self, time: EmulatedTime) {
        self.magic.debug_check();
        self.time = time;
    }

    /// The event data.
    pub fn data(self) -> EventData {
        self.magic.debug_check();
        self.data
    }
}

impl PartialEq for Event {
    fn eq(&self, other: &Self) -> bool {
        self.magic.debug_check();
        other.magic.debug_check();

        fn check_impl_eq(_: impl Eq) {}

        // below we impl Eq for Event, so we should make sure that all of our comparisons below are
        // also Eq
        check_impl_eq(self.time);
        check_impl_eq(&self.data);

        // check every field except '_counter'
        self.time == other.time && self.data == other.data
    }
}

// we checked above that Event's `PartialEq` impl is also `Eq`
impl Eq for Event {}

impl PartialOrd for Event {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        self.magic.debug_check();
        other.magic.debug_check();

        // sort by event time, then by the event data
        let cmp = self.time.cmp(&other.time);

        if cmp.is_ne() {
            Some(cmp)
        } else {
            // event times were equal
            self.data.partial_cmp(&other.data)
        }
    }
}

/// Data for an event. Different event types will contain different data.
#[derive(Debug, PartialEq, Eq, PartialOrd)]
pub enum EventData {
    // IMPORTANT: The order of these enum variants is important and deliberate. The `PartialOrd`
    // derive affects the order of events in the event queue, and therefore which events are
    // processed before others (packet events will be processed before local events), and changing
    // this could significantly affect the simulation, possibly leading to incorrect behaviour.
    Packet(PacketEventData),
    Local(LocalEventData),
}

#[derive(Debug, PartialEq, Eq)]
pub struct PacketEventData {
    packet: PacketRc,
    src_host_id: HostId,
    src_host_event_id: u64,
}

#[derive(Debug, PartialEq, Eq)]
pub struct LocalEventData {
    task: TaskRef,
    event_id: u64,
}

impl From<PacketEventData> for PacketRc {
    fn from(data: PacketEventData) -> Self {
        data.packet
    }
}

impl PartialOrd for PacketEventData {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        // sort by src host ID, then by event ID
        let cmp = self
            .src_host_id
            .cmp(&other.src_host_id)
            .then_with(|| self.src_host_event_id.cmp(&other.src_host_event_id));

        // if the above fields were all equal (this should ideally not occur in practice since it
        // leads to non-determinism, but we handle it anyways)
        if cmp == std::cmp::Ordering::Equal {
            if self.packet != other.packet {
                // packets are not equal, so the events must not be equal
                assert_ne!(self, other);
                // we have nothing left to order them by
                return None;
            }

            // packets are equal, so the events must be equal
            assert_eq!(self, other);
        }

        Some(cmp)
    }
}

impl From<LocalEventData> for TaskRef {
    fn from(data: LocalEventData) -> Self {
        data.task
    }
}

impl PartialOrd for LocalEventData {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        // they are local events and should be on the same host, so we can just sort by event ID
        let cmp = self.event_id.cmp(&other.event_id);

        // if the above fields were all equal (this should ideally not occur in practice since it
        // leads to non-determinism, but we handle it anyways)
        if cmp == std::cmp::Ordering::Equal {
            if self.task != other.task {
                // tasks are not equal, so the events must not be equal
                assert_ne!(self, other);
                // we have nothing left to order them by
                return None;
            }

            // tasks are equal, so the events must be equal
            assert_eq!(self, other);
        }

        Some(cmp)
    }
}
