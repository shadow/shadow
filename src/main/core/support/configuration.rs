use std::collections::{BTreeMap, HashSet};
use std::ffi::{CStr, CString, OsStr, OsString};
use std::num::NonZeroU32;
use std::os::unix::ffi::OsStrExt;
use std::os::unix::fs::MetadataExt;
use std::str::FromStr;

use clap::ArgEnum;
use clap::Parser;
use merge::Merge;
use once_cell::sync::Lazy;
use schemars::{schema_for, JsonSchema};
use serde::{Deserialize, Serialize};

use super::simulation_time::{SIMTIME_INVALID, SIMTIME_ONE_NANOSECOND, SIMTIME_ONE_SECOND};
use super::units::{self, Unit};
use crate::cshadow as c;
use crate::host::syscall::format::StraceFmtMode;
use log_bindings as c_log;

const START_HELP_TEXT: &str = "\
    Run real applications over simulated networks.\n\n\
    For documentation, visit https://shadow.github.io/docs/guide";

const END_HELP_TEXT: &str = "\
    If units are not specified, all values are assumed to be given in their base \
    unit (seconds, bytes, bits, etc). Units can optionally be specified (for \
    example: '1024 B', '1024 bytes', '1 KiB', '1 kibibyte', etc) and are \
    case-sensitive.";

#[derive(Debug, Clone, Parser)]
#[clap(name = "Shadow", about = START_HELP_TEXT, after_help = END_HELP_TEXT)]
#[clap(version = std::option_env!("SHADOW_VERSION").unwrap_or(std::env!("CARGO_PKG_VERSION")))]
pub struct CliOptions {
    /// Path to the Shadow configuration file. Use '-' to read from stdin
    #[clap(required_unless_present_any(&["show-build-info", "shm-cleanup"]))]
    pub config: Option<String>,

    /// Pause to allow gdb to attach
    #[clap(long, short = 'g')]
    pub gdb: bool,

    /// Exit after running shared memory cleanup routine
    #[clap(long, exclusive(true))]
    pub shm_cleanup: bool,

    /// Exit after printing build information
    #[clap(long, exclusive(true))]
    pub show_build_info: bool,

    /// Exit after printing the final configuration
    #[clap(long)]
    pub show_config: bool,

    #[clap(flatten)]
    pub general: GeneralOptions,

    #[clap(flatten)]
    pub network: NetworkOptions,

    #[clap(flatten)]
    pub host_defaults: HostDefaultOptions,

    #[clap(flatten)]
    pub experimental: ExperimentalOptions,
}

/// Options contained in a configuration file.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ConfigFileOptions {
    pub general: GeneralOptions,

    pub network: NetworkOptions,

    #[serde(default)]
    pub host_defaults: HostDefaultOptions,

    #[serde(default)]
    pub experimental: ExperimentalOptions,

    // we use a BTreeMap so that the hosts are sorted by their hostname (useful for determinism)
    // note: serde 'with' is incompatible with 'derive(JsonSchema)': https://github.com/GREsau/schemars/issues/89
    #[serde(with = "serde_with::rust::maps_duplicate_key_is_error")]
    pub hosts: BTreeMap<String, HostOptions>,
}

/// Shadow configuration options after processing command-line and configuration file options.
#[derive(Debug, Clone)]
pub struct ConfigOptions {
    pub general: GeneralOptions,

    pub network: NetworkOptions,

    pub experimental: ExperimentalOptions,

    // we use a BTreeMap so that the hosts are sorted by their hostname (useful for determinism)
    pub hosts: BTreeMap<String, HostOptions>,
}

impl ConfigOptions {
    pub fn new(mut config_file: ConfigFileOptions, options: CliOptions) -> Self {
        // override config options with command line options
        config_file.general = options.general.with_defaults(config_file.general);
        config_file.network = options.network.with_defaults(config_file.network);
        config_file.host_defaults = options
            .host_defaults
            .with_defaults(config_file.host_defaults);
        config_file.experimental = options.experimental.with_defaults(config_file.experimental);

        // copy the host defaults to all of the hosts
        for host in config_file.hosts.values_mut() {
            host.options = host
                .options
                .clone()
                .with_defaults(config_file.host_defaults.clone());
        }

        Self {
            general: config_file.general,
            network: config_file.network,
            experimental: config_file.experimental,
            hosts: config_file.hosts,
        }
    }
}

/// Help messages used by Clap for command line arguments, combining the doc string with
/// the Serde default.
static GENERAL_HELP: Lazy<std::collections::HashMap<String, String>> =
    Lazy::new(|| generate_help_strs(schema_for!(GeneralOptions)));

// these must all be Option types since they aren't required by the CLI, even if they're
// required in the configuration file
#[derive(Debug, Clone, Parser, Serialize, Deserialize, Merge, JsonSchema)]
#[clap(next_help_heading = "GENERAL (Override configuration file options)")]
#[serde(deny_unknown_fields)]
pub struct GeneralOptions {
    /// The simulated time at which simulated processes are sent a SIGKILL signal
    #[clap(long, value_name = "seconds")]
    #[clap(help = GENERAL_HELP.get("stop_time").unwrap().as_str())]
    pub stop_time: Option<units::Time<units::TimePrefixUpper>>,

    /// Initialize randomness using seed N
    #[clap(long, value_name = "N")]
    #[clap(help = GENERAL_HELP.get("seed").unwrap().as_str())]
    #[serde(default = "default_some_1")]
    pub seed: Option<u32>,

    /// How many parallel threads to use to run the simulation. Optimal
    /// performance is usually obtained with `cores`, or sometimes `cores/2`
    /// with hyperthreading.
    #[clap(long, short = 'p', value_name = "cores")]
    #[clap(help = GENERAL_HELP.get("parallelism").unwrap().as_str())]
    #[serde(default = "default_some_nz_1")]
    pub parallelism: Option<NonZeroU32>,

    /// The simulated time that ends Shadow's high network bandwidth/reliability bootstrap period
    #[clap(long, value_name = "seconds")]
    #[clap(help = GENERAL_HELP.get("bootstrap_end_time").unwrap().as_str())]
    #[serde(default = "default_some_time_0")]
    pub bootstrap_end_time: Option<units::Time<units::TimePrefixUpper>>,

    /// Log level of output written on stdout. If Shadow was built in release mode, then log
    /// messages at level 'trace' will always be dropped
    #[clap(long, short = 'l', value_name = "level")]
    #[clap(help = GENERAL_HELP.get("log_level").unwrap().as_str())]
    #[serde(default = "default_some_info")]
    pub log_level: Option<LogLevel>,

    /// Interval at which to print heartbeat messages
    #[clap(long, value_name = "seconds")]
    #[clap(help = GENERAL_HELP.get("heartbeat_interval").unwrap().as_str())]
    #[serde(default = "default_some_time_1")]
    pub heartbeat_interval: Option<units::Time<units::TimePrefixUpper>>,

    /// Path to store simulation output
    #[clap(long, short = 'd', value_name = "path")]
    #[clap(help = GENERAL_HELP.get("data_directory").unwrap().as_str())]
    #[serde(default = "default_data_directory")]
    pub data_directory: Option<String>,

    /// Path to recursively copy during startup and use as the data-directory
    #[clap(long, short = 'e', value_name = "path")]
    #[clap(help = GENERAL_HELP.get("template_directory").unwrap().as_str())]
    #[serde(default)]
    pub template_directory: Option<String>,

    /// Show the simulation progress on stderr
    #[clap(long, value_name = "bool")]
    #[clap(help = GENERAL_HELP.get("progress").unwrap().as_str())]
    #[serde(default = "default_some_false")]
    pub progress: Option<bool>,

    /// Model syscalls and VDSO functions that don't block as having some
    /// latency. This should have minimal effect on typical simulations, but
    /// can be helpful for programs with "busy loops" that otherwise deadlock
    /// under Shadow.
    #[clap(long, value_name = "bool")]
    #[clap(help = GENERAL_HELP.get("model_unblocked_syscall_latency").unwrap().as_str())]
    #[serde(default = "default_some_false")]
    pub model_unblocked_syscall_latency: Option<bool>,
}

impl GeneralOptions {
    /// Replace unset (`None`) values of `base` with values from `default`.
    pub fn with_defaults(mut self, default: Self) -> Self {
        self.merge(default);
        self
    }
}

/// Help messages used by Clap for command line arguments, combining the doc string with
/// the Serde default.
static NETWORK_HELP: Lazy<std::collections::HashMap<String, String>> =
    Lazy::new(|| generate_help_strs(schema_for!(NetworkOptions)));

// these must all be Option types since they aren't required by the CLI, even if they're
// required in the configuration file
#[derive(Debug, Clone, Parser, Serialize, Deserialize, Merge, JsonSchema)]
#[clap(next_help_heading = "NETWORK (Override network options)")]
#[serde(deny_unknown_fields)]
pub struct NetworkOptions {
    /// The network topology graph
    #[clap(skip)]
    pub graph: Option<GraphOptions>,

    /// When routing packets, follow the shortest path rather than following a direct
    /// edge between nodes. If false, the network graph is required to be complete.
    #[serde(default = "default_some_true")]
    #[clap(long, value_name = "bool")]
    #[clap(help = NETWORK_HELP.get("use_shortest_path").unwrap().as_str())]
    pub use_shortest_path: Option<bool>,
}

impl NetworkOptions {
    /// Replace unset (`None`) values of `base` with values from `default`.
    pub fn with_defaults(mut self, default: Self) -> Self {
        self.merge(default);
        self
    }
}

/// Help messages used by Clap for command line arguments, combining the doc string with
/// the Serde default.
static EXP_HELP: Lazy<std::collections::HashMap<String, String>> =
    Lazy::new(|| generate_help_strs(schema_for!(ExperimentalOptions)));

#[derive(Debug, Clone, Parser, Serialize, Deserialize, Merge, JsonSchema)]
#[clap(
    next_help_heading = "EXPERIMENTAL (Unstable and may change or be removed at any time, regardless of Shadow version)"
)]
#[serde(default, deny_unknown_fields)]
pub struct ExperimentalOptions {
    /// Use the SCHED_FIFO scheduler. Requires CAP_SYS_NICE. See sched(7), capabilities(7)
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bool")]
    #[clap(help = EXP_HELP.get("use_sched_fifo").unwrap().as_str())]
    pub use_sched_fifo: Option<bool>,

    /// Use performance workarounds for waitpid being O(n). Beneficial to disable if waitpid
    /// is patched to be O(1), if using one logical processor per host, or in some cases where
    /// it'd otherwise result in excessive detaching and reattaching
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bool")]
    #[clap(help = EXP_HELP.get("use_o_n_waitpid_workarounds").unwrap().as_str())]
    pub use_o_n_waitpid_workarounds: Option<bool>,

    /// Send message to plugin telling it to stop spinning when a syscall blocks
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bool")]
    #[clap(help = EXP_HELP.get("use_explicit_block_message").unwrap().as_str())]
    pub use_explicit_block_message: Option<bool>,

    /// Use seccomp to trap syscalls. Default is true for preload mode, false otherwise.
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bool")]
    #[clap(help = EXP_HELP.get("use_seccomp").unwrap().as_str())]
    pub use_seccomp: Option<bool>,

    /// Count the number of occurrences for individual syscalls
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bool")]
    #[clap(help = EXP_HELP.get("use_syscall_counters").unwrap().as_str())]
    pub use_syscall_counters: Option<bool>,

    /// Count object allocations and deallocations. If disabled, we will not be able to detect object memory leaks
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bool")]
    #[clap(help = EXP_HELP.get("use_object_counters").unwrap().as_str())]
    pub use_object_counters: Option<bool>,

    /// Preload our libc library for all managed processes for fast syscall interposition when possible.
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bool")]
    #[clap(help = EXP_HELP.get("use_preload_libc").unwrap().as_str())]
    pub use_preload_libc: Option<bool>,

    /// Preload our OpenSSL RNG library for all managed processes to mitigate non-deterministic use of OpenSSL.
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bool")]
    #[clap(help = EXP_HELP.get("use_preload_openssl_rng").unwrap().as_str())]
    pub use_preload_openssl_rng: Option<bool>,

    /// Preload our OpenSSL crypto library for all managed processes to skip some crypto operations
    /// (may speed up simulation if your CPU lacks AES-NI support, but can cause bugs so do not use
    /// unless you know what you're doing).
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bool")]
    #[clap(help = EXP_HELP.get("use_preload_openssl_crypto").unwrap().as_str())]
    pub use_preload_openssl_crypto: Option<bool>,

    /// Max number of iterations to busy-wait on IPC semaphore before blocking
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "iterations")]
    #[clap(help = EXP_HELP.get("preload_spin_max").unwrap().as_str())]
    pub preload_spin_max: Option<i32>,

    /// Use the MemoryManager. It can be useful to disable for debugging, but will hurt performance in
    /// most cases
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bool")]
    #[clap(help = EXP_HELP.get("use_memory_manager").unwrap().as_str())]
    pub use_memory_manager: Option<bool>,

    /// Use shim-side syscall handler to force hot-path syscalls to be handled via an inter-process syscall with Shadow
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bool")]
    #[clap(help = EXP_HELP.get("use_shim_syscall_handler").unwrap().as_str())]
    pub use_shim_syscall_handler: Option<bool>,

    /// Pin each thread and any processes it executes to the same logical CPU Core to improve cache affinity
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bool")]
    #[clap(help = EXP_HELP.get("use_cpu_pinning").unwrap().as_str())]
    pub use_cpu_pinning: Option<bool>,

    /// Which interposition method to use
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "method")]
    #[clap(help = EXP_HELP.get("interpose_method").unwrap().as_str())]
    pub interpose_method: Option<InterposeMethod>,

    /// If set, overrides the automatically calculated minimum time workers may run ahead when sending events between nodes
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "seconds")]
    #[clap(help = EXP_HELP.get("runahead").unwrap().as_str())]
    pub runahead: Option<units::Time<units::TimePrefix>>,

    /// Update the minimum runahead dynamically throughout the simulation.
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bool")]
    #[clap(help = EXP_HELP.get("use_dynamic_runahead").unwrap().as_str())]
    pub use_dynamic_runahead: Option<bool>,

    /// The event scheduler's policy for thread synchronization
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "policy")]
    #[clap(help = EXP_HELP.get("scheduler_policy").unwrap().as_str())]
    pub scheduler_policy: Option<SchedulerPolicy>,

    /// Initial size of the socket's send buffer
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bytes")]
    #[clap(help = EXP_HELP.get("socket_send_buffer").unwrap().as_str())]
    pub socket_send_buffer: Option<units::Bytes<units::SiPrefixUpper>>,

    /// Enable send window autotuning
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bool")]
    #[clap(help = EXP_HELP.get("socket_send_autotune").unwrap().as_str())]
    pub socket_send_autotune: Option<bool>,

    /// Initial size of the socket's receive buffer
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bytes")]
    #[clap(help = EXP_HELP.get("socket_recv_buffer").unwrap().as_str())]
    pub socket_recv_buffer: Option<units::Bytes<units::SiPrefixUpper>>,

    /// Enable receive window autotuning
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bool")]
    #[clap(help = EXP_HELP.get("socket_recv_autotune").unwrap().as_str())]
    pub socket_recv_autotune: Option<bool>,

    /// Size of the interface receive buffer that accepts incoming packets
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bytes")]
    #[clap(help = EXP_HELP.get("interface_buffer").unwrap().as_str())]
    pub interface_buffer: Option<units::Bytes<units::SiPrefixUpper>>,

    /// The queueing discipline to use at the network interface
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "mode")]
    #[clap(help = EXP_HELP.get("interface_qdisc").unwrap().as_str())]
    pub interface_qdisc: Option<QDiscMode>,

    /// Create N worker threads. Note though, that `--parallelism` of them will
    /// be allowed to run simultaneously. If unset, will create a thread for
    /// each simulated Host. This is to work around limitations in ptrace, and
    /// may change in the future.
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "N")]
    #[clap(help = EXP_HELP.get("worker_threads").unwrap().as_str())]
    pub worker_threads: Option<NonZeroU32>,

    /// Don't adjust the working directories of the plugins
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bool")]
    #[clap(help = EXP_HELP.get("use_legacy_working_dir").unwrap().as_str())]
    pub use_legacy_working_dir: Option<bool>,

    /// Log level at which to print host statistics
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "level")]
    #[clap(help = EXP_HELP.get("host_heartbeat_log_level").unwrap().as_str())]
    pub host_heartbeat_log_level: Option<LogLevel>,

    /// List of information to show in the host's heartbeat message
    #[clap(hide_short_help = true)]
    #[clap(parse(try_from_str = parse_set_log_info_flags))]
    #[clap(long, value_name = "options")]
    #[clap(help = EXP_HELP.get("host_heartbeat_log_info").unwrap().as_str())]
    pub host_heartbeat_log_info: Option<HashSet<LogInfoFlag>>,

    /// Amount of time between heartbeat messages for this host
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "seconds")]
    #[clap(help = EXP_HELP.get("host_heartbeat_interval").unwrap().as_str())]
    pub host_heartbeat_interval: Option<units::Time<units::TimePrefixUpper>>,

    /// Log the syscalls for each process to individual "strace" files
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "mode")]
    #[clap(help = EXP_HELP.get("strace_logging_mode").unwrap().as_str())]
    pub strace_logging_mode: Option<StraceLoggingMode>,

    /// Max amount of execution-time latency allowed to accumulate before the
    /// clock is moved forward. Moving the clock forward is a potentially
    /// expensive operation, so larger values reduce simulation overhead, at the
    /// cost of coarser time jumps. Note also that accumulated-but-unapplied
    /// latency is discarded when a thread is blocked on a syscall.
    ///
    /// 0 to never account for CPU latency.
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "count")]
    #[clap(help = EXP_HELP.get("max_unapplied_cpu_latency").unwrap().as_str())]
    pub max_unapplied_cpu_latency: Option<units::Time<units::TimePrefix>>,

    /// Simulated latency of an unblocked syscall. For efficiency Shadow only
    /// actually adds this latency if and when `unblocked_syscall_limit` is
    /// reached.
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "seconds")]
    #[clap(help = EXP_HELP.get("unblocked_syscall_latency").unwrap().as_str())]
    pub unblocked_syscall_latency: Option<units::Time<units::TimePrefix>>,

    /// Simulated latency of a vdso "syscall". For efficiency Shadow only
    /// actually adds this latency if and when `unblocked_syscall_limit` is
    /// reached.
    #[clap(long, value_name = "seconds")]
    #[clap(help = EXP_HELP.get("unblocked_vdso_latency").unwrap().as_str())]
    pub unblocked_vdso_latency: Option<units::Time<units::TimePrefix>>,

    /// List of hostnames to debug
    #[clap(hide_short_help = true)]
    #[clap(parse(try_from_str = parse_set_str))]
    #[clap(long, value_name = "hostnames")]
    // a required attribute when we move this to `CliOptions`:
    //#[clap(default_value = "")]
    #[clap(help = EXP_HELP.get("debug_hosts").unwrap().as_str())]
    pub debug_hosts: Option<HashSet<String>>,
}

impl ExperimentalOptions {
    /// Replace unset (`None`) values of `base` with values from `default`.
    pub fn with_defaults(mut self, default: Self) -> Self {
        self.merge(default);
        self
    }
}

impl Default for ExperimentalOptions {
    fn default() -> Self {
        Self {
            use_sched_fifo: Some(false),
            use_o_n_waitpid_workarounds: Some(false),
            use_explicit_block_message: Some(false),
            use_seccomp: None,
            use_syscall_counters: Some(true),
            use_object_counters: Some(true),
            use_preload_libc: Some(true),
            use_preload_openssl_rng: Some(true),
            use_preload_openssl_crypto: Some(false),
            preload_spin_max: Some(0),
            max_unapplied_cpu_latency: Some(units::Time::new(10, units::TimePrefix::Micro)),
            // 2 microseconds is a ballpark estimate of the minimal latency for
            // context switching to the kernel and back on modern machines.
            unblocked_syscall_latency: Some(units::Time::new(2, units::TimePrefix::Micro)),
            // Actual latencies vary from ~40 to ~400 CPU cycles. https://stackoverflow.com/a/13096917
            unblocked_vdso_latency: Some(units::Time::new(100, units::TimePrefix::Nano)),
            use_memory_manager: Some(true),
            use_shim_syscall_handler: Some(true),
            use_cpu_pinning: Some(true),
            interpose_method: Some(InterposeMethod::Preload),
            runahead: Some(units::Time::new(1, units::TimePrefix::Milli)),
            use_dynamic_runahead: Some(false),
            scheduler_policy: Some(SchedulerPolicy::Host),
            socket_send_buffer: Some(units::Bytes::new(131_072, units::SiPrefixUpper::Base)),
            socket_send_autotune: Some(true),
            socket_recv_buffer: Some(units::Bytes::new(174_760, units::SiPrefixUpper::Base)),
            socket_recv_autotune: Some(true),
            interface_buffer: Some(units::Bytes::new(1_024_000, units::SiPrefixUpper::Base)),
            interface_qdisc: Some(QDiscMode::Fifo),
            worker_threads: None,
            use_legacy_working_dir: Some(false),
            host_heartbeat_log_level: Some(LogLevel::Info),
            host_heartbeat_log_info: Some(IntoIterator::into_iter([LogInfoFlag::Node]).collect()),
            host_heartbeat_interval: None,
            strace_logging_mode: Some(StraceLoggingMode::Off),
            debug_hosts: Some(HashSet::new()),
        }
    }
}

/// Help messages used by Clap for command line arguments, combining the doc string with
/// the Serde default.
static HOST_HELP: Lazy<std::collections::HashMap<String, String>> =
    Lazy::new(|| generate_help_strs(schema_for!(HostDefaultOptions)));

#[derive(Debug, Clone, Parser, Serialize, Deserialize, Merge, JsonSchema)]
#[clap(next_help_heading = "HOST DEFAULTS (Default options for hosts)")]
#[serde(default, deny_unknown_fields)]
pub struct HostDefaultOptions {
    /// Log level at which to print node messages
    #[clap(long = "host-log-level", name = "host-log-level")]
    #[clap(value_name = "level")]
    #[clap(help = HOST_HELP.get("log_level").unwrap().as_str())]
    pub log_level: Option<LogLevel>,

    /// Where to save the pcap files (relative to the host directory)
    #[clap(long, value_name = "path")]
    #[clap(help = HOST_HELP.get("pcap_directory").unwrap().as_str())]
    pub pcap_directory: Option<String>,

    /// How much data to capture per packet (header and payload) if pcap logging is enabled
    #[clap(long, value_name = "bytes")]
    #[clap(help = HOST_HELP.get("pcap_capture_size").unwrap().as_str())]
    pub pcap_capture_size: Option<units::Bytes<units::SiPrefixUpper>>,
}

impl HostDefaultOptions {
    pub fn new_empty() -> Self {
        Self {
            log_level: None,
            pcap_directory: None,
            pcap_capture_size: None,
        }
    }

    /// Replace unset (`None`) values of `base` with values from `default`.
    pub fn with_defaults(mut self, default: Self) -> Self {
        self.merge(default);
        self
    }
}

impl Default for HostDefaultOptions {
    fn default() -> Self {
        Self {
            log_level: None,
            pcap_directory: None,
            // From pcap(3): "A value of 65535 should be sufficient, on most if not all networks, to
            // capture all the data available from the packet". The maximum length of an IP packet
            // (including the header) is 65535 bytes.
            pcap_capture_size: Some(units::Bytes::new(65535, units::SiPrefixUpper::Base)),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct ProcessOptions {
    pub path: std::path::PathBuf,

    /// Process arguments
    #[serde(default = "default_args_empty")]
    pub args: ProcessArgs,

    /// Environment variables passed when executing this process. Multiple variables can be
    /// specified by using a semicolon separator (ex: `ENV_A=1;ENV_B=2`)
    #[serde(default)]
    pub environment: String,

    /// The number of replicas of this process to execute
    #[serde(default)]
    pub quantity: Quantity,

    /// The simulated time at which to execute the process
    #[serde(default)]
    pub start_time: units::Time<units::TimePrefixUpper>,

    /// The simulated time at which to send a SIGKILL signal to the process
    #[serde(default)]
    pub stop_time: Option<units::Time<units::TimePrefixUpper>>,
}

#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct HostOptions {
    /// Network graph node ID to assign the host to
    pub network_node_id: u32,

    pub processes: Vec<ProcessOptions>,

    /// IP address to assign to the host
    #[serde(default)]
    pub ip_addr: Option<std::net::Ipv4Addr>,

    /// Number of hosts to start
    #[serde(default)]
    pub quantity: Quantity,

    /// Downstream bandwidth capacity of the host
    #[serde(default)]
    pub bandwidth_down: Option<units::BitsPerSec<units::SiPrefixUpper>>,

    /// Upstream bandwidth capacity of the host
    #[serde(default)]
    pub bandwidth_up: Option<units::BitsPerSec<units::SiPrefixUpper>>,

    #[serde(default = "HostDefaultOptions::new_empty")]
    pub options: HostDefaultOptions,
}

#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
#[serde(rename_all = "lowercase")]
pub enum LogLevel {
    Error,
    Warning,
    Info,
    Debug,
    Trace,
}

impl FromStr for LogLevel {
    type Err = serde_yaml::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        serde_yaml::from_str(s)
    }
}

impl LogLevel {
    pub fn to_c_loglevel(&self) -> c_log::LogLevel {
        match self {
            Self::Error => c_log::_LogLevel_LOGLEVEL_ERROR,
            Self::Warning => c_log::_LogLevel_LOGLEVEL_WARNING,
            Self::Info => c_log::_LogLevel_LOGLEVEL_INFO,
            Self::Debug => c_log::_LogLevel_LOGLEVEL_DEBUG,
            Self::Trace => c_log::_LogLevel_LOGLEVEL_TRACE,
        }
    }
}

#[derive(Debug, Clone, Copy, Hash, PartialEq, Eq, Serialize, Deserialize, JsonSchema)]
#[serde(rename_all = "lowercase")]
#[repr(C)]
pub enum InterposeMethod {
    /// Attach to child using ptrace and use it to interpose syscalls etc.
    Ptrace,
    /// Use LD_PRELOAD to load a library that implements the libC interface which will
    /// route syscalls to Shadow.
    Preload,
}

impl FromStr for InterposeMethod {
    type Err = serde_yaml::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        serde_yaml::from_str(s)
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
#[serde(rename_all = "lowercase")]
pub enum SchedulerPolicy {
    Host,
    Steal,
    Thread,
    ThreadXThread,
    ThreadXHost,
}

impl FromStr for SchedulerPolicy {
    type Err = serde_yaml::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        serde_yaml::from_str(s)
    }
}

impl SchedulerPolicy {
    pub fn to_c_sched_policy_type(&self) -> c::SchedulerPolicyType {
        match self {
            Self::Host => c::SchedulerPolicyType_SP_PARALLEL_HOST_SINGLE,
            Self::Steal => c::SchedulerPolicyType_SP_PARALLEL_HOST_STEAL,
            Self::Thread => c::SchedulerPolicyType_SP_PARALLEL_THREAD_SINGLE,
            Self::ThreadXThread => c::SchedulerPolicyType_SP_PARALLEL_THREAD_PERTHREAD,
            Self::ThreadXHost => c::SchedulerPolicyType_SP_PARALLEL_THREAD_PERHOST,
        }
    }
}

fn default_data_directory() -> Option<String> {
    Some("shadow.data".into())
}

#[derive(Debug, Clone, Hash, PartialEq, Eq, ArgEnum, Serialize, Deserialize, JsonSchema)]
#[serde(rename_all = "lowercase")]
pub enum LogInfoFlag {
    Node,
    Socket,
    Ram,
}

impl LogInfoFlag {
    pub fn to_c_loginfoflag(&self) -> c::LogInfoFlags {
        match self {
            Self::Node => c::_LogInfoFlags_LOG_INFO_FLAGS_NODE,
            Self::Socket => c::_LogInfoFlags_LOG_INFO_FLAGS_SOCKET,
            Self::Ram => c::_LogInfoFlags_LOG_INFO_FLAGS_RAM,
        }
    }
}

impl FromStr for LogInfoFlag {
    type Err = serde_yaml::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        serde_yaml::from_str(s)
    }
}

/// Parse a string as a comma-delimited set of `T` values.
fn parse_set<T>(s: &str) -> Result<HashSet<T>, <T as FromStr>::Err>
where
    T: std::cmp::Eq + std::hash::Hash + FromStr,
{
    s.split(',').map(|x| x.trim().parse()).collect()
}

/// Parse a string as a comma-delimited set of `LogInfoFlag` values.
fn parse_set_log_info_flags(
    s: &str,
) -> Result<HashSet<LogInfoFlag>, <LogInfoFlag as FromStr>::Err> {
    parse_set(s)
}

/// Parse a string as a comma-delimited set of `String` values.
fn parse_set_str(s: &str) -> Result<HashSet<String>, <String as FromStr>::Err> {
    parse_set(s)
}

#[derive(Debug, Clone, Copy, Hash, PartialEq, Eq, ArgEnum, Serialize, Deserialize, JsonSchema)]
#[serde(rename_all = "lowercase")]
#[repr(C)]
pub enum QDiscMode {
    Fifo,
    RoundRobin,
}

impl FromStr for QDiscMode {
    type Err = serde_yaml::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        serde_yaml::from_str(s)
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
#[serde(rename_all = "snake_case")]
pub enum Compression {
    Xz,
}

#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct FileSource {
    /// The path to the file
    pub path: String,
    /// The file's compression format
    pub compression: Option<Compression>,
}

#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
#[serde(rename_all = "snake_case")]
pub enum GraphSource {
    File(FileSource),
    Inline(String),
}

#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum GraphOptions {
    Gml(GraphSource),
    #[serde(rename = "1_gbit_switch")]
    OneGbitSwitch,
}

#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct Quantity(u32);

impl Default for Quantity {
    fn default() -> Self {
        Self(1)
    }
}

impl std::ops::Deref for Quantity {
    type Target = u32;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

#[derive(Debug, Clone, Serialize, JsonSchema)]
#[serde(untagged)]
pub enum ProcessArgs {
    List(Vec<String>),
    Str(String),
}

/// Serde doesn't provide good deserialization error messages for untagged enums, so we implement
/// our own. For example, if serde finds a yaml value such as 4 for the process arguments, it won't
/// deserialize it to the string "4" and the yaml parsing will fail. The serde-generated error
/// message will say something like "data did not match any variant of untagged enum ProcessArgs at
/// line X column Y" which isn't very helpful to the user, so here we try to give a better error
/// message.
impl<'de> serde::Deserialize<'de> for ProcessArgs {
    fn deserialize<D: serde::Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        struct ProcessArgsVisitor;

        impl<'de> serde::de::Visitor<'de> for ProcessArgsVisitor {
            type Value = ProcessArgs;

            fn expecting(&self, formatter: &mut std::fmt::Formatter) -> std::fmt::Result {
                formatter.write_str("a string or a sequence of strings")
            }

            fn visit_str<E>(self, v: &str) -> Result<Self::Value, E>
            where
                E: serde::de::Error,
            {
                Ok(Self::Value::Str(v.to_owned()))
            }

            fn visit_seq<A>(self, mut seq: A) -> Result<Self::Value, A::Error>
            where
                A: serde::de::SeqAccess<'de>,
            {
                let mut v = vec![];

                while let Some(val) = seq.next_element()? {
                    v.push(val);
                }

                Ok(Self::Value::List(v))
            }
        }

        deserializer.deserialize_any(ProcessArgsVisitor)
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
#[serde(rename_all = "snake_case")]
pub enum StraceLoggingMode {
    Off,
    Standard,
    Deterministic,
}

impl FromStr for StraceLoggingMode {
    type Err = serde_yaml::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        serde_yaml::from_str(s)
    }
}

/// Helper function for serde default `ProcessArgs::Str("")` values.
fn default_args_empty() -> ProcessArgs {
    ProcessArgs::Str("".to_string())
}

/// Helper function for serde default `Some(0)` values.
fn default_some_time_0() -> Option<units::Time<units::TimePrefixUpper>> {
    Some(units::Time::new(0, units::TimePrefixUpper::Sec))
}

/// Helper function for serde default `Some(true)` values.
fn default_some_true() -> Option<bool> {
    Some(true)
}

/// Helper function for serde default `Some(false)` values.
fn default_some_false() -> Option<bool> {
    Some(false)
}

/// Helper function for serde default `Some(1)` values.
fn default_some_1() -> Option<u32> {
    Some(1)
}

/// Helper function for serde default `Some(1)` values.
fn default_some_nz_1() -> Option<NonZeroU32> {
    Some(std::num::NonZeroU32::new(1).unwrap())
}

/// Helper function for serde default `Some(1 sec)` values.
fn default_some_time_1() -> Option<units::Time<units::TimePrefixUpper>> {
    Some(units::Time::new(1, units::TimePrefixUpper::Sec))
}

/// Helper function for serde default `Some(LogLevel::Info)` values.
fn default_some_info() -> Option<LogLevel> {
    Some(LogLevel::Info)
}

// when updating this graph, make sure to also update the copy in docs/shadow_config_spec.md
pub const ONE_GBIT_SWITCH_GRAPH: &str = r#"graph [
  directed 0
  node [
    id 0
    host_bandwidth_up "1 Gbit"
    host_bandwidth_down "1 Gbit"
  ]
  edge [
    source 0
    target 0
    latency "1 ms"
    packet_loss 0.0
  ]
]"#;

/// Generate help strings for objects in a JSON schema, including the Serde defaults if available.
fn generate_help_strs(
    schema: schemars::schema::RootSchema,
) -> std::collections::HashMap<String, String> {
    let mut defaults = std::collections::HashMap::<String, String>::new();
    for (name, obj) in &schema.schema.object.as_ref().unwrap().properties {
        if let Some(meta) = obj.clone().into_object().metadata {
            let description = meta.description.or(Some("".to_string())).unwrap();
            let space = if !description.is_empty() { " " } else { "" };
            match meta.default {
                Some(default) => defaults.insert(
                    name.clone(),
                    format!("{}{}[default: {}]", description, space, default),
                ),
                None => defaults.insert(name.clone(), description.to_string()),
            };
        }
    }
    defaults
}

pub fn tilde_expansion(path: &str) -> std::path::PathBuf {
    // if the path begins with a "~"
    if let Some(x) = path.strip_prefix('~') {
        // get the tilde-prefix (everything before the first separator)
        let mut parts = x.splitn(2, '/');
        let (tilde_prefix, remainder) = (parts.next().unwrap(), parts.next().unwrap_or(""));
        assert!(parts.next().is_none());
        // we only support expansion for our own home directory
        // (nothing between the "~" and the separator)
        if tilde_prefix.is_empty() {
            if let Ok(ref home) = std::env::var("HOME") {
                return [home, remainder].iter().collect::<std::path::PathBuf>();
            }
        }
    }

    // if we don't have a tilde-prefix that we support, just return the unmodified path
    std::path::PathBuf::from(path)
}

/// Parses a string as a list of arguments following the shell's parsing rules. This
/// uses `g_shell_parse_argv()` for parsing.
fn parse_string_as_args(args_str: &OsStr) -> Result<Vec<OsString>, String> {
    if args_str.is_empty() {
        return Ok(Vec::new());
    }

    let args_str = CString::new(args_str.as_bytes()).unwrap();

    // parse the argument string
    let mut argc: libc::c_int = 0;
    let mut argv: *mut *mut libc::c_char = std::ptr::null_mut();
    let mut error: *mut libc::c_char = std::ptr::null_mut();
    let rv = unsafe { c::process_parseArgStr(args_str.as_ptr(), &mut argc, &mut argv, &mut error) };

    // if there was an error, return a copy of the error string
    if !rv {
        let error_message = match error.is_null() {
            false => unsafe { CStr::from_ptr(error) }.to_str().unwrap(),
            true => "Unknown parsing error",
        }
        .to_string();

        unsafe { c::process_parseArgStrFree(argv, error) };
        return Err(error_message);
    }

    assert!(!argv.is_null());

    // copy the arg strings
    let args: Vec<_> = (0..argc)
        .map(|x| unsafe {
            let arg_ptr = *argv.add(x as usize);
            assert!(!arg_ptr.is_null());
            OsStr::from_bytes(CStr::from_ptr(arg_ptr).to_bytes()).to_os_string()
        })
        .collect();

    unsafe { c::process_parseArgStrFree(argv, error) };
    Ok(args)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_args() {
        let arg_str = r#"the quick brown fox "jumped over" the "\"lazy\" dog""#;
        let expected_args = &[
            "the",
            "quick",
            "brown",
            "fox",
            "jumped over",
            "the",
            "\"lazy\" dog",
        ];

        let arg_str: OsString = arg_str.into();
        let args = parse_string_as_args(&arg_str).unwrap();

        assert_eq!(args, expected_args);
    }

    #[test]
    fn test_parse_args_empty() {
        let arg_str = "";
        let expected_args: &[&str] = &[];

        let arg_str: OsString = arg_str.into();
        let args = parse_string_as_args(&arg_str).unwrap();

        assert_eq!(args, expected_args);
    }

    #[test]
    fn test_parse_args_error() {
        let arg_str = r#"hello "world"#;

        let arg_str: OsString = arg_str.into();
        let err_str = parse_string_as_args(&arg_str).unwrap_err();

        assert!(!err_str.is_empty());
    }

    #[test]
    fn test_tilde_expansion() {
        if let Ok(ref home) = std::env::var("HOME") {
            assert_eq!(
                tilde_expansion("~/test"),
                [home, "test"].iter().collect::<std::path::PathBuf>()
            );

            assert_eq!(
                tilde_expansion("~"),
                [home].iter().collect::<std::path::PathBuf>()
            );

            assert_eq!(
                tilde_expansion("~/"),
                [home].iter().collect::<std::path::PathBuf>()
            );

            assert_eq!(
                tilde_expansion("~someuser/test"),
                ["~someuser", "test"].iter().collect::<std::path::PathBuf>()
            );

            assert_eq!(
                tilde_expansion("/~/test"),
                ["/", "~", "test"].iter().collect::<std::path::PathBuf>()
            );

            assert_eq!(
                tilde_expansion(""),
                [""].iter().collect::<std::path::PathBuf>()
            );
        }
    }
}

mod export {
    use super::*;

    #[no_mangle]
    pub extern "C" fn hashsetstring_contains(
        set: *const HashSet<String>,
        hostname: *const libc::c_char,
    ) -> bool {
        let set = unsafe { set.as_ref() }.unwrap();
        assert!(!hostname.is_null());
        let hostname = unsafe { CStr::from_ptr(hostname) };

        set.contains(hostname.to_str().unwrap())
    }

    #[no_mangle]
    pub extern "C" fn clioptions_freeString(string: *mut libc::c_char) {
        if !string.is_null() {
            unsafe { CString::from_raw(string) };
        }
    }

    #[no_mangle]
    pub extern "C" fn clioptions_getGdb(options: *const CliOptions) -> bool {
        assert!(!options.is_null());
        let options = unsafe { &*options };
        options.gdb
    }

    #[no_mangle]
    pub extern "C" fn clioptions_getShmCleanup(options: *const CliOptions) -> bool {
        assert!(!options.is_null());
        let options = unsafe { &*options };
        options.shm_cleanup
    }

    #[no_mangle]
    pub extern "C" fn clioptions_getShowBuildInfo(options: *const CliOptions) -> bool {
        assert!(!options.is_null());
        let options = unsafe { &*options };
        options.show_build_info
    }

    #[no_mangle]
    pub extern "C" fn clioptions_getShowConfig(options: *const CliOptions) -> bool {
        assert!(!options.is_null());
        let options = unsafe { &*options };
        options.show_config
    }

    #[no_mangle]
    pub extern "C" fn clioptions_getConfig(options: *const CliOptions) -> *mut libc::c_char {
        assert!(!options.is_null());
        let options = unsafe { &*options };

        match &options.config {
            Some(s) => CString::into_raw(CString::new(s.clone()).unwrap()),
            None => std::ptr::null_mut(),
        }
    }

    #[no_mangle]
    pub extern "C" fn config_freeString(string: *mut libc::c_char) {
        if !string.is_null() {
            unsafe { CString::from_raw(string) };
        }
    }

    #[no_mangle]
    pub extern "C" fn config_showConfig(config: *const ConfigOptions) {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        eprintln!("{:#?}", config);
    }

    #[no_mangle]
    pub extern "C" fn config_getSeed(config: *const ConfigOptions) -> libc::c_uint {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        config.general.seed.unwrap()
    }

    #[no_mangle]
    pub extern "C" fn config_getLogLevel(config: *const ConfigOptions) -> c_log::LogLevel {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        config.general.log_level.as_ref().unwrap().to_c_loglevel()
    }

    #[no_mangle]
    pub extern "C" fn config_getHeartbeatInterval(
        config: *const ConfigOptions,
    ) -> c::SimulationTime {
        assert!(!config.is_null());
        let config = unsafe { &*config };

        match config.general.heartbeat_interval {
            Some(x) => x.convert(units::TimePrefixUpper::Sec).unwrap().value() * SIMTIME_ONE_SECOND,
            None => SIMTIME_INVALID,
        }
    }

    #[no_mangle]
    pub extern "C" fn config_getRunahead(config: *const ConfigOptions) -> c::SimulationTime {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        match config.experimental.runahead {
            Some(x) => x.convert(units::TimePrefix::Nano).unwrap().value() * SIMTIME_ONE_NANOSECOND,
            // shadow uses a value of 0 as "not set" instead of SIMTIME_INVALID
            None => 0,
        }
    }

    #[no_mangle]
    pub extern "C" fn config_getUseDynamicRunahead(config: *const ConfigOptions) -> bool {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        config.experimental.use_dynamic_runahead.unwrap()
    }

    #[no_mangle]
    pub extern "C" fn config_getUseCpuPinning(config: *const ConfigOptions) -> bool {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        config.experimental.use_cpu_pinning.unwrap()
    }

    #[no_mangle]
    pub extern "C" fn config_getInterposeMethod(config: *const ConfigOptions) -> InterposeMethod {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        config.experimental.interpose_method.unwrap()
    }

    #[no_mangle]
    pub extern "C" fn config_getUseSchedFifo(config: *const ConfigOptions) -> bool {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        config.experimental.use_sched_fifo.unwrap()
    }

    #[no_mangle]
    pub extern "C" fn config_getUseOnWaitpidWorkarounds(config: *const ConfigOptions) -> bool {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        config.experimental.use_o_n_waitpid_workarounds.unwrap()
    }

    #[no_mangle]
    pub extern "C" fn config_getUseExplicitBlockMessage(config: *const ConfigOptions) -> bool {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        config.experimental.use_explicit_block_message.unwrap()
    }

    #[no_mangle]
    pub extern "C" fn config_getUseSeccomp(config: *const ConfigOptions) -> bool {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        match config.experimental.use_seccomp {
            Some(b) => b,
            None => config_getInterposeMethod(config) == InterposeMethod::Preload,
        }
    }

    #[no_mangle]
    pub extern "C" fn config_getUseSyscallCounters(config: *const ConfigOptions) -> bool {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        config.experimental.use_syscall_counters.unwrap()
    }

    #[no_mangle]
    pub extern "C" fn config_getUseObjectCounters(config: *const ConfigOptions) -> bool {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        config.experimental.use_object_counters.unwrap()
    }

    #[no_mangle]
    pub extern "C" fn config_getUseLibcPreload(config: *const ConfigOptions) -> bool {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        config.experimental.use_preload_libc.unwrap()
    }

    #[no_mangle]
    pub extern "C" fn config_getUseOpensslRNGPreload(config: *const ConfigOptions) -> bool {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        config.experimental.use_preload_openssl_rng.unwrap()
    }

    #[no_mangle]
    pub extern "C" fn config_getUseOpensslCryptoPreload(config: *const ConfigOptions) -> bool {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        config.experimental.use_preload_openssl_crypto.unwrap()
    }

    #[no_mangle]
    pub extern "C" fn config_getUseMemoryManager(config: *const ConfigOptions) -> bool {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        config.experimental.use_memory_manager.unwrap()
    }

    #[no_mangle]
    pub extern "C" fn config_getUseShimSyscallHandler(config: *const ConfigOptions) -> bool {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        config.experimental.use_shim_syscall_handler.unwrap()
    }

    #[no_mangle]
    pub extern "C" fn config_getPreloadSpinMax(config: *const ConfigOptions) -> i32 {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        config.experimental.preload_spin_max.unwrap()
    }

    #[no_mangle]
    pub extern "C" fn config_getMaxUnappliedCpuLatency(config: *const ConfigOptions) -> u64 {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        match config.experimental.max_unapplied_cpu_latency {
            Some(x) => x.convert(units::TimePrefix::Nano).unwrap().value() * SIMTIME_ONE_NANOSECOND,
            // shadow uses a value of 0 as "not set" instead of SIMTIME_INVALID
            None => 0,
        }
    }

    #[no_mangle]
    pub extern "C" fn config_getUnblockedSyscallLatency(
        config: *const ConfigOptions,
    ) -> c::SimulationTime {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        match config.experimental.unblocked_syscall_latency {
            Some(x) => x.convert(units::TimePrefix::Nano).unwrap().value() * SIMTIME_ONE_NANOSECOND,
            // shadow uses a value of 0 as "not set" instead of SIMTIME_INVALID
            None => 0,
        }
    }

    #[no_mangle]
    pub extern "C" fn config_getUnblockedVdsoLatency(
        config: *const ConfigOptions,
    ) -> c::SimulationTime {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        match config.experimental.unblocked_vdso_latency {
            Some(x) => x.convert(units::TimePrefix::Nano).unwrap().value() * SIMTIME_ONE_NANOSECOND,
            // shadow uses a value of 0 as "not set" instead of SIMTIME_INVALID
            None => 0,
        }
    }

    #[no_mangle]
    pub extern "C" fn config_getParallelism(config: *const ConfigOptions) -> NonZeroU32 {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        config.general.parallelism.unwrap()
    }

    #[no_mangle]
    pub extern "C" fn config_getStopTime(config: *const ConfigOptions) -> c::SimulationTime {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        config
            .general
            .stop_time
            .unwrap()
            .convert(units::TimePrefixUpper::Sec)
            .unwrap()
            .value()
            * SIMTIME_ONE_SECOND
    }

    #[no_mangle]
    pub extern "C" fn config_getBootstrapEndTime(
        config: *const ConfigOptions,
    ) -> c::SimulationTime {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        config
            .general
            .bootstrap_end_time
            .unwrap()
            .convert(units::TimePrefixUpper::Sec)
            .unwrap()
            .value()
            * SIMTIME_ONE_SECOND
    }

    #[no_mangle]
    pub extern "C" fn config_getWorkers(config: *const ConfigOptions) -> NonZeroU32 {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        match &config.experimental.worker_threads {
            Some(w) => *w,
            None => {
                // By default use 1 worker per host.
                NonZeroU32::new(config_getNHosts(config)).unwrap()
            }
        }
    }

    #[no_mangle]
    pub extern "C" fn config_getSchedulerPolicy(
        config: *const ConfigOptions,
    ) -> c::SchedulerPolicyType {
        assert!(!config.is_null());
        let config = unsafe { &*config };

        config
            .experimental
            .scheduler_policy
            .as_ref()
            .unwrap()
            .to_c_sched_policy_type()
    }

    #[no_mangle]
    pub extern "C" fn config_getDataDirectory(config: *const ConfigOptions) -> *mut libc::c_char {
        assert!(!config.is_null());
        let config = unsafe { &*config };

        let data_directory = config.general.data_directory.as_ref().unwrap();
        let data_directory = tilde_expansion(data_directory);
        CString::into_raw(CString::new(data_directory.to_str().unwrap()).unwrap())
    }

    #[no_mangle]
    pub extern "C" fn config_getTemplateDirectory(
        config: *const ConfigOptions,
    ) -> *mut libc::c_char {
        assert!(!config.is_null());
        let config = unsafe { &*config };

        match config.general.template_directory {
            Some(ref x) => {
                let x = tilde_expansion(x);
                CString::into_raw(CString::new(x.to_str().unwrap()).unwrap())
            }
            None => std::ptr::null_mut(),
        }
    }

    #[no_mangle]
    pub extern "C" fn config_getSocketRecvBuffer(config: *const ConfigOptions) -> u64 {
        assert!(!config.is_null());
        let config = unsafe { &*config };

        config
            .experimental
            .socket_recv_buffer
            .unwrap()
            .convert(units::SiPrefixUpper::Base)
            .unwrap()
            .value()
    }

    #[no_mangle]
    pub extern "C" fn config_getSocketSendBuffer(config: *const ConfigOptions) -> u64 {
        assert!(!config.is_null());
        let config = unsafe { &*config };

        config
            .experimental
            .socket_send_buffer
            .unwrap()
            .convert(units::SiPrefixUpper::Base)
            .unwrap()
            .value()
    }

    #[no_mangle]
    pub extern "C" fn config_getSocketSendAutotune(config: *const ConfigOptions) -> bool {
        assert!(!config.is_null());
        let config = unsafe { &*config };

        config.experimental.socket_send_autotune.unwrap()
    }

    #[no_mangle]
    pub extern "C" fn config_getSocketRecvAutotune(config: *const ConfigOptions) -> bool {
        assert!(!config.is_null());
        let config = unsafe { &*config };

        config.experimental.socket_recv_autotune.unwrap()
    }

    #[no_mangle]
    pub extern "C" fn config_getInterfaceBuffer(config: *const ConfigOptions) -> u64 {
        assert!(!config.is_null());
        let config = unsafe { &*config };

        config
            .experimental
            .interface_buffer
            .unwrap()
            .convert(units::SiPrefixUpper::Base)
            .unwrap()
            .value()
    }

    #[no_mangle]
    pub extern "C" fn config_getInterfaceQdisc(config: *const ConfigOptions) -> QDiscMode {
        assert!(!config.is_null());
        let config = unsafe { &*config };

        config.experimental.interface_qdisc.unwrap()
    }

    #[no_mangle]
    pub extern "C" fn config_getUseLegacyWorkingDir(config: *const ConfigOptions) -> bool {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        config.experimental.use_legacy_working_dir.unwrap()
    }

    #[no_mangle]
    pub extern "C" fn config_getProgress(config: *const ConfigOptions) -> bool {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        config.general.progress.unwrap()
    }

    #[no_mangle]
    pub extern "C" fn config_getModelUnblockedSyscallLatency(config: *const ConfigOptions) -> bool {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        config.general.model_unblocked_syscall_latency.unwrap()
    }

    #[no_mangle]
    pub extern "C" fn config_getHostHeartbeatLogLevel(
        config: *const ConfigOptions,
    ) -> c_log::LogLevel {
        assert!(!config.is_null());
        let config = unsafe { &*config };

        match &config.experimental.host_heartbeat_log_level {
            Some(x) => x.to_c_loglevel(),
            None => c_log::_LogLevel_LOGLEVEL_UNSET,
        }
    }

    #[no_mangle]
    pub extern "C" fn config_getHostHeartbeatLogInfo(
        config: *const ConfigOptions,
    ) -> c::LogInfoFlags {
        assert!(!config.is_null());
        let config = unsafe { &*config };

        let mut flags = 0;

        for f in config
            .experimental
            .host_heartbeat_log_info
            .as_ref()
            .unwrap()
        {
            flags |= f.to_c_loginfoflag();
        }

        flags
    }

    #[no_mangle]
    pub extern "C" fn config_getHostHeartbeatInterval(
        config: *const ConfigOptions,
    ) -> c::SimulationTime {
        assert!(!config.is_null());
        let config = unsafe { &*config };

        match config.experimental.host_heartbeat_interval {
            Some(x) => x.convert(units::TimePrefixUpper::Sec).unwrap().value() * SIMTIME_ONE_SECOND,
            None => SIMTIME_INVALID,
        }
    }

    #[no_mangle]
    pub extern "C" fn config_getStraceLoggingMode(config: *const ConfigOptions) -> StraceFmtMode {
        assert!(!config.is_null());
        let config = unsafe { &*config };

        match config.experimental.strace_logging_mode.as_ref().unwrap() {
            StraceLoggingMode::Standard => StraceFmtMode::Standard,
            StraceLoggingMode::Deterministic => StraceFmtMode::Deterministic,
            StraceLoggingMode::Off => StraceFmtMode::Off,
        }
    }

    #[no_mangle]
    pub extern "C" fn config_getUseShortestPath(config: *const ConfigOptions) -> bool {
        assert!(!config.is_null());
        let config = unsafe { &*config };

        config.network.use_shortest_path.unwrap()
    }

    #[no_mangle]
    #[must_use]
    pub extern "C" fn config_iterHosts(
        config: *const ConfigOptions,
        f: unsafe extern "C" fn(
            *const libc::c_char,
            *const ConfigOptions,
            *const HostOptions,
            *mut libc::c_void,
        ) -> libc::c_int,
        data: *mut libc::c_void,
    ) -> libc::c_int {
        assert!(!config.is_null());
        let config = unsafe { &*config };

        for (name, host) in &config.hosts {
            // bind the string to a local variable so it's not dropped before f() runs
            let name = CString::new(name.clone()).unwrap();
            let rv = unsafe {
                f(
                    name.as_c_str().as_ptr(),
                    config as *const _,
                    host as *const _,
                    data,
                )
            };
            if rv != 0 {
                return rv;
            }
        }

        0
    }

    #[no_mangle]
    pub extern "C" fn config_getNHosts(config: *const ConfigOptions) -> u32 {
        assert!(!config.is_null());
        let config = unsafe { &*config };

        config
            .hosts
            .iter()
            .map(|(_, host)| hostoptions_getQuantity(host))
            .sum()
    }

    #[no_mangle]
    pub extern "C" fn hostoptions_freeString(string: *mut libc::c_char) {
        if !string.is_null() {
            unsafe { CString::from_raw(string) };
        }
    }

    #[no_mangle]
    pub extern "C" fn hostoptions_getNetworkNodeId(host: *const HostOptions) -> libc::c_uint {
        assert!(!host.is_null());
        let host = unsafe { &*host };

        host.network_node_id
    }

    #[no_mangle]
    pub extern "C" fn hostoptions_getIpAddr(
        host: *const HostOptions,
        addr: *mut libc::in_addr_t,
    ) -> libc::c_int {
        assert!(!host.is_null());
        assert!(!addr.is_null());
        let host = unsafe { &*host };
        let addr = unsafe { &mut *addr };

        match host.ip_addr {
            Some(x) => {
                *addr = u32::to_be(x.into());
                0
            }
            None => -1,
        }
    }

    #[no_mangle]
    pub extern "C" fn hostoptions_getQuantity(host: *const HostOptions) -> libc::c_uint {
        assert!(!host.is_null());
        let host = unsafe { &*host };

        *host.quantity
    }

    #[no_mangle]
    pub extern "C" fn hostoptions_getLogLevel(host: *const HostOptions) -> c_log::LogLevel {
        assert!(!host.is_null());
        let host = unsafe { &*host };

        match &host.options.log_level {
            Some(x) => x.to_c_loglevel(),
            None => c_log::_LogLevel_LOGLEVEL_UNSET,
        }
    }

    #[no_mangle]
    pub extern "C" fn hostoptions_getPcapDirectory(host: *const HostOptions) -> *mut libc::c_char {
        assert!(!host.is_null());
        let host = unsafe { &*host };

        match &host.options.pcap_directory {
            Some(pcap_dir) => {
                let pcap_dir = tilde_expansion(pcap_dir);
                CString::into_raw(CString::new(pcap_dir.to_str().unwrap()).unwrap())
            }
            None => std::ptr::null_mut(),
        }
    }

    #[no_mangle]
    pub extern "C" fn hostoptions_getPcapCaptureSize(host: *const HostOptions) -> u32 {
        assert!(!host.is_null());
        let host = unsafe { &*host };

        host.options
            .pcap_capture_size
            .unwrap()
            .convert(units::SiPrefixUpper::Base)
            .unwrap()
            .value()
            .try_into()
            .unwrap()
    }

    /// Get the downstream bandwidth of the host if it exists. A non-zero return value means that
    /// the host did not have a downstream bandwidth and that `bandwidth_down` was not updated.
    #[no_mangle]
    pub extern "C" fn hostoptions_getBandwidthDown(
        host: *const HostOptions,
        bandwidth_down: *mut u64,
    ) -> libc::c_int {
        assert!(!host.is_null());
        assert!(!bandwidth_down.is_null());
        let host = unsafe { &*host };
        let bandwidth_down = unsafe { &mut *bandwidth_down };

        match host.bandwidth_down {
            Some(x) => {
                *bandwidth_down = x.convert(units::SiPrefixUpper::Base).unwrap().value();
                0
            }
            None => -1,
        }
    }

    /// Get the upstream bandwidth of the host if it exists. A non-zero return value means that
    /// the host did not have an upstream bandwidth and that `bandwidth_up` was not updated.
    #[no_mangle]
    pub extern "C" fn hostoptions_getBandwidthUp(
        host: *const HostOptions,
        bandwidth_up: *mut u64,
    ) -> libc::c_int {
        assert!(!host.is_null());
        assert!(!bandwidth_up.is_null());
        let host = unsafe { &*host };
        let bandwidth_up = unsafe { &mut *bandwidth_up };

        match host.bandwidth_up {
            Some(x) => {
                *bandwidth_up = x.convert(units::SiPrefixUpper::Base).unwrap().value();
                0
            }
            None => -1,
        }
    }

    #[no_mangle]
    #[must_use]
    pub extern "C" fn hostoptions_iterProcesses(
        host: *const HostOptions,
        f: unsafe extern "C" fn(*const ProcessOptions, *mut libc::c_void) -> libc::c_int,
        data: *mut libc::c_void,
    ) -> libc::c_int {
        assert!(!host.is_null());
        let host = unsafe { &*host };

        for proc in &host.processes {
            let rv = unsafe { f(proc as *const _, data) };

            if rv != 0 {
                return rv;
            }
        }

        0
    }

    #[no_mangle]
    pub extern "C" fn processoptions_freeString(string: *mut libc::c_char) {
        if !string.is_null() {
            unsafe { CString::from_raw(string) };
        }
    }

    /// Will return a NULL pointer if the path does not exist.
    #[no_mangle]
    pub extern "C" fn processoptions_getPath(proc: *const ProcessOptions) -> *mut libc::c_char {
        assert!(!proc.is_null());
        let proc = unsafe { &*proc };

        let expanded = tilde_expansion(proc.path.to_str().unwrap());

        let path = match expanded.canonicalize() {
            Ok(path) => path,
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => {
                log::warn!("Unable to find {:?}", &expanded);
                return std::ptr::null_mut();
            }
            Err(_) => panic!(),
        };

        let metadata = std::fs::metadata(&path).unwrap();

        if !metadata.is_file() {
            log::warn!("The path {:?} is not a file", &path);
            return std::ptr::null_mut();
        }

        // this mask doesn't guarantee that we can execute the file (the file might have S_IXUSR
        // but be owned by a different user), but it should catch most errors
        let mask = libc::S_IXUSR | libc::S_IXGRP | libc::S_IXOTH;
        if (metadata.mode() & mask) == 0 {
            log::warn!("The path {:?} is not executable", &path);
            return std::ptr::null_mut();
        }

        CString::into_raw(CString::new(path.to_str().unwrap()).unwrap())
    }

    /// Returns the path exactly as specified in the config. Caller must free returned string.
    #[no_mangle]
    pub extern "C" fn processoptions_getRawPath(proc: *const ProcessOptions) -> *mut libc::c_char {
        assert!(!proc.is_null());
        let proc = unsafe { proc.as_ref().unwrap() };
        CString::into_raw(CString::new(proc.path.to_string_lossy().as_bytes()).unwrap())
    }

    #[no_mangle]
    pub extern "C" fn processoptions_getArgs(
        proc: *const ProcessOptions,
        f: unsafe extern "C" fn(*const libc::c_char, *mut libc::c_void),
        data: *mut libc::c_void,
    ) {
        assert!(!proc.is_null());
        let proc = unsafe { &*proc };

        let args = match &proc.args {
            ProcessArgs::List(x) => x.iter().map(|y| OsStr::new(y).to_os_string()).collect(),
            ProcessArgs::Str(x) => parse_string_as_args(OsStr::new(&x.trim())).unwrap(),
        };

        for arg in &args {
            // bind the string to a local variable so it's not dropped before f() runs
            let arg = CString::new(arg.as_bytes()).unwrap();
            unsafe { f(arg.as_c_str().as_ptr(), data) }
        }
    }

    #[no_mangle]
    pub extern "C" fn processoptions_getEnvironment(
        proc: *const ProcessOptions,
    ) -> *mut libc::c_char {
        assert!(!proc.is_null());
        let proc = unsafe { &*proc };

        CString::into_raw(CString::new(proc.environment.clone()).unwrap())
    }

    #[no_mangle]
    pub extern "C" fn processoptions_getQuantity(proc: *const ProcessOptions) -> u32 {
        assert!(!proc.is_null());
        let proc = unsafe { &*proc };

        *proc.quantity
    }

    #[no_mangle]
    pub extern "C" fn processoptions_getStartTime(
        proc: *const ProcessOptions,
    ) -> c::SimulationTime {
        assert!(!proc.is_null());
        let proc = unsafe { &*proc };

        proc.start_time
            .convert(units::TimePrefixUpper::Sec)
            .unwrap()
            .value()
            * SIMTIME_ONE_SECOND
    }

    #[no_mangle]
    pub extern "C" fn processoptions_getStopTime(proc: *const ProcessOptions) -> c::SimulationTime {
        assert!(!proc.is_null());
        let proc = unsafe { &*proc };

        match proc.stop_time {
            Some(x) => x.convert(units::TimePrefixUpper::Sec).unwrap().value() * SIMTIME_ONE_SECOND,
            // shadow uses a value of 0 as "not set" instead of SIMTIME_INVALID
            None => 0,
        }
    }
}
