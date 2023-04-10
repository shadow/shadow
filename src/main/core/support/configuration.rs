use std::collections::{BTreeMap, HashSet};
use std::ffi::{CStr, CString, OsStr, OsString};
use std::num::NonZeroU32;
use std::os::unix::ffi::OsStrExt;
use std::str::FromStr;

use clap::Parser;
use logger as c_log;
use merge::Merge;
use once_cell::sync::Lazy;
use schemars::{schema_for, JsonSchema};
use serde::{Deserialize, Serialize};
use shadow_shim_helper_rs::simulation_time::SimulationTime;

use super::units::{self, Unit};
use crate::cshadow as c;
use crate::host::syscall::formatter::FmtOptions;

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
#[clap(next_display_order = None)]
pub struct CliOptions {
    /// Path to the Shadow configuration file. Use '-' to read from stdin
    #[clap(required_unless_present_any(&["show_build_info", "shm_cleanup"]))]
    pub config: Option<String>,

    /// Pause to allow gdb to attach
    #[clap(long, short = 'g')]
    pub gdb: bool,

    /// Pause after starting any processes on the comma-delimited list of hostnames
    #[clap(value_parser = parse_set_str)]
    #[clap(long, value_name = "hostnames")]
    pub debug_hosts: Option<HashSet<String>>,

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
    // since shadow parses to a serde_yaml::Value initially, we don't need to worry about duplicate
    // hostnames here
    pub hosts: BTreeMap<HostName, HostOptions>,
}

/// Shadow configuration options after processing command-line and configuration file options.
#[derive(Debug, Clone, Serialize)]
pub struct ConfigOptions {
    pub general: GeneralOptions,

    pub network: NetworkOptions,

    pub experimental: ExperimentalOptions,

    // we use a BTreeMap so that the hosts are sorted by their hostname (useful for determinism)
    pub hosts: BTreeMap<HostName, HostOptions>,
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

    pub fn model_unblocked_syscall_latency(&self) -> bool {
        self.general.model_unblocked_syscall_latency.unwrap()
    }

    pub fn max_unapplied_cpu_latency(&self) -> SimulationTime {
        let nanos = self.experimental.max_unapplied_cpu_latency.unwrap();
        let nanos = nanos.convert(units::TimePrefix::Nano).unwrap().value();
        SimulationTime::from_nanos(nanos)
    }

    pub fn unblocked_syscall_latency(&self) -> SimulationTime {
        let nanos = self.experimental.unblocked_syscall_latency.unwrap();
        let nanos = nanos.convert(units::TimePrefix::Nano).unwrap().value();
        SimulationTime::from_nanos(nanos)
    }

    pub fn unblocked_vdso_latency(&self) -> SimulationTime {
        let nanos = self.experimental.unblocked_vdso_latency.unwrap();
        let nanos = nanos.convert(units::TimePrefix::Nano).unwrap().value();
        SimulationTime::from_nanos(nanos)
    }

    pub fn use_legacy_working_dir(&self) -> bool {
        self.experimental.use_legacy_working_dir.unwrap()
    }

    pub fn strace_logging_mode(&self) -> Option<FmtOptions> {
        match self.experimental.strace_logging_mode.as_ref().unwrap() {
            StraceLoggingMode::Standard => Some(FmtOptions::Standard),
            StraceLoggingMode::Deterministic => Some(FmtOptions::Deterministic),
            StraceLoggingMode::Off => None,
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
#[clap(next_help_heading = "General (Override configuration file options)")]
#[clap(next_display_order = None)]
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
    /// performance is usually obtained with `nproc`, or sometimes `nproc`/2
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
    #[serde(default = "default_some_nullable_time_1")]
    pub heartbeat_interval: Option<NullableOption<units::Time<units::TimePrefixUpper>>>,

    /// Path to store simulation output
    #[clap(long, short = 'd', value_name = "path")]
    #[clap(help = GENERAL_HELP.get("data_directory").unwrap().as_str())]
    #[serde(default = "default_data_directory")]
    pub data_directory: Option<String>,

    /// Path to recursively copy during startup and use as the data-directory
    #[clap(long, short = 'e', value_name = "path")]
    #[clap(help = GENERAL_HELP.get("template_directory").unwrap().as_str())]
    #[serde(default)]
    pub template_directory: Option<NullableOption<String>>,

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
#[clap(next_help_heading = "Network (Override network options)")]
#[clap(next_display_order = None)]
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
    next_help_heading = "Experimental (Unstable and may change or be removed at any time, regardless of Shadow version)"
)]
#[clap(next_display_order = None)]
#[serde(default, deny_unknown_fields)]
pub struct ExperimentalOptions {
    /// Use the SCHED_FIFO scheduler. Requires CAP_SYS_NICE. See sched(7), capabilities(7)
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bool")]
    #[clap(help = EXP_HELP.get("use_sched_fifo").unwrap().as_str())]
    pub use_sched_fifo: Option<bool>,

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

    /// Use the MemoryManager. It can be useful to disable for debugging, but will hurt performance in
    /// most cases
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bool")]
    #[clap(help = EXP_HELP.get("use_memory_manager").unwrap().as_str())]
    pub use_memory_manager: Option<bool>,

    /// Pin each thread and any processes it executes to the same logical CPU Core to improve cache affinity
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bool")]
    #[clap(help = EXP_HELP.get("use_cpu_pinning").unwrap().as_str())]
    pub use_cpu_pinning: Option<bool>,

    /// If set, overrides the automatically calculated minimum time workers may run ahead when sending events between nodes
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "seconds")]
    #[clap(help = EXP_HELP.get("runahead").unwrap().as_str())]
    pub runahead: Option<NullableOption<units::Time<units::TimePrefix>>>,

    /// Update the minimum runahead dynamically throughout the simulation.
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bool")]
    #[clap(help = EXP_HELP.get("use_dynamic_runahead").unwrap().as_str())]
    pub use_dynamic_runahead: Option<bool>,

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

    /// The queueing discipline to use at the network interface
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "mode")]
    #[clap(help = EXP_HELP.get("interface_qdisc").unwrap().as_str())]
    pub interface_qdisc: Option<QDiscMode>,

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
    #[clap(value_parser = parse_set_log_info_flags)]
    #[clap(long, value_name = "options")]
    #[clap(help = EXP_HELP.get("host_heartbeat_log_info").unwrap().as_str())]
    pub host_heartbeat_log_info: Option<HashSet<LogInfoFlag>>,

    /// Amount of time between heartbeat messages for this host
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "seconds")]
    #[clap(help = EXP_HELP.get("host_heartbeat_interval").unwrap().as_str())]
    pub host_heartbeat_interval: Option<NullableOption<units::Time<units::TimePrefixUpper>>>,

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
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "seconds")]
    #[clap(help = EXP_HELP.get("max_unapplied_cpu_latency").unwrap().as_str())]
    pub max_unapplied_cpu_latency: Option<units::Time<units::TimePrefix>>,

    /// Simulated latency of an unblocked syscall. For efficiency Shadow only
    /// actually adds this latency if and when `max_unapplied_cpu_latency` is
    /// reached.
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "seconds")]
    #[clap(help = EXP_HELP.get("unblocked_syscall_latency").unwrap().as_str())]
    pub unblocked_syscall_latency: Option<units::Time<units::TimePrefix>>,

    /// Simulated latency of a vdso "syscall". For efficiency Shadow only
    /// actually adds this latency if and when `max_unapplied_cpu_latency` is
    /// reached.
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "seconds")]
    #[clap(help = EXP_HELP.get("unblocked_vdso_latency").unwrap().as_str())]
    pub unblocked_vdso_latency: Option<units::Time<units::TimePrefix>>,

    /// The host scheduler implementation, which decides how to assign hosts to threads and threads
    /// to CPU cores
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "name")]
    #[clap(help = EXP_HELP.get("scheduler").unwrap().as_str())]
    pub scheduler: Option<Scheduler>,
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
            use_syscall_counters: Some(true),
            use_object_counters: Some(true),
            use_preload_libc: Some(true),
            use_preload_openssl_rng: Some(true),
            use_preload_openssl_crypto: Some(false),
            max_unapplied_cpu_latency: Some(units::Time::new(1, units::TimePrefix::Micro)),
            // 1-2 microseconds is a ballpark estimate of the minimal latency for
            // context switching to the kernel and back on modern machines.
            // Default to the lower end to minimize effect in simualations without busy loops.
            unblocked_syscall_latency: Some(units::Time::new(1, units::TimePrefix::Micro)),
            // Actual latencies vary from ~40 to ~400 CPU cycles. https://stackoverflow.com/a/13096917
            // Default to the lower end to minimize effect in simualations without busy loops.
            unblocked_vdso_latency: Some(units::Time::new(10, units::TimePrefix::Nano)),
            use_memory_manager: Some(true),
            use_cpu_pinning: Some(true),
            runahead: Some(NullableOption::Value(units::Time::new(
                1,
                units::TimePrefix::Milli,
            ))),
            use_dynamic_runahead: Some(false),
            socket_send_buffer: Some(units::Bytes::new(131_072, units::SiPrefixUpper::Base)),
            socket_send_autotune: Some(true),
            socket_recv_buffer: Some(units::Bytes::new(174_760, units::SiPrefixUpper::Base)),
            socket_recv_autotune: Some(true),
            interface_qdisc: Some(QDiscMode::Fifo),
            use_legacy_working_dir: Some(false),
            host_heartbeat_log_level: Some(LogLevel::Info),
            host_heartbeat_log_info: Some(IntoIterator::into_iter([LogInfoFlag::Node]).collect()),
            host_heartbeat_interval: Some(NullableOption::Value(units::Time::new(
                1,
                units::TimePrefixUpper::Sec,
            ))),
            strace_logging_mode: Some(StraceLoggingMode::Off),
            scheduler: Some(Scheduler::ThreadPerCore),
        }
    }
}

/// Help messages used by Clap for command line arguments, combining the doc string with
/// the Serde default.
static HOST_HELP: Lazy<std::collections::HashMap<String, String>> =
    Lazy::new(|| generate_help_strs(schema_for!(HostDefaultOptions)));

#[derive(Debug, Clone, Parser, Serialize, Deserialize, Merge, JsonSchema)]
#[clap(next_help_heading = "Host Defaults (Default options for hosts)")]
#[clap(next_display_order = None)]
#[serde(default, deny_unknown_fields)]
pub struct HostDefaultOptions {
    /// Log level at which to print node messages
    #[clap(long = "host-log-level", name = "host-log-level")]
    #[clap(value_name = "level")]
    #[clap(help = HOST_HELP.get("log_level").unwrap().as_str())]
    pub log_level: Option<NullableOption<LogLevel>>,

    /// Should shadow generate pcap files?
    #[clap(long, value_name = "bool")]
    #[clap(help = HOST_HELP.get("pcap_enabled").unwrap().as_str())]
    pub pcap_enabled: Option<bool>,

    /// How much data to capture per packet (header and payload) if pcap logging is enabled
    #[clap(long, value_name = "bytes")]
    #[clap(help = HOST_HELP.get("pcap_capture_size").unwrap().as_str())]
    pub pcap_capture_size: Option<units::Bytes<units::SiPrefixUpper>>,
}

impl HostDefaultOptions {
    pub fn new_empty() -> Self {
        Self {
            log_level: None,
            pcap_enabled: None,
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
            pcap_enabled: Some(false),
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

#[derive(Debug, Copy, Clone, Serialize, Deserialize, JsonSchema)]
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

impl From<LogLevel> for log::Level {
    fn from(level: LogLevel) -> Self {
        match level {
            LogLevel::Error => log::Level::Error,
            LogLevel::Warning => log::Level::Warn,
            LogLevel::Info => log::Level::Info,
            LogLevel::Debug => log::Level::Debug,
            LogLevel::Trace => log::Level::Trace,
        }
    }
}

#[derive(Debug, Clone, PartialOrd, Ord, PartialEq, Eq, Serialize, JsonSchema)]
pub struct HostName(String);

impl<'de> serde::Deserialize<'de> for HostName {
    fn deserialize<D: serde::Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        struct HostNameVisitor;

        impl<'de> serde::de::Visitor<'de> for HostNameVisitor {
            type Value = HostName;

            fn expecting(&self, formatter: &mut std::fmt::Formatter) -> std::fmt::Result {
                formatter.write_str("a string")
            }

            fn visit_string<E>(self, v: String) -> Result<Self::Value, E>
            where
                E: serde::de::Error,
            {
                // hostname(7): "Valid characters for hostnames are ASCII(7) letters from a to z,
                // the digits from 0 to 9, and the hyphen (-)."
                fn is_allowed(c: char) -> bool {
                    c.is_ascii_lowercase() || c.is_ascii_digit() || c == '-' || c == '.'
                }
                if let Some(invalid_char) = v.chars().find(|x| !is_allowed(*x)) {
                    return Err(E::custom(format!(
                        "invalid hostname character: '{invalid_char}'"
                    )));
                }

                if v.is_empty() {
                    return Err(E::custom("empty hostname"));
                }

                // hostname(7): "A hostname may not start with a hyphen."
                if v.starts_with('-') {
                    return Err(E::custom("hostname begins with a '-' character"));
                }

                // hostname(7): "Each element of the hostname must be from 1 to 63 characters long
                // and the entire hostname, including the dots, can be at most 253 characters long."
                if v.len() > 253 {
                    return Err(E::custom("hostname exceeds 253 characters"));
                }

                Ok(HostName(v))
            }

            fn visit_str<E>(self, v: &str) -> Result<Self::Value, E>
            where
                E: serde::de::Error,
            {
                // serde::de::Visitor: "It is never correct to implement `visit_string` without
                // implementing `visit_str`. Implement neither, both, or just `visit_str`.'
                self.visit_string(v.to_string())
            }
        }

        deserializer.deserialize_string(HostNameVisitor)
    }
}

impl std::ops::Deref for HostName {
    type Target = String;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl From<HostName> for String {
    fn from(name: HostName) -> Self {
        name.0
    }
}

impl std::fmt::Display for HostName {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        self.0.fmt(f)
    }
}

#[derive(Debug, Copy, Clone, Serialize, Deserialize, JsonSchema)]
#[serde(rename_all = "kebab-case")]
pub enum Scheduler {
    ThreadPerHost,
    ThreadPerCore,
}

impl FromStr for Scheduler {
    type Err = serde_yaml::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        serde_yaml::from_str(s)
    }
}

fn default_data_directory() -> Option<String> {
    Some("shadow.data".into())
}

#[derive(Debug, Clone, Hash, PartialEq, Eq, Serialize, Deserialize, JsonSchema)]
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

#[derive(Debug, Clone, Copy, Hash, PartialEq, Eq, Serialize, Deserialize, JsonSchema)]
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

/// This wrapper type allows cli options to specify "null" to overwrite a config file option with
/// `None`, and is intended to be used for options where "null" is a valid option value.
///
/// **Warning**: This may result in unexpected behaviour when wrapping string types. For example, if
/// this is used for a file path option, the value "null" will conflict with the valid filename
/// "null". So if the user specifies "null" for this option, Shadow will assume it means "no value"
/// rather than the filename "null".
///
/// ### Motivation
///
/// For configuration options, there are generally three states:
/// - set
/// - not set
/// - null
///
/// For serde, all three states are configurable:
/// - set: `runahead: 5ms`
/// - not set: (no `runahead` option used in yaml)
/// - null: `runahead: null`
///
/// For clap, there are only two states:
/// - set: `--runahead 5ms`
/// - not set: (no `--runahead` option used in command)
///
/// There is no way to set a "null" state for cli options with clap.
///
/// ### Configuration in Shadow
///
/// Shadow first parses the config file and cli options separately before merging them.
///
/// Parsing for serde:
/// - set: `runahead: 5ms` => runahead is set to `Some(5ms)`
/// - not set: (no `runahead` option used in yaml) => runahead is set to its default (either
///   `Some(..)` or `None`)
/// - null: `runahead: null` => runahead is set to `None`
///
/// Parsing for clap:
/// - set: `--runahead 5ms` => runahead is set to `Some(5ms)`
/// - not set: (no `--runahead` option used in command) => runahead is set to `None`
///
/// Then the options are merged such that any `Some(..)` options from the cli options will overwrite
/// any `Some` or `None` options from the config file.
///
/// The issue is that no clap option can overwrite a config file option of `Some` with a value of
/// `None`. For example if the config file specifies `runahead: 5ms`, then with clap you can only
/// use `--runahead 2ms` to change the runahead to a `Some(2ms)` value, or you can not set
/// `--runahead` at all to leave it as a `Some(5ms)` value. But there is no cli option to change the
/// runahead to a `None` value.
///
/// This `NullableOption` type is a wrapper to allow you to specify "null" on the command line to
/// overwrite the config file value with `None`. From the example above, you could now specify
/// "--runahead null" to overwrite the config file value (for example `Some(5ms)`) with a `None`
/// value.
#[derive(Debug, Copy, Clone, JsonSchema, Eq, PartialEq)]
pub enum NullableOption<T> {
    Value(T),
    Null,
}

impl<T> NullableOption<T> {
    pub fn as_ref(&self) -> NullableOption<&T> {
        match self {
            NullableOption::Value(ref x) => NullableOption::Value(x),
            NullableOption::Null => NullableOption::Null,
        }
    }

    pub fn as_mut(&mut self) -> NullableOption<&mut T> {
        match self {
            NullableOption::Value(ref mut x) => NullableOption::Value(x),
            NullableOption::Null => NullableOption::Null,
        }
    }

    /// Easier to use than `Into<Option<T>>` since `Option` has a lot of blanket `From`
    /// implementations, requiring a lot of type annotations.
    pub fn to_option(self) -> Option<T> {
        match self {
            NullableOption::Value(x) => Some(x),
            NullableOption::Null => None,
        }
    }
}

impl<T: serde::Serialize> serde::Serialize for NullableOption<T> {
    fn serialize<S: serde::Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        match self {
            // use the inner type's serialize function
            Self::Value(x) => Ok(T::serialize(x, serializer)?),
            Self::Null => serializer.serialize_none(),
        }
    }
}

impl<'de, T: serde::Deserialize<'de>> serde::Deserialize<'de> for NullableOption<T> {
    fn deserialize<D: serde::Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        // always use the inner type's deserialize function
        Ok(Self::Value(T::deserialize(deserializer)?))
    }
}

impl<T> FromStr for NullableOption<T>
where
    <T as FromStr>::Err: std::fmt::Debug + std::fmt::Display,
    T: FromStr,
{
    type Err = T::Err;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            // since we use serde-yaml, use "null" to match yaml's "null"
            "null" => Ok(Self::Null),
            x => Ok(Self::Value(FromStr::from_str(x)?)),
        }
    }
}

/// A trait for `Option`-like types that can be flattened into a single `Option`.
pub trait Flatten<T> {
    fn flatten(self) -> Option<T>;
    fn flatten_ref(&self) -> Option<&T>;
}

impl<T> Flatten<T> for Option<NullableOption<T>> {
    fn flatten(self) -> Option<T> {
        self.and_then(|x| x.to_option())
    }

    fn flatten_ref(&self) -> Option<&T> {
        self.as_ref().and_then(|x| x.as_ref().to_option())
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

/// Helper function for serde default `Some(NullableOption::Value(1 sec))` values.
fn default_some_nullable_time_1() -> Option<NullableOption<units::Time<units::TimePrefixUpper>>> {
    let time = units::Time::new(1, units::TimePrefixUpper::Sec);
    Some(NullableOption::Value(time))
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
            let description = meta.description.unwrap_or_default();
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

/// Parses a string as a list of arguments following the shell's parsing rules. This
/// uses `g_shell_parse_argv()` for parsing.
pub fn parse_string_as_args(args_str: &OsStr) -> Result<Vec<OsString>, String> {
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
    // can't call foreign function: process_parseArgStr
    #[cfg_attr(miri, ignore)]
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
    // can't call foreign function: process_parseArgStr
    #[cfg_attr(miri, ignore)]
    fn test_parse_args_empty() {
        let arg_str = "";
        let expected_args: &[&str] = &[];

        let arg_str: OsString = arg_str.into();
        let args = parse_string_as_args(&arg_str).unwrap();

        assert_eq!(args, expected_args);
    }

    #[test]
    // can't call foreign function: process_parseArgStr
    #[cfg_attr(miri, ignore)]
    fn test_parse_args_error() {
        let arg_str = r#"hello "world"#;

        let arg_str: OsString = arg_str.into();
        let err_str = parse_string_as_args(&arg_str).unwrap_err();

        assert!(!err_str.is_empty());
    }

    #[test]
    // can't call foreign function: process_parseArgStr
    #[cfg_attr(miri, ignore)]
    fn test_nullable_option() {
        // format the yaml with an optional general option
        let yaml_fmt_fn = |option| {
            format!(
                r#"
                general:
                  stop_time: 1 min
                  {}
                network:
                  graph:
                    type: 1_gbit_switch
                hosts:
                  myhost:
                    network_node_id: 0
                    processes:
                    - path: /bin/true
                "#,
                option
            )
        };

        let time_1_sec = units::Time::new(1, units::TimePrefixUpper::Sec);
        let time_5_sec = units::Time::new(5, units::TimePrefixUpper::Sec);

        // "heartbeat_interval: null" with no cli option => None
        let yaml = yaml_fmt_fn("heartbeat_interval: null");
        let config_file: ConfigFileOptions = serde_yaml::from_str(&yaml).unwrap();
        let cli: CliOptions = CliOptions::try_parse_from(["shadow", "-"]).unwrap();

        let merged = ConfigOptions::new(config_file, cli);
        assert_eq!(merged.general.heartbeat_interval, None);

        // "heartbeat_interval: null" with "--heartbeat-interval 5s" => 5s
        let yaml = yaml_fmt_fn("heartbeat_interval: null");
        let config_file: ConfigFileOptions = serde_yaml::from_str(&yaml).unwrap();
        let cli: CliOptions =
            CliOptions::try_parse_from(["shadow", "--heartbeat-interval", "5s", "-"]).unwrap();

        let merged = ConfigOptions::new(config_file, cli);
        assert_eq!(
            merged.general.heartbeat_interval,
            Some(NullableOption::Value(time_5_sec))
        );

        // "heartbeat_interval: null" with "--heartbeat-interval null" => NullableOption::Null
        let yaml = yaml_fmt_fn("heartbeat_interval: null");
        let config_file: ConfigFileOptions = serde_yaml::from_str(&yaml).unwrap();
        let cli: CliOptions =
            CliOptions::try_parse_from(["shadow", "--heartbeat-interval", "null", "-"]).unwrap();

        let merged = ConfigOptions::new(config_file, cli);
        assert_eq!(
            merged.general.heartbeat_interval,
            Some(NullableOption::Null)
        );

        // "heartbeat_interval: 5s" with no cli option => 5s
        let yaml = yaml_fmt_fn("heartbeat_interval: 5s");
        let config_file: ConfigFileOptions = serde_yaml::from_str(&yaml).unwrap();
        let cli: CliOptions = CliOptions::try_parse_from(["shadow", "-"]).unwrap();

        let merged = ConfigOptions::new(config_file, cli);
        assert_eq!(
            merged.general.heartbeat_interval,
            Some(NullableOption::Value(time_5_sec))
        );

        // "heartbeat_interval: 5s" with "--heartbeat-interval 5s" => 5s
        let yaml = yaml_fmt_fn("heartbeat_interval: 5s");
        let config_file: ConfigFileOptions = serde_yaml::from_str(&yaml).unwrap();
        let cli: CliOptions =
            CliOptions::try_parse_from(["shadow", "--heartbeat-interval", "5s", "-"]).unwrap();

        let merged = ConfigOptions::new(config_file, cli);
        assert_eq!(
            merged.general.heartbeat_interval,
            Some(NullableOption::Value(time_5_sec))
        );

        // "heartbeat_interval: 5s" with "--heartbeat-interval null" => NullableOption::Null
        let yaml = yaml_fmt_fn("heartbeat_interval: 5s");
        let config_file: ConfigFileOptions = serde_yaml::from_str(&yaml).unwrap();
        let cli: CliOptions =
            CliOptions::try_parse_from(["shadow", "--heartbeat-interval", "null", "-"]).unwrap();

        let merged = ConfigOptions::new(config_file, cli);
        assert_eq!(
            merged.general.heartbeat_interval,
            Some(NullableOption::Null)
        );

        // no config option with no cli option => 1s (default)
        let yaml = yaml_fmt_fn("");
        let config_file: ConfigFileOptions = serde_yaml::from_str(&yaml).unwrap();
        let cli: CliOptions = CliOptions::try_parse_from(["shadow", "-"]).unwrap();

        let merged = ConfigOptions::new(config_file, cli);
        assert_eq!(
            merged.general.heartbeat_interval,
            Some(NullableOption::Value(time_1_sec))
        );

        // no config option with "--heartbeat-interval 5s" => 5s
        let yaml = yaml_fmt_fn("");
        let config_file: ConfigFileOptions = serde_yaml::from_str(&yaml).unwrap();
        let cli: CliOptions =
            CliOptions::try_parse_from(["shadow", "--heartbeat-interval", "5s", "-"]).unwrap();

        let merged = ConfigOptions::new(config_file, cli);
        assert_eq!(
            merged.general.heartbeat_interval,
            Some(NullableOption::Value(time_5_sec))
        );

        // no config option with "--heartbeat-interval null" => NullableOption::Null
        let yaml = yaml_fmt_fn("");
        let config_file: ConfigFileOptions = serde_yaml::from_str(&yaml).unwrap();
        let cli: CliOptions =
            CliOptions::try_parse_from(["shadow", "--heartbeat-interval", "null", "-"]).unwrap();

        let merged = ConfigOptions::new(config_file, cli);
        assert_eq!(
            merged.general.heartbeat_interval,
            Some(NullableOption::Null)
        );
    }
}

mod export {
    use super::*;

    #[no_mangle]
    pub extern "C" fn config_getUseSyscallCounters(config: *const ConfigOptions) -> bool {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        config.experimental.use_syscall_counters.unwrap()
    }

    #[no_mangle]
    pub extern "C" fn config_getUseMemoryManager(config: *const ConfigOptions) -> bool {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        config.experimental.use_memory_manager.unwrap()
    }
}
