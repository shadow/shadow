use crate::core::support::configuration::QDiscMode;
use crate::core::work::event::Event;
use crate::core::work::event_queue::EventQueue;
use crate::core::work::task::TaskRef;
use crate::core::worker::Worker;
use crate::cshadow;
use crate::host::descriptor::socket::abstract_unix_ns::AbstractUnixNamespace;
use crate::host::network_interface::NetworkInterface;
use crate::network::router::Router;
use crate::utility::{self, HostTreePointer, SyncSendPointer};
use atomic_refcell::AtomicRefCell;
use log::{debug, info, trace, warn};
use logger::LogLevel;
use once_cell::unsync::OnceCell;
use rand::{Rng, SeedableRng};
use rand_xoshiro::Xoshiro256PlusPlus;
use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::rootedcell::Root;
use shadow_shim_helper_rs::shim_shmem::{HostShmem, HostShmemProtected};
use shadow_shim_helper_rs::simulation_time::SimulationTime;
use shadow_shim_helper_rs::HostId;
use shadow_shmem::allocator::ShMemBlock;
use shadow_shmem::scmutex::SelfContainedMutexGuard;
use shadow_tsc::Tsc;
use std::cell::{Cell, Ref, RefCell, RefMut, UnsafeCell};
use std::collections::BTreeMap;
use std::ffi::{CStr, CString, OsString};
use std::net::{Ipv4Addr, SocketAddrV4};
use std::ops::{Deref, DerefMut};
use std::os::unix::prelude::OsStringExt;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};

#[cfg(feature = "perf_timers")]
use crate::utility::perf_timer::PerfTimer;

// The start of our random port range in host order, used if application doesn't
// specify the port it wants to bind to, and for client connections.
const MIN_RANDOM_PORT: u16 = 10000;

pub struct HostParameters {
    pub id: HostId,
    pub node_seed: u64,
    // TODO: Remove when we don't need C compatibility.
    // Already storing as a String in HostInfo.
    pub hostname: CString,
    pub node_id: u32,
    pub ip_addr: libc::in_addr_t,
    pub sim_end_time: EmulatedTime,
    pub requested_bw_down_bits: u64,
    pub requested_bw_up_bits: u64,
    pub cpu_frequency: u64,
    pub cpu_threshold: Option<SimulationTime>,
    pub cpu_precision: Option<SimulationTime>,
    pub heartbeat_interval: Option<SimulationTime>,
    pub heartbeat_log_level: LogLevel,
    pub heartbeat_log_info: cshadow::LogInfoFlags,
    pub log_level: LogLevel,
    // TODO: change to PathBuf when we don't need C compatibility
    pub pcap_dir: Option<CString>,
    pub pcap_capture_size: u32,
    pub qdisc: QDiscMode,
    pub init_sock_recv_buf_size: u64,
    pub autotune_recv_buf: bool,
    pub init_sock_send_buf_size: u64,
    pub autotune_send_buf: bool,
    pub native_tsc_frequency: u64,
    pub model_unblocked_syscall_latency: bool,
    pub max_unapplied_cpu_latency: SimulationTime,
    pub unblocked_syscall_latency: SimulationTime,
    pub unblocked_vdso_latency: SimulationTime,
}

use super::cpu::Cpu;
use super::process::ProcessId;

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

    random: RefCell<Xoshiro256PlusPlus>,

    // the upstream router that will queue packets until we can receive them.
    // this only applies the the ethernet interface, the loopback interface
    // does not receive packets from a router.
    router: RefCell<Router>,

    // a statistics tracker for in/out bytes, CPU, memory, etc.
    tracker: RefCell<Option<SyncSendPointer<cshadow::Tracker>>>,

    // map address to futex objects
    futex_table: RefCell<SyncSendPointer<cshadow::FutexTable>>,

    #[cfg(feature = "perf_timers")]
    execution_timer: RefCell<PerfTimer>,

    pub params: HostParameters,

    cpu: RefCell<Cpu>,

    // TODO: Use a Rust address type.
    default_address: RefCell<SyncSendPointer<cshadow::Address>>,

    // map abstract socket addresses to unix sockets
    abstract_unix_namespace: Arc<AtomicRefCell<AbstractUnixNamespace>>,

    // TODO: rearrange our setup process so we don't need Option types here.
    localhost: RefCell<Option<NetworkInterface>>,
    internet: RefCell<Option<NetworkInterface>>,

    // Store as a CString so that we can return a borrowed pointer to C code
    // instead of having to allocate a new string.
    //
    // TODO: Remove `data_dir_path_cstring` once we can remove `host_getDataPath`. (Or maybe don't
    // store it at all)
    data_dir_path: PathBuf,
    data_dir_path_cstring: CString,

    // virtual process and event id counter
    process_id_counter: Cell<u32>,
    event_id_counter: Cell<u64>,
    packet_id_counter: Cell<u64>,

    // Enables us to sort objects deterministically based on their creation order.
    determinism_sequence_counter: Cell<u64>,

    // track the order in which the application sent us application data
    packet_priority_counter: Cell<f64>,

    // Owned pointers to processes.
    processes: RefCell<BTreeMap<ProcessId, HostTreePointer<cshadow::Process>>>,

    tsc: Tsc,
    // Cached lock for shim_shmem. `[Host::shmem_lock]` uses unsafe code to give it
    // a 'static lifetime.
    // SAFETY:
    // * This field must not outlive `shim_shmem`. We achieve this by:
    //   * Declaring this field before `shim_shmem` so that it's dropped before
    //   it.
    //   * We never expose the guard itself via non-unsafe interfaces. e.g.  our
    //   safe interfaces don't allow access to the guard itself, nor to the
    //   internal data with a lifetime that could outlive `self` (and thereby
    //   `shim_shmem`).
    shim_shmem_lock:
        RefCell<Option<UnsafeCell<SelfContainedMutexGuard<'static, HostShmemProtected>>>>,
    // Shared memory with the shim.
    //
    // SAFETY: The data inside HostShmem::protected aliases shim_shmem_lock when
    // the latter is held.  Even when holding `&mut self` or `self`, if
    // `shim_shmem_lock` is held we must avoid invalidating it, e.g. by
    // `std::mem::replace`.
    //
    // Note though that we're already prevented from creating another reference
    // to the data inside `HostShmem::protected` through this field, since
    // `self.shim_shmem...protected.lock()` will fail if the lock is already
    // held.
    shim_shmem: UnsafeCell<ShMemBlock<'static, HostShmem>>,
}

/// Host must be `Send`.
impl crate::utility::IsSend for Host {}

// TODO: use derive(Debug) if/when all fields implement Debug.
impl std::fmt::Debug for Host {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Host")
            .field("info", &self.info)
            .finish_non_exhaustive()
    }
}

impl Host {
    pub fn new(params: HostParameters, host_root_path: &Path, raw_cpu_freq_khz: u64) -> Self {
        #[cfg(feature = "perf_timers")]
        let execution_timer = RefCell::new(PerfTimer::new());

        let root = Root::new();
        let random = RefCell::new(Xoshiro256PlusPlus::seed_from_u64(params.node_seed));
        let cpu = RefCell::new(Cpu::new(
            params.cpu_frequency,
            raw_cpu_freq_khz,
            params.cpu_threshold,
            params.cpu_precision,
        ));
        let data_dir_path = Self::data_dir_path(&params.hostname, host_root_path);
        let data_dir_path_cstring = utility::pathbuf_to_nul_term_cstring(data_dir_path.clone());
        let default_address = RefCell::new(unsafe { SyncSendPointer::new(std::ptr::null_mut()) });

        let host_shmem = HostShmem::new(
            params.id,
            params.model_unblocked_syscall_latency,
            params.max_unapplied_cpu_latency,
            params.unblocked_syscall_latency,
            params.unblocked_vdso_latency,
        );
        let shim_shmem =
            UnsafeCell::new(shadow_shmem::allocator::Allocator::global().alloc(host_shmem));

        // Process IDs start at 1000
        let process_id_counter = Cell::new(1000);
        let event_id_counter = Cell::new(0);
        let packet_id_counter = Cell::new(0);
        let determinism_sequence_counter = Cell::new(0);
        // Packet priorities start at 1.0. "0.0" is used for control packets.
        let packet_priority_counter = Cell::new(1.0);
        let tsc = Tsc::new(params.native_tsc_frequency);

        std::fs::create_dir_all(&data_dir_path).unwrap();

        let res = Self {
            default_address,
            info: OnceCell::new(),
            root,
            event_queue: Arc::new(Mutex::new(EventQueue::new())),
            params,
            router: RefCell::new(Router::new()),
            tracker: RefCell::new(None),
            futex_table: RefCell::new(unsafe { SyncSendPointer::new(cshadow::futextable_new()) }),
            random,
            shim_shmem,
            shim_shmem_lock: RefCell::new(None),
            abstract_unix_namespace: Arc::new(AtomicRefCell::new(AbstractUnixNamespace::new())),
            cpu,
            localhost: RefCell::new(None),
            internet: RefCell::new(None),
            data_dir_path,
            data_dir_path_cstring,
            process_id_counter,
            event_id_counter,
            packet_id_counter,
            packet_priority_counter,
            determinism_sequence_counter,
            tsc,
            processes: RefCell::new(BTreeMap::new()),
            #[cfg(feature = "perf_timers")]
            execution_timer,
        };

        res.stop_execution_timer();

        res
    }

    /// Must free the returned `*mut cshadow::Address` using [`cshadow::address_unref`].
    unsafe fn setup_net_interface(
        params: &HostParameters,
        ip: Ipv4Addr,
        uses_router: bool,
        pcap_dir_path: Option<PathBuf>,
        dns: *mut cshadow::DNS,
    ) -> (NetworkInterface, *mut cshadow::Address) {
        let ip = u32::from(ip).to_be();
        let addr = unsafe { cshadow::dns_register(dns, params.id, params.hostname.as_ptr(), ip) };
        assert!(!addr.is_null());

        let interface = unsafe {
            NetworkInterface::new(
                params.id,
                addr,
                pcap_dir_path,
                params.pcap_capture_size,
                params.qdisc,
                uses_router,
            )
        };

        (interface, addr)
    }

    pub unsafe fn setup(&self, dns: *mut cshadow::DNS) {
        // Register using the param hints.
        // We already checked that the addresses are available, so fail if they are not.

        let (localhost, local_addr) = unsafe {
            Self::setup_net_interface(
                &self.params,
                Ipv4Addr::LOCALHOST,
                /* uses_router= */ false,
                self.pcap_dir_path(),
                dns,
            )
        };

        let public_ip: Ipv4Addr = u32::from_be(self.params.ip_addr).into();
        let (internet, inet_addr) = unsafe {
            Self::setup_net_interface(
                &self.params,
                public_ip,
                /* uses_router= */ true,
                self.pcap_dir_path(),
                dns,
            )
        };

        *self.default_address.borrow_mut() = unsafe { SyncSendPointer::new(inet_addr) };
        unsafe { cshadow::address_unref(local_addr) };

        self.localhost.borrow_mut().replace(localhost);
        self.internet.borrow_mut().replace(internet);

        info!(
            concat!(
                "Setup host id '{:?}'",
                " name '{name}'",
                " with seed {seed},",
                " {bw_up_kiBps} bwUpKiBps,",
                " {bw_down_kiBps} bwDownKiBps,",
                " {init_sock_send_buf_size} initSockSendBufSize,",
                " {init_sock_recv_buf_size} initSockRecvBufSize, ",
                " {cpu_frequency} cpuFrequency, ",
                " {cpu_threshold} cpuThreshold, ",
                " {cpu_precision} cpuPrecision"
            ),
            self.id(),
            name = self.info().name,
            seed = self.params.node_seed,
            bw_up_kiBps = self.bw_up_kiBps(),
            bw_down_kiBps = self.bw_down_kiBps(),
            init_sock_send_buf_size = self.params.init_sock_send_buf_size,
            init_sock_recv_buf_size = self.params.init_sock_recv_buf_size,
            cpu_frequency = format!("{:?}", self.params.cpu_frequency),
            cpu_threshold = format!("{:?}", self.params.cpu_threshold),
            cpu_precision = format!("{:?}", self.params.cpu_precision),
        );
    }

    fn data_dir_path(hostname: &CStr, host_root_path: &Path) -> PathBuf {
        let hostname: OsString = { OsString::from_vec(hostname.to_bytes().to_vec()) };

        let mut data_dir_path = PathBuf::new();
        data_dir_path.push(host_root_path);
        data_dir_path.push(&hostname);
        data_dir_path
    }

    fn pcap_dir_path(&self) -> Option<PathBuf> {
        let Some(pcap_dir) = &self.params.pcap_dir else {
            return None;
        };
        let path_string: OsString = { OsString::from_vec(pcap_dir.to_bytes().to_vec()) };

        let mut path = self.data_dir_path.clone();
        // If relative it will append, if absolute it will replace.
        path.push(PathBuf::from(path_string));
        path.canonicalize().ok()
    }

    pub fn add_application(
        &self,
        start_time: SimulationTime,
        stop_time: Option<SimulationTime>,
        plugin_name: &CStr,
        plugin_path: &CStr,
        mut envv: Vec<CString>,
        argv: &[CString],
        pause_for_debugging: bool,
    ) {
        {
            // SAFETY: We're not touching the data inside the block, only
            // using its metadata to create a serialized pointer to it.
            let block = unsafe { &*self.shim_shmem.get() };
            let mut envvar = String::from("SHADOW_SHM_HOST_BLK=");
            envvar.push_str(&block.serialize().encode_to_string());
            envv.push(CString::new(envvar).unwrap());
        }

        let process_id = self.get_new_process_id();

        let envv_ptrs: Vec<*const i8> = envv
            .iter()
            .map(|x| x.as_ptr())
            // the last element of envv must be NULL
            .chain(std::iter::once(std::ptr::null()))
            .collect();
        let argv_ptrs: Vec<*const i8> = argv
            .iter()
            .map(|x| x.as_ptr())
            // the last element of argv must be NULL
            .chain(std::iter::once(std::ptr::null()))
            .collect();

        let process = unsafe {
            cshadow::process_new(
                self,
                process_id.into(),
                SimulationTime::to_c_simtime(Some(start_time)),
                SimulationTime::to_c_simtime(stop_time),
                self.params.hostname.as_ptr(),
                plugin_name.as_ptr(),
                plugin_path.as_ptr(),
                envv_ptrs.as_ptr(),
                argv_ptrs.as_ptr(),
                pause_for_debugging,
            )
        };

        unsafe { cshadow::process_schedule(process, self) };

        self.processes.borrow_mut().insert(
            process_id,
            HostTreePointer::new_for_host(self.id(), process),
        );
    }

    /// Information about the Host. Made available as an Arc for cheap cloning
    /// into, e.g. Worker and ShadowLogger. When there's no need to clone the
    /// Arc, generally prefer the top-level `Host` methods for accessing this
    /// information, which are likely to be more stable.
    pub fn info(&self) -> &Arc<HostInfo> {
        self.info.get_or_init(|| {
            Arc::new(HostInfo {
                id: self.id(),
                name: self.params.hostname.to_str().unwrap().to_owned(),
                default_ip: self.default_ip(),
                log_level: self.log_level(),
            })
        })
    }

    pub fn id(&self) -> HostId {
        self.params.id
    }

    pub fn name(&self) -> &str {
        &self.info().name
    }

    pub fn default_ip(&self) -> Ipv4Addr {
        let addr = self.default_address.borrow().ptr();
        let addr = unsafe { cshadow::address_toNetworkIP(addr) };
        u32::from_be(addr).into()
    }

    pub fn abstract_unix_namespace(&self) -> &Arc<AtomicRefCell<AbstractUnixNamespace>> {
        &self.abstract_unix_namespace
    }

    pub fn log_level(&self) -> Option<log::LevelFilter> {
        let level = self.params.log_level;
        crate::core::logger::log_wrapper::c_to_rust_log_level(level).map(|l| l.to_level_filter())
    }

    #[track_caller]
    pub fn upstream_router_mut(&self) -> impl Deref<Target = Router> + DerefMut + '_ {
        self.router.borrow_mut()
    }

    #[track_caller]
    pub fn tracker_mut(&self) -> Option<impl Deref<Target = cshadow::Tracker> + DerefMut + '_> {
        let tracker = self.tracker.borrow_mut();
        if let Some(tracker) = &*tracker {
            debug_assert!(!tracker.ptr().is_null());
            let tracker = unsafe { &mut *tracker.ptr() };
            Some(tracker)
        } else {
            None
        }
    }

    #[track_caller]
    pub fn futextable_mut(&self) -> impl Deref<Target = cshadow::FutexTable> + DerefMut + '_ {
        let futex_table_ref = self.futex_table.borrow_mut();
        RefMut::map(futex_table_ref, |r| unsafe { &mut *r.ptr() })
    }

    fn interface_cell(&self, addr: Ipv4Addr) -> Option<&RefCell<Option<NetworkInterface>>> {
        if addr.is_loopback() {
            Some(&self.localhost)
        } else if addr == self.info().default_ip {
            Some(&self.internet)
        } else {
            None
        }
    }

    #[allow(non_snake_case)]
    pub fn bw_up_kiBps(&self) -> u64 {
        self.params.requested_bw_up_bits / (8 * 1024)
    }

    #[allow(non_snake_case)]
    pub fn bw_down_kiBps(&self) -> u64 {
        self.params.requested_bw_down_bits / (8 * 1024)
    }

    /// Returns `None` if there is no such interface.
    ///
    /// Panics if we have not yet initialized our network interfaces yet.
    #[track_caller]
    pub fn interface_mut(
        &self,
        addr: Ipv4Addr,
    ) -> Option<impl Deref<Target = NetworkInterface> + DerefMut + '_> {
        match self.interface_cell(addr) {
            Some(rc) => {
                let interface = rc.borrow_mut();
                Some(RefMut::map(interface, |i| i.as_mut().unwrap()))
            }
            None => None,
        }
    }

    /// Returns `None` if there is no such interface.
    ///
    /// Panics if we have not yet initialized our network interfaces yet.
    #[track_caller]
    pub fn interface(&self, addr: Ipv4Addr) -> Option<impl Deref<Target = NetworkInterface> + '_> {
        match self.interface_cell(addr) {
            Some(rc) => {
                let interface = rc.borrow();
                Some(Ref::map(interface, |i| i.as_ref().unwrap()))
            }
            None => None,
        }
    }

    #[track_caller]
    pub fn random_mut(&self) -> impl Deref<Target = Xoshiro256PlusPlus> + DerefMut + '_ {
        self.random.borrow_mut()
    }

    pub fn get_new_event_id(&self) -> u64 {
        let res = self.event_id_counter.get();
        self.event_id_counter.set(res + 1);
        res
    }

    pub fn get_new_process_id(&self) -> ProcessId {
        let res = self.process_id_counter.get();
        self.process_id_counter.set(res + 1);
        res.into()
    }

    pub fn get_new_packet_id(&self) -> u64 {
        let res = self.packet_id_counter.get();
        self.packet_id_counter.set(res + 1);
        res
    }

    pub fn get_next_deterministic_sequence_value(&self) -> u64 {
        let res = self.determinism_sequence_counter.get();
        self.determinism_sequence_counter.set(res + 1);
        res
    }

    pub fn get_next_packet_priority(&self) -> f64 {
        let res = self.packet_priority_counter.get();
        self.packet_priority_counter.set(res + 1.0);
        res
    }

    pub fn continue_execution_timer(&self) {
        #[cfg(feature = "perf_timers")]
        self.execution_timer.borrow_mut().start();
    }

    pub fn stop_execution_timer(&self) {
        #[cfg(feature = "perf_timers")]
        self.execution_timer.borrow_mut().stop();
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
        if event.time() >= self.params.sim_end_time {
            return false;
        }
        self.event_queue.lock().unwrap().push(event);
        true
    }

    pub fn boot(&self) {
        // Start refilling the token buckets for all interfaces.
        let bw_down = self.bw_down_kiBps();
        let bw_up = self.bw_up_kiBps();
        self.localhost
            .borrow()
            .as_ref()
            .unwrap()
            .start_refilling_token_buckets(bw_down, bw_up);
        self.internet
            .borrow()
            .as_ref()
            .unwrap()
            .start_refilling_token_buckets(bw_down, bw_up);

        // must be done after the default IP exists so tracker_heartbeat works
        if let Some(heartbeat_interval) = self.params.heartbeat_interval {
            let heartbeat_interval = SimulationTime::to_c_simtime(Some(heartbeat_interval));
            let tracker = unsafe {
                cshadow::tracker_new(
                    self,
                    heartbeat_interval,
                    self.params.heartbeat_log_level,
                    self.params.heartbeat_log_info,
                )
            };
            // SAFETY: we synchronize access to the Host's tracker using a RefCell.
            self.tracker
                .borrow_mut()
                .replace(unsafe { SyncSendPointer::new(tracker) });
        }
    }

    pub fn shutdown(&self) {
        self.continue_execution_timer();

        debug!("shutting down host {}", self.name());

        // Need to drop the interfaces early because they trigger worker accesses
        // that will not be valid at the normal drop time. The interfaces will
        // become None after this and should not be unwrapped anymore.
        // TODO: clean this up when removing the interface's C internals.
        {
            self.localhost.replace(None);
            self.internet.replace(None);
        }

        assert!(self.processes.borrow().is_empty());

        // Deregistering localhost is a no-op, so we skip it.
        Worker::with_dns(|dns| unsafe {
            let dns = dns as *const cshadow::DNS;
            cshadow::dns_deregister(dns.cast_mut(), self.default_address.borrow().ptr())
        });

        self.stop_execution_timer();
        #[cfg(feature = "perf_timers")]
        info!(
            "host '{}' has been shut down, total execution time was {:?}",
            self.name(),
            self.execution_timer.borrow().elapsed()
        );
    }

    pub fn free_all_applications(&self) {
        trace!("start freeing applications for host '{}'", self.name());
        let processes = std::mem::take(&mut *self.processes.borrow_mut());
        for (_id, process) in processes.into_iter() {
            unsafe { cshadow::process_stop(process.ptr()) };
            unsafe { cshadow::process_free(process.ptr()) };
        }
        trace!("done freeing application for host '{}'", self.name());
    }

    pub fn execute(&self, until: EmulatedTime) {
        loop {
            let mut event = {
                let mut event_queue = self.event_queue.lock().unwrap();
                match event_queue.next_event_time() {
                    Some(t) if t < until => {}
                    _ => break,
                };
                event_queue.pop().unwrap()
            };

            {
                let mut cpu = self.cpu.borrow_mut();
                cpu.update_time(event.time());
                let cpu_delay = cpu.delay();
                if cpu_delay > SimulationTime::ZERO {
                    trace!(
                        "event blocked on CPU, rescheduled for {:?} from now",
                        cpu_delay
                    );

                    // track the event delay time
                    let tracker = self.tracker.borrow_mut();
                    if let Some(tracker) = &*tracker {
                        unsafe {
                            cshadow::tracker_addVirtualProcessingDelay(
                                tracker.ptr(),
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

    pub fn packets_are_available_to_receive(&self) {
        // TODO: ideally we call
        //   `self.internet.borrow().as_ref().unwrap().receive_packets(self);`
        // but that causes a double-borrow loop. See `host_socketWantsToSend()`.
        unsafe {
            let netif_ptr = self.internet.borrow().as_ref().unwrap().borrow_inner();
            cshadow::networkinterface_receivePackets(netif_ptr, self)
        };
    }

    /// Locks the Host's shared memory, caching the lock internally.
    ///
    /// Dropping the Host before calling [`Host::unlock_shmem`] will panic.
    ///
    /// TODO: Consider removing this API once we don't need to cache the lock for the C API.
    pub fn lock_shmem(&self) {
        // We're extending this lifetime to extend the lifetime of `lock`, below, without
        // having to `transmute` the type itself.
        //
        // SAFETY:
        // * We ensure that `self.shim_shmem_lock` doesn't outlive `self.shim_shmem`.
        //   See SAFETY requirements on Self::shim_shmem_lock itself.
        // * We never mutate `self.shim_shmem` nor borrow the internals of
        //   `self.shim_shmem.protected` while the lock is held, since that would
        //   conflict with the cached guard's mutable reference.
        // * `ShMemBlock` guarantees that its data doesn't move even if the block does.
        //    So moving `shim_shmem` (e.g. by moving `self`) doesn't invalidate the lock.
        let shim_shmem: &'static ShMemBlock<HostShmem> =
            unsafe { self.shim_shmem.get().as_ref().unwrap() };
        let lock = shim_shmem.protected().lock();
        let prev = self
            .shim_shmem_lock
            .borrow_mut()
            .replace(UnsafeCell::new(lock));
        assert!(prev.is_none());
    }

    pub fn unlock_shmem(&self) {
        let prev = self.shim_shmem_lock.borrow_mut().take();
        assert!(prev.is_some());
    }

    pub fn is_interface_available(
        &self,
        protocol_type: cshadow::ProtocolType,
        src: SocketAddrV4,
        dst: SocketAddrV4,
    ) -> bool {
        if src.ip().is_unspecified() {
            // Check that all interfaces are available.
            !self
                .localhost
                .borrow()
                .as_ref()
                .unwrap()
                .is_associated(protocol_type, src.port(), dst)
                && !self.internet.borrow().as_ref().unwrap().is_associated(
                    protocol_type,
                    src.port(),
                    dst,
                )
        } else {
            // The interface is not available if it does not exist.
            match self.interface(*src.ip()) {
                Some(i) => !i.is_associated(protocol_type, src.port(), dst),
                None => false,
            }
        }
    }

    // Returns a random port in host byte order
    pub fn get_random_free_port(
        &self,
        protocol_type: cshadow::ProtocolType,
        interface_ip: Ipv4Addr,
        peer: SocketAddrV4,
    ) -> Option<u16> {
        // we need a random port that is free everywhere we need it to be.
        // we have two modes here: first we just try grabbing a random port until we
        // get a free one. if we cannot find one fast enough, then as a fallback we
        // do an inefficient linear search that is guaranteed to succeed or fail.

        // if choosing randomly doesn't succeed within 10 tries, then we have already
        // allocated a lot of ports (>90% on average). then we fall back to linear search.
        for _ in 0..10 {
            let random_port = self
                .random
                .borrow_mut()
                .gen_range(MIN_RANDOM_PORT..=u16::MAX);

            // this will check all interfaces in the case of INADDR_ANY
            if self.is_interface_available(
                protocol_type,
                SocketAddrV4::new(interface_ip, random_port),
                peer,
            ) {
                return Some(random_port);
            }
        }

        // now if we tried too many times and still don't have a port, fall back
        // to a linear search to make sure we get a free port if we have one.
        // but start from a random port instead of the min.
        let start = self
            .random
            .borrow_mut()
            .gen_range(MIN_RANDOM_PORT..=u16::MAX);
        let mut port = start;
        loop {
            port = if port == u16::MAX {
                MIN_RANDOM_PORT
            } else {
                port + 1
            };
            if port == start {
                break;
            }
            if self.is_interface_available(
                protocol_type,
                SocketAddrV4::new(interface_ip, port),
                peer,
            ) {
                return Some(port);
            }
        }

        warn!("unable to find free ephemeral port for {protocol_type} peer {peer}");
        None
    }

    /// Timestamp Counter emulation for this Host. It ticks at the same rate as
    /// the native Timestamp Counter, if we were able to find it.
    pub fn tsc(&self) -> &Tsc {
        &self.tsc
    }
}

impl Drop for Host {
    fn drop(&mut self) {
        let default_addr = self.default_address.borrow().ptr();
        if !default_addr.is_null() {
            unsafe { cshadow::address_unref(default_addr) };
        }

        if let Some(tracker) = self.tracker.borrow_mut().take() {
            debug_assert!(!tracker.ptr().is_null());
            unsafe { cshadow::tracker_free(tracker.ptr()) };
        };

        let futex_table = self.futex_table.borrow_mut().ptr();
        debug_assert!(!futex_table.is_null());
        unsafe { cshadow::futextable_unref(futex_table) };

        // Validate that the shmem lock isn't held, which would potentially
        // violate the SAFETY argument in `lock_shmem`. (AFAIK Rust makes no formal
        // guarantee about the order in which fields are dropped)
        assert!(self.shim_shmem_lock.borrow().is_none());
    }
}

mod export {
    use libc::{in_addr_t, in_port_t};
    use rand::{Rng, RngCore};
    use shadow_shim_helper_rs::shim_shmem;
    use shadow_shmem::allocator::ShMemBlockSerialized;
    use std::{
        ops::{Deref, DerefMut},
        os::raw::c_char,
        time::Duration,
    };

    use crate::{
        cshadow::{CEmulatedTime, CSimulationTime},
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
        hostrc.get_new_process_id().into()
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_getNewPacketID(hostrc: *const Host) -> u64 {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.get_new_packet_id()
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_freeAllApplications(hostrc: *const Host) {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.free_all_applications()
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_getID(hostrc: *const Host) -> HostId {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.id()
    }

    /// SAFETY: The returned pointer belongs to Host, and is invalidated when
    /// `host` is moved or freed.
    #[no_mangle]
    pub unsafe extern "C" fn host_getTsc(host: *const Host) -> *const Tsc {
        let hostrc = unsafe { host.as_ref().unwrap() };
        hostrc.tsc()
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_getName(hostrc: *const Host) -> *const c_char {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.params.hostname.as_ptr()
    }

    /// SAFETY: Returned pointer belongs to Host, and is only safe to access
    /// while no other threads are accessing Host.
    #[no_mangle]
    pub unsafe extern "C" fn host_getDefaultAddress(hostrc: *const Host) -> *mut cshadow::Address {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.default_address.borrow().ptr()
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
        hostrc.get_next_packet_priority()
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_autotuneReceiveBuffer(hostrc: *const Host) -> bool {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.params.autotune_recv_buf
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_autotuneSendBuffer(hostrc: *const Host) -> bool {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.params.autotune_send_buf
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_getConfiguredRecvBufSize(hostrc: *const Host) -> u64 {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.params.init_sock_recv_buf_size
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_getConfiguredSendBufSize(hostrc: *const Host) -> u64 {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.params.init_sock_send_buf_size
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_getUpstreamRouter(hostrc: *const Host) -> *mut Router {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        &mut *hostrc.upstream_router_mut()
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_get_bw_down_kiBps(hostrc: *const Host) -> u64 {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.bw_down_kiBps()
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_get_bw_up_kiBps(hostrc: *const Host) -> u64 {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.bw_up_kiBps()
    }

    /// Returns a pointer to the Host's Tracker, if there is one, otherwise
    /// NULL.
    ///
    /// SAFETY: The returned pointer belongs to and is synchronized by the Host,
    /// and is invalidated when the Host is no longer accessible to the current
    /// thread, or something else accesses its Tracker.
    #[no_mangle]
    pub unsafe extern "C" fn host_getTracker(hostrc: *const Host) -> *mut cshadow::Tracker {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        if let Some(mut tracker) = hostrc.tracker_mut() {
            &mut *tracker
        } else {
            std::ptr::null_mut()
        }
    }

    /// SAFETY: The returned pointer is owned by the Host, and will be invalidated when
    /// the Host is destroyed, and possibly when it is otherwise moved or mutated.
    #[no_mangle]
    pub unsafe extern "C" fn host_getDataPath(hostrc: *const Host) -> *const c_char {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.data_dir_path_cstring.as_ptr()
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_doesInterfaceExist(
        hostrc: *const Host,
        interface_ip: in_addr_t,
    ) -> bool {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        let ipv4 = Ipv4Addr::from(u32::from_be(interface_ip));
        ipv4.is_unspecified() || hostrc.interface(ipv4).is_some()
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_isInterfaceAvailable(
        hostrc: *const Host,
        protocol_type: cshadow::ProtocolType,
        interface_addr: in_addr_t,
        port: in_port_t,
        peer_addr: in_addr_t,
        peer_port: in_port_t,
    ) -> bool {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        let src = SocketAddrV4::new(
            Ipv4Addr::from(u32::from_be(interface_addr)),
            u16::from_be(port),
        );
        let dst = SocketAddrV4::new(
            Ipv4Addr::from(u32::from_be(peer_addr)),
            u16::from_be(peer_port),
        );
        hostrc.is_interface_available(protocol_type, src, dst)
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_associateInterface(
        hostrc: *const Host,
        socket: *const cshadow::CompatSocket,
        protocol: cshadow::ProtocolType,
        bind_ip: in_addr_t,
        bind_port: in_port_t,
        peer_ip: in_addr_t,
        peer_port: in_port_t,
    ) {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };

        let bind_ip = Ipv4Addr::from(u32::from_be(bind_ip));
        let peer_ip = Ipv4Addr::from(u32::from_be(peer_ip));
        let bind_port = u16::from_be(bind_port);
        let peer_port = u16::from_be(peer_port);

        let bind_addr = SocketAddrV4::new(bind_ip, bind_port);
        let peer_addr = SocketAddrV4::new(peer_ip, peer_port);

        // Associate the interfaces corresponding to bind_addr with socket
        if bind_addr.ip().is_unspecified() {
            // Need to associate all interfaces.
            hostrc.localhost.borrow().as_ref().unwrap().associate(
                socket,
                protocol,
                bind_addr.port(),
                peer_addr,
            );
            hostrc.internet.borrow().as_ref().unwrap().associate(
                socket,
                protocol,
                bind_addr.port(),
                peer_addr,
            );
        } else {
            // TODO: return error if interface does not exist.
            if let Some(iface) = hostrc.interface_mut(*bind_addr.ip()) {
                iface.associate(socket, protocol, bind_addr.port(), peer_addr);
            }
        }
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_disassociateInterface(
        hostrc: *const Host,
        protocol: cshadow::ProtocolType,
        sock_ip: in_addr_t,
        sock_port: in_port_t,
        peer_ip: in_addr_t,
        peer_port: in_port_t,
    ) {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };

        let sock_ip = Ipv4Addr::from(u32::from_be(sock_ip));
        let peer_ip = Ipv4Addr::from(u32::from_be(peer_ip));
        let sock_port = u16::from_be(sock_port);
        let peer_port = u16::from_be(peer_port);

        let sock_addr = SocketAddrV4::new(sock_ip, sock_port);
        let peer_addr = SocketAddrV4::new(peer_ip, peer_port);

        // Associate the interfaces corresponding to bind_addr with socket
        if sock_addr.ip().is_unspecified() {
            // Need to disassociate all interfaces.
            hostrc.localhost.borrow().as_ref().unwrap().disassociate(
                protocol,
                sock_addr.port(),
                peer_addr,
            );

            hostrc.internet.borrow().as_ref().unwrap().disassociate(
                protocol,
                sock_addr.port(),
                peer_addr,
            );
        } else {
            // TODO: return error if interface does not exist.
            if let Some(iface) = hostrc.interface_mut(*sock_addr.ip()) {
                iface.disassociate(protocol, sock_addr.port(), peer_addr);
            }
        }
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

        let interface_ip = Ipv4Addr::from(u32::from_be(interface_ip));
        let peer_addr = SocketAddrV4::new(
            Ipv4Addr::from(u32::from_be(peer_ip)),
            u16::from_be(peer_port),
        );

        hostrc
            .get_random_free_port(protocol_type, interface_ip, peer_addr)
            .unwrap_or(0)
            .to_be()
    }

    /// Returns a pointer to the Host's FutexTable.
    ///
    /// SAFETY: The returned pointer belongs to and is synchronized by the Host,
    /// and is invalidated when the Host is no longer accessible to the current
    /// thread, or something else accesses its FutexTable.
    #[no_mangle]
    pub unsafe extern "C" fn host_getFutexTable(hostrc: *const Host) -> *mut cshadow::FutexTable {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        &mut *hostrc.futextable_mut()
    }

    /// converts a virtual (shadow) tid into the native tid
    #[no_mangle]
    pub unsafe extern "C" fn host_getNativeTID(
        host: *const Host,
        virtual_pid: libc::pid_t,
        virtual_tid: libc::pid_t,
    ) -> libc::pid_t {
        let host = unsafe { host.as_ref().unwrap() };
        for process in host.processes.borrow().values() {
            let process = unsafe { process.ptr() };
            let native_tid =
                unsafe { cshadow::process_findNativeTID(process, virtual_pid, virtual_tid) };
            if native_tid > 0 {
                return native_tid;
            }
        }
        0
    }

    /// Returns the specified process, or NULL if it doesn't exist.
    #[no_mangle]
    pub unsafe extern "C" fn host_getProcess(
        host: *const Host,
        virtual_pid: libc::pid_t,
    ) -> *mut cshadow::Process {
        let host = unsafe { host.as_ref().unwrap() };
        let virtual_pid = ProcessId::try_from(virtual_pid).unwrap();
        let processes = host.processes.borrow();
        let Some(process) = processes.get(&virtual_pid) else {
            return std::ptr::null_mut();
        };
        unsafe { process.ptr() }
    }

    /// Returns the specified thread, or NULL if it doesn't exist.
    /// If you already have the thread's Process*, `process_getThread` may be more
    /// efficient.
    #[no_mangle]
    pub unsafe extern "C" fn host_getThread(
        host: *const Host,
        virtual_tid: libc::pid_t,
    ) -> *mut cshadow::Thread {
        let host = unsafe { host.as_ref().unwrap() };
        for process in host.processes.borrow().values() {
            let process = unsafe { process.ptr() };
            let thread = unsafe { cshadow::process_getThread(process, virtual_tid) };
            if !thread.is_null() {
                return thread;
            }
        }
        std::ptr::null_mut()
    }

    /// Returns host-specific state that's kept in memory shared with the shim(s).
    #[no_mangle]
    pub unsafe extern "C" fn host_getSharedMem(
        hostrc: *const Host,
    ) -> *const shim_shmem::export::ShimShmemHost {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        // SAFETY: The requirements documented on `shim_shmem`, that we don't move
        // `shim_shmem` or otherwise invalidate the lock, are upheld since we aren't
        // exposing a mutable pointer.
        unsafe { hostrc.shim_shmem.get().as_ref().unwrap().deref() }
    }

    /// Returns the lock, or panics if the lock isn't held by Shadow.
    ///
    /// Generally the lock can and should be held when Shadow is running, and *not*
    /// held when any of the host's managed threads are running (leaving it available
    /// to be taken by the shim). While this can be a little fragile to ensure
    /// properly, debug builds detect if we get it wrong (e.g. we try accessing
    /// protected data without holding the lock, or the shim tries to take the lock
    /// but can't).
    ///
    /// SAFETY: The returned pointer is invalidated when the memory is unlocked, e.g.
    /// via `host_unlockShimShmemLock`.
    #[no_mangle]
    pub unsafe extern "C" fn host_getShimShmemLock(
        hostrc: *const Host,
    ) -> *mut shim_shmem::export::ShimShmemHostLock {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        let mut opt_lock = hostrc.shim_shmem_lock.borrow_mut();
        let lock = opt_lock.as_mut().unwrap();
        // SAFETY: The caller is responsible for not accessing the returned pointer
        // after the lock has been released.
        unsafe { lock.get().as_mut().unwrap().deref_mut() }
    }

    /// Take the host's shared memory lock. See `host_getShimShmemLock`.
    #[no_mangle]
    pub unsafe extern "C" fn host_lockShimShmemLock(hostrc: *const Host) {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.lock_shmem()
    }

    /// Release the host's shared memory lock. See `host_getShimShmemLock`.
    #[no_mangle]
    pub unsafe extern "C" fn host_unlockShimShmemLock(hostrc: *const Host) {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.unlock_shmem()
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_serializeShmem(hostrc: *const Host) -> ShMemBlockSerialized {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        // SAFETY: wrt the `shim_shmem` field requirements: We're calling an
        // immutable method of `ShMemBlock`; this doesn't touch the HostShmem
        // data (including HostShmem::protected).
        unsafe { hostrc.shim_shmem.get().as_ref().unwrap().serialize() }
    }

    /// Returns the next value and increments our monotonically increasing
    /// determinism sequence counter. The resulting values can be sorted to
    /// established a deterministic ordering, which can be useful when iterating
    /// items that are otherwise inconsistently ordered (e.g. hash table iterators).
    #[no_mangle]
    pub unsafe extern "C" fn host_getNextDeterministicSequenceValue(hostrc: *const Host) -> u64 {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.get_next_deterministic_sequence_value()
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
        host.random_mut().gen()
    }

    /// Fills the buffer with pseudo-random bytes.
    #[no_mangle]
    pub extern "C" fn host_rngNextNBytes(host: *const Host, buf: *mut u8, len: usize) {
        let host = unsafe { host.as_ref().unwrap() };
        let buf = unsafe { std::slice::from_raw_parts_mut(buf, len) };
        host.random_mut().fill_bytes(buf);
    }

    #[no_mangle]
    pub extern "C" fn host_paramsCpuFrequencyHz(host: *const Host) -> u64 {
        let host = unsafe { host.as_ref().unwrap() };
        host.params.cpu_frequency
    }

    #[no_mangle]
    pub extern "C" fn host_addDelayNanos(host: *const Host, delay_nanos: u64) {
        let host = unsafe { host.as_ref().unwrap() };
        let delay = Duration::from_nanos(delay_nanos);
        host.cpu.borrow_mut().add_delay(delay);
    }

    #[no_mangle]
    pub extern "C" fn host_paramsHeartbeatInterval(host: *const Host) -> CSimulationTime {
        let host = unsafe { host.as_ref().unwrap() };
        SimulationTime::to_c_simtime(host.params.heartbeat_interval)
    }

    #[no_mangle]
    pub extern "C" fn host_paramsHeartbeatLogLevel(host: *const Host) -> LogLevel {
        let host = unsafe { host.as_ref().unwrap() };
        host.params.heartbeat_log_level
    }

    #[no_mangle]
    pub extern "C" fn host_paramsHeartbeatLogInfo(host: *const Host) -> cshadow::LogInfoFlags {
        let host = unsafe { host.as_ref().unwrap() };
        host.params.heartbeat_log_info
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_socketWantsToSend(
        hostrc: *const Host,
        socket: *const cshadow::CompatSocket,
        addr: in_addr_t,
    ) {
        let host = unsafe { hostrc.as_ref().unwrap() };
        let ipv4 = u32::from_be(addr).into();

        // TODO: ideally we call `iface.wants_send(socket, hostrc)` in the closure,
        // but that causes a double borrow loop. This will be fixed in Rob's next
        // PR, but will cause us to process packets slightly differently than we do now.
        // For now, we mimic the call flow of the old C code.
        if let Some(iface) = host.interface_mut(ipv4) {
            unsafe {
                cshadow::networkinterface_wantsSend(iface.borrow_inner(), host, socket);
            };
        }
    }
}
