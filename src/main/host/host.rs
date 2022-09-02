use once_cell::unsync::OnceCell;
use std::net::IpAddr;
use std::sync::Arc;

use crate::core::support::emulated_time::EmulatedTime;
use crate::core::support::simulation_time::SimulationTime;
use crate::core::work::event_queue::ThreadSafeEventQueue;
use crate::core::work::task::TaskRef;
use crate::cshadow;
use crate::host::descriptor::socket::abstract_unix_ns::AbstractUnixNamespace;
use crate::utility::SyncSendPointer;

use atomic_refcell::AtomicRefCell;

#[repr(transparent)]
#[derive(Debug, PartialEq, Eq, PartialOrd, Ord, Hash, Copy, Clone)]
pub struct HostId(cshadow::HostId);

impl From<cshadow::HostId> for HostId {
    fn from(val: cshadow::HostId) -> Self {
        HostId(val)
    }
}

impl From<HostId> for cshadow::HostId {
    fn from(val: HostId) -> Self {
        val.0
    }
}

/// Immutable information about the Host.
#[derive(Debug, Clone)]
pub struct HostInfo {
    pub id: HostId,
    pub name: String,
    pub default_ip: IpAddr,
    pub log_level: Option<log::LevelFilter>,
}

/// A simulated Host.
///
/// This is currently an ephemeral proxy object a C Host (cshadow::Host).
/// Eventually cshadow::Host's contents and functionality will be migrated into
/// there though, and this will become the "real" Host object.
#[derive(Debug)]
pub struct Host {
    chost: SyncSendPointer<cshadow::Host>,

    // Store immutable info in an Arc, that we can safely clone into the Worker
    // and ShadowLogger.
    //
    // Created on-demand for now, to avoid building unnecessarily for ephemeral
    // Host objects.
    //
    // TODO: Consider caching a couple copies that we can "lend" out by value,
    // without having to do any atomic operation?
    info: OnceCell<Arc<HostInfo>>,

    // The C host has a lock so it could be thought of as Sync, but we may want to remove the host
    // lock entirely in the future, which would make the host !Sync. If we decide to keep the host
    // lock in the future, we can remove this and make the host Sync. Since Cell is !Sync, this will
    // make Host !Sync.
    _make_unsync: std::marker::PhantomData<std::cell::Cell<()>>,
}

impl Host {
    /// For now, this should only be called via Worker, to borrow the current
    /// Host. This will ensure there is only one reference to a given Host
    /// in Rust.
    ///
    /// SAFETY: `p` must point to a valid c::Host, to which this Host will
    /// have exclusive access over its lifetime. `p` must outlive the returned object.
    pub unsafe fn borrow_from_c(p: *mut cshadow::Host) -> Self {
        assert!(!p.is_null());
        Host {
            chost: unsafe { SyncSendPointer::new(p) },
            info: OnceCell::new(),
            _make_unsync: Default::default(),
        }
    }

    /// Information about the Host. Made available as an Arc for cheap cloning
    /// into, e.g. Worker and ShadowLogger. When there's no need to clone the
    /// Arc, generally prefer the top-level `Host` methods for accessing this
    /// information, which are likely to be more stable.
    pub fn info(&self) -> &Arc<HostInfo> {
        self.info.get_or_init(|| {
            Arc::new(HostInfo {
                id: self.id(),
                name: self.name().into(),
                default_ip: self.default_ip(),
                log_level: self.log_level(),
            })
        })
    }

    pub fn id(&self) -> HostId {
        HostId(unsafe { cshadow::host_getID(self.chost()) })
    }

    pub fn name(&self) -> &str {
        let slice = unsafe { std::ffi::CStr::from_ptr(cshadow::host_getName(self.chost())) };
        slice.to_str().unwrap()
    }

    pub fn default_ip(&self) -> std::net::IpAddr {
        use std::net;
        let addr = unsafe { cshadow::host_getDefaultIP(self.chost()) };
        net::IpAddr::V4(net::Ipv4Addr::from(addr.to_le_bytes()))
    }

    pub fn abstract_unix_namespace(&self) -> &Arc<AtomicRefCell<AbstractUnixNamespace>> {
        let ptr = unsafe { cshadow::host_getAbstractUnixNamespace(self.chost()) };
        assert!(!ptr.is_null());
        unsafe { &*ptr }
    }

    pub fn log_level(&self) -> Option<log::LevelFilter> {
        let level = unsafe { cshadow::host_getLogLevel(self.chost()) };
        crate::core::logger::log_wrapper::c_to_rust_log_level(level).map(|l| l.to_level_filter())
    }

    pub fn random(&mut self) -> &mut impl rand::Rng {
        let ptr = unsafe { cshadow::host_getRandom(self.chost()) };
        let random = unsafe { ptr.as_mut() }.unwrap();
        &mut random.0
    }

    pub fn get_new_event_id(&mut self) -> u64 {
        unsafe { cshadow::host_getNewEventID(self.chost()) }
    }

    pub fn continue_execution_timer(&mut self) {
        unsafe { cshadow::host_continueExecutionTimer(self.chost()) };
    }

    pub fn stop_execution_timer(&mut self) {
        unsafe { cshadow::host_stopExecutionTimer(self.chost()) };
    }

    pub fn schedule_task_at_emulated_time(&mut self, mut task: TaskRef, t: EmulatedTime) -> bool {
        let res = unsafe {
            cshadow::host_scheduleTaskAtEmulatedTime(
                self.chost(),
                &mut task,
                EmulatedTime::to_c_emutime(Some(t)),
            )
        };
        // Intentionally drop `task`. An eventual event_new clones.
        res != 0
    }

    pub fn schedule_task_with_delay(&mut self, mut task: TaskRef, t: SimulationTime) -> bool {
        let res = unsafe {
            cshadow::host_scheduleTaskWithDelay(
                self.chost(),
                &mut task,
                SimulationTime::to_c_simtime(Some(t)),
            )
        };
        // Intentionally drop `task`. An eventual event_new clones.
        res != 0
    }

    pub fn event_queue(&self) -> Arc<ThreadSafeEventQueue> {
        let new_arc = unsafe { cshadow::host_getOwnedEventQueue(self.chost()) };
        unsafe { Arc::from_raw(new_arc) }
    }

    pub fn boot(&mut self) {
        unsafe { cshadow::host_boot(self.chost()) };
    }

    pub fn shutdown(&mut self) {
        unsafe { cshadow::host_shutdown(self.chost()) };
    }

    pub fn free_all_applications(&mut self) {
        unsafe { cshadow::host_freeAllApplications(self.chost()) };
    }

    pub fn execute(&mut self, until: EmulatedTime) {
        unsafe { cshadow::host_execute(self.chost(), EmulatedTime::to_c_emutime(Some(until))) };
    }

    pub fn next_event_time(&self) -> Option<EmulatedTime> {
        EmulatedTime::from_c_emutime(unsafe { cshadow::host_nextEventTime(self.chost()) })
    }

    pub unsafe fn lock(&mut self) {
        unsafe { cshadow::host_lock(self.chost()) };
        unsafe { cshadow::host_lockShimShmemLock(self.chost()) };
    }

    pub unsafe fn unlock(&mut self) {
        unsafe { cshadow::host_unlockShimShmemLock(self.chost()) };
        unsafe { cshadow::host_unlock(self.chost()) };
    }

    pub fn chost(&self) -> *mut cshadow::Host {
        self.chost.ptr()
    }
}
