use std::ffi::CString;
use std::os::unix::ffi::OsStrExt;
use std::sync::RwLock;
use std::time::Duration;

use rand::Rng;
use rand_xoshiro::Xoshiro256PlusPlus;

use crate::core::sim_config::{HostInfo, SimConfig};
use crate::core::support::configuration::ConfigOptions;
use crate::core::support::configuration::Flatten;
use crate::core::support::simulation_time::SimulationTime;
use crate::cshadow as c;
use crate::routing::network_graph::{IpAssignment, RoutingInfo};

pub struct Controller<'a> {
    // general options and user configuration for the simulation
    config: &'a ConfigOptions,

    // random source from which all node random sources originate
    random: Xoshiro256PlusPlus,

    // global network connectivity info
    ip_assignment: IpAssignment<u32>,
    routing_info: RoutingInfo<u32>,
    dns: *mut c::DNS,
    is_runahead_dynamic: bool,

    hosts: Vec<HostInfo>,
    scheduling_data: RwLock<ControllerScheduling>,
}

impl<'a> Controller<'a> {
    pub fn new(sim_config: SimConfig, config: &'a ConfigOptions) -> Self {
        let min_runahead_config: Option<Duration> =
            config.experimental.runahead.flatten().map(|x| x.into());
        let min_runahead_config: Option<SimulationTime> =
            min_runahead_config.map(|x| x.try_into().unwrap());

        let end_time: Duration = config.general.stop_time.unwrap().into();
        let end_time: SimulationTime = end_time.try_into().unwrap();

        let dns = unsafe { c::dns_new() };
        assert!(!dns.is_null());

        let smallest_latency =
            SimulationTime::from_nanos(sim_config.routing_info.get_smallest_latency_ns().unwrap());

        Self {
            is_runahead_dynamic: config.experimental.use_dynamic_runahead.unwrap(),
            config,
            hosts: sim_config.hosts,
            random: sim_config.random,
            ip_assignment: sim_config.ip_assignment,
            routing_info: sim_config.routing_info,
            dns,
            scheduling_data: RwLock::new(ControllerScheduling {
                min_runahead_config,
                smallest_latency,
                // we don't know the min runahead yet
                min_runahead: None,
                end_time,
            }),
        }
    }

    pub fn run(mut self) -> anyhow::Result<()> {
        let end_time = self.scheduling_data.read().unwrap().end_time;

        // take the host list so that we can free the list later without mutating self
        let mut hosts = self.hosts.split_off(0);

        let manager_seed = self.random.gen();

        // the manager takes a const pointer and not a reference, so we use this reference to make
        // sure we don't mutate self after the manager is created
        let _fake_ref_for_manager = &self;

        // scope used to prevent manager from being accessed after it's freed
        let rv = {
            // The controller will be responsible for distributing the actions to the managers so that
            // they all have a consistent view of the simulation, topology, etc.  For now we only have
            // one manager so send it everything.
            let manager = unsafe {
                c::manager_new(
                    &self,
                    self.config,
                    SimulationTime::to_c_simtime(Some(end_time)),
                    manager_seed,
                )
            };
            assert!(!manager.is_null());

            // add hosts and processes to the manager
            for host in &hosts {
                let hostname = CString::new(&*host.name).unwrap();
                let pcap_dir = host
                    .pcap_dir
                    .as_ref()
                    .map(|x| CString::new(x.to_str().unwrap()).unwrap());

                // scope used to enforce drop order for pointers
                {
                    let mut params = c::HostParameters {
                        // the manager sets this ID
                        id: 0,
                        // the manager sets this CPU frequency
                        cpuFrequency: 0,
                        // cast the u64 to a u32, ignoring truncated bits
                        nodeSeed: host.seed as u32,
                        hostname: hostname.as_ptr(),
                        nodeId: host.network_node_id,
                        ipAddr: match host.ip_addr.unwrap() {
                            std::net::IpAddr::V4(ip) => u32::to_be(ip.into()),
                            // the config only allows ipv4 addresses, so this shouldn't happen
                            std::net::IpAddr::V6(_) => unreachable!("IPv6 not supported"),
                        },
                        requestedBwDownBits: host.bandwidth_down_bits.unwrap(),
                        requestedBwUpBits: host.bandwidth_up_bits.unwrap(),
                        cpuThreshold: host.cpu_threshold,
                        cpuPrecision: host.cpu_precision,
                        heartbeatInterval: SimulationTime::to_c_simtime(host.heartbeat_interval),
                        heartbeatLogLevel: host
                            .heartbeat_log_level
                            .map(|x| x.to_c_loglevel())
                            .unwrap_or(c::_LogLevel_LOGLEVEL_UNSET),
                        heartbeatLogInfo: host
                            .heartbeat_log_info
                            .iter()
                            .map(|x| x.to_c_loginfoflag())
                            .reduce(|x, y| x | y)
                            .unwrap_or(c::_LogInfoFlags_LOG_INFO_FLAGS_NONE),
                        logLevel: host
                            .log_level
                            .map(|x| x.to_c_loglevel())
                            .unwrap_or(c::_LogLevel_LOGLEVEL_UNSET),
                        // the `as_ref()` is important to prevent `map()` from consuming the `Option`
                        // and using a pointer to a temporary value
                        pcapDir: pcap_dir
                            .as_ref()
                            .map(|x| x.as_ptr())
                            .unwrap_or(std::ptr::null()),
                        pcapCaptureSize: host.pcap_capture_size.try_into().unwrap(),
                        qdisc: host.qdisc,
                        recvBufSize: host.recv_buf_size,
                        autotuneRecvBuf: if host.autotune_recv_buf { 1 } else { 0 },
                        sendBufSize: host.send_buf_size,
                        autotuneSendBuf: if host.autotune_send_buf { 1 } else { 0 },
                        interfaceBufSize: host.interface_buf_size,
                    };

                    if unsafe { c::manager_addNewVirtualHost(manager, &mut params) } != 0 {
                        panic!("Could not add the host '{}'", host.name);
                    }

                    // make sure we never accidentally drop the following objects before running the
                    // unsafe code (will be a compile-time error if they were dropped)
                    let _ = &hostname;
                    let _ = &pcap_dir;
                }

                for proc in &host.processes {
                    let plugin_path = CString::new(proc.plugin.to_str().unwrap()).unwrap();
                    let env = CString::new(&*proc.env).unwrap();
                    let pause_for_debugging = host.pause_for_debugging;

                    let argv: Vec<CString> = proc
                        .args
                        .iter()
                        .map(|x| CString::new(x.as_bytes()).unwrap())
                        .collect();

                    // scope used to enforce drop order for pointers
                    {
                        let argv_ptrs: Vec<*const i8> = argv
                            .iter()
                            .map(|x| x.as_ptr())
                            // the last element of argv must be NULL
                            .chain(std::iter::once(std::ptr::null()))
                            .collect();

                        unsafe {
                            c::manager_addNewVirtualProcess(
                                manager,
                                hostname.as_ptr(),
                                plugin_path.as_ptr(),
                                SimulationTime::to_c_simtime(Some(proc.start_time)),
                                SimulationTime::to_c_simtime(proc.stop_time),
                                argv_ptrs.as_ptr(),
                                env.as_ptr(),
                                pause_for_debugging,
                            )
                        }

                        // make sure we never accidentally drop the following objects before running the
                        // unsafe code (will be a compile-time error if they were dropped)
                        let _ = &plugin_path;
                        let _ = &env;
                        let _ = &argv;
                        let _ = &argv_ptrs;
                    }
                }
            }

            // we don't use the host/process list anymore, so may as well free the memory
            hosts.clear();
            hosts.shrink_to_fit();

            log::info!("Running simulation");
            unsafe { c::manager_run(manager) };
            log::info!("Finished simulation, cleaning up now");

            unsafe { c::manager_free(manager) }
        };

        if rv != 0 {
            return Err(anyhow::anyhow!("Manager exited with code {}", rv));
        }

        // access the immutable reference to make sure we haven't mutated self
        let _fake_ref_for_manager = _fake_ref_for_manager;

        Ok(())
    }
}

impl std::ops::Drop for Controller<'_> {
    fn drop(&mut self) {
        unsafe { c::dns_free(self.dns) };
        self.dns = std::ptr::null_mut();
    }
}

/// Controller methods that are accessed by the manager.
trait SimController {
    unsafe fn get_dns(&self) -> *mut c::DNS;
    fn get_latency(&self, src: std::net::IpAddr, dst: std::net::IpAddr) -> Option<SimulationTime>;
    fn get_reliability(&self, src: std::net::IpAddr, dst: std::net::IpAddr) -> Option<f32>;
    fn increment_packet_count(&self, src: std::net::IpAddr, dst: std::net::IpAddr);
    fn is_routable(&self, src: std::net::IpAddr, dst: std::net::IpAddr) -> bool;
    fn manager_finished_current_round(
        &self,
        min_next_event_time: SimulationTime,
    ) -> Option<(SimulationTime, SimulationTime)>;
    fn update_min_runahead(&self, min_path_latency: SimulationTime);
}

impl SimController for Controller<'_> {
    unsafe fn get_dns(&self) -> *mut c::DNS {
        self.dns
    }

    fn get_latency(&self, src: std::net::IpAddr, dst: std::net::IpAddr) -> Option<SimulationTime> {
        let src = self.ip_assignment.get_node(src)?;
        let dst = self.ip_assignment.get_node(dst)?;

        Some(SimulationTime::from_nanos(
            self.routing_info.path(src, dst)?.latency_ns,
        ))
    }

    fn get_reliability(&self, src: std::net::IpAddr, dst: std::net::IpAddr) -> Option<f32> {
        let src = self.ip_assignment.get_node(src)?;
        let dst = self.ip_assignment.get_node(dst)?;

        Some(1.0 - self.routing_info.path(src, dst)?.packet_loss)
    }

    fn increment_packet_count(&self, src: std::net::IpAddr, dst: std::net::IpAddr) {
        let src = self.ip_assignment.get_node(src).unwrap();
        let dst = self.ip_assignment.get_node(dst).unwrap();

        self.routing_info.increment_packet_count(src, dst)
    }

    fn is_routable(&self, src: std::net::IpAddr, dst: std::net::IpAddr) -> bool {
        if self.ip_assignment.get_node(src).is_none() {
            return false;
        }

        if self.ip_assignment.get_node(dst).is_none() {
            return false;
        }

        // the network graph is required to be a connected graph, so they must be routable
        true
    }

    fn manager_finished_current_round(
        &self,
        min_next_event_time: SimulationTime,
    ) -> Option<(SimulationTime, SimulationTime)> {
        // TODO: once we get multiple managers, we have to block them here until they have all
        // notified us that they are finished

        let scheduling_data = self.scheduling_data.read().unwrap();
        let (new_start, new_end) = scheduling_data.next_interval_window(min_next_event_time);

        let continue_running = new_start < new_end;
        continue_running.then(|| (new_start, new_end))
    }

    fn update_min_runahead(&self, min_path_latency: SimulationTime) {
        assert!(min_path_latency > SimulationTime::ZERO);

        // if dynamic runahead is disabled, we don't update 'min_runahead'
        if !self.is_runahead_dynamic {
            return;
        }

        // helper function for checking if we should update the min_runahead
        let should_update = |scheduling_data: &ControllerScheduling| {
            if let Some(min_runahead) = scheduling_data.min_runahead {
                if min_path_latency >= min_runahead {
                    return false;
                }
            }
            // true if runahead was never set before, or new latency is smaller than the old latency
            true
        };

        // an initial check with only a read lock
        {
            let scheduling_data = self.scheduling_data.read().unwrap();

            if !should_update(&scheduling_data) {
                return;
            }
        }

        let old_runahead;
        let min_runahead_config;

        // check the same condition again, but with a write lock
        {
            let mut scheduling_data = self.scheduling_data.write().unwrap();

            if !should_update(&scheduling_data) {
                return;
            }

            // cache the values for logging
            old_runahead = scheduling_data.min_runahead;
            min_runahead_config = scheduling_data.min_runahead_config;

            // update the min runahead
            scheduling_data.min_runahead = Some(min_path_latency);
        }

        // these info messages may appear out-of-order in the log
        log::info!(
            "Minimum time runahead for next scheduling round updated from {:?} \
             to {} ns; the minimum config override is {:?} ns",
            old_runahead.map(|x| x.as_nanos()),
            min_path_latency.as_nanos(),
            min_runahead_config.map(|x| x.as_nanos())
        );
    }
}

// the min runahead time is updated by workers, so needs to be locked
struct ControllerScheduling {
    // minimum allowed runahead when sending events between nodes
    min_runahead_config: Option<SimulationTime>,
    smallest_latency: SimulationTime,
    // minimum allowed runahead when sending events between nodes
    min_runahead: Option<SimulationTime>,
    // the simulator should attempt to end immediately after this time
    end_time: SimulationTime,
}

impl ControllerScheduling {
    fn next_interval_window(
        &self,
        min_next_event_time: SimulationTime,
    ) -> (SimulationTime, SimulationTime) {
        // If the min_runahead is None, we haven't yet been given a latency value to base our
        // runahead off of (or dynamic runahead is disabled). We use the smallest latency between
        // any in-use graph nodes to start.
        let runahead = self.min_runahead.unwrap_or(self.smallest_latency);

        // the 'runahead' config option sets a lower bound for the runahead
        let runahead_config = self.min_runahead_config.unwrap_or(SimulationTime::ZERO);
        let runahead = std::cmp::max(runahead, runahead_config);
        assert_ne!(runahead, SimulationTime::ZERO);

        let start = min_next_event_time;

        // update the new window end as one interval past the new window start, making sure we don't
        // run over the experiment end time
        let end = min_next_event_time
            .checked_add(runahead)
            .unwrap_or(SimulationTime::MAX);
        let end = std::cmp::min(end, self.end_time);

        (start, end)
    }
}

mod export {
    use super::*;

    #[no_mangle]
    pub extern "C" fn controller_getDNS(controller: *const Controller) -> *mut c::DNS {
        let controller = unsafe { controller.as_ref() }.unwrap();
        unsafe { controller.get_dns() }
    }

    #[no_mangle]
    pub extern "C" fn controller_getLatency(
        controller: *const Controller,
        src: libc::in_addr_t,
        dst: libc::in_addr_t,
    ) -> c::SimulationTime {
        let controller = unsafe { controller.as_ref() }.unwrap();
        let src = std::net::IpAddr::V4(u32::from_be(src).into());
        let dst = std::net::IpAddr::V4(u32::from_be(dst).into());

        SimulationTime::to_c_simtime(controller.get_latency(src, dst))
    }

    #[no_mangle]
    pub extern "C" fn controller_getReliability(
        controller: *const Controller,
        src: libc::in_addr_t,
        dst: libc::in_addr_t,
    ) -> libc::c_float {
        let controller = unsafe { controller.as_ref() }.unwrap();
        let src = std::net::IpAddr::V4(u32::from_be(src).into());
        let dst = std::net::IpAddr::V4(u32::from_be(dst).into());

        controller.get_reliability(src, dst).unwrap()
    }

    #[no_mangle]
    pub extern "C" fn controller_incrementPacketCount(
        controller: *const Controller,
        src: libc::in_addr_t,
        dst: libc::in_addr_t,
    ) {
        let controller = unsafe { controller.as_ref() }.unwrap();
        let src = std::net::IpAddr::V4(u32::from_be(src).into());
        let dst = std::net::IpAddr::V4(u32::from_be(dst).into());

        controller.increment_packet_count(src, dst)
    }

    #[no_mangle]
    pub extern "C" fn controller_isRoutable(
        controller: *const Controller,
        src: libc::in_addr_t,
        dst: libc::in_addr_t,
    ) -> bool {
        let controller = unsafe { controller.as_ref() }.unwrap();
        let src = std::net::IpAddr::V4(u32::from_be(src).into());
        let dst = std::net::IpAddr::V4(u32::from_be(dst).into());

        controller.is_routable(src, dst)
    }

    #[no_mangle]
    pub extern "C" fn controller_managerFinishedCurrentRound(
        controller: *const Controller,
        min_next_event_time: c::SimulationTime,
        execute_window_start: *mut c::SimulationTime,
        execute_window_end: *mut c::SimulationTime,
    ) -> bool {
        let controller = unsafe { controller.as_ref() }.unwrap();
        let execute_window_start = unsafe { execute_window_start.as_mut() }.unwrap();
        let execute_window_end = unsafe { execute_window_end.as_mut() }.unwrap();
        let min_next_event_time = SimulationTime::from_c_simtime(min_next_event_time).unwrap();

        let (start, end) = match controller.manager_finished_current_round(min_next_event_time) {
            Some(x) => x,
            None => return false,
        };

        *execute_window_start = SimulationTime::to_c_simtime(Some(start));
        *execute_window_end = SimulationTime::to_c_simtime(Some(end));
        true
    }

    #[no_mangle]
    pub extern "C" fn controller_updateMinRunahead(
        controller: *const Controller,
        min_path_latency: c::SimulationTime,
    ) {
        let controller = unsafe { controller.as_ref() }.unwrap();
        let min_path_latency = SimulationTime::from_c_simtime(min_path_latency).unwrap();

        controller.update_min_runahead(min_path_latency);
    }
}
