use std::cell::{Cell, Ref, RefCell, RefMut, UnsafeCell};
use std::collections::BTreeMap;
use std::ffi::{CStr, CString, OsString};
use std::net::{Ipv4Addr, SocketAddrV4};
use std::num::NonZeroU8;
use std::ops::{Deref, DerefMut};
use std::os::unix::prelude::OsStringExt;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};

use atomic_refcell::AtomicRefCell;
use linux_api::signal::{siginfo_t, Signal};
use log::{debug, trace};
use logger::LogLevel;
use once_cell::unsync::OnceCell;
use rand::SeedableRng;
use rand_xoshiro::Xoshiro256PlusPlus;
use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::explicit_drop::ExplicitDrop;
use shadow_shim_helper_rs::rootedcell::cell::RootedCell;
use shadow_shim_helper_rs::rootedcell::rc::RootedRc;
use shadow_shim_helper_rs::rootedcell::refcell::RootedRefCell;
use shadow_shim_helper_rs::rootedcell::Root;
use shadow_shim_helper_rs::shim_shmem::{HostShmem, HostShmemProtected, ManagerShmem};
use shadow_shim_helper_rs::simulation_time::SimulationTime;
use shadow_shim_helper_rs::util::SyncSendPointer;
use shadow_shim_helper_rs::HostId;
use shadow_shmem::allocator::ShMemBlock;
use shadow_tsc::Tsc;
use vasi_sync::scmutex::SelfContainedMutexGuard;

use crate::core::configuration::{ProcessFinalState, QDiscMode};
use crate::core::sim_config::PcapConfig;
use crate::core::work::event::{Event, EventData};
use crate::core::work::event_queue::EventQueue;
use crate::core::work::task::TaskRef;
use crate::core::worker::Worker;
use crate::cshadow;
use crate::host::descriptor::socket::abstract_unix_ns::AbstractUnixNamespace;
use crate::host::descriptor::socket::inet::InetSocket;
use crate::host::futex_table::FutexTable;
use crate::host::network::interface::{FifoPacketPriority, NetworkInterface, PcapOptions};
use crate::host::network::namespace::NetworkNamespace;
use crate::host::process::Process;
use crate::host::thread::ThreadId;
use crate::network::relay::{RateLimit, Relay};
use crate::network::router::Router;
use crate::network::PacketDevice;
use crate::utility;
#[cfg(feature = "perf_timers")]
use crate::utility::perf_timer::PerfTimer;

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
    pub pcap_config: Option<PcapConfig>,
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
    pub strace_logging_options: Option<FmtOptions>,
    pub shim_log_level: LogLevel,
    pub use_new_tcp: bool,
    pub use_mem_mapper: bool,
    pub use_syscall_counters: bool,
}

use super::cpu::Cpu;
use super::process::ProcessId;
use super::syscall::formatter::FmtOptions;

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
    root: Root,

    event_queue: Arc<Mutex<EventQueue>>,

    random: RefCell<Xoshiro256PlusPlus>,

    // The upstream router that will queue packets until we can receive them.
    // This only applies to the internet interface; the localhost interface
    // does not receive packets from a router.
    router: RefCell<Router>,

    // Forwards packets out from our internet interface to the router.
    relay_inet_out: Arc<Relay>,
    // Forwards packets from the router in to our internet interface.
    relay_inet_in: Arc<Relay>,
    // Forwards packets from the localhost interface back to itself.
    relay_loopback: Arc<Relay>,

    // a statistics tracker for in/out bytes, CPU, memory, etc.
    tracker: RefCell<Option<SyncSendPointer<cshadow::Tracker>>>,

    // map address to futex objects
    futex_table: RefCell<FutexTable>,

    #[cfg(feature = "perf_timers")]
    execution_timer: RefCell<PerfTimer>,

    pub params: HostParameters,

    cpu: RefCell<Cpu>,

    net_ns: NetworkNamespace,

    // Store as a CString so that we can return a borrowed pointer to C code
    // instead of having to allocate a new string.
    //
    // TODO: Remove `data_dir_path_cstring` once we can remove `host_getDataPath`. (Or maybe don't
    // store it at all)
    data_dir_path: PathBuf,
    data_dir_path_cstring: CString,

    // virtual process and event id counter
    thread_id_counter: Cell<libc::pid_t>,
    event_id_counter: Cell<u64>,
    packet_id_counter: Cell<u64>,

    // Enables us to sort objects deterministically based on their creation order.
    determinism_sequence_counter: Cell<u64>,

    // track the order in which the application sent us application data
    packet_priority_counter: Cell<FifoPacketPriority>,

    // Owned pointers to processes.
    processes: RefCell<BTreeMap<ProcessId, RootedRc<RootedRefCell<Process>>>>,

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

    in_notify_socket_has_packets: RootedCell<bool>,

    /// Paths to be added to LD_PRELOAD of managed processes.
    preload_paths: Arc<Vec<PathBuf>>,
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
    /// # Safety
    ///
    /// `dns` must be a valid pointer, and must outlive the returned Host.
    pub unsafe fn new(
        params: HostParameters,
        host_root_path: &Path,
        raw_cpu_freq_khz: u64,
        dns: *mut cshadow::DNS,
        manager_shmem: &ShMemBlock<ManagerShmem>,
        preload_paths: Arc<Vec<PathBuf>>,
    ) -> Self {
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
        let data_dir_path = Self::make_data_dir_path(&params.hostname, host_root_path);
        let data_dir_path_cstring = utility::pathbuf_to_nul_term_cstring(data_dir_path.clone());

        let host_shmem = HostShmem::new(
            params.id,
            params.model_unblocked_syscall_latency,
            params.max_unapplied_cpu_latency,
            params.unblocked_syscall_latency,
            params.unblocked_vdso_latency,
            nix::unistd::getpid().as_raw(),
            params.native_tsc_frequency,
            params.shim_log_level,
            manager_shmem,
        );
        let shim_shmem = UnsafeCell::new(shadow_shmem::allocator::shmalloc(host_shmem));

        // Process IDs start at 1000
        let thread_id_counter = Cell::new(1000);
        let event_id_counter = Cell::new(0);
        let packet_id_counter = Cell::new(0);
        let determinism_sequence_counter = Cell::new(0);
        // Packet priorities start at 1. "0" is used for control packets.
        let packet_priority_counter = Cell::new(1);
        let tsc = Tsc::new(params.native_tsc_frequency);

        std::fs::create_dir_all(&data_dir_path).unwrap();

        // Register using the param hints.
        // We already checked that the addresses are available, so fail if they are not.

        let public_ip: Ipv4Addr = u32::from_be(params.ip_addr).into();

        let hostname: Vec<NonZeroU8> = params
            .hostname
            .as_bytes()
            .iter()
            .map(|x| (*x).try_into().unwrap())
            .collect();

        let pcap_options = params.pcap_config.as_ref().map(|x| PcapOptions {
            path: data_dir_path.clone(),
            capture_size_bytes: x.capture_size.try_into().unwrap(),
        });

        let net_ns = unsafe {
            NetworkNamespace::new(
                params.id,
                hostname,
                public_ip,
                pcap_options,
                params.qdisc,
                dns,
            )
        };

        // Packets that are not for localhost or our public ip go to the router.
        // Use `Ipv4Addr::UNSPECIFIED` for the router to encode this for our
        // routing table logic inside of `Host::get_packet_device()`.
        let router = Router::new(Ipv4Addr::UNSPECIFIED);
        let relay_inet_out = Relay::new(
            RateLimit::BytesPerSecond(params.requested_bw_up_bits / 8),
            net_ns.internet.borrow().get_address(),
        );
        let relay_inet_in = Relay::new(
            RateLimit::BytesPerSecond(params.requested_bw_down_bits / 8),
            router.get_address(),
        );
        let relay_loopback = Relay::new(
            RateLimit::Unlimited,
            net_ns.localhost.borrow().get_address(),
        );

        let in_notify_socket_has_packets = RootedCell::new(&root, false);

        let res = Self {
            info: OnceCell::new(),
            root,
            event_queue: Arc::new(Mutex::new(EventQueue::new())),
            params,
            router: RefCell::new(router),
            relay_inet_out: Arc::new(relay_inet_out),
            relay_inet_in: Arc::new(relay_inet_in),
            relay_loopback: Arc::new(relay_loopback),
            tracker: RefCell::new(None),
            futex_table: RefCell::new(FutexTable::new()),
            random,
            shim_shmem,
            shim_shmem_lock: RefCell::new(None),
            cpu,
            net_ns,
            data_dir_path,
            data_dir_path_cstring,
            thread_id_counter,
            event_id_counter,
            packet_id_counter,
            packet_priority_counter,
            determinism_sequence_counter,
            tsc,
            processes: RefCell::new(BTreeMap::new()),
            #[cfg(feature = "perf_timers")]
            execution_timer,
            in_notify_socket_has_packets,
            preload_paths,
        };

        res.stop_execution_timer();

        debug!(
            concat!(
                "Setup host id '{:?}'",
                " name '{name}'",
                " with seed {seed},",
                " {bw_up_kiBps} bwUpKiBps,",
                " {bw_down_kiBps} bwDownKiBps,",
                " {init_sock_send_buf_size} initSockSendBufSize,",
                " {init_sock_recv_buf_size} initSockRecvBufSize, ",
                " {cpu_frequency:?} cpuFrequency, ",
                " {cpu_threshold:?} cpuThreshold, ",
                " {cpu_precision:?} cpuPrecision"
            ),
            res.id(),
            name = res.info().name,
            seed = res.params.node_seed,
            bw_up_kiBps = res.bw_up_kiBps(),
            bw_down_kiBps = res.bw_down_kiBps(),
            init_sock_send_buf_size = res.params.init_sock_send_buf_size,
            init_sock_recv_buf_size = res.params.init_sock_recv_buf_size,
            cpu_frequency = res.params.cpu_frequency,
            cpu_threshold = res.params.cpu_threshold,
            cpu_precision = res.params.cpu_precision,
        );

        res
    }

    pub fn root(&self) -> &Root {
        &self.root
    }

    fn make_data_dir_path(hostname: &CStr, host_root_path: &Path) -> PathBuf {
        let hostname: OsString = { OsString::from_vec(hostname.to_bytes().to_vec()) };

        let mut data_dir_path = PathBuf::new();
        data_dir_path.push(host_root_path);
        data_dir_path.push(&hostname);
        data_dir_path
    }

    pub fn data_dir_path(&self) -> &Path {
        &self.data_dir_path
    }

    pub fn add_application(
        &self,
        start_time: SimulationTime,
        shutdown_time: Option<SimulationTime>,
        shutdown_signal: nix::sys::signal::Signal,
        plugin_name: CString,
        plugin_path: CString,
        argv: Vec<CString>,
        envv: Vec<CString>,
        pause_for_debugging: bool,
        expected_final_state: ProcessFinalState,
    ) {
        debug_assert!(shutdown_time.is_none() || shutdown_time.unwrap() > start_time);

        // Schedule spawning the process.
        let task = TaskRef::new(move |host| {
            // We can't move out of these captured variables, since TaskRef takes
            // a Fn, not a FnOnce.
            // TODO: Add support for FnOnce?
            let envv = envv.clone();
            let argv = argv.clone();
            let plugin_name = plugin_name.clone();

            let process = Process::spawn(
                host,
                plugin_name,
                &plugin_path,
                argv,
                envv,
                pause_for_debugging,
                host.params.strace_logging_options,
                expected_final_state,
            )
            .expect("Failed to initialize application {plugin_name:?}");
            let (process_id, thread_id) = {
                let process = process.borrow(host.root());
                (process.id(), process.thread_group_leader_id())
            };
            host.processes.borrow_mut().insert(process_id, process);

            if let Some(shutdown_time) = shutdown_time {
                let task = TaskRef::new(move |host| {
                    let Some(process) = host.process_borrow(process_id) else {
                        debug!("Can't send shutdown signal to process {process_id}; it no longer exists");
                        return;
                    };
                    let process = process.borrow(host.root());
                    let siginfo_t = siginfo_t::new_for_kill(
                        Signal::try_from(shutdown_signal as i32).unwrap(),
                        1,
                        0,
                    );
                    process.signal(host, None, &siginfo_t);
                });
                host.schedule_task_at_emulated_time(
                    task,
                    EmulatedTime::SIMULATION_START + shutdown_time,
                );
            }

            host.resume(process_id, thread_id);
        });
        self.schedule_task_at_emulated_time(task, EmulatedTime::SIMULATION_START + start_time);
    }

    pub fn add_and_schedule_forked_process(
        &self,
        host: &Host,
        process: RootedRc<RootedRefCell<Process>>,
    ) {
        let (process_id, thread_id) = {
            let process = process.borrow(&self.root);
            (process.id(), process.thread_group_leader_id())
        };
        host.processes.borrow_mut().insert(process_id, process);
        // Schedule process to run.
        let task = TaskRef::new(move |host| {
            host.resume(process_id, thread_id);
        });
        self.schedule_task_with_delay(task, SimulationTime::ZERO);
    }

    pub fn resume(&self, pid: ProcessId, tid: ThreadId) {
        let Some(processrc) = self
            .process_borrow(pid)
            .map(|p| RootedRc::clone(&p, &self.root))
        else {
            trace!("{pid:?} doesn't exist");
            return;
        };
        let died;
        let is_orphan;
        {
            Worker::set_active_process(&processrc);
            let process = processrc.borrow(self.root());
            process.resume(self, tid);
            Worker::clear_active_process();
            let zombie_state = process.borrow_as_zombie();
            if let Some(zombie) = zombie_state {
                died = true;
                is_orphan = zombie.reaper(self).is_none();
            } else {
                died = false;
                is_orphan = false;
            }
        };
        RootedRc::explicit_drop(processrc, &self.root);

        if !died {
            return;
        }

        // Reparent children, and collect IDs of children that are dead.
        let mut orphaned_zombie_pids: Vec<ProcessId> = self
            .processes
            .borrow()
            .iter()
            .filter_map(|(other_pid, processrc)| {
                let process = processrc.borrow(&self.root);
                if process.parent_id() != pid {
                    // Not a child of the current process
                    return None;
                }
                process.set_parent_id(ProcessId::INIT);
                let Some(z) = process.borrow_as_zombie() else {
                    // Not a zombie
                    return None;
                };
                if z.reaper(self).is_some() {
                    // Not an orphan
                    None
                } else {
                    // Is a zombie orphan child
                    Some(*other_pid)
                }
            })
            .collect();

        // Process we ran is a zombie; is it also an orphan?
        debug_assert!(died);
        if is_orphan {
            orphaned_zombie_pids.push(pid);
        }

        // Free orphaned zombies.
        let mut processes = self.processes.borrow_mut();
        for pid in orphaned_zombie_pids {
            trace!("Dropping orphan zombie process {pid:?}");
            let processrc = processes.remove(&pid).unwrap();
            RootedRc::explicit_drop(processrc, &self.root)
        }
    }

    #[track_caller]
    pub fn process_borrow(
        &self,
        id: ProcessId,
    ) -> Option<impl Deref<Target = RootedRc<RootedRefCell<Process>>> + '_> {
        Ref::filter_map(self.processes.borrow(), |processes| processes.get(&id)).ok()
    }

    /// Remove the given process from the Host, if it exists.
    #[track_caller]
    pub fn process_remove(&self, id: ProcessId) -> Option<RootedRc<RootedRefCell<Process>>> {
        self.processes.borrow_mut().remove(&id)
    }

    /// Borrow the set of processes. Generally this should only be used to
    /// iterate over the set of processes. e.g. fetching a specific process
    /// should be done via via `process_borrow`.
    // TODO: It would be preferable to return an iterator instead of the
    // collection itself. There has to be an intermediate object though since we
    // need both the borrowed map of processes, and an iterator that borrows
    // from that. I suppose we could create an abstract "Iterator factory" and
    // return that here instead of exposing BTreeMap type.
    #[track_caller]
    pub fn processes_borrow(
        &self,
    ) -> impl Deref<Target = BTreeMap<ProcessId, RootedRc<RootedRefCell<Process>>>> + '_ {
        self.processes.borrow()
    }

    pub fn cpu_borrow(&self) -> impl Deref<Target = Cpu> + '_ {
        self.cpu.borrow()
    }

    pub fn cpu_borrow_mut(&self) -> impl DerefMut<Target = Cpu> + '_ {
        self.cpu.borrow_mut()
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
        let addr = self.net_ns.default_address.ptr();
        let addr = unsafe { cshadow::address_toNetworkIP(addr) };
        u32::from_be(addr).into()
    }

    pub fn abstract_unix_namespace(
        &self,
    ) -> impl Deref<Target = Arc<AtomicRefCell<AbstractUnixNamespace>>> + '_ {
        &self.net_ns.unix
    }

    pub fn log_level(&self) -> Option<log::LevelFilter> {
        let level = self.params.log_level;
        log_c2rust::c_to_rust_log_level(level).map(|l| l.to_level_filter())
    }

    #[track_caller]
    pub fn upstream_router_borrow_mut(&self) -> impl DerefMut<Target = Router> + '_ {
        self.router.borrow_mut()
    }

    #[track_caller]
    pub fn network_namespace_borrow(&self) -> impl Deref<Target = NetworkNamespace> + '_ {
        &self.net_ns
    }

    #[track_caller]
    pub fn tracker_borrow_mut(&self) -> Option<impl DerefMut<Target = cshadow::Tracker> + '_> {
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
    pub fn futextable_borrow(&self) -> impl Deref<Target = FutexTable> + '_ {
        self.futex_table.borrow()
    }

    #[track_caller]
    pub fn futextable_borrow_mut(&self) -> impl DerefMut<Target = FutexTable> + '_ {
        self.futex_table.borrow_mut()
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
    /// Panics if we have shut down.
    pub fn interface_borrow_mut(
        &self,
        addr: Ipv4Addr,
    ) -> Option<impl DerefMut<Target = NetworkInterface> + '_> {
        self.net_ns.interface_borrow_mut(addr)
    }

    /// Returns `None` if there is no such interface.
    ///
    /// Panics if we have shut down.
    pub fn interface_borrow(
        &self,
        addr: Ipv4Addr,
    ) -> Option<impl Deref<Target = NetworkInterface> + '_> {
        self.net_ns.interface_borrow(addr)
    }

    #[track_caller]
    pub fn random_mut(&self) -> impl DerefMut<Target = Xoshiro256PlusPlus> + '_ {
        self.random.borrow_mut()
    }

    pub fn get_new_event_id(&self) -> u64 {
        let res = self.event_id_counter.get();
        self.event_id_counter.set(res + 1);
        res
    }

    pub fn get_new_thread_id(&self) -> ThreadId {
        let res = self.thread_id_counter.get();
        self.thread_id_counter.set(res + 1);
        res.try_into().unwrap()
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

    pub fn get_next_packet_priority(&self) -> FifoPacketPriority {
        let res = self.packet_priority_counter.get();
        self.packet_priority_counter
            .set(res.checked_add(1).unwrap());
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
        let event = Event::new_local(task, t, self);
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

    /// Shut down the host. This should be called while `Worker` has the active host set.
    pub fn shutdown(&self) {
        self.continue_execution_timer();

        debug!("shutting down host {}", self.name());

        // the network namespace object needs to be cleaned up before it's dropped
        Worker::with_dns(|dns| self.net_ns.cleanup(dns));

        assert!(self.processes.borrow().is_empty());

        self.stop_execution_timer();
        #[cfg(feature = "perf_timers")]
        debug!(
            "host '{}' has been shut down, total execution time was {:?}",
            self.name(),
            self.execution_timer.borrow().elapsed()
        );
    }

    pub fn free_all_applications(&self) {
        trace!("start freeing applications for host '{}'", self.name());
        let processes = std::mem::take(&mut *self.processes.borrow_mut());
        for (_id, processrc) in processes.into_iter() {
            {
                Worker::set_active_process(&processrc);
                let process = processrc.borrow(self.root());
                process.stop(self);
                Worker::clear_active_process();
                // Reparent to Shadow/INIT, since the original parent is or is
                // about to be dead.
                process.set_parent_id(ProcessId::INIT);
            }

            processrc.explicit_drop(self.root());
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
            self.continue_execution_timer();
            match event.data() {
                EventData::Packet(data) => {
                    self.upstream_router_borrow_mut()
                        .route_incoming_packet(data.into());
                    self.notify_router_has_packets();
                }
                EventData::Local(data) => TaskRef::from(data).execute(self),
            }
            self.stop_execution_timer();
            Worker::clear_current_time();
        }
    }

    pub fn next_event_time(&self) -> Option<EmulatedTime> {
        self.event_queue.lock().unwrap().next_event_time()
    }

    /// The unprotected part of the Host's shared memory.
    ///
    /// Do not try to take the lock of [`HostShmem::protected`] directly.
    /// Instead use [`Host::lock_shmem`], [`Host::shim_shmem_lock_borrow`], and
    /// [`Host::shim_shmem_lock_borrow_mut`].
    pub fn shim_shmem(&self) -> &ShMemBlock<'static, HostShmem> {
        unsafe { &*self.shim_shmem.get() }
    }

    /// Returns `true` if the host has a process that contains the specified thread.
    pub fn has_thread(&self, virtual_tid: ThreadId) -> bool {
        for process in self.processes.borrow().values() {
            let process = process.borrow(self.root());
            if process.thread_borrow(virtual_tid).is_some() {
                return true;
            }
        }

        false
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

    /// Panics if there is still an outstanding reference returned by
    /// `shim_shmem_lock_borrow` or `shim_shmem_lock_borrow_mut`.
    pub fn unlock_shmem(&self) {
        let prev = self.shim_shmem_lock.borrow_mut().take();
        assert!(prev.is_some());
    }

    pub fn shim_shmem_lock_borrow(&self) -> Option<impl Deref<Target = HostShmemProtected> + '_> {
        Ref::filter_map(self.shim_shmem_lock.borrow(), |l| {
            l.as_ref().map(|l| {
                // SAFETY: Returned object holds a checked borrow of the lock;
                // trying to release the lock before the returned object is
                // dropped will result in a panic.
                let guard = unsafe { &*l.get() };
                guard.deref()
            })
        })
        .ok()
    }

    pub fn shim_shmem_lock_borrow_mut(
        &self,
    ) -> Option<impl Deref<Target = HostShmemProtected> + DerefMut + '_> {
        RefMut::filter_map(self.shim_shmem_lock.borrow_mut(), |l| {
            l.as_ref().map(|l| {
                // SAFETY: Returned object holds a checked borrow of the lock;
                // trying to release the lock before the returned object is
                // dropped will result in a panic.
                let guard = unsafe { &mut *l.get() };
                guard.deref_mut()
            })
        })
        .ok()
    }

    /// Timestamp Counter emulation for this Host. It ticks at the same rate as
    /// the native Timestamp Counter, if we were able to find it.
    pub fn tsc(&self) -> &Tsc {
        &self.tsc
    }

    /// Get the packet device that handles packets for the given address. This
    /// could be the source device from which we forward packets, or the device
    /// that will receive and process packets with a given destination address.
    /// In the latter case, if the packet destination is not on this host, we
    /// return the router to route it to the correct host.
    pub fn get_packet_device(&self, address: Ipv4Addr) -> Ref<dyn PacketDevice> {
        if address == Ipv4Addr::LOCALHOST {
            self.net_ns.localhost.borrow()
        } else if address == self.default_ip() {
            self.net_ns.internet.borrow()
        } else {
            self.router.borrow()
        }
    }

    /// Call to trigger the forwarding of packets from the router to the network
    /// interface.
    pub fn notify_router_has_packets(&self) {
        self.relay_inet_in.notify(self);
    }

    /// Call to trigger the forwarding of packets from the network interface to
    /// the next hop (either back to the network interface for loopback, or up to
    /// the router for internet-bound packets).
    ///
    /// WARNING: This is not reentrant. Do not allow this to be called recursively. Nothing in
    /// `add_data_source()` or `notify()` can call back into this method. This includes any socket
    /// code called in any indirect way from here.
    pub fn notify_socket_has_packets(&self, addr: Ipv4Addr, socket: &InetSocket) {
        if self.in_notify_socket_has_packets.replace(&self.root, true) {
            panic!("Recursively calling host.notify_socket_has_packets()");
        }

        if let Some(iface) = self.interface_borrow(addr) {
            iface.add_data_source(socket);
            match addr {
                Ipv4Addr::LOCALHOST => self.relay_loopback.notify(self),
                _ => self.relay_inet_out.notify(self),
            };
        }

        self.in_notify_socket_has_packets.set(&self.root, false);
    }

    /// Returns the Session ID for the given process group ID, if it exists.
    pub fn process_session_id_of_group_id(&self, group_id: ProcessId) -> Option<ProcessId> {
        let processes = self.processes.borrow();
        for processrc in processes.values() {
            let process = processrc.borrow(&self.root);
            if process.group_id() == group_id {
                return Some(process.session_id());
            }
        }
        None
    }

    /// Paths of libraries that should be preloaded into managed processes.
    pub fn preload_paths(&self) -> &[PathBuf] {
        &self.preload_paths
    }
}

impl Drop for Host {
    fn drop(&mut self) {
        if let Some(tracker) = self.tracker.borrow_mut().take() {
            debug_assert!(!tracker.ptr().is_null());
            unsafe { cshadow::tracker_free(tracker.ptr()) };
        };

        // Validate that the shmem lock isn't held, which would potentially
        // violate the SAFETY argument in `lock_shmem`. (AFAIK Rust makes no formal
        // guarantee about the order in which fields are dropped)
        assert!(self.shim_shmem_lock.borrow().is_none());
    }
}

mod export {
    use std::{
        ops::{Deref, DerefMut},
        os::raw::c_char,
        time::Duration,
    };

    use libc::{in_addr_t, in_port_t};
    use rand::{Rng, RngCore};
    use shadow_shim_helper_rs::shim_shmem;

    use super::*;
    use crate::{
        cshadow::{CEmulatedTime, CSimulationTime},
        host::{process::Process, thread::Thread},
        network::router::Router,
    };

    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_execute(hostrc: *const Host, until: CEmulatedTime) {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        let until = EmulatedTime::from_c_emutime(until).unwrap();
        hostrc.execute(until)
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_nextEventTime(hostrc: *const Host) -> CEmulatedTime {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        EmulatedTime::to_c_emutime(hostrc.next_event_time())
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_getNewPacketID(hostrc: *const Host) -> u64 {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.get_new_packet_id()
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_freeAllApplications(hostrc: *const Host) {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.free_all_applications()
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_getID(hostrc: *const Host) -> HostId {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.id()
    }

    /// SAFETY: The returned pointer belongs to Host, and is invalidated when
    /// `host` is moved or freed.
    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_getTsc(host: *const Host) -> *const Tsc {
        let hostrc = unsafe { host.as_ref().unwrap() };
        hostrc.tsc()
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_getName(hostrc: *const Host) -> *const c_char {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.params.hostname.as_ptr()
    }

    /// SAFETY: Returned pointer belongs to Host, and is only safe to access
    /// while no other threads are accessing Host.
    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_getDefaultAddress(
        hostrc: *const Host,
    ) -> *mut cshadow::Address {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.net_ns.default_address.ptr()
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_getDefaultIP(hostrc: *const Host) -> in_addr_t {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        let ip = hostrc.default_ip();
        u32::from(ip).to_be()
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_getNextPacketPriority(
        hostrc: *const Host,
    ) -> FifoPacketPriority {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.get_next_packet_priority()
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_autotuneReceiveBuffer(hostrc: *const Host) -> bool {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.params.autotune_recv_buf
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_autotuneSendBuffer(hostrc: *const Host) -> bool {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.params.autotune_send_buf
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_getConfiguredRecvBufSize(hostrc: *const Host) -> u64 {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.params.init_sock_recv_buf_size
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_getConfiguredSendBufSize(hostrc: *const Host) -> u64 {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.params.init_sock_send_buf_size
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_getUpstreamRouter(hostrc: *const Host) -> *mut Router {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        &mut *hostrc.upstream_router_borrow_mut()
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_get_bw_down_kiBps(hostrc: *const Host) -> u64 {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.bw_down_kiBps()
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_get_bw_up_kiBps(hostrc: *const Host) -> u64 {
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
    pub unsafe extern "C-unwind" fn host_getTracker(hostrc: *const Host) -> *mut cshadow::Tracker {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        if let Some(mut tracker) = hostrc.tracker_borrow_mut() {
            &mut *tracker
        } else {
            std::ptr::null_mut()
        }
    }

    /// SAFETY: The returned pointer is owned by the Host, and will be invalidated when
    /// the Host is destroyed, and possibly when it is otherwise moved or mutated.
    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_getDataPath(hostrc: *const Host) -> *const c_char {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.data_dir_path_cstring.as_ptr()
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_disassociateInterface(
        hostrc: *const Host,
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

        // associate the interfaces corresponding to bind_addr with socket
        hostrc
            .net_ns
            .disassociate_interface(protocol, bind_addr, peer_addr);
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_getRandomFreePort(
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
            .net_ns
            .get_random_free_port(
                protocol_type,
                interface_ip,
                peer_addr,
                hostrc.random.borrow_mut().deref_mut(),
            )
            .unwrap_or(0)
            .to_be()
    }

    /// Returns a pointer to the Host's FutexTable.
    ///
    /// SAFETY: The returned pointer belongs to and is synchronized by the Host,
    /// and is invalidated when the Host is no longer accessible to the current
    /// thread, or something else accesses its FutexTable.
    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_getFutexTable(hostrc: *const Host) -> *mut FutexTable {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        &mut *hostrc.futextable_borrow_mut()
    }

    /// Returns the specified process, or NULL if it doesn't exist.
    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_getProcess(
        host: *const Host,
        virtual_pid: libc::pid_t,
    ) -> *const Process {
        let host = unsafe { host.as_ref().unwrap() };
        let virtual_pid = ProcessId::try_from(virtual_pid).unwrap();
        host.process_borrow(virtual_pid)
            .map(|x| &*x.borrow(host.root()) as *const _)
            .unwrap_or(std::ptr::null_mut())
    }

    /// Returns the specified thread, or NULL if it doesn't exist.
    /// If you already have the thread's Process*, `process_getThread` may be more
    /// efficient.
    ///
    /// # Safety
    ///
    /// The pointer should not be accessed from threads other than the calling thread,
    /// or after `host` is no longer active on the current thread.
    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_getThread(
        host: *const Host,
        virtual_tid: libc::pid_t,
    ) -> *const Thread {
        let host = unsafe { host.as_ref().unwrap() };
        let tid = ThreadId::try_from(virtual_tid).unwrap();
        for process in host.processes.borrow().values() {
            let process = process.borrow(host.root());
            if let Some(thread) = process.thread_borrow(tid) {
                // We're returning a pointer to the Thread itself after having
                // dropped the borrow. In addition to the requirements noted for the calling code,
                // this could cause soundness issues if we were to ever take mutable borrows of
                // the RootedRefCell, since it'd be difficult to ensure we didn't have any simultaneous
                // additional references from dereferencing a C pointer.
                //
                // TODO: Add a variant of RootedRefCell that doesn't allow
                // mutable borrows, use it for Thread, and name that type
                // explicitly here to ensure a compilation error if the type is
                // changed again to one that would allow mutable references.
                let thread = thread.borrow(host.root());
                return &*thread as *const _;
            };
        }
        std::ptr::null_mut()
    }

    /// Returns host-specific state that's kept in memory shared with the shim(s).
    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_getSharedMem(
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
    pub unsafe extern "C-unwind" fn host_getShimShmemLock(
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
    pub unsafe extern "C-unwind" fn host_lockShimShmemLock(hostrc: *const Host) {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.lock_shmem()
    }

    /// Release the host's shared memory lock. See `host_getShimShmemLock`.
    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_unlockShimShmemLock(hostrc: *const Host) {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.unlock_shmem()
    }

    /// Returns the next value and increments our monotonically increasing
    /// determinism sequence counter. The resulting values can be sorted to
    /// established a deterministic ordering, which can be useful when iterating
    /// items that are otherwise inconsistently ordered (e.g. hash table iterators).
    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_getNextDeterministicSequenceValue(
        hostrc: *const Host,
    ) -> u64 {
        let hostrc = unsafe { hostrc.as_ref().unwrap() };
        hostrc.get_next_deterministic_sequence_value()
    }

    /// Schedule a task for this host at time 'time'.
    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_scheduleTaskAtEmulatedTime(
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
    pub unsafe extern "C-unwind" fn host_scheduleTaskWithDelay(
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
    pub unsafe extern "C-unwind" fn host_rngDouble(host: *const Host) -> f64 {
        let host = unsafe { host.as_ref().unwrap() };
        host.random_mut().gen()
    }

    /// Fills the buffer with pseudo-random bytes.
    #[no_mangle]
    pub extern "C-unwind" fn host_rngNextNBytes(host: *const Host, buf: *mut u8, len: usize) {
        let host = unsafe { host.as_ref().unwrap() };
        let buf = unsafe { std::slice::from_raw_parts_mut(buf, len) };
        host.random_mut().fill_bytes(buf);
    }

    #[no_mangle]
    pub extern "C-unwind" fn host_paramsCpuFrequencyHz(host: *const Host) -> u64 {
        let host = unsafe { host.as_ref().unwrap() };
        host.params.cpu_frequency
    }

    #[no_mangle]
    pub extern "C-unwind" fn host_addDelayNanos(host: *const Host, delay_nanos: u64) {
        let host = unsafe { host.as_ref().unwrap() };
        let delay = Duration::from_nanos(delay_nanos);
        host.cpu.borrow_mut().add_delay(delay);
    }

    #[no_mangle]
    pub extern "C-unwind" fn host_paramsHeartbeatInterval(host: *const Host) -> CSimulationTime {
        let host = unsafe { host.as_ref().unwrap() };
        SimulationTime::to_c_simtime(host.params.heartbeat_interval)
    }

    #[no_mangle]
    pub extern "C-unwind" fn host_paramsHeartbeatLogLevel(host: *const Host) -> LogLevel {
        let host = unsafe { host.as_ref().unwrap() };
        host.params.heartbeat_log_level
    }

    #[no_mangle]
    pub extern "C-unwind" fn host_paramsHeartbeatLogInfo(
        host: *const Host,
    ) -> cshadow::LogInfoFlags {
        let host = unsafe { host.as_ref().unwrap() };
        host.params.heartbeat_log_info
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_socketWantsToSend(
        hostrc: *const Host,
        socket: *const InetSocket,
        addr: in_addr_t,
    ) {
        let host = unsafe { hostrc.as_ref().unwrap() };
        let socket = unsafe { socket.as_ref().unwrap() };
        let addr = u32::from_be(addr).into();
        host.notify_socket_has_packets(addr, socket);
    }

    #[no_mangle]
    pub unsafe extern "C-unwind" fn host_continue(
        host: *const Host,
        pid: libc::pid_t,
        tid: libc::pid_t,
    ) {
        let host = unsafe { host.as_ref().unwrap() };
        host.resume(pid.try_into().unwrap(), tid.try_into().unwrap())
    }
}
