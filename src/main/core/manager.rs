use std::cell::RefCell;
use std::collections::HashMap;
use std::ffi::{CStr, CString, OsStr, OsString};
use std::os::unix::ffi::OsStrExt;
use std::path::PathBuf;
use std::sync::atomic::AtomicU32;
use std::sync::Arc;
use std::time::Duration;

use anyhow::{self, Context};
use atomic_refcell::AtomicRefCell;
use log::warn;
use rand::seq::SliceRandom;
use rand_xoshiro::Xoshiro256PlusPlus;
use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::shim_shmem::ManagerShmem;
use shadow_shim_helper_rs::simulation_time::SimulationTime;
use shadow_shim_helper_rs::util::SyncSendPointer;
use shadow_shim_helper_rs::HostId;
use shadow_shmem::allocator::ShMemBlock;

use crate::core::configuration::{self, ConfigOptions, Flatten};
use crate::core::controller::{Controller, ShadowStatusBarState, SimController};
use crate::core::cpu;
use crate::core::resource_usage;
use crate::core::scheduler::runahead::Runahead;
use crate::core::scheduler::{HostIter, Scheduler, ThreadPerCoreSched, ThreadPerHostSched};
use crate::core::sim_config::{Bandwidth, HostInfo};
use crate::core::sim_stats;
use crate::core::worker;
use crate::cshadow as c;
use crate::host::host::{Host, HostParameters};
use crate::network::graph::{IpAssignment, RoutingInfo};
use crate::utility;
use crate::utility::childpid_watcher::ChildPidWatcher;
use crate::utility::status_bar::Status;

pub struct Manager<'a> {
    manager_config: Option<ManagerConfig>,
    controller: &'a Controller<'a>,
    config: &'a ConfigOptions,

    raw_frequency: u64,
    native_tsc_frequency: u64,
    end_time: EmulatedTime,

    data_path: PathBuf,
    hosts_path: PathBuf,

    preload_paths: Arc<Vec<PathBuf>>,

    check_fd_usage: bool,
    check_mem_usage: bool,

    meminfo_file: std::fs::File,
    shmem: ShMemBlock<'static, ManagerShmem>,
}

impl<'a> Manager<'a> {
    pub fn new(
        manager_config: ManagerConfig,
        controller: &'a Controller<'a>,
        config: &'a ConfigOptions,
        end_time: EmulatedTime,
    ) -> anyhow::Result<Self> {
        // get the system's CPU frequency
        let raw_frequency = get_raw_cpu_frequency_hz().unwrap_or_else(|e| {
            let default_freq = 2_500_000_000; // 2.5 GHz
            log::debug!(
                "Failed to get raw CPU frequency, using {} Hz instead: {}",
                default_freq,
                e
            );
            default_freq
        });

        let native_tsc_frequency = if let Some(f) = shadow_tsc::Tsc::native_cycles_per_second() {
            f
        } else {
            warn!(
                "Couldn't find native TSC frequency. Emulated rdtsc may use a rate different than managed code expects"
            );
            raw_frequency
        };

        let mut preload_paths = Vec::new();

        // we always preload the injector lib to ensure that the shim is loaded into the managed
        // processes
        const PRELOAD_INJECTOR_LIB: &str = "libshadow_injector.so";
        preload_paths.push(
            get_required_preload_path(PRELOAD_INJECTOR_LIB).with_context(|| {
                format!("Failed to get path to preload library '{PRELOAD_INJECTOR_LIB}'")
            })?,
        );

        // preload libc lib if option is enabled
        const PRELOAD_LIBC_LIB: &str = "libshadow_libc.so";
        if config.experimental.use_preload_libc.unwrap() {
            let path = get_required_preload_path(PRELOAD_LIBC_LIB).with_context(|| {
                format!("Failed to get path to preload library '{PRELOAD_LIBC_LIB}'")
            })?;
            preload_paths.push(path);
        } else {
            log::info!("Preloading the libc library is disabled");
        };

        // preload openssl rng lib if option is enabled
        const PRELOAD_OPENSSL_RNG_LIB: &str = "libshadow_openssl_rng.so";
        if config.experimental.use_preload_openssl_rng.unwrap() {
            let path = get_required_preload_path(PRELOAD_OPENSSL_RNG_LIB).with_context(|| {
                format!("Failed to get path to preload library '{PRELOAD_OPENSSL_RNG_LIB}'")
            })?;
            preload_paths.push(path);
        } else {
            log::info!("Preloading the openssl rng library is disabled");
        };

        // preload openssl crypto lib if option is enabled
        const PRELOAD_OPENSSL_CRYPTO_LIB: &str = "libshadow_openssl_crypto.so";
        if config.experimental.use_preload_openssl_crypto.unwrap() {
            let path =
                get_required_preload_path(PRELOAD_OPENSSL_CRYPTO_LIB).with_context(|| {
                    format!("Failed to get path to preload library '{PRELOAD_OPENSSL_CRYPTO_LIB}'")
                })?;
            preload_paths.push(path);
        } else {
            log::info!("Preloading the openssl crypto library is disabled");
        };

        // use the working dir to generate absolute paths
        let cwd = std::env::current_dir()?;
        let template_path = config
            .general
            .template_directory
            .flatten_ref()
            .map(|x| cwd.clone().join(x));
        let data_path = cwd.join(config.general.data_directory.as_ref().unwrap());
        let hosts_path = data_path.join("hosts");

        if let Some(template_path) = template_path {
            log::debug!(
                "Copying template directory '{}' to '{}'",
                template_path.display(),
                data_path.display()
            );

            // copy the template directory to the data directory path
            utility::copy_dir_all(&template_path, &data_path).with_context(|| {
                format!(
                    "Failed to copy template directory '{}' to '{}'",
                    template_path.display(),
                    data_path.display()
                )
            })?;

            // create the hosts directory if it doesn't exist
            let result = std::fs::create_dir(&hosts_path);
            if let Err(e) = result {
                if e.kind() != std::io::ErrorKind::AlreadyExists {
                    return Err(e).context(format!(
                        "Failed to create hosts directory '{}'",
                        hosts_path.display()
                    ));
                }
            }
        } else {
            // create the data and hosts directories
            std::fs::create_dir(&data_path).with_context(|| {
                format!("Failed to create data directory '{}'", data_path.display())
            })?;
            std::fs::create_dir(&hosts_path).with_context(|| {
                format!(
                    "Failed to create hosts directory '{}'",
                    hosts_path.display(),
                )
            })?;
        }

        // save the processed config as yaml
        let config_out_filename = data_path.join("processed-config.yaml");
        let config_out_file = std::fs::File::create(&config_out_filename).with_context(|| {
            format!("Failed to create file '{}'", config_out_filename.display())
        })?;

        serde_yaml::to_writer(config_out_file, &config).with_context(|| {
            format!(
                "Failed to write processed config yaml to file '{}'",
                config_out_filename.display()
            )
        })?;

        let meminfo_file =
            std::fs::File::open("/proc/meminfo").context("Failed to open '/proc/meminfo'")?;

        let shmem = shadow_shmem::allocator::shmalloc(ManagerShmem {
            log_start_time_micros: unsafe { c::logger_get_global_start_time_micros() },
        });

        Ok(Self {
            manager_config: Some(manager_config),
            controller,
            config,
            raw_frequency,
            native_tsc_frequency,
            end_time,
            data_path,
            hosts_path,
            preload_paths: Arc::new(preload_paths),
            check_fd_usage: true,
            check_mem_usage: true,
            meminfo_file,
            shmem,
        })
    }

    pub fn run(
        mut self,
        status_logger_state: Option<&Arc<Status<ShadowStatusBarState>>>,
    ) -> anyhow::Result<u32> {
        let mut manager_config = self.manager_config.take().unwrap();

        let min_runahead_config: Option<Duration> = self
            .config
            .experimental
            .runahead
            .flatten()
            .map(|x| x.into());
        let min_runahead_config: Option<SimulationTime> =
            min_runahead_config.map(|x| x.try_into().unwrap());

        let bootstrap_end_time: Duration = self.config.general.bootstrap_end_time.unwrap().into();
        let bootstrap_end_time: SimulationTime = bootstrap_end_time.try_into().unwrap();
        let bootstrap_end_time = EmulatedTime::SIMULATION_START + bootstrap_end_time;

        let smallest_latency = SimulationTime::from_nanos(
            manager_config
                .routing_info
                .get_smallest_latency_ns()
                .unwrap(),
        );

        let dns = unsafe { c::dns_new() };
        assert!(!dns.is_null());

        let parallelism: usize = match self.config.general.parallelism.unwrap() {
            0 => {
                let cores = cpu::count_physical_cores().try_into().unwrap();
                log::info!("The parallelism option was 0, so using parallelism={cores}");
                cores
            }
            x => x.try_into().unwrap(),
        };

        // note: there are several return points before we add these hosts to the scheduler and we
        // would leak memory if we return before then, but not worrying about that since the issues
        // will go away when we move the hosts to rust, and if we don't add them to the scheduler
        // then it means there was an error and we're going to exit anyways
        let mut hosts: Vec<_> = manager_config
            .hosts
            .iter()
            .enumerate()
            .map(|(i, x)| {
                self.build_host(HostId::from(u32::try_from(i).unwrap()), x, dns)
                    .with_context(|| format!("Failed to build host '{}'", x.name))
            })
            .collect::<anyhow::Result<_>>()?;

        // shuffle the list of hosts to make sure that they are randomly assigned by the scheduler
        hosts.shuffle(&mut manager_config.random);

        let use_cpu_pinning = self.config.experimental.use_cpu_pinning.unwrap();

        // an infinite iterator that always returns `<Option<Option<u32>>>::Some`
        let cpu_iter =
            std::iter::from_fn(|| {
                // if cpu pinning is enabled, return Some(Some(cpu_id)), otherwise return Some(None)
                Some(use_cpu_pinning.then(|| {
                    u32::try_from(unsafe { c::affinity_getGoodWorkerAffinity() }).unwrap()
                }))
            });

        // shadow is parallelized at the host level, so we don't need more parallelism than the
        // number of hosts
        let parallelism = std::cmp::min(parallelism, hosts.len());

        // should have either all `Some` values, or all `None` values
        let cpus: Vec<Option<u32>> = cpu_iter.take(parallelism).collect();
        if cpus[0].is_some() {
            log::debug!("Pinning to cpus: {:?}", cpus);
            assert!(cpus.iter().all(|x| x.is_some()));
        } else {
            log::debug!("Not pinning to CPUs");
            assert!(cpus.iter().all(|x| x.is_none()));
        }
        assert_eq!(cpus.len(), parallelism);

        // set the simulation's global state
        worker::WORKER_SHARED
            .borrow_mut()
            .replace(worker::WorkerShared {
                ip_assignment: manager_config.ip_assignment,
                routing_info: manager_config.routing_info,
                host_bandwidths: manager_config.host_bandwidths,
                // safe since the DNS type has an internal mutex
                dns: unsafe { SyncSendPointer::new(dns) },
                num_plugin_errors: AtomicU32::new(0),
                // allow the status logger's state to be updated from anywhere
                status_logger_state: status_logger_state.map(Arc::clone),
                runahead: Runahead::new(
                    self.config.experimental.use_dynamic_runahead.unwrap(),
                    smallest_latency,
                    min_runahead_config,
                ),
                child_pid_watcher: ChildPidWatcher::new(),
                event_queues: hosts
                    .iter()
                    .map(|x| (x.id(), x.event_queue().clone()))
                    .collect(),
                bootstrap_end_time,
                sim_end_time: self.end_time,
            });

        // scope used so that the scheduler is dropped before we log the global counters below
        {
            let mut scheduler = match self.config.experimental.scheduler.unwrap() {
                configuration::Scheduler::ThreadPerHost => {
                    std::thread_local! {
                        /// A thread-local required by the thread-per-host scheduler.
                        static SCHED_HOST_STORAGE: RefCell<Option<Box<Host>>> = const { RefCell::new(None) };
                    }
                    Scheduler::ThreadPerHost(ThreadPerHostSched::new(
                        &cpus,
                        &SCHED_HOST_STORAGE,
                        hosts,
                    ))
                }
                configuration::Scheduler::ThreadPerCore => {
                    Scheduler::ThreadPerCore(ThreadPerCoreSched::new(
                        &cpus,
                        hosts,
                        self.config.experimental.use_worker_spinning.unwrap(),
                    ))
                }
            };

            // initialize the thread-local Worker
            scheduler.scope(|s| {
                s.run(|thread_id| {
                    worker::Worker::new_for_this_thread(worker::WorkerThreadID(thread_id as u32))
                });
            });

            // boot each host
            scheduler.scope(|s| {
                s.run_with_hosts(move |_, hosts| {
                    for_each_host(hosts, |host| {
                        worker::Worker::set_current_time(EmulatedTime::SIMULATION_START);
                        host.lock_shmem();
                        host.boot();
                        host.unlock_shmem();
                        worker::Worker::clear_current_time();
                    });
                });
            });

            // the current simulation interval
            let mut window = Some((
                EmulatedTime::SIMULATION_START,
                EmulatedTime::SIMULATION_START + SimulationTime::NANOSECOND,
            ));

            // the next event times for each thread; allocated here to avoid re-allocating each
            // scheduling loop
            let thread_next_event_times: Vec<AtomicRefCell<Option<EmulatedTime>>> =
                vec![AtomicRefCell::new(None); scheduler.parallelism()];

            // how often to log heartbeat messages
            let heartbeat_interval = self
                .config
                .general
                .heartbeat_interval
                .flatten()
                .map(|x| Duration::from(x).try_into().unwrap());

            let mut last_heartbeat = EmulatedTime::SIMULATION_START;
            let mut time_of_last_usage_check = std::time::Instant::now();

            // the scheduling loop
            while let Some((window_start, window_end)) = window {
                // update the status logger
                let display_time = std::cmp::min(window_start, window_end);
                worker::WORKER_SHARED
                    .borrow()
                    .as_ref()
                    .unwrap()
                    .update_status_logger(|state| {
                        state.current = display_time;
                    });

                // run the events
                scheduler.scope(|s| {
                    // run the closure on each of the scheduler's threads
                    s.run_with_data(
                        &thread_next_event_times,
                        // each call of the closure is given an abstract thread-specific host
                        // iterator, and an element of 'thread_next_event_times'
                        move |_, hosts, next_event_time| {
                            let mut next_event_time = next_event_time.borrow_mut();

                            worker::Worker::reset_next_event_time();
                            worker::Worker::set_round_end_time(window_end);

                            for_each_host(hosts, |host| {
                                let host_next_event_time = {
                                    host.lock_shmem();
                                    host.execute(window_end);
                                    let host_next_event_time = host.next_event_time();
                                    host.unlock_shmem();
                                    host_next_event_time
                                };
                                *next_event_time = [*next_event_time, host_next_event_time]
                                    .into_iter()
                                    .flatten() // filter out None
                                    .reduce(std::cmp::min);
                            });

                            let packet_next_event_time = worker::Worker::get_next_event_time();

                            *next_event_time = [*next_event_time, packet_next_event_time]
                                .into_iter()
                                .flatten() // filter out None
                                .reduce(std::cmp::min);
                        },
                    );

                    // log a heartbeat message every 'heartbeat_interval' amount of simulated time
                    if let Some(heartbeat_interval) = heartbeat_interval {
                        if window_start > last_heartbeat + heartbeat_interval {
                            last_heartbeat = window_start;
                            self.log_heartbeat(window_start);
                        }
                    }

                    // check resource usage every 30 real seconds
                    let current_time = std::time::Instant::now();
                    if current_time.duration_since(time_of_last_usage_check)
                        > Duration::from_secs(30)
                    {
                        time_of_last_usage_check = current_time;
                        self.check_resource_usage();
                    }
                });

                // get the minimum next event time for all threads (also resets the next event times
                // to None while we have them borrowed)
                let min_next_event_time = thread_next_event_times
                    .iter()
                    // the take() resets it to None for the next scheduling loop
                    .filter_map(|x| x.borrow_mut().take())
                    .reduce(std::cmp::min)
                    .unwrap_or(EmulatedTime::MAX);

                log::debug!(
                    "Finished execution window [{}--{}], next event at {}",
                    (window_start - EmulatedTime::SIMULATION_START).as_nanos(),
                    (window_end - EmulatedTime::SIMULATION_START).as_nanos(),
                    (min_next_event_time - EmulatedTime::SIMULATION_START).as_nanos(),
                );

                // notify controller that we finished this round, and the time of our next event in
                // order to fast-forward our execute window if possible
                window = self
                    .controller
                    .manager_finished_current_round(min_next_event_time);
            }

            scheduler.scope(|s| {
                s.run_with_hosts(move |_, hosts| {
                    for_each_host(hosts, |host| {
                        worker::Worker::set_current_time(self.end_time);
                        host.free_all_applications();
                        host.shutdown();
                        worker::Worker::clear_current_time();
                    });
                });
            });

            // add each thread's local sim statistics to the global sim statistics.
            scheduler.scope(|s| {
                s.run(|_| {
                    worker::Worker::add_to_global_sim_stats();
                });
            });

            scheduler.join();
        }

        // simulation is finished, so update the status logger
        worker::WORKER_SHARED
            .borrow()
            .as_ref()
            .unwrap()
            .update_status_logger(|state| {
                state.current = self.end_time;
            });

        let num_plugin_errors = worker::WORKER_SHARED
            .borrow()
            .as_ref()
            .unwrap()
            .plugin_error_count();

        // drop the simulation's global state
        // must drop before the allocation counters have been checked
        worker::WORKER_SHARED.borrow_mut().take();

        // since the scheduler was dropped, all workers should have completed and the global object
        // and syscall counters should have been updated

        worker::with_global_sim_stats(|stats| {
            if self.config.experimental.use_syscall_counters.unwrap() {
                log::info!(
                    "Global syscall counts: {}",
                    stats.syscall_counts.lock().unwrap()
                );
            }
            if self.config.experimental.use_object_counters.unwrap() {
                let alloc_counts = stats.alloc_counts.lock().unwrap();
                let dealloc_counts = stats.dealloc_counts.lock().unwrap();
                log::info!("Global allocated object counts: {}", alloc_counts);
                log::info!("Global deallocated object counts: {}", dealloc_counts);

                if *alloc_counts == *dealloc_counts {
                    log::info!("We allocated and deallocated the same number of objects :)");
                } else {
                    // don't change the formatting of this line as we search for it in test cases
                    log::warn!("Memory leak detected");
                }
            }

            let stats_filename = self.data_path.clone().join("sim-stats.json");
            sim_stats::write_stats_to_file(&stats_filename, stats)
        })?;

        Ok(num_plugin_errors)
    }

    fn build_host(
        &self,
        host_id: HostId,
        host_info: &HostInfo,
        dns: *mut c::DNS,
    ) -> anyhow::Result<Box<Host>> {
        let hostname = CString::new(&*host_info.name).unwrap();

        // scope used to enforce drop order for pointers
        let host = {
            let params = HostParameters {
                // the manager sets this ID
                id: host_id,
                // the manager sets this CPU frequency
                cpu_frequency: self.raw_frequency,
                node_seed: host_info.seed,
                hostname,
                node_id: host_info.network_node_id,
                ip_addr: match host_info.ip_addr.unwrap() {
                    std::net::IpAddr::V4(ip) => u32::to_be(ip.into()),
                    // the config only allows ipv4 addresses, so this shouldn't happen
                    std::net::IpAddr::V6(_) => unreachable!("IPv6 not supported"),
                },
                sim_end_time: self.end_time,
                requested_bw_down_bits: host_info.bandwidth_down_bits.unwrap(),
                requested_bw_up_bits: host_info.bandwidth_up_bits.unwrap(),
                cpu_threshold: host_info.cpu_threshold,
                cpu_precision: host_info.cpu_precision,
                heartbeat_interval: host_info.heartbeat_interval,
                heartbeat_log_level: host_info
                    .heartbeat_log_level
                    .map(|x| x.to_c_loglevel())
                    .unwrap_or(c::_LogLevel_LOGLEVEL_UNSET),
                heartbeat_log_info: host_info
                    .heartbeat_log_info
                    .iter()
                    .map(|x| x.to_c_loginfoflag())
                    .reduce(|x, y| x | y)
                    .unwrap_or(c::_LogInfoFlags_LOG_INFO_FLAGS_NONE),
                log_level: host_info
                    .log_level
                    .map(|x| x.to_c_loglevel())
                    .unwrap_or(c::_LogLevel_LOGLEVEL_UNSET),
                pcap_config: host_info.pcap_config,
                qdisc: host_info.qdisc,
                init_sock_recv_buf_size: host_info.recv_buf_size,
                autotune_recv_buf: host_info.autotune_recv_buf,
                init_sock_send_buf_size: host_info.send_buf_size,
                autotune_send_buf: host_info.autotune_send_buf,
                native_tsc_frequency: self.native_tsc_frequency,
                model_unblocked_syscall_latency: self.config.model_unblocked_syscall_latency(),
                max_unapplied_cpu_latency: self.config.max_unapplied_cpu_latency(),
                unblocked_syscall_latency: self.config.unblocked_syscall_latency(),
                unblocked_vdso_latency: self.config.unblocked_vdso_latency(),
                strace_logging_options: self.config.strace_logging_mode(),
                shim_log_level: host_info
                    .log_level
                    .unwrap_or_else(|| self.config.general.log_level.unwrap())
                    .to_c_loglevel(),
                use_new_tcp: self.config.experimental.use_new_tcp.unwrap(),
                use_mem_mapper: self.config.experimental.use_memory_manager.unwrap(),
                use_syscall_counters: self.config.experimental.use_syscall_counters.unwrap(),
            };

            Box::new(unsafe {
                Host::new(
                    params,
                    &self.hosts_path,
                    self.raw_frequency,
                    dns,
                    self.shmem(),
                    self.preload_paths.clone(),
                )
            })
        };

        host.lock_shmem();

        for proc in &host_info.processes {
            let plugin_path =
                CString::new(proc.plugin.clone().into_os_string().as_bytes()).unwrap();
            let plugin_name = CString::new(proc.plugin.file_name().unwrap().as_bytes()).unwrap();
            let pause_for_debugging = host_info.pause_for_debugging;

            let argv: Vec<CString> = proc
                .args
                .iter()
                .map(|x| CString::new(x.as_bytes()).unwrap())
                .collect();

            let envv: Vec<CString> = proc
                .env
                .clone()
                .into_iter()
                .map(|(x, y)| {
                    let mut x: OsString = String::from(x).into();
                    x.push("=");
                    x.push(y);
                    CString::new(x.as_bytes()).unwrap()
                })
                .collect();

            host.continue_execution_timer();

            host.add_application(
                proc.start_time,
                proc.shutdown_time,
                proc.shutdown_signal,
                plugin_name,
                plugin_path,
                argv,
                envv,
                pause_for_debugging,
                proc.expected_final_state,
            );

            host.stop_execution_timer();
        }

        host.unlock_shmem();

        Ok(host)
    }

    fn log_heartbeat(&mut self, now: EmulatedTime) {
        let mut resources: libc::rusage = unsafe { std::mem::zeroed() };
        if unsafe { libc::getrusage(libc::RUSAGE_SELF, &mut resources) } != 0 {
            let err = nix::errno::Errno::last();
            log::warn!("Unable to get shadow's resource usage: {}", err);
            return;
        }

        // the sysinfo syscall also would give memory usage info, but it's less detailed
        let mem_info = resource_usage::meminfo(&mut self.meminfo_file).unwrap();

        // the linux man page says this is in kilobytes, but it seems to be in kibibytes
        let max_memory = (resources.ru_maxrss as f64) / 1048576.0; // KiB->GiB
        let user_time_minutes = (resources.ru_utime.tv_sec as f64) / 60.0;
        let system_time_minutes = (resources.ru_stime.tv_sec as f64) / 60.0;

        // tornettools assumes a specific log format for this message, so don't change it without
        // testing that tornettools can parse resource usage information from the shadow log
        // https://github.com/shadow/tornettools/blob/6c00856c3f08899da30bfc452b6a055572cc4536/tornettools/parse_rusage.py#L58-L86
        log::info!(
            "Process resource usage at simtime {} reported by getrusage(): \
            ru_maxrss={:.03} GiB, \
            ru_utime={:.03} minutes, \
            ru_stime={:.03} minutes, \
            ru_nvcsw={}, \
            ru_nivcsw={}",
            (now - EmulatedTime::SIMULATION_START).as_nanos(),
            max_memory,
            user_time_minutes,
            system_time_minutes,
            resources.ru_nvcsw,
            resources.ru_nivcsw,
        );

        // there are different ways of calculating system memory usage (for example 'free' will
        // calculate used memory differently than 'htop'), so we'll log the values we think are
        // useful, and something parsing the log can calculate whatever it wants
        log::info!(
            "System memory usage in bytes at simtime {} ns reported by /proc/meminfo: {}",
            (now - EmulatedTime::SIMULATION_START).as_nanos(),
            serde_json::to_string(&mem_info).unwrap(),
        );
    }

    fn check_resource_usage(&mut self) {
        if self.check_fd_usage {
            match self.fd_usage() {
                // if more than 90% in use
                Ok((usage, limit)) if usage > limit * 90 / 100 => {
                    log::warn!(
                        "Using more than 90% ({usage}/{limit}) of available file descriptors"
                    );
                    self.check_fd_usage = false;
                }
                Err(e) => {
                    log::warn!("Unable to check fd usage: {e}");
                    self.check_fd_usage = false;
                }
                Ok(_) => {}
            }
        }

        if self.check_mem_usage {
            match self.memory_remaining() {
                // if less than 500 MiB available
                Ok(remaining) if remaining < 500 * 1024 * 1024 => {
                    log::warn!("Only {} MiB of memory available", remaining / 1024 / 1024);
                    self.check_mem_usage = false;
                }
                Err(e) => {
                    log::warn!("Unable to check memory usage: {e}");
                    self.check_mem_usage = false;
                }
                Ok(_) => {}
            }
        }
    }

    /// Returns a tuple of (usage, limit).
    fn fd_usage(&mut self) -> anyhow::Result<(u64, u64)> {
        let dir = std::fs::read_dir("/proc/self/fd").context("Failed to open '/proc/self/fd'")?;

        let mut fd_count: u64 = 0;
        for entry in dir {
            // short-circuit and return on error
            entry.context("Failed to read entry in '/proc/self/fd'")?;
            fd_count += 1;
        }

        let (soft_limit, _) =
            nix::sys::resource::getrlimit(nix::sys::resource::Resource::RLIMIT_NOFILE)
                .context("Failed to get the fd limit")?;

        Ok((fd_count, soft_limit))
    }

    /// Returns the number of bytes remaining.
    fn memory_remaining(&mut self) -> anyhow::Result<u64> {
        let page_size = nix::unistd::sysconf(nix::unistd::SysconfVar::PAGE_SIZE)
            .context("Failed to get the page size")?
            .ok_or_else(|| anyhow::anyhow!("Failed to get the page size (no errno)"))?;

        let avl_pages = nix::unistd::sysconf(nix::unistd::SysconfVar::_AVPHYS_PAGES)
            .context("Failed to get the number of available pages of physical memory")?
            .ok_or_else(|| {
                anyhow::anyhow!(
                    "Failed to get the number of available pages of physical memory (no errno)"
                )
            })?;

        let page_size: u64 = page_size.try_into().unwrap();
        let avl_pages: u64 = avl_pages.try_into().unwrap();

        Ok(page_size * avl_pages)
    }

    pub fn shmem(&self) -> &ShMemBlock<ManagerShmem> {
        &self.shmem
    }
}

pub struct ManagerConfig {
    // deterministic source of randomness for this manager
    pub random: Xoshiro256PlusPlus,

    // map of ip addresses to graph nodes
    pub ip_assignment: IpAssignment<u32>,

    // routing information for paths between graph nodes
    pub routing_info: RoutingInfo<u32>,

    // bandwidths of hosts at ip addresses
    pub host_bandwidths: HashMap<std::net::IpAddr, Bandwidth>,

    // a list of hosts and their processes
    pub hosts: Vec<HostInfo>,
}

/// Helper function to initialize the global [`Host`] before running the closure.
fn for_each_host(host_iter: &mut HostIter, mut f: impl FnMut(&Host)) {
    host_iter.for_each(|host| {
        worker::Worker::set_active_host(host);
        worker::Worker::with_active_host(|host| {
            f(host);
        })
        .unwrap();
        worker::Worker::take_active_host()
    });
}

/// Get the raw speed of the experiment machine.
fn get_raw_cpu_frequency_hz() -> anyhow::Result<u64> {
    const CONFIG_CPU_MAX_FREQ_FILE: &str = "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq";
    let khz: u64 = std::fs::read_to_string(CONFIG_CPU_MAX_FREQ_FILE)?.parse()?;
    Ok(khz * 1000)
}

fn get_required_preload_path(libname: &str) -> anyhow::Result<PathBuf> {
    let libname_c = CString::new(libname).unwrap();
    let libpath_c = unsafe { c::scanRpathForLib(libname_c.as_ptr()) };

    // scope needed to make sure the CStr is dropped before we free libpath_c
    let libpath = if !libpath_c.is_null() {
        let libpath = unsafe { CStr::from_ptr(libpath_c) };
        let libpath = OsStr::from_bytes(libpath.to_bytes());
        Some(PathBuf::from(libpath.to_os_string()))
    } else {
        None
    };

    unsafe { libc::free(libpath_c as *mut libc::c_void) };

    let libpath = libpath.ok_or_else(|| anyhow::anyhow!(format!("Could not library in rpath")))?;

    let bytes = libpath.as_os_str().as_bytes();
    if bytes.iter().any(|c| *c == b' ' || *c == b':') {
        // These are unescapable separators in LD_PRELOAD.
        anyhow::bail!("Preload path contains LD_PRELOAD-incompatible characters: {libpath:?}");
    }

    log::debug!(
        "Found required preload library {} at path {}",
        libname,
        libpath.display(),
    );

    Ok(libpath)
}
