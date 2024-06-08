//! Shadow's configuration and cli parsing code using [serde] and [clap]. This contains all of
//! Shadow's configuration options, some of which are also exposed as CLI options.
//!
//! Shadow uses [schemars] to get the option description (its doc comment) and default value so that
//! it can be shown in the CLI help text.
//!
//! This code should be careful about validating or interpreting values. It should be focused on
//! parsing and checking that the format is correct, and not validating the values. For example for
//! options that take paths, this code should not verify that the path actually exists or perform
//! any path canonicalization. That should be left to other code outside of this module. This is so
//! that the configuration parsing does not become environment-dependent. If a configuration file
//! parses on one system, it should parse successfully on other systems as well.

use std::collections::{BTreeMap, HashSet};
use std::ffi::{CStr, CString, OsStr, OsString};
use std::os::unix::ffi::OsStrExt;
use std::str::FromStr;

use clap::Parser;
use logger as c_log;
use merge::Merge;
use once_cell::sync::Lazy;
use schemars::{schema_for, JsonSchema};
use serde::{Deserialize, Serialize};
use shadow_shim_helper_rs::simulation_time::SimulationTime;

use crate::cshadow as c;
use crate::host::syscall::formatter::FmtOptions;
use crate::utility::units::{self, Unit};

const START_HELP_TEXT: &str = "\
    Run real applications over simulated networks.\n\n\
    For documentation, visit https://shadow.github.io/docs/guide";

const END_HELP_TEXT: &str = "\
    If units are not specified, all values are assumed to be given in their base \
    unit (seconds, bytes, bits, etc). Units can optionally be specified (for \
    example: '1024 B', '1024 bytes', '1 KiB', '1 kibibyte', etc) and are \
    case-sensitive.";

// clap requires a 'static str for the version
static VERSION: Lazy<String> = Lazy::new(crate::shadow::version);

#[derive(Debug, Clone, Parser)]
#[clap(name = "Shadow", about = START_HELP_TEXT, after_help = END_HELP_TEXT)]
#[clap(version = VERSION.as_str())]
#[clap(next_display_order = None)]
// clap only shows the possible values for bool options (unless we add support for the other
// non-bool options in the future), which isn't very helpful
#[clap(hide_possible_values = true)]
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
    pub host_option_defaults: HostDefaultOptions,

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
    pub host_option_defaults: HostDefaultOptions,

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
        // the `HostDefaultOptions::default` contains only `None` values, so we must first merge the
        // config file with the real defaults from `HostDefaultOptions::new_with_defaults`
        config_file.host_option_defaults = config_file
            .host_option_defaults
            .with_defaults(HostDefaultOptions::new_with_defaults());

        // override config options with command line options
        config_file.general = options.general.with_defaults(config_file.general);
        config_file.network = options.network.with_defaults(config_file.network);
        config_file.host_option_defaults = options
            .host_option_defaults
            .with_defaults(config_file.host_option_defaults);
        config_file.experimental = options.experimental.with_defaults(config_file.experimental);

        // copy the host defaults to all of the hosts
        for host in config_file.hosts.values_mut() {
            host.host_options = host
                .host_options
                .clone()
                .with_defaults(config_file.host_option_defaults.clone());
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
    pub stop_time: Option<units::Time<units::TimePrefix>>,

    /// Initialize randomness using seed N
    #[clap(long, value_name = "N")]
    #[clap(help = GENERAL_HELP.get("seed").unwrap().as_str())]
    #[serde(default = "default_some_1")]
    pub seed: Option<u32>,

    /// How many parallel threads to use to run the simulation. A value of 0 will allow Shadow to
    /// choose the number of threads.
    #[clap(long, short = 'p', value_name = "cores")]
    #[clap(help = GENERAL_HELP.get("parallelism").unwrap().as_str())]
    #[serde(default = "default_some_0")]
    pub parallelism: Option<u32>,

    /// The simulated time that ends Shadow's high network bandwidth/reliability bootstrap period
    #[clap(long, value_name = "seconds")]
    #[clap(help = GENERAL_HELP.get("bootstrap_end_time").unwrap().as_str())]
    #[serde(default = "default_some_time_0")]
    pub bootstrap_end_time: Option<units::Time<units::TimePrefix>>,

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
    pub heartbeat_interval: Option<NullableOption<units::Time<units::TimePrefix>>>,

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

    /// Use the MemoryManager in memory-mapping mode. This can improve
    /// performance, but disables support for dynamically spawning processes
    /// inside the simulation (e.g. the `fork` syscall).
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bool")]
    #[clap(help = EXP_HELP.get("use_memory_manager").unwrap().as_str())]
    pub use_memory_manager: Option<bool>,

    /// Pin each thread and any processes it executes to the same logical CPU Core to improve cache affinity
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bool")]
    #[clap(help = EXP_HELP.get("use_cpu_pinning").unwrap().as_str())]
    pub use_cpu_pinning: Option<bool>,

    /// Each worker thread will spin in a `sched_yield` loop while waiting for a new task. This is
    /// ignored if not using the thread-per-core scheduler.
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bool")]
    #[clap(help = EXP_HELP.get("use_worker_spinning").unwrap().as_str())]
    pub use_worker_spinning: Option<bool>,

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
    pub host_heartbeat_interval: Option<NullableOption<units::Time<units::TimePrefix>>>,

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

    /// When true, log error-level messages to stderr in addition to stdout when
    /// stdout is not a tty but stderr is.
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bool")]
    #[clap(help = EXP_HELP.get("log_errors_to_tty").unwrap().as_str())]
    pub log_errors_to_tty: Option<bool>,

    /// Use the rust TCP implementation
    #[clap(hide_short_help = true)]
    #[clap(long, value_name = "bool")]
    #[clap(help = EXP_HELP.get("use_new_tcp").unwrap().as_str())]
    pub use_new_tcp: Option<bool>,
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
            use_memory_manager: Some(false),
            use_cpu_pinning: Some(true),
            use_worker_spinning: Some(true),
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
            host_heartbeat_log_level: Some(LogLevel::Info),
            host_heartbeat_log_info: Some(IntoIterator::into_iter([LogInfoFlag::Node]).collect()),
            host_heartbeat_interval: Some(NullableOption::Value(units::Time::new(
                1,
                units::TimePrefix::Sec,
            ))),
            strace_logging_mode: Some(StraceLoggingMode::Off),
            scheduler: Some(Scheduler::ThreadPerCore),
            log_errors_to_tty: Some(true),
            use_new_tcp: Some(false),
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
// serde will default all fields to `None`, but in the cli help we want the actual defaults
#[schemars(default = "HostDefaultOptions::new_with_defaults")]
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
    pub fn new_with_defaults() -> Self {
        Self {
            log_level: None,
            pcap_enabled: Some(false),
            // From pcap(3): "A value of 65535 should be sufficient, on most if not all networks, to
            // capture all the data available from the packet". The maximum length of an IP packet
            // (including the header) is 65535 bytes.
            pcap_capture_size: Some(units::Bytes::new(65535, units::SiPrefixUpper::Base)),
        }
    }

    /// Replace unset (`None`) values of `base` with values from `default`.
    pub fn with_defaults(mut self, default: Self) -> Self {
        self.merge(default);
        self
    }
}

#[allow(clippy::derivable_impls)]
impl Default for HostDefaultOptions {
    fn default() -> Self {
        // Our config fields would typically be initialized with their real defaults here in the
        // `Default::default` implementation, but we need to handle the host options differently
        // because the global `host_option_defaults` can be overridden by host-specific
        // `host_options`. So instead we use defaults of `None` here and set the real defaults with
        // `Self::new_with_defaults` in `ConfigOptions::new`.
        Self {
            log_level: None,
            pcap_enabled: None,
            pcap_capture_size: None,
        }
    }
}

#[derive(Serialize, Deserialize, Eq, PartialEq, Debug, Copy, Clone, JsonSchema)]
#[serde(rename_all = "kebab-case")]
pub enum RunningVal {
    Running,
}

/// The enum variants here have an extra level of indirection to get the
/// serde serialization that we want.
#[derive(Debug, Copy, Clone, Eq, PartialEq, Serialize, Deserialize, JsonSchema)]
#[serde(untagged)]
pub enum ProcessFinalState {
    Exited { exited: i32 },
    Signaled { signaled: Signal },
    Running(RunningVal),
}

impl Default for ProcessFinalState {
    fn default() -> Self {
        Self::Exited { exited: 0 }
    }
}

impl std::fmt::Display for ProcessFinalState {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        // We use the yaml serialization here so that when reporting that an
        // expected state didn't match the actual state, it's clear how to set
        // the expected state in the config file to match the actual state if
        // desired.
        //
        // The current enum works OK for this since there are no internal
        // newlines in the serialization; if there are some later we might wand
        // to serialize to json instead, which can always be put on a single
        // line and should also be valid yaml.
        let s = serde_yaml::to_string(self).or(Err(std::fmt::Error))?;
        write!(f, "{}", s.trim())
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct ProcessOptions {
    pub path: std::path::PathBuf,

    /// Process arguments
    #[serde(default = "default_args_empty")]
    pub args: ProcessArgs,

    /// Environment variables passed when executing this process
    #[serde(default)]
    pub environment: BTreeMap<EnvName, String>,

    /// The simulated time at which to execute the process
    #[serde(default)]
    pub start_time: units::Time<units::TimePrefix>,

    /// The simulated time at which to send a `shutdown_signal` signal to the process
    #[serde(default)]
    pub shutdown_time: Option<units::Time<units::TimePrefix>>,

    /// The signal that will be sent to the process at `shutdown_time`
    #[serde(default = "default_sigterm")]
    pub shutdown_signal: Signal,

    /// The expected final state of the process. Shadow will report an error
    /// if the actual state doesn't match.
    #[serde(default)]
    pub expected_final_state: ProcessFinalState,
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

    /// Downstream bandwidth capacity of the host
    #[serde(default)]
    pub bandwidth_down: Option<units::BitsPerSec<units::SiPrefixUpper>>,

    /// Upstream bandwidth capacity of the host
    #[serde(default)]
    pub bandwidth_up: Option<units::BitsPerSec<units::SiPrefixUpper>>,

    #[serde(default)]
    pub host_options: HostDefaultOptions,
}

#[derive(Debug, Copy, Clone, Serialize, Deserialize, JsonSchema)]
#[serde(rename_all = "kebab-case")]
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

#[derive(Debug, Clone, PartialOrd, Ord, PartialEq, Eq, Serialize, JsonSchema)]
pub struct EnvName(String);

impl EnvName {
    pub fn new(name: impl Into<String>) -> Option<Self> {
        let name = name.into();

        // an environment variable name cannot contain a '=' character
        if name.contains('=') {
            return None;
        }

        Some(Self(name))
    }
}

impl<'de> serde::Deserialize<'de> for EnvName {
    fn deserialize<D: serde::Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        struct EnvNameVisitor;

        impl<'de> serde::de::Visitor<'de> for EnvNameVisitor {
            type Value = EnvName;

            fn expecting(&self, formatter: &mut std::fmt::Formatter) -> std::fmt::Result {
                formatter.write_str("a string")
            }

            fn visit_string<E>(self, v: String) -> Result<Self::Value, E>
            where
                E: serde::de::Error,
            {
                let Some(name) = EnvName::new(v) else {
                    let e = "environment variable name contains a '=' character";
                    return Err(E::custom(e));
                };

                Ok(name)
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

        deserializer.deserialize_string(EnvNameVisitor)
    }
}

impl std::ops::Deref for EnvName {
    type Target = String;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl From<EnvName> for String {
    fn from(name: EnvName) -> Self {
        name.0
    }
}

impl std::fmt::Display for EnvName {
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
#[serde(rename_all = "kebab-case")]
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
#[serde(rename_all = "kebab-case")]
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
#[serde(rename_all = "kebab-case")]
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
#[serde(rename_all = "kebab-case")]
pub enum GraphSource {
    File(FileSource),
    Inline(String),
}

#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
// we use "kebab-case" for other shadow options, but are leaving this as "snake_case" for backwards
// compatibility
#[serde(tag = "type", rename_all = "snake_case")]
pub enum GraphOptions {
    Gml(GraphSource),
    #[serde(rename = "1_gbit_switch")]
    OneGbitSwitch,
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

// TODO: use linux_api's Signal internally, which we control and which supports
// realtime signals. We need to implement conversion to and from strings to do
// so, while being careful that the conversion is compatible with nix's so as
// not to be a breaking change to our configuration format.
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct Signal(nix::sys::signal::Signal);

impl From<nix::sys::signal::Signal> for Signal {
    fn from(value: nix::sys::signal::Signal) -> Self {
        Self(value)
    }
}

impl TryFrom<linux_api::signal::Signal> for Signal {
    type Error = <nix::sys::signal::Signal as TryFrom<i32>>::Error;
    fn try_from(value: linux_api::signal::Signal) -> Result<Self, Self::Error> {
        let signal = nix::sys::signal::Signal::try_from(value.as_i32())?;
        Ok(Self(signal))
    }
}

impl serde::Serialize for Signal {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        serializer.serialize_str(self.0.as_str())
    }
}

impl<'de> serde::Deserialize<'de> for Signal {
    fn deserialize<D: serde::Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        struct SignalVisitor;

        impl<'de> serde::de::Visitor<'de> for SignalVisitor {
            type Value = Signal;

            fn expecting(&self, formatter: &mut std::fmt::Formatter) -> std::fmt::Result {
                formatter.write_str("a signal string (e.g. \"SIGINT\") or integer")
            }

            fn visit_str<E>(self, v: &str) -> Result<Self::Value, E>
            where
                E: serde::de::Error,
            {
                nix::sys::signal::Signal::from_str(v)
                    .map(Signal)
                    .map_err(|_e| E::custom(format!("Invalid signal string: {v}")))
            }

            fn visit_i64<E>(self, v: i64) -> Result<Self::Value, E>
            where
                E: serde::de::Error,
            {
                let v = i32::try_from(v)
                    .map_err(|_e| E::custom(format!("Invalid signal number: {v}")))?;
                nix::sys::signal::Signal::try_from(v)
                    .map(Signal)
                    .map_err(|_e| E::custom(format!("Invalid signal number: {v}")))
            }

            fn visit_u64<E>(self, v: u64) -> Result<Self::Value, E>
            where
                E: serde::de::Error,
            {
                let v = i64::try_from(v)
                    .map_err(|_e| E::custom(format!("Invalid signal number: {v}")))?;
                self.visit_i64(v)
            }
        }

        deserializer.deserialize_any(SignalVisitor)
    }
}

impl std::fmt::Display for Signal {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.0)
    }
}

impl JsonSchema for Signal {
    fn schema_name() -> String {
        String::from("Signal")
    }

    fn json_schema(_gen: &mut schemars::gen::SchemaGenerator) -> schemars::schema::Schema {
        // Use the "anything" schema. The Deserialize implementation does the
        // actual parsing and error handling.
        // TODO: Ideally we'd only accept strings or integers here. The
        // documentation isn't very clear about how to construct such a schema
        // though, and we currently only use the schemas for command-line-option
        // help strings. Since we don't currently take Signals in
        // command-line-options, it doesn't matter.
        schemars::schema::Schema::Bool(true)
    }
}

impl std::ops::Deref for Signal {
    type Target = nix::sys::signal::Signal;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
#[serde(rename_all = "kebab-case")]
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
    T: FromStr<Err: std::fmt::Debug + std::fmt::Display>,
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

/// Helper function for serde default `Signal(Signal::SIGTERM)` values.
fn default_sigterm() -> Signal {
    Signal(nix::sys::signal::Signal::SIGTERM)
}

/// Helper function for serde default `Some(0)` values.
fn default_some_time_0() -> Option<units::Time<units::TimePrefix>> {
    Some(units::Time::new(0, units::TimePrefix::Sec))
}

/// Helper function for serde default `Some(true)` values.
fn default_some_true() -> Option<bool> {
    Some(true)
}

/// Helper function for serde default `Some(false)` values.
fn default_some_false() -> Option<bool> {
    Some(false)
}

/// Helper function for serde default `Some(0)` values.
fn default_some_0() -> Option<u32> {
    Some(0)
}

/// Helper function for serde default `Some(1)` values.
fn default_some_1() -> Option<u32> {
    Some(1)
}

/// Helper function for serde default `Some(NullableOption::Value(1 sec))` values.
fn default_some_nullable_time_1() -> Option<NullableOption<units::Time<units::TimePrefix>>> {
    let time = units::Time::new(1, units::TimePrefix::Sec);
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

        let time_1_sec = units::Time::new(1, units::TimePrefix::Sec);
        let time_5_sec = units::Time::new(5, units::TimePrefix::Sec);

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
