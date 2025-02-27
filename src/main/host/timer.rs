use std::sync::{Arc, Weak};

use atomic_refcell::AtomicRefCell;
use log::trace;
use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::simulation_time::SimulationTime;

use super::host::Host;
use crate::core::work::task::TaskRef;
use crate::core::worker::Worker;
use crate::utility::{Magic, ObjectCounter};

pub struct Timer {
    magic: Magic<Self>,
    _counter: ObjectCounter,
    // Internals in an Arc so that we can schedule tasks that refer back to it.
    // This is the only persistent strong reference - callbacks use a Weak
    // reference.  i.e. dropping the outer object will drop this field as well;
    // scheduled callbacks with weak references that can't be upgraded become
    // no-ops.
    internal: Arc<AtomicRefCell<TimerInternal>>,
}

struct TimerInternal {
    next_expire_time: Option<EmulatedTime>,
    expire_interval: Option<SimulationTime>,
    expiration_count: u64,
    next_expire_id: u64,
    min_valid_expire_id: u64,
    on_expire: Box<dyn Fn(&Host) + Send + Sync>,
}

impl TimerInternal {
    fn reset(
        &mut self,
        next_expire_time: Option<EmulatedTime>,
        expire_interval: Option<SimulationTime>,
    ) {
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
    pub fn new<F: 'static + Fn(&Host) + Send + Sync>(on_expire: F) -> Self {
        Self {
            magic: Magic::new(),
            _counter: ObjectCounter::new("Timer"),
            internal: Arc::new(AtomicRefCell::new(TimerInternal {
                next_expire_time: None,
                expire_interval: None,
                expiration_count: 0,
                next_expire_id: 0,
                min_valid_expire_id: 0,
                on_expire: Box::new(on_expire),
            })),
        }
    }

    /// Returns the number of timer expirations that have occurred since the last time
    /// [`Timer::consume_expiration_count()`] was called without resetting the counter.
    pub fn expiration_count(&self) -> u64 {
        self.magic.debug_check();
        self.internal.borrow().expiration_count
    }

    /// Returns the currently configured timer expiration interval if this timer is configured to
    /// periodically expire, or None if the timer is configured for a one-shot expiration.
    pub fn expire_interval(&self) -> Option<SimulationTime> {
        self.magic.debug_check();
        self.internal.borrow().expire_interval
    }

    /// Returns the number of timer expirations that have occurred since the last time
    /// [`Timer::consume_expiration_count()`] was called and resets the counter to zero.
    pub fn consume_expiration_count(&mut self) -> u64 {
        self.magic.debug_check();
        let mut internal = self.internal.borrow_mut();
        let e = internal.expiration_count;
        internal.expiration_count = 0;
        e
    }

    /// Returns the remaining time until the next expiration if the timer is
    /// armed, or None otherwise.
    pub fn remaining_time(&self) -> Option<SimulationTime> {
        self.magic.debug_check();
        let t = self.internal.borrow().next_expire_time?;
        let now = Worker::current_time().unwrap();
        Some(t.saturating_duration_since(&now))
    }

    /// Deactivate the timer so that it does not issue `on_expire()` callback notifications.
    pub fn disarm(&mut self) {
        self.magic.debug_check();
        let mut internal = self.internal.borrow_mut();
        internal.reset(None, None);
    }

    fn timer_expire(
        internal_weak: &Weak<AtomicRefCell<TimerInternal>>,
        host: &Host,
        expire_id: u64,
    ) {
        let Some(internal) = Weak::upgrade(internal_weak) else {
            trace!("Expired Timer no longer exists.");
            return;
        };

        let mut internal_brw = internal.borrow_mut();
        trace!(
            "timer expire check; expireID={expire_id} minValidExpireID={}",
            internal_brw.min_valid_expire_id
        );

        // The timer may have been canceled/disarmed after we scheduled the callback task.
        if expire_id < internal_brw.min_valid_expire_id {
            // Cancelled.
            return;
        }

        let next_expire_time = internal_brw.next_expire_time.unwrap();
        if next_expire_time > Worker::current_time().unwrap() {
            // Hasn't expired yet. Check again later.
            Self::schedule_new_expire_event(&mut internal_brw, internal_weak.clone(), host);
            return;
        }

        // Now we know it's a valid expiration.
        internal_brw.expiration_count += 1;

        // A timer configured with an interval continues to periodically expire.
        if let Some(interval) = internal_brw.expire_interval {
            // The interval must be positive.
            debug_assert!(interval.is_positive());
            internal_brw.next_expire_time = Some(next_expire_time + interval);
            Self::schedule_new_expire_event(&mut internal_brw, internal_weak.clone(), host);
        } else {
            // Reset next expire time to None, so that `remaining_time`
            // correctly returns `None`, instead of `Some(0)`. (i.e. `Some(0)`
            // should mean that the timer is scheduled to fire now, but the
            // event hasn't executed yet).
            internal_brw.next_expire_time = None;
        }

        // Re-borrow as an immutable reference while executing the callback.
        drop(internal_brw);
        let internal_brw = internal.borrow();
        (internal_brw.on_expire)(host);
    }

    fn schedule_new_expire_event(
        internal_ref: &mut TimerInternal,
        internal_ptr: Weak<AtomicRefCell<TimerInternal>>,
        host: &Host,
    ) {
        let now = Worker::current_time().unwrap();

        // have the timer expire between (1,2] seconds from now, but on a 1-second edge so that all
        // timer events for all hosts will expire at the same times (and therefore in the same
        // scheduling rounds, hopefully improving scheduling parallelization)
        let since_start = now.duration_since(&EmulatedTime::SIMULATION_START);
        let early_expire_time_since_start =
            SimulationTime::from_secs(since_start.as_secs()) + SimulationTime::SECOND * 2;

        let time = std::cmp::min(
            internal_ref.next_expire_time.unwrap(),
            EmulatedTime::SIMULATION_START + early_expire_time_since_start,
        );
        let expire_id = internal_ref.next_expire_id;
        internal_ref.next_expire_id += 1;
        let task = TaskRef::new(move |host| Self::timer_expire(&internal_ptr, host, expire_id));
        host.schedule_task_at_emulated_time(task, time);
    }

    /// Activate the timer so that it starts issuing `on_expire()` callback notifications.
    ///
    /// The `expire_time` instant specifies the next time that the timer will expire and issue an
    /// `on_expire()` notification callback. The `expire_interval` duration is optional: if `Some`,
    /// it configures the timer in periodic mode where it issues `on_expire()` notification
    /// callbacks every interval of time; if `None`, the timer is configured in one-shot mode and
    /// will become disarmed after the first expiration.
    ///
    /// Panics if `expire_time` is in the past or if `expire_interval` is `Some` but not positive.
    pub fn arm(
        &mut self,
        host: &Host,
        expire_time: EmulatedTime,
        expire_interval: Option<SimulationTime>,
    ) {
        self.magic.debug_check();
        debug_assert!(expire_time >= Worker::current_time().unwrap());

        // None is a valid expire interval, but zero is not.
        if let Some(interval) = expire_interval {
            debug_assert!(interval.is_positive());
        }

        let mut internal = self.internal.borrow_mut();
        internal.reset(Some(expire_time), expire_interval);
        Self::schedule_new_expire_event(&mut internal, Arc::downgrade(&self.internal), host);
    }
}

pub mod export {
    use shadow_shim_helper_rs::emulated_time::CEmulatedTime;
    use shadow_shim_helper_rs::simulation_time::CSimulationTime;

    use super::*;

    /// Create a new Timer that synchronously executes `task` on expiration.
    ///
    /// # Safety
    ///
    /// `task` must be dereferenceable, and must not call mutable methods of
    /// the enclosing `Timer`; if it needs to do so it should schedule a new
    /// task to do so.
    #[unsafe(no_mangle)]
    pub unsafe extern "C-unwind" fn timer_new(task: *const TaskRef) -> *mut Timer {
        let task = unsafe { task.as_ref() }.unwrap().clone();
        let timer = Timer::new(move |host| task.execute(host));
        Box::into_raw(Box::new(timer))
    }

    /// # Safety
    ///
    /// `timer` must be safely dereferenceable. Consumes `timer`.
    #[unsafe(no_mangle)]
    pub unsafe extern "C-unwind" fn timer_drop(timer: *mut Timer) {
        drop(unsafe { Box::from_raw(timer) });
    }

    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[unsafe(no_mangle)]
    #[allow(non_snake_case)]
    pub unsafe extern "C-unwind" fn timer_arm(
        timer: *mut Timer,
        host: *const Host,
        nextExpireTime: CEmulatedTime,
        expireInterval: CSimulationTime,
    ) {
        let timer = unsafe { timer.as_mut() }.unwrap();
        let host = unsafe { host.as_ref().unwrap() };
        let nextExpireTime = EmulatedTime::from_c_emutime(nextExpireTime).unwrap();
        let expireInterval = SimulationTime::from_c_simtime(expireInterval).unwrap();
        timer.arm(
            host,
            nextExpireTime,
            expireInterval.is_positive().then_some(expireInterval),
        )
    }

    /// # Safety
    ///
    /// Pointer args must be safely dereferenceable.
    #[unsafe(no_mangle)]
    pub unsafe extern "C-unwind" fn timer_disarm(timer: *mut Timer) {
        let timer = unsafe { timer.as_mut() }.unwrap();
        timer.disarm()
    }
}
