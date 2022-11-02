use crate::host::host::Host;
use crate::utility::{Magic, ObjectCounter};
use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::HostId;

use super::task::TaskRef;

#[derive(Debug)]
pub struct Event {
    magic: Magic<Self>,
    task: TaskRef,
    time: EmulatedTime,
    src_host_id: HostId,
    dst_host_id: HostId,
    src_host_event_id: u64,
    _counter: ObjectCounter,
}

impl Event {
    pub fn new(task: TaskRef, time: EmulatedTime, src_host: &Host, dst_host_id: HostId) -> Self {
        Self {
            magic: Magic::new(),
            task,
            time,
            src_host_id: src_host.id(),
            dst_host_id,
            src_host_event_id: src_host.get_new_event_id(),
            _counter: ObjectCounter::new("Event"),
        }
    }

    pub fn execute(self, host: &Host) {
        self.magic.debug_check();

        // make sure we're executing on the correct host
        assert_eq!(self.host_id(), host.id());

        host.continue_execution_timer();
        self.task.execute(host);
        host.stop_execution_timer();
    }

    pub fn time(&self) -> EmulatedTime {
        self.magic.debug_check();
        self.time
    }

    pub fn host_id(&self) -> HostId {
        self.magic.debug_check();
        self.dst_host_id
    }

    pub fn set_time(&mut self, time: EmulatedTime) {
        self.magic.debug_check();
        self.time = time;
    }
}

impl PartialEq for Event {
    fn eq(&self, other: &Self) -> bool {
        self.magic.debug_check();
        other.magic.debug_check();

        // check every field except '_counter'
        self.task == other.task
            && self.time == other.time
            && self.src_host_id == other.src_host_id
            && self.dst_host_id == other.dst_host_id
            && self.src_host_event_id == other.src_host_event_id
    }
}

impl Eq for Event {}

impl PartialOrd for Event {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        self.magic.debug_check();
        other.magic.debug_check();

        // sort by event time first, then use other fields we're able to compare
        let cmp = self
            .time
            .cmp(&other.time)
            .then_with(|| self.dst_host_id.cmp(&other.dst_host_id))
            .then_with(|| self.src_host_id.cmp(&other.src_host_id))
            .then_with(|| self.src_host_event_id.cmp(&other.src_host_event_id));

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

mod export {
    use super::*;

    use shadow_shim_helper_rs::simulation_time::{CSimulationTime, SimulationTime};

    #[no_mangle]
    pub unsafe extern "C" fn event_new(
        task_ref: *mut TaskRef,
        time: CSimulationTime,
        src_host: *const Host,
        dst_host_id: HostId,
    ) -> *mut Event {
        let task_ref = unsafe { task_ref.as_mut() }.unwrap();
        let src_host = unsafe { src_host.as_ref().unwrap() };
        let time = EmulatedTime::from_abs_simtime(SimulationTime::from_c_simtime(time).unwrap());

        Box::into_raw(Box::new(Event::new(
            task_ref.clone(),
            time,
            src_host,
            dst_host_id,
        )))
    }

    #[no_mangle]
    pub unsafe extern "C" fn event_free(event: *mut Event) {
        assert!(!event.is_null());
        unsafe { Box::from_raw(event) };
    }

    /// Execute the event. **This frees the event.**
    #[no_mangle]
    pub unsafe extern "C" fn event_executeAndFree(event: *mut Event, host: *const Host) {
        assert!(!event.is_null());
        let event = unsafe { Box::from_raw(event) };
        let host = unsafe { host.as_ref().unwrap() };

        event.execute(host);
    }

    #[no_mangle]
    pub unsafe extern "C" fn event_getHostID(event: *mut Event) -> HostId {
        let event = unsafe { event.as_ref() }.unwrap();
        event.host_id()
    }

    #[no_mangle]
    pub unsafe extern "C" fn event_getTime(event: *const Event) -> CSimulationTime {
        let event = unsafe { event.as_ref() }.unwrap();
        SimulationTime::to_c_simtime(Some(event.time().to_abs_simtime()))
    }

    #[no_mangle]
    pub unsafe extern "C" fn event_setTime(event: *mut Event, time: CSimulationTime) {
        let event = unsafe { event.as_mut() }.unwrap();
        let time = EmulatedTime::from_abs_simtime(SimulationTime::from_c_simtime(time).unwrap());

        event.set_time(time);
    }
}
