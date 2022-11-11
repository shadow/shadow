use crate::core::support::configuration::QDiscMode;
use crate::core::work::event::Event;
use crate::core::work::event_queue::EventQueue;
use crate::core::work::task::TaskRef;
use crate::core::worker::Worker;
use crate::cshadow;
use crate::host::descriptor::socket::abstract_unix_ns::AbstractUnixNamespace;
use crate::host::network_interface::NetworkInterface;
use crate::network::router::Router;
use crate::utility::{self, SyncSendPointer};
use atomic_refcell::AtomicRefCell;
use log::{info, trace, warn};
use logger::LogLevel;
use once_cell::unsync::OnceCell;
use rand::{Rng, SeedableRng};
use rand_xoshiro::Xoshiro256PlusPlus;
use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::rootedcell::refcell::RootedRefCell;
use shadow_shim_helper_rs::rootedcell::Root;
use shadow_shim_helper_rs::scmutex::SelfContainedMutexGuard;
use shadow_shim_helper_rs::shim_shmem::{HostShmem, HostShmemProtected};
use shadow_shim_helper_rs::simulation_time::SimulationTime;
use shadow_shim_helper_rs::HostId;
use shadow_shmem::allocator::ShMemBlock;
use std::cell::{Cell, RefCell};
use std::ffi::{CString, OsString};
use std::net::{Ipv4Addr, SocketAddrV4};
use std::os::raw::c_char;
use std::os::unix::prelude::OsStringExt;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};

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
    pub model_unblocked_syscall_latency: bool,
    pub max_unapplied_cpu_latency: SimulationTime,
    pub unblocked_syscall_latency: SimulationTime,
    pub unblocked_vdso_latency: SimulationTime,
}

use super::cpu::Cpu;

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
    params: HostParameters,

    cpu: RefCell<Option<Cpu>>,

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
    // TODO: Store as PathBuf once we can remove `host_getDataPath`. (Or maybe
    // don't store it at all)
    data_dir_path: RootedRefCell<Option<CString>>,

    // virtual process and event id counter
    process_id_counter: Cell<u32>,
    event_id_counter: Cell<u64>,
    packet_id_counter: Cell<u64>,

    // Enables us to sort objects deterministically based on their creation order.
    determinism_sequence_counter: Cell<u64>,

    // track the order in which the application sent us application data
    packet_priority_counter: Cell<f64>,

    // Cached lock for shim_shmem. `[Host::shmem_lock]` uses unsafe code to give it
    // a 'static lifetime.
    // SAFETY:
    // * This field must be delared before `shim_shmem` so that it's dropped
    //   before it
    // * Non `unsafe` APIs shouldn't expose this with a lifetime longer than `self`.
    shim_shmem_lock: RefCell<Option<SelfContainedMutexGuard<'static, HostShmemProtected>>>,
    // Shared memory with the shim.
    shim_shmem: ShMemBlock<'static, HostShmem>,
}

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
    pub fn new(params: HostParameters) -> Self {
        // Ok because hostc_new copies params rather than saving this pointer.
        // TODO: remove params from HostCInternal.
        let chost = unsafe { cshadow::hostc_new(params.id, params.hostname.as_ptr()) };
        let root = Root::new();
        let random = RootedRefCell::new(&root, Xoshiro256PlusPlus::seed_from_u64(params.node_seed));
        let cpu = RefCell::new(None);
        let data_dir_path = RootedRefCell::new(&root, None);
        let default_address = RefCell::new(unsafe { SyncSendPointer::new(std::ptr::null_mut()) });

        let host_shmem = HostShmem::new(
            params.id,
            params.model_unblocked_syscall_latency,
            params.max_unapplied_cpu_latency,
            params.unblocked_syscall_latency,
            params.unblocked_vdso_latency,
        );
        let shim_shmem = shadow_shmem::allocator::Allocator::global().alloc(host_shmem);

        // Process IDs start at 1000
        let process_id_counter = Cell::new(1000);
        let event_id_counter = Cell::new(0);
        let packet_id_counter = Cell::new(0);
        let determinism_sequence_counter = Cell::new(0);
        // Packet priorities start at 1.0. "0.0" is used for control packets.
        let packet_priority_counter = Cell::new(1.0);
        Self {
            chost: unsafe { SyncSendPointer::new(chost) },
            default_address,
            info: OnceCell::new(),
            root,
            event_queue: Arc::new(Mutex::new(EventQueue::new())),
            params,
            random,
            shim_shmem,
            shim_shmem_lock: RefCell::new(None),
            abstract_unix_namespace: Arc::new(AtomicRefCell::new(AbstractUnixNamespace::new())),
            cpu,
            localhost: RefCell::new(None),
            internet: RefCell::new(None),
            data_dir_path,
            process_id_counter,
            event_id_counter,
            packet_id_counter,
            packet_priority_counter,
            determinism_sequence_counter,
        }
    }

    pub unsafe fn setup(
        &self,
        dns: *mut cshadow::DNS,
        raw_cpu_freq_khz: u64,
        host_root_path: &Path,
    ) {
        self.cpu.borrow_mut().replace(Cpu::new(
            self.params.cpu_frequency,
            raw_cpu_freq_khz,
            self.params.cpu_threshold,
            self.params.cpu_precision,
        ));

        {
            let data_dir_path = self.data_dir_path(host_root_path);
            std::fs::create_dir_all(&data_dir_path).unwrap();

            let data_dir_path = utility::pathbuf_to_nul_term_cstring(data_dir_path);
            self.data_dir_path
                .borrow_mut(&self.root)
                .replace(data_dir_path);
        }

        // Register using the param hints.
        // We already checked that the addresses are available, so fail if they are not.
        let local_ipv4 = u32::from(Ipv4Addr::LOCALHOST).to_be();
        let local_addr = unsafe {
            cshadow::dns_register(
                dns,
                self.params.id,
                self.params.hostname.as_ptr(),
                local_ipv4,
            )
        };
        assert!(!local_addr.is_null());

        let inet_addr = unsafe {
            cshadow::dns_register(
                dns,
                self.params.id,
                self.params.hostname.as_ptr(),
                self.params.ip_addr,
            )
        };
        assert!(!inet_addr.is_null());
        *self.default_address.borrow_mut() = unsafe { SyncSendPointer::new(inet_addr) };

        unsafe { cshadow::hostc_setup(self) }

        // Virtual addresses and interfaces for managing network I/O
        let localhost = unsafe {
            NetworkInterface::new(
                self.id(),
                local_addr,
                self.pcap_dir_path(host_root_path),
                self.params.pcap_capture_size,
                self.params.qdisc,
                false,
            )
        };
        self.localhost.borrow_mut().replace(localhost);

        let internet = unsafe {
            NetworkInterface::new(
                self.id(),
                inet_addr,
                self.pcap_dir_path(host_root_path),
                self.params.pcap_capture_size,
                self.params.qdisc,
                true,
            )
        };
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

        // Cleanup
        unsafe { cshadow::address_unref(local_addr) };
    }

    fn data_dir_path(&self, host_root_path: &Path) -> PathBuf {
        let hostname: OsString = { OsString::from_vec(self.params.hostname.to_bytes().to_vec()) };

        let mut data_dir_path = PathBuf::new();
        data_dir_path.push(host_root_path);
        data_dir_path.push(&hostname);
        data_dir_path
    }

    fn pcap_dir_path(&self, host_root_path: &Path) -> Option<PathBuf> {
        let Some(pcap_dir) = &self.params.pcap_dir else {
            return None;
        };
        let path_string: OsString = { OsString::from_vec(pcap_dir.to_bytes().to_vec()) };

        let mut path = self.data_dir_path(host_root_path);
        // If relative it will append, if absolute it will replace.
        path.push(PathBuf::from(path_string));
        path.canonicalize().ok()
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

    pub fn upstream_router(&self) -> *mut Router {
        unsafe { cshadow::hostc_getUpstreamRouter(self.chost()) }
    }

    fn interface(&self, addr: Ipv4Addr) -> Option<&RefCell<Option<NetworkInterface>>> {
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

    /// Run `f` with a reference to the network interface associated with
    /// `addr`, or returns `None` if there is no such interface.
    ///
    /// Panics if we have not yet initialized our network interfaces yet.
    #[must_use]
    fn with_interface_mut<Func, Res>(&self, addr: Ipv4Addr, f: Func) -> Option<Res>
    where
        Func: FnOnce(&mut NetworkInterface) -> Res,
    {
        match self.interface(addr) {
            Some(rc) => {
                let mut iface = rc.borrow_mut();
                Some(f(iface.as_mut().unwrap()))
            }
            None => None,
        }
    }

    pub fn with_random_mut<Res>(&self, f: impl FnOnce(&mut Xoshiro256PlusPlus) -> Res) -> Res {
        let mut rng = self.random.borrow_mut(&self.root);
        f(&mut *rng)
    }

    pub fn get_new_event_id(&self) -> u64 {
        let res = self.event_id_counter.get();
        self.event_id_counter.set(res + 1);
        res
    }

    pub fn get_new_process_id(&self) -> u32 {
        let res = self.process_id_counter.get();
        self.process_id_counter.set(res + 1);
        res
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
        if event.time() >= self.params.sim_end_time {
            return false;
        }
        self.event_queue.lock().unwrap().push(event);
        true
    }

    pub fn boot(&self) {
        unsafe { cshadow::hostc_boot(self) };

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
    }

    pub fn shutdown(&self) {
        // Need to drop the interfaces early because they trigger worker accesses
        // that will not be valid at the normal drop time. The interfaces will
        // become None after this and should not be unwrapped anymore.
        // TODO: clean this up when removing the interface's C internals.
        {
            self.localhost.replace(None);
            self.internet.replace(None);
        }

        unsafe { cshadow::hostc_shutdown(self) };

        // Deregistering localhost is a no-op, so we skip it.
        let _ = Worker::with_dns(|dns| unsafe {
            let dns = dns as *const cshadow::DNS;
            cshadow::dns_deregister(dns.cast_mut(), self.default_address.borrow().ptr())
        });
    }

    pub fn free_all_applications(&self) {
        unsafe { cshadow::hostc_freeAllApplications(self) };
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
                let mut opt_cpu = self.cpu.borrow_mut();
                let cpu = opt_cpu.as_mut().unwrap();
                cpu.update_time(event.time());
                let cpu_delay = cpu.delay();
                if cpu_delay > SimulationTime::ZERO {
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
    /// TODO: Remove this API once we don't need to cache the lock for the C API.
    pub fn lock_shmem(&self) {
        let shim_shmem = &self.shim_shmem;
        let shim_shmem: *const ShMemBlock<HostShmem> = shim_shmem;
        // We're extending this lifetime to extend the lifetime of `lock`, below, without
        // having to `transmute` the type itself.
        //
        // SAFETY:
        // * We ensure that `self.shim_shmem_lock` doesn't outlive `self.shim_shmem`.
        //   We never drop `self.shim_shmem` before `self` itself is dropped, and in `Self`'s
        //   `Drop` implementation we validate that the lock isn't held.
        // * `ShMemBlock` guarantees that its data doesn't move even if the block does.
        //    So moving `shim_shmem` (e.g. by moving `self`) doesn't invalidate the lock.
        // * We never mutate `self.shim_shmem` nor borrow the internals of
        //   `self.shim_shmem.protected` while the lock is held, since that would
        //   conflict with the cached guard's mutable reference.
        let shim_shmem: &'static ShMemBlock<HostShmem> = unsafe { &*shim_shmem };

        let lock = shim_shmem.protected().lock();
        let prev = self.shim_shmem_lock.borrow_mut().replace(lock);
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
        let port = src.port().to_be();
        let peer_addr = u32::from(*dst.ip()).to_be();
        let peer_port = dst.port().to_be();

        if src.ip().is_unspecified() {
            // Check that all interfaces are available.
            !self.localhost.borrow().as_ref().unwrap().is_associated(
                protocol_type,
                port,
                peer_addr,
                peer_port,
            ) && !self.internet.borrow().as_ref().unwrap().is_associated(
                protocol_type,
                port,
                peer_addr,
                peer_port,
            )
        } else {
            // The interface is not available if it does not exist.
            match self.with_interface_mut(*src.ip(), |iface| {
                iface.is_associated(protocol_type, port, peer_addr, peer_port)
            }) {
                Some(is_associated) => !is_associated,
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
                .borrow_mut(&self.root)
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
            .borrow_mut(&self.root)
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

    pub fn chost(&self) -> *mut cshadow::HostCInternal {
        self.chost.ptr()
    }
}

impl Drop for Host {
    fn drop(&mut self) {
        let default_addr = self.default_address.borrow().ptr();
        if !default_addr.is_null() {
            unsafe { cshadow::address_unref(default_addr) };
        }
        unsafe { cshadow::hostc_unref(self.chost()) };
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
        hostrc.get_new_process_id()
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

    #[no_mangle]
    pub unsafe extern "C" fn host_getTsc(hostrc: *const Host) -> *mut cshadow::Tsc {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_getTsc(hostrc.chost()) }
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
        hostrc.upstream_router()
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

    #[no_mangle]
    pub unsafe extern "C" fn host_getTracker(hostrc: *const Host) -> *mut cshadow::Tracker {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        unsafe { cshadow::hostc_getTracker(hostrc.chost()) }
    }

    /// SAFETY: The returned pointer is owned by the Host, and will be invalidated when
    /// the Host is destroyed, and possibly when it is otherwise moved or mutated.
    #[no_mangle]
    pub unsafe extern "C" fn host_getDataPath(hostrc: *const Host) -> *const c_char {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc
            .data_dir_path
            .borrow(&hostrc.root)
            .as_ref()
            .unwrap()
            .as_ptr()
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
        bind_addr: in_addr_t,
    ) {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        let ipv4 = Ipv4Addr::from(u32::from_be(bind_addr));

        // Associate the interfaces corresponding to bind_addr with socket
        if ipv4.is_unspecified() {
            // Need to associate all interfaces.
            hostrc
                .localhost
                .borrow()
                .as_ref()
                .unwrap()
                .associate(socket);
            hostrc.internet.borrow().as_ref().unwrap().associate(socket);
        } else {
            // TODO: return error if interface does not exist.
            let _ = hostrc.with_interface_mut(ipv4, |iface| iface.associate(socket));
        }
    }

    #[no_mangle]
    pub unsafe extern "C" fn host_disassociateInterface(
        hostrc: *const Host,
        socket: *const cshadow::CompatSocket,
    ) {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };

        let mut bind_addr = 0;
        let found = unsafe {
            cshadow::compatsocket_getSocketName(socket, &mut bind_addr, std::ptr::null_mut())
        };
        if found {
            let ipv4 = Ipv4Addr::from(u32::from_be(bind_addr));

            // Associate the interfaces corresponding to bind_addr with socket
            if ipv4.is_unspecified() {
                // Need to disassociate all interfaces.
                hostrc
                    .localhost
                    .borrow()
                    .as_ref()
                    .unwrap()
                    .disassociate(socket);
                hostrc
                    .internet
                    .borrow()
                    .as_ref()
                    .unwrap()
                    .disassociate(socket);
            } else {
                // TODO: return error if interface does not exist.
                let _ = hostrc.with_interface_mut(ipv4, |iface| iface.disassociate(socket));
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
        let Some(port) = hostrc.get_random_free_port(
            protocol_type,
            Ipv4Addr::from(u32::from_be(interface_ip)),
            SocketAddrV4::new(Ipv4Addr::from(u32::from_be(peer_ip)),
            u16::from_be(peer_port))) else {
                return 0;
            };
        port.to_be()
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
    ) -> *const shim_shmem::export::ShimShmemHost {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.shim_shmem.deref()
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
        lock.deref_mut()
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
        hostrc.shim_shmem.serialize()
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

    #[no_mangle]
    pub extern "C" fn host_paramsCpuFrequencyHz(host: *const Host) -> u64 {
        let host = unsafe { host.as_ref().unwrap() };
        host.params.cpu_frequency
    }

    #[no_mangle]
    pub extern "C" fn host_addDelayNanos(host: *const Host, delay_nanos: u64) {
        let host = unsafe { host.as_ref().unwrap() };
        let delay = Duration::from_nanos(delay_nanos);
        host.cpu.borrow_mut().as_mut().unwrap().add_delay(delay);
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

    /// Should only be used from host.c
    #[no_mangle]
    pub unsafe extern "C" fn host_internal(hostrc: *const Host) -> *mut HostCInternal {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.chost()
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
        unsafe {
            if let Some(netif_ptr) = host.with_interface_mut(ipv4, |iface| iface.borrow_inner()) {
                cshadow::networkinterface_wantsSend(netif_ptr, host, socket);
            }
        };
    }
}
