use log::trace;
use once_cell::unsync::OnceCell;
use rand::SeedableRng;
use rand_xoshiro::Xoshiro256PlusPlus;
use shadow_shim_helper_rs::rootedcell::refcell::RootedRefCell;
use shadow_shim_helper_rs::rootedcell::Root;

use std::net::Ipv4Addr;
use std::os::raw::c_char;
use std::sync::{Arc, Mutex};

use crate::core::work::event::Event;
use crate::core::work::event_queue::EventQueue;
use crate::core::work::task::TaskRef;
use crate::core::worker::Worker;
use crate::cshadow;
use crate::host::descriptor::socket::abstract_unix_ns::AbstractUnixNamespace;
use crate::utility::SyncSendPointer;
use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::simulation_time::SimulationTime;
use shadow_shim_helper_rs::HostId;

use atomic_refcell::AtomicRefCell;

/// Immutable information about the Host.
#[derive(Debug, Clone)]
pub struct HostInfo {
    pub id: HostId,
    pub name: String,
    pub default_ip: Ipv4Addr,
    pub log_level: Option<log::LevelFilter>,
}

/// A simulated Host.
pub struct Host {
    // Legacy C implementation of the host. Fields and functionality should be
    // migrated from here to this Host.
    chost: SyncSendPointer<cshadow::HostCInternal>,

    // Store immutable info in an Arc, that we can safely clone into the
    // ShadowLogger. We can't use a RootedRc here since this needs to be cloned
    // into the logger thread, which doesn't have access to the Host's Root.
    //
    // TODO: Get rid of the enclosing OnceCell and initialize at the point where
    // the necessary data is available.
    info: OnceCell<Arc<HostInfo>>,

    // Inside the Host "object graph", we use the Host's Root for RootedRc and RootedRefCells,
    // giving us atomic-free refcounting and checked borrowing.
    //
    // This makes the Host !Sync.
    //
    // Not used yet.
    #[allow(unused)]
    root: Root,

    event_queue: Arc<Mutex<EventQueue>>,

    random: RootedRefCell<Xoshiro256PlusPlus>,

    params: cshadow::HostParameters,
}

/// HostParameters is !Send by default due to string pointers, but those are
/// statically allocated and immutable.
unsafe impl Send for cshadow::HostParameters {}

/// Host must be `Send`.
impl crate::utility::IsSend for Host {}

// TODO: use derive(Debug) if/when all fields implement Debug.
impl std::fmt::Debug for Host {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Host")
            .field("chost", &self.chost)
            .field("info", &self.info)
            .finish()
    }
}

impl Host {
    pub fn new(params: cshadow::HostParameters) -> Self {
        // Ok because hostc_new copies params rather than saving this pointer.
        // TODO: remove params from HostCInternal.
        let chost = unsafe { cshadow::hostc_new(&params) };
        let root = Root::new();
        let random = RootedRefCell::new(
            &root,
            Xoshiro256PlusPlus::seed_from_u64(params.nodeSeed as u64),
        );
        Self {
            chost: unsafe { SyncSendPointer::new(chost) },
            info: OnceCell::new(),
            root,
            event_queue: Arc::new(Mutex::new(EventQueue::new())),
            params,
            random,
        }
    }

    pub unsafe fn setup(
        &self,
        dns: *mut cshadow::DNS,
        raw_cpu_freq: u64,
        host_root_path: *const c_char,
    ) {
        unsafe { cshadow::hostc_setup(self.chost(), dns, raw_cpu_freq, host_root_path) }
    }

    pub unsafe fn add_application(
        &self,
        start_time: SimulationTime,
        stop_time: Option<SimulationTime>,
        plugin_name: *const c_char,
        plugin_path: *const c_char,
        envv: *const *const c_char,
        argv: *const *const c_char,
        pause_for_debugging: bool,
    ) {
        unsafe {
            cshadow::hostc_addApplication(
                self,
                SimulationTime::to_c_simtime(Some(start_time)),
                SimulationTime::to_c_simtime(stop_time),
                plugin_name,
                plugin_path,
                envv,
                argv,
                pause_for_debugging,
            )
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
        unsafe { cshadow::hostc_getID(self.chost()) }
    }

    pub fn name(&self) -> &str {
        let slice = unsafe { std::ffi::CStr::from_ptr(cshadow::hostc_getName(self.chost())) };
        slice.to_str().unwrap()
    }

    pub fn default_ip(&self) -> Ipv4Addr {
        let addr = unsafe { cshadow::hostc_getDefaultIP(self.chost()) };
        u32::from_be(addr).into()
    }

    pub fn abstract_unix_namespace(&self) -> &Arc<AtomicRefCell<AbstractUnixNamespace>> {
        let ptr = unsafe { cshadow::hostc_getAbstractUnixNamespace(self.chost()) };
        assert!(!ptr.is_null());
        unsafe { &*ptr }
    }

    pub fn log_level(&self) -> Option<log::LevelFilter> {
        let level = unsafe { cshadow::hostc_getLogLevel(self.chost()) };
        crate::core::logger::log_wrapper::c_to_rust_log_level(level).map(|l| l.to_level_filter())
    }

    pub fn with_random_mut<Res>(&self, f: impl FnOnce(&mut Xoshiro256PlusPlus) -> Res) -> Res {
        let mut rng = self.random.borrow_mut(&self.root);
        f(&mut *rng)
    }

    pub fn get_new_event_id(&self) -> u64 {
        unsafe { cshadow::hostc_getNewEventID(self.chost()) }
    }

    pub fn continue_execution_timer(&self) {
        unsafe { cshadow::hostc_continueExecutionTimer(self.chost()) };
    }

    pub fn stop_execution_timer(&self) {
        unsafe { cshadow::hostc_stopExecutionTimer(self.chost()) };
    }

    pub fn schedule_task_at_emulated_time(&self, task: TaskRef, t: EmulatedTime) -> bool {
        let event = Event::new(task, t, self, self.id());
        self.push_local_event(event)
    }

    pub fn schedule_task_with_delay(&self, task: TaskRef, t: SimulationTime) -> bool {
        self.schedule_task_at_emulated_time(task, Worker::current_time().unwrap() + t)
    }

    pub fn event_queue(&self) -> &Arc<Mutex<EventQueue>> {
        &self.event_queue
    }

    pub fn push_local_event(&self, event: Event) -> bool {
        if event.time() >= EmulatedTime::from_c_emutime(self.params.simEndTime).unwrap() {
            return false;
        }
        self.event_queue.lock().unwrap().push(event);
        true
    }

    pub fn boot(&self) {
        unsafe { cshadow::hostc_boot(self) };
    }

    pub fn shutdown(&self) {
        unsafe { cshadow::hostc_shutdown(self.chost()) };
    }

    pub fn free_all_applications(&self) {
        unsafe { cshadow::hostc_freeAllApplications(self.chost()) };
    }

    pub fn execute(&self, until: EmulatedTime) {
        let cpu = unsafe { cshadow::hostc_getCPU(self.chost()) };
        loop {
            let mut event = {
                let mut event_queue = self.event_queue.lock().unwrap();
                match event_queue.next_event_time() {
                    Some(t) if t < until => {}
                    _ => break,
                };
                event_queue.pop().unwrap()
            };

            unsafe {
                cshadow::cpu_updateTime(
                    cpu,
                    SimulationTime::to_c_simtime(Some(event.time().to_abs_simtime())),
                )
            };

            if unsafe { cshadow::cpu_isBlocked(cpu) != 0 } {
                let cpu_delay =
                    SimulationTime::from_c_simtime(unsafe { cshadow::cpu_getDelay(cpu) }).unwrap();
                trace!(
                    "event blocked on CPU, rescheduled for {:?} from now",
                    cpu_delay
                );

                // track the event delay time
                let tracker = unsafe { cshadow::hostc_getTracker(self.chost()) };
                if !tracker.is_null() {
                    unsafe {
                        cshadow::tracker_addVirtualProcessingDelay(
                            tracker,
                            SimulationTime::to_c_simtime(Some(cpu_delay)),
                        )
                    };
                }

                // reschedule the event after the CPU delay time
                event.set_time(event.time() + cpu_delay);
                self.push_local_event(event);

                // want to continue pushing back events until we reach the delay time
                continue;
            }

            // run the event
            Worker::set_current_time(event.time());
            event.execute(self);
            Worker::clear_current_time();
        }
    }

    pub fn next_event_time(&self) -> Option<EmulatedTime> {
        self.event_queue.lock().unwrap().next_event_time()
    }

    pub unsafe fn lock_shmem(&self) {
        unsafe { cshadow::hostc_lockShimShmemLock(self.chost()) };
    }

    pub unsafe fn unlock_shmem(&self) {
        unsafe { cshadow::hostc_unlockShimShmemLock(self.chost()) };
    }

    pub fn chost(&self) -> *mut cshadow::HostCInternal {
        self.chost.ptr()
    }
}

impl Drop for Host {
    fn drop(&mut self) {
        unsafe { cshadow::hostc_unref(self.chost()) };
    }
}

mod export {
    use std::os::raw::c_char;

    use libc::{in_addr_t, in_port_t};
    use rand::{Rng, RngCore};

    use crate::{
        cshadow::{CEmulatedTime, CSimulationTime, HostCInternal},
        network::router::Router,
    };

    use super::*;

    #[no_mangle]
    pub unsafe extern "C" fn host_execute(hostrc: *const Host, until: CEmulatedTime) {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        let until = EmulatedTime::from_c_emutime(until).unwrap();
        hostrc.execute(until)
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_nextEventTime(hostrc: *const Host) -> CEmulatedTime {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        EmulatedTime::to_c_emutime(hostrc.next_event_time())
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_getNewProcessID(hostrc: *const Host) -> u32 {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_getNewProcessID(hostrc.chost()) }
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_getNewPacketID(hostrc: *const Host) -> u64 {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_getNewPacketID(hostrc.chost()) }
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_freeAllApplications(hostrc: *const Host) {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_freeAllApplications(hostrc.chost()) }
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_getID(hostrc: *const Host) -> HostId {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_getID(hostrc.chost()) }
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_getCPU(hostrc: *const Host) -> *mut cshadow::CPU {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_getCPU(hostrc.chost()) }
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_getTsc(hostrc: *const Host) -> *mut cshadow::Tsc {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_getTsc(hostrc.chost()) }
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_getName(hostrc: *const Host) -> *const c_char {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_getName(hostrc.chost()) }
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_getDefaultAddress(hostrc: *const Host) -> *mut cshadow::Address {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_getDefaultAddress(hostrc.chost()) }
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_getDefaultIP(hostrc: *const Host) -> in_addr_t {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        let ip = hostrc.default_ip();
        u32::from(ip).to_be()
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_getNextPacketPriority(hostrc: *const Host) -> f64 {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_getNextPacketPriority(hostrc.chost()) }
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_autotuneReceiveBuffer(hostrc: *const Host) -> bool {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_autotuneReceiveBuffer(hostrc.chost()) != 0 }
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_autotuneSendBuffer(hostrc: *const Host) -> bool {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_autotuneSendBuffer(hostrc.chost()) != 0 }
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_getConfiguredRecvBufSize(hostrc: *const Host) -> u64 {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_getConfiguredRecvBufSize(hostrc.chost()) }
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_getConfiguredSendBufSize(hostrc: *const Host) -> u64 {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_getConfiguredSendBufSize(hostrc.chost()) }
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_lookupInterface(
        hostrc: *const Host,
        handle: in_addr_t,
    ) -> *mut cshadow::NetworkInterface {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_lookupInterface(hostrc.chost(), handle) }
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_getUpstreamRouter(hostrc: *const Host) -> *mut Router {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_getUpstreamRouter(hostrc.chost()) }
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_get_bw_down_kiBps(hostrc: *const Host) -> u64 {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_get_bw_down_kiBps(hostrc.chost()) }
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_get_bw_up_kiBps(hostrc: *const Host) -> u64 {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_get_bw_up_kiBps(hostrc.chost()) }
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_getTracker(hostrc: *const Host) -> *mut cshadow::Tracker {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_getTracker(hostrc.chost()) }
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_getLogLevel(hostrc: *const Host) -> logger::LogLevel {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_getLogLevel(hostrc.chost()) }
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_getDataPath(hostrc: *const Host) -> *const c_char {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_getDataPath(hostrc.chost()) }
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_doesInterfaceExist(
        hostrc: *const Host,
        interface_ip: in_addr_t,
    ) -> bool {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_doesInterfaceExist(hostrc.chost(), interface_ip) != 0 }
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_isInterfaceAvailable(
        hostrc: *const Host,
        protocol_type: cshadow::ProtocolType,
        interface_ip: in_addr_t,
        port: in_port_t,
        peer_ip: in_addr_t,
        peer_port: in_port_t,
    ) -> bool {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe {
            cshadow::hostc_isInterfaceAvailable(
                hostrc.chost(),
                protocol_type,
                interface_ip,
                port,
                peer_ip,
                peer_port,
            ) != 0
        }
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_associateInterface(
        hostrc: *const Host,
        socket: *const cshadow::CompatSocket,
        bind_address: in_addr_t,
    ) {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_associateInterface(hostrc.chost(), socket, bind_address) }
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_disassociateInterface(
        hostrc: *const Host,
        socket: *const cshadow::CompatSocket,
    ) {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_disassociateInterface(hostrc.chost(), socket) }
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_getRandomFreePort(
        hostrc: *const Host,
        protocol_type: cshadow::ProtocolType,
        interface_ip: in_addr_t,
        peer_ip: in_addr_t,
        peer_port: in_port_t,
    ) -> in_port_t {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe {
            cshadow::hostc_getRandomFreePort(
                hostrc,
                protocol_type,
                interface_ip,
                peer_ip,
                peer_port,
            )
        }
    }

    // Arc_AtomicRefCell_AbstractUnixNamespace* host_getAbstractUnixNamespace(HostRcCInternal* host);
    #[no_mangle]
    pub unsafe extern "C" fn host_getAbstractUnixNamespace(
        hostrc: *const Host,
    ) -> *mut Arc<AtomicRefCell<AbstractUnixNamespace>> {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_getAbstractUnixNamespace(hostrc.chost()) }
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_getFutexTable(hostrc: *const Host) -> *mut cshadow::FutexTable {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_getFutexTable(hostrc.chost()) }
    }

    /// converts a virtual (shadow) tid into the native tid
    #[no_mangle]
    pub unsafe extern "C" fn host_getNativeTID(
        hostrc: *const Host,
        virtual_pid: libc::pid_t,
        virtual_tid: libc::pid_t,
    ) -> libc::pid_t {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_getNativeTID(hostrc.chost(), virtual_pid, virtual_tid) }
    }

    /// Returns the specified process, or NULL if it doesn't exist.
    #[no_mangle]
    pub unsafe extern "C" fn host_getProcess(
        hostrc: *const Host,
        virtual_pid: libc::pid_t,
    ) -> *mut cshadow::Process {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_getProcess(hostrc.chost(), virtual_pid) }
    }

    /// Returns the specified thread, or NULL if it doesn't exist.
    /// If you already have the thread's Process*, `process_getThread` may be more
    /// efficient.
    #[no_mangle]
    pub unsafe extern "C" fn host_getThread(
        hostrc: *const Host,
        virtual_tid: libc::pid_t,
    ) -> *mut cshadow::Thread {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_getThread(hostrc.chost(), virtual_tid) }
    }

    /// Returns host-specific state that's kept in memory shared with the shim(s).
    #[no_mangle]
    pub unsafe extern "C" fn host_getSharedMem(
        hostrc: *const Host,
    ) -> *const cshadow::ShimShmemHost {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_getSharedMem(hostrc.chost()) }
    }

    /// Returns the lock, or NULL if the lock isn't held by Shadow.
    ///
    /// Generally the lock can and should be held when Shadow is running, and *not*
    /// held when any of the host's managed threads are running (leaving it available
    /// to be taken by the shim). While this can be a little fragile to ensure
    /// properly, debug builds detect if we get it wrong (e.g. we try accessing
    /// protected data without holding the lock, or the shim tries to take the lock
    /// but can't).
    #[no_mangle]
    pub unsafe extern "C" fn host_getShimShmemLock(
        hostrc: *const Host,
    ) -> *mut cshadow::ShimShmemHostLock {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_getShimShmemLock(hostrc.chost()) }
    }

    /// Take the host's shared memory lock. See `host_getShimShmemLock`.
    #[no_mangle]
    pub unsafe extern "C" fn host_lockShimShmemLock(hostrc: *const Host) {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_lockShimShmemLock(hostrc.chost()) }
    }

    /// Release the host's shared memory lock. See `host_getShimShmemLock`.
    #[no_mangle]
    pub unsafe extern "C" fn host_unlockShimShmemLock(hostrc: *const Host) {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_unlockShimShmemLock(hostrc.chost()) }
    }

    /// Returns the next value and increments our monotonically increasing
    /// determinism sequence counter. The resulting values can be sorted to
    /// established a deterministic ordering, which can be useful when iterating
    /// items that are otherwise inconsistently ordered (e.g. hash table iterators).
    #[no_mangle]
    pub unsafe extern "C" fn host_getNextDeterministicSequenceValue(hostrc: *const Host) -> u64 {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_getNextDeterministicSequenceValue(hostrc.chost()) }
    }

    /// Schedule a task for this host at time 'time'.
    #[no_mangle]
    pub unsafe extern "C" fn host_scheduleTaskAtEmulatedTime(
        hostrc: *const Host,
        task: *mut TaskRef,
        time: CEmulatedTime,
    ) -> bool {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        let task = unsafe { task.as_ref().unwrap().clone() };
        let time = EmulatedTime::from_c_emutime(time).unwrap();
        hostrc.schedule_task_at_emulated_time(task, time)
    }

    /// Schedule a task for this host at a time 'nanoDelay' from now,.
    #[no_mangle]
    pub unsafe extern "C" fn host_scheduleTaskWithDelay(
        hostrc: *const Host,
        task: *mut TaskRef,
        delay: CSimulationTime,
    ) -> bool {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        let task = unsafe { task.as_ref().unwrap().clone() };
        let delay = SimulationTime::from_c_simtime(delay).unwrap();
        hostrc.schedule_task_with_delay(task, delay)
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_rngDouble(host: *const Host) -> f64 {
        let host = unsafe { host.as_ref().unwrap() };
        host.with_random_mut(|rng| rng.gen())
    }

    /// Fills the buffer with pseudo-random bytes.
    #[no_mangle]
    pub extern "C" fn host_rngNextNBytes(host: *const Host, buf: *mut u8, len: usize) {
        let host = unsafe { host.as_ref().unwrap() };
        host.with_random_mut(|rng| {
            let buf = unsafe { std::slice::from_raw_parts_mut(buf, len) };
            rng.fill_bytes(buf);
        })
    }

    /// Should only be used from host.c
    #[no_mangle]
    pub unsafe extern "C" fn host_internal(hostrc: *const Host) -> *mut HostCInternal {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.chost()
    }
}
