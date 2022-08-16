use std::sync::{Arc, Weak};

use log::trace;

use crate::core::support::emulated_time::EmulatedTime;
use crate::core::support::simulation_time::SimulationTime;
use crate::core::work::task::TaskRef;
use crate::core::worker::Worker;
use crate::utility::{Magic, ObjectCounter};
use objgraph::refcell::RootedRefCell;

use super::host::Host;

pub struct Timer {
    magic: Magic<Self>,
    _counter: ObjectCounter,
    // Internals in an Arc so that we can schedule tasks that refer back to it.
    // This is the only persistent strong reference - callbacks use a Weak
    // reference.  i.e. dropping the outer object will drop this field as well;
    // scheduled callbacks with weak references that can't be upgraded become
    // no-ops.
    internal: Arc<RootedRefCell<TimerInternal>>,
}

struct TimerInternal {
    next_expire_time: Option<EmulatedTime>,
    expire_interval: SimulationTime,
    expiration_count: u64,
    next_expire_id: u64,
    min_valid_expire_id: u64,
    on_expire: Box<dyn Fn(&mut Host) + Send + Sync>,
}

impl TimerInternal {
    fn reset(&mut self, next_expire_time: Option<EmulatedTime>, expire_interval: SimulationTime) {
        self.min_valid_expire_id = self.next_expire_id;
        self.expiration_count = 0;
        self.next_expire_time = next_expire_time;
        self.expire_interval = expire_interval;
    }
}

impl Timer {
    /// Create a new Timer that directly executes `on_expire` on
    /// expiration. `on_expire` will cause a panic if it calls mutable methods
    /// of the enclosing Timer.  If it may need to call mutable methods of the
    /// Timer, it should push a new task to the scheduler to do so.
    pub fn new<F: 'static + Fn(&mut Host) + Send + Sync>(host: &Host, on_expire: F) -> Self {
        Self {
            magic: Magic::new(),
            _counter: ObjectCounter::new("Timer"),
            internal: Arc::new(RootedRefCell::new(
                host.root(),
                TimerInternal {
                next_expire_time: None,
                expire_interval: SimulationTime::ZERO,
                expiration_count: 0,
                next_expire_id: 0,
                min_valid_expire_id: 0,
                on_expire: Box::new(on_expire),
            })),
        }
    }

    pub fn expiration_count(&self) -> u64 {
        self.magic.debug_check();
        self.internal.borrow(Host::thread_root_guard()).expiration_count
    }

    pub fn expire_interval(&self) -> SimulationTime {
        self.magic.debug_check();
        self.internal.borrow(Host::thread_root_guard()).expire_interval
    }

    pub fn consume_expiration_count(&mut self) -> u64 {
        self.magic.debug_check();
        let mut internal = self.internal.borrow_mut(Host::thread_root_guard());
        let e = internal.expiration_count;
        internal.expiration_count = 0;
        e
    }

    /// Returns the remaining time until the next expiration if the timer is
    /// armed, or None otherwise.
    pub fn remaining_time(&self) -> Option<SimulationTime> {
        self.magic.debug_check();
        let t = if let Some(t) = self.internal.borrow(Host::thread_root_guard()).next_expire_time {
            t
        } else {
            return None;
        };
        let now = Worker::current_time().unwrap();
        Some(t.saturating_duration_since(&now))
    }

    pub fn interval(&self) -> SimulationTime {
        self.magic.debug_check();
        self.internal.borrow(Host::thread_root_guard()).expire_interval
    }

    pub fn disarm(&mut self) {
        self.magic.debug_check();
        let mut internal = self.internal.borrow_mut(Host::thread_root_guard());
        internal.reset(None, SimulationTime::ZERO);
    }

    fn timer_expire(
        internal_weak: &Weak<RootedRefCell<TimerInternal>>,
        host: &mut Host,
        expire_id: u64,
    ) {
        let internal = if let Some(internal) = Weak::upgrade(internal_weak) {
            internal
        } else {
            trace!("Expired Timer no longer exists.");
            return;
        };
        let mut internal_brw = internal.borrow_mut(Host::thread_root_guard());
        trace!(
            "timer expire check; expireID={} minValidExpireID={}",
            expire_id,
            internal_brw.min_valid_expire_id
        );
        if expire_id < internal_brw.min_valid_expire_id {
            // Cancelled.
            return;
        }

        let next_expire_time = internal_brw.next_expire_time.unwrap();
        if next_expire_time > Worker::current_time().unwrap() {
            // Hasn't expired yet. Check again later.
            Self::schedule_new_expire_event(&mut *internal_brw, internal_weak.clone(), host);
            return;
        }

        internal_brw.expiration_count += 1;
        if internal_brw.expire_interval > SimulationTime::ZERO {
            internal_brw.next_expire_time =
                Some(internal_brw.next_expire_time.unwrap() + internal_brw.expire_interval);
            Self::schedule_new_expire_event(&mut *internal_brw, internal_weak.clone(), host);
        }

        // Re-borrow as an immutable reference while executing the callback.
        drop(internal_brw);
        let internal_brw = internal.borrow(Host::thread_root_guard());
        (internal_brw.on_expire)(host);
    }

    fn schedule_new_expire_event(
        internal_ref: &mut TimerInternal,
        internal_ptr: Weak<RootedRefCell<TimerInternal>>,
        host: &mut Host,
    ) {
        let now = Worker::current_time().unwrap();
        let delay = std::cmp::min(
            internal_ref.next_expire_time.unwrap() - now,
            SimulationTime::SECOND,
        );
        let expire_id = internal_ref.next_expire_id;
        internal_ref.next_expire_id += 1;
        let task = TaskRef::new(move |host| Self::timer_expire(&internal_ptr, host, expire_id));
        host.schedule_task_with_delay(task, delay);
    }

    pub fn arm(
        &mut self,
        host: &mut Host,
        expire_time: EmulatedTime,
        expire_interval: SimulationTime,
    ) {
        self.magic.debug_check();
        debug_assert!(expire_time >= Worker::current_time().unwrap());

        let mut internal = self.internal.borrow_mut(Host::thread_root_guard());
        internal.reset(Some(expire_time), expire_interval);
        Self::schedule_new_expire_event(&mut *internal, Arc::downgrade(&self.internal), host);
    }
}

pub mod export {
    use crate::cshadow;

    use super::*;

    /// Create a new Timer that synchronously executes `task` on expiration.
    /// `task` should not call mutable methods of the enclosing `Timer`; if it needs
    /// to do so it should schedule a new task to do so.
    #[no_mangle]
    pub unsafe extern "C" fn timer_new(host: *mut cshadow::Host, task: *const TaskRef) -> *mut Timer {
        let host = unsafe { Host::borrow_from_c(host) };
        let task = unsafe { task.as_ref() }.unwrap().clone();
        let timer = Timer::new(&host, move |host| task.execute(host));
        Box::into_raw(Box::new(timer))
    }

    #[no_mangle]
    pub unsafe extern "C" fn timer_drop(timer: *mut Timer) {
        unsafe { Box::from_raw(timer) };
    }

    #[no_mangle]
    pub unsafe extern "C" fn timer_getExpirationCount(timer: *const Timer) -> u64 {
        let timer = unsafe { timer.as_ref() }.unwrap();
        timer.expiration_count()
    }

    #[no_mangle]
    pub unsafe extern "C" fn timer_consumeExpirationCount(timer: *mut Timer) -> u64 {
        let timer = unsafe { timer.as_mut() }.unwrap();
        timer.consume_expiration_count()
    }

    /// Returns the remaining time until the next expiration. Returns 0 if the
    /// timer isn't armed.
    #[no_mangle]
    pub unsafe extern "C" fn timer_getRemainingTime(
        timer: *const Timer,
    ) -> cshadow::SimulationTime {
        let timer = unsafe { timer.as_ref() }.unwrap();
        let remaining = if let Some(t) = timer.remaining_time() {
            t
        } else {
            SimulationTime::ZERO
        };
        remaining.into()
    }

    #[no_mangle]
    pub unsafe extern "C" fn timer_getInterval(timer: *const Timer) -> cshadow::SimulationTime {
        let timer = unsafe { timer.as_ref() }.unwrap();
        timer.interval().into()
    }

    #[no_mangle]
    #[allow(non_snake_case)]
    pub unsafe extern "C" fn timer_arm(
        timer: *mut Timer,
        host: *mut cshadow::Host,
        nextExpireTime: cshadow::EmulatedTime,
        expireInterval: cshadow::SimulationTime,
    ) {
        let timer = unsafe { timer.as_mut() }.unwrap();
        let mut host = unsafe { Host::borrow_from_c(host) };
        let nextExpireTime = EmulatedTime::from_c_emutime(nextExpireTime).unwrap();
        let expireInterval = SimulationTime::from_c_simtime(expireInterval).unwrap();
        timer.arm(&mut host, nextExpireTime, expireInterval)
    }

    #[no_mangle]
    pub unsafe extern "C" fn timer_disarm(timer: *mut Timer) {
        let timer = unsafe { timer.as_mut() }.unwrap();
        timer.disarm()
    }
}
