use std::collections::HashMap;
use std::ffi::{CStr, CString, OsStr, OsString};
use std::marker::PhantomData;
use std::os::unix::ffi::OsStrExt;
use std::path::PathBuf;
use std::time::Duration;

use anyhow::{self, Context};
use rand::Rng;
use rand_xoshiro::Xoshiro256PlusPlus;

use crate::core::controller::{Controller, SimController};
use crate::core::sim_config::HostInfo;
use crate::core::support::configuration::{ConfigOptions, Flatten, LogLevel, SchedulerPolicy};
use crate::core::support::simulation_time::SimulationTime;
use crate::core::worker;
use crate::cshadow as c;
use crate::utility;
use crate::utility::childpid_watcher::ChildPidWatcher;

pub struct Manager<'a> {
    controller: &'a Controller<'a>,
    config: &'a ConfigOptions,
    hosts: Vec<HostInfo>,

    // manager random source, init from controller random, used to init host randoms
    random: Xoshiro256PlusPlus,
    raw_frequency_khz: u64,
    end_time: SimulationTime,

    hosts_path: PathBuf,

    // path to the injector lib that we preload for managed processes (if no other lib is preloaded)
    preload_injector_path: PathBuf,
    // path to the libc lib that we preload for managed processes
    preload_libc_path: Option<PathBuf>,
    // path to the openssl rng lib that we preload for managed processes
    preload_openssl_rng_path: Option<PathBuf>,
    // path to the openssl crypto lib that we preload for managed processes
    preload_openssl_crypto_path: Option<PathBuf>,

    check_fd_usage: bool,
    check_mem_usage: bool,
}

impl<'a> Manager<'a> {
    pub fn new(
        controller: &'a Controller<'a>,
        config: &'a ConfigOptions,
        hosts: Vec<HostInfo>,
        end_time: SimulationTime,
        random: Xoshiro256PlusPlus,
    ) -> anyhow::Result<Self> {
        // get the system's CPU frequency
        let raw_frequency_khz = get_raw_cpu_frequency().unwrap_or_else(|e| {
            let default_freq = 2_500_000; // 2.5 GHz
            log::debug!(
                "Failed to get raw CPU frequency, using {} Hz instead: {}",
                default_freq,
                e
            );
            default_freq
        });

        // we always preload the injector lib to ensure that the shim is loaded into the managed
        // processes
        const PRELOAD_INJECTOR_LIB: &str = "libshadow_injector.so";
        let preload_injector_path =
            get_required_preload_path(PRELOAD_INJECTOR_LIB).with_context(|| {
                format!("Failed to get path to preload library '{PRELOAD_INJECTOR_LIB}'")
            })?;

        // preload libc lib if option is enabled
        const PRELOAD_LIBC_LIB: &str = "libshadow_libc.so";
        let preload_libc_path = if config.experimental.use_preload_libc.unwrap() {
            let path = get_required_preload_path(PRELOAD_LIBC_LIB).with_context(|| {
                format!("Failed to get path to preload library '{PRELOAD_LIBC_LIB}'")
            })?;
            Some(path)
        } else {
            log::info!("Preloading the libc library is disabled");
            None
        };

        // preload openssl rng lib if option is enabled
        const PRELOAD_OPENSSL_RNG_LIB: &str = "libshadow_openssl_rng.so";
        let preload_openssl_rng_path = if config.experimental.use_preload_openssl_rng.unwrap() {
            let path = get_required_preload_path(PRELOAD_OPENSSL_RNG_LIB).with_context(|| {
                format!("Failed to get path to preload library '{PRELOAD_OPENSSL_RNG_LIB}'")
            })?;
            Some(path)
        } else {
            log::info!("Preloading the openssl rng library is disabled");
            None
        };

        // preload openssl crypto lib if option is enabled
        const PRELOAD_OPENSSL_CRYPTO_LIB: &str = "libshadow_openssl_crypto.so";
        let preload_openssl_crypto_path = if config.experimental.use_preload_openssl_crypto.unwrap()
        {
            let path =
                get_required_preload_path(PRELOAD_OPENSSL_CRYPTO_LIB).with_context(|| {
                    format!("Failed to get path to preload library '{PRELOAD_OPENSSL_CRYPTO_LIB}'")
                })?;
            Some(path)
        } else {
            log::info!("Preloading the openssl crypto library is disabled");
            None
        };

        // use the working dir to generate absolute paths
        let cwd = std::env::current_dir()?;
        let template_path = config
            .general
            .template_directory
            .flatten_ref()
            .map(|x| cwd.clone().join(x));
        let data_path = cwd
            .clone()
            .join(config.general.data_directory.as_ref().unwrap());
        let hosts_path = data_path.clone().join("hosts");

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
        let config_out_filename = data_path.clone().join("processed-config.yaml");
        let config_out_file = std::fs::File::create(&config_out_filename).with_context(|| {
            format!("Failed to create file '{}'", config_out_filename.display())
        })?;

        serde_yaml::to_writer(config_out_file, &config).with_context(|| {
            format!(
                "Failed to write processed config yaml to file '{}'",
                config_out_filename.display()
            )
        })?;

        Ok(Self {
            controller,
            config,
            hosts,
            random,
            raw_frequency_khz,
            end_time,
            hosts_path,
            preload_injector_path,
            preload_libc_path,
            preload_openssl_rng_path,
            preload_openssl_crypto_path,
            check_fd_usage: true,
            check_mem_usage: true,
        })
    }

    pub fn run(mut self) -> anyhow::Result<()> {
        let num_workers = self
            .config
            .experimental
            .worker_threads
            .unwrap_or_else(|| u32::try_from(self.hosts.len()).unwrap().try_into().unwrap())
            .get();

        let sched_policy = self.config.experimental.scheduler_policy.unwrap();

        // scope used so that the scheduler is dropped before we log the global counters below
        {
            let pid_watcher = ChildPidWatcher::new();
            let mut scheduler = SchedulerWrapper::new(
                self.controller,
                &pid_watcher,
                self.config,
                sched_policy,
                num_workers,
                self.random.gen(),
                self.end_time,
            );

            for host in self.hosts.split_off(0) {
                self.add_host(&mut scheduler, &host)
                    .with_context(|| format!("Failed to add host '{}'", host.name))?;
            }

            // we are the main thread, we manage the execution window updates while the workers run
            // events

            // the current simulation interval
            let mut window = Some((SimulationTime::ZERO, SimulationTime::NANOSECOND));

            // how often to log heartbeat messages
            let heartbeat_interval = self
                .config
                .general
                .heartbeat_interval
                .flatten()
                .map(|x| Duration::from(x).try_into().unwrap());

            let mut last_heartbeat = SimulationTime::ZERO;
            let mut time_of_last_usage_check = std::time::Instant::now();

            scheduler.start();

            while let Some((window_start, window_end)) = window {
                // release the workers and run next round
                scheduler.continue_next_round(window_start, window_end);

                // log a heartbeat message every 'heartbeat_interval' amount of simulated time
                if let Some(heartbeat_interval) = heartbeat_interval {
                    if window_start > last_heartbeat + heartbeat_interval {
                        last_heartbeat = window_start;
                        self.log_heartbeat(window_start);
                    }
                }

                // check resource usage every 30 real seconds
                let current_time = std::time::Instant::now();
                if current_time.duration_since(time_of_last_usage_check) > Duration::from_secs(30) {
                    time_of_last_usage_check = current_time;
                    self.check_resource_usage();
                }

                // wait for the workers to finish processing nodes before we update the execution
                // window
                let min_next_event_time =
                    scheduler.await_next_round().unwrap_or(SimulationTime::MAX);

                // we are in control now, the workers are waiting for the next round
                log::debug!(
                    "Finished execution window [{}--{}], next event at {}",
                    window_start.as_nanos(),
                    window_end.as_nanos(),
                    min_next_event_time.as_nanos(),
                );

                // notify controller that we finished this round, and the time of our next event in
                // order to fast-forward our execute window if possible
                window = self
                    .controller
                    .manager_finished_current_round(min_next_event_time);
            }
        }

        // since the scheduler was dropped, all workers should have completed and the global object
        // and syscall counters should have been updated

        // log syscall counters
        if self.config.experimental.use_syscall_counters.unwrap() {
            worker::with_global_syscall_counter(|counter| {
                log::info!("Global syscall counts: {}", counter);
            });
        }

        // log and check object allocation/deallocation counters
        if self.config.experimental.use_object_counters.unwrap() {
            worker::with_global_object_counters(|alloc_counter, dealloc_counter| {
                log::info!("Global allocated object counts: {}", alloc_counter);
                log::info!("Global deallocated object counts: {}", dealloc_counter);

                if alloc_counter == dealloc_counter {
                    log::info!("We allocated and deallocated the same number of objects :)");
                } else {
                    // don't change the formatting of this line as we search for it in test cases
                    log::warn!("Memory leak detected");
                }
            });
        }

        Ok(())
    }

    fn add_host(&self, scheduler: &mut SchedulerWrapper, host: &HostInfo) -> anyhow::Result<()> {
        let hostname = CString::new(&*host.name).unwrap();
        let pcap_dir = host
            .pcap_dir
            .as_ref()
            .map(|x| CString::new(x.to_str().unwrap()).unwrap());

        // scope used to enforce drop order for pointers
        let c_host = {
            let params = c::HostParameters {
                // the manager sets this ID
                id: unsafe { c::g_quark_from_string(hostname.as_ptr()) },
                // the manager sets this CPU frequency
                cpuFrequency: self.raw_frequency_khz,
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

            let hosts_path =
                CString::new(self.hosts_path.clone().into_os_string().as_bytes()).unwrap();

            let c_host = unsafe { c::host_new(&params) };
            assert!(!c_host.is_null());
            unsafe {
                c::host_setup(
                    c_host,
                    self.controller.get_dns(),
                    self.raw_frequency_khz,
                    hosts_path.as_ptr(),
                )
            };

            // make sure we never accidentally drop the following objects before running the
            // unsafe code (will be a compile-time error if they were dropped)
            let _ = &hostname;
            let _ = &pcap_dir;

            c_host
        };

        for proc in &host.processes {
            let plugin_path =
                CString::new(proc.plugin.clone().into_os_string().as_bytes()).unwrap();
            let plugin_name = CString::new(proc.plugin.file_name().unwrap().as_bytes()).unwrap();
            let pause_for_debugging = host.pause_for_debugging;

            let argv: Vec<CString> = proc
                .args
                .iter()
                .map(|x| CString::new(x.as_bytes()).unwrap())
                .collect();

            let shim_log_level = host
                .log_level
                .unwrap_or(self.config.general.log_level.unwrap());

            let envv = self.generate_env_vars(&proc.env, shim_log_level);
            let envv: Vec<CString> = envv
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

                let envv_ptrs: Vec<*const i8> = envv
                    .iter()
                    .map(|x| x.as_ptr())
                    // the last element of envv must be NULL
                    .chain(std::iter::once(std::ptr::null()))
                    .collect();

                unsafe { c::host_continueExecutionTimer(c_host) };

                unsafe {
                    c::host_addApplication(
                        c_host,
                        SimulationTime::to_c_simtime(Some(proc.start_time)),
                        SimulationTime::to_c_simtime(proc.stop_time),
                        plugin_name.as_ptr(),
                        plugin_path.as_ptr(),
                        envv_ptrs.as_ptr(),
                        argv_ptrs.as_ptr(),
                        pause_for_debugging,
                    )
                };

                unsafe { c::host_stopExecutionTimer(c_host) };

                // make sure we never accidentally drop the following objects before running the
                // unsafe code (will be a compile-time error if they were dropped)
                let _ = &plugin_path;
                let _ = &plugin_name;
                let _ = &envv;
                let _ = &argv;
                let _ = &argv_ptrs;
            }
        }

        scheduler.add_host(c_host)
    }

    // assume that the provided env variables are UTF-8, since working with str instead of OsStr is
    // much less painful
    fn generate_env_vars(&self, user_env: &str, shim_log_level: LogLevel) -> Vec<OsString> {
        let mut env: HashMap<OsString, Option<OsString>> = HashMap::new();

        env.insert("SHADOW_SPAWNED".into(), Some("TRUE".into()));

        // pass the (real) start time to the plugin, so that shim-side logging can log real time
        // from the correct offset.
        env.insert(
            "SHADOW_LOG_START_TIME".into(),
            Some(
                unsafe { c::logger_get_global_start_time_micros() }
                    .to_string()
                    .into(),
            ),
        );

        env.insert(
            "SHADOW_LOG_LEVEL".into(),
            Some(shim_log_level.to_c_loglevel().to_string().into()),
        );

        // also insert the plugin preload entries
        // precendence here is:
        //   - preload path of the injector
        //   - preload path of the libc lib
        //   - preload path of the openssl rng lib
        //   - preload path of the openssl crypto lib
        //   - preload values from LD_PRELOAD entries in the environment process option

        let mut preload = vec![];

        preload.push(self.preload_injector_path.clone().into_os_string());

        if let Some(ref path) = self.preload_libc_path {
            preload.push(path.clone().into_os_string());
        }

        if let Some(ref path) = self.preload_openssl_rng_path {
            preload.push(path.clone().into_os_string());
        }

        if let Some(ref path) = self.preload_openssl_crypto_path {
            preload.push(path.clone().into_os_string());
        }

        // scan the other env variables that were given in the shadow config file
        for entry in user_env.split(";") {
            let (name, value) = entry
                .split_once("=")
                .map(|(name, value)| (name, Some(value)))
                .unwrap_or((entry, None));

            // if it's not LD_PRELOAD, insert if there's no existing entry
            if name != "LD_PRELOAD" {
                env.entry(name.into()).or_insert(value.map(|x| x.into()));
                continue;
            }

            // it's LD_PRELOAD, so skip if there's no value
            let value = match value {
                Some(x) => x,
                None => continue,
            };

            // both ':' and ' ' are valid LD_PRELOAD separators
            for path in value.split(&[':', ' '][..]) {
                let path = utility::tilde_expansion(path);

                // user-provided paths are added to the list after shadow's paths
                preload.push(path.into());
            }
        }

        let preload = {
            let mut preload_string = OsString::new();
            for (x, path) in preload.iter().enumerate() {
                if x > 0 {
                    preload_string.push(&":");
                }
                preload_string.push(path);
            }
            preload_string
        };

        env.insert("LD_PRELOAD".into(), Some(preload));

        env.into_iter()
            .map(|(mut x, y)| {
                if let Some(y) = y {
                    x.push("=");
                    x.push(y);
                    x
                } else {
                    x
                }
            })
            .collect()
    }

    fn log_heartbeat(&self, now: SimulationTime) {
        let mut resources: libc::rusage = unsafe { std::mem::zeroed() };
        if unsafe { libc::getrusage(libc::RUSAGE_SELF, &mut resources) } != 0 {
            let err = nix::errno::Errno::last();
            log::warn!("Unable to get shadow's resource usage: {}", err);
            return;
        }

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
            ru_nivcsw={}, \
            _manager_heartbeat", // this is required for tornettools
            now.as_nanos(),
            max_memory,
            user_time_minutes,
            system_time_minutes,
            resources.ru_nvcsw,
            resources.ru_nivcsw,
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

        Ok((fd_count, u64::try_from(soft_limit).unwrap()))
    }

    /// Returns the number of bytes remaining.
    fn memory_remaining(&mut self) -> anyhow::Result<u64> {
        let page_size = nix::unistd::sysconf(nix::unistd::SysconfVar::PAGE_SIZE)
            .context("Failed to get the page size")?
            .ok_or(anyhow::anyhow!("Failed to get the page size (no errno)"))?;

        // can use nix once the following is in a released version:
        // https://github.com/nix-rust/nix/pull/1761
        let avl_pages = unsafe { libc::sysconf(libc::_SC_AVPHYS_PAGES) };
        if avl_pages < 0 {
            return Err(anyhow::anyhow!(
                "Failed to get the number of available pages of physical memory"
            ));
        }

        let page_size: u64 = page_size.try_into().unwrap();
        let avl_pages: u64 = avl_pages.try_into().unwrap();

        Ok(page_size * avl_pages)
    }
}

struct SchedulerWrapper<'a> {
    pub ptr: *mut c::Scheduler,
    _phantom_controller: PhantomData<&'a Controller<'a>>,
    _phantom_pid_watcher: PhantomData<&'a ChildPidWatcher>,
    _phantom_config: PhantomData<&'a ConfigOptions>,
}

impl<'a> SchedulerWrapper<'a> {
    pub fn new(
        controller: &'a Controller,
        pid_watcher: &'a ChildPidWatcher,
        config: &'a ConfigOptions,
        policy: SchedulerPolicy,
        num_workers: u32,
        scheduler_seed: u32,
        end_time: SimulationTime,
    ) -> Self {
        Self {
            ptr: unsafe {
                c::scheduler_new(
                    controller,
                    pid_watcher,
                    config,
                    policy.to_c_sched_policy_type(),
                    num_workers,
                    scheduler_seed,
                    end_time.into(),
                )
            },
            _phantom_controller: Default::default(),
            _phantom_pid_watcher: Default::default(),
            _phantom_config: Default::default(),
        }
    }

    pub fn add_host(&mut self, host: *mut c::Host) -> anyhow::Result<()> {
        let rv = unsafe { c::scheduler_addHost(self.ptr, host) };
        if rv != 0 {
            return Err(anyhow::anyhow!(
                "Failed to add host to scheduler (see the log for details)"
            ));
        }

        Ok(())
    }

    pub fn start(&mut self) {
        unsafe { c::scheduler_start(self.ptr) }
    }

    pub fn finish(&mut self) {
        unsafe { c::scheduler_finish(self.ptr) }
    }

    pub fn shutdown(&mut self) {
        unsafe { c::scheduler_shutdown(self.ptr) }
    }

    pub fn continue_next_round(
        &mut self,
        window_start: SimulationTime,
        window_end: SimulationTime,
    ) {
        let window_start = window_start.into();
        let window_end = window_end.into();
        unsafe { c::scheduler_continueNextRound(self.ptr, window_start, window_end) }
    }

    pub fn await_next_round(&mut self) -> Option<SimulationTime> {
        let min_next_event_time = unsafe { c::scheduler_awaitNextRound(self.ptr) };
        SimulationTime::from_c_simtime(min_next_event_time)
    }
}

impl<'a> std::ops::Drop for SchedulerWrapper<'a> {
    fn drop(&mut self) {
        // shadow requires that the work pool is properly shutdown before it's freed (will block
        // until worker threads are joined)
        self.finish();
        self.shutdown();

        unsafe { c::scheduler_unref(self.ptr) };
    }
}

/// Get the raw speed of the experiment machine.
fn get_raw_cpu_frequency() -> anyhow::Result<u64> {
    const CONFIG_CPU_MAX_FREQ_FILE: &str = "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq";
    Ok(std::fs::read_to_string(CONFIG_CPU_MAX_FREQ_FILE)?.parse()?)
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

    log::info!(
        "Found required preload library {} at path {}",
        libname,
        libpath.display(),
    );

    Ok(libpath)
}

mod export {
    use std::ffi::CStr;

    use crate::core::support::configuration::ConfigOptions;

    #[no_mangle]
    pub extern "C" fn manager_saveProcessedConfigYaml(
        config: *const ConfigOptions,
        filename: *const libc::c_char,
    ) -> libc::c_int {
        let config = unsafe { config.as_ref() }.unwrap();
        let filename = unsafe { CStr::from_ptr(filename) }.to_str().unwrap();

        let file = match std::fs::File::create(&filename) {
            Ok(f) => f,
            Err(e) => {
                log::warn!("Could not create file {:?}: {}", filename, e);
                return 1;
            }
        };

        if let Err(e) = serde_yaml::to_writer(file, &config) {
            log::warn!(
                "Could not write processed config yaml to file {:?}: {}",
                filename,
                e
            );
            return 1;
        }

        return 0;
    }
}
