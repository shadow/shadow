use std::collections::BTreeMap;
use std::ffi::{CStr, CString, OsStr, OsString};
use std::os::unix::ffi::OsStrExt;

use clap::ArgEnum;
use clap::Clap;
use merge::Merge;
use once_cell::sync::Lazy;
use schemars::{schema_for, JsonSchema};
use serde::{Deserialize, Serialize};
use std::num::NonZeroU32;

use super::simulation_time::{SIMTIME_ONE_NANOSECOND, SIMTIME_ONE_SECOND};
use super::units::{self, Unit};
use crate::cshadow as c;
use log_bindings as c_log;

const END_HELP_TEXT: &str = "\
    If units are not specified, all values are assumed to be given in their base \
    unit (seconds, bytes, bits, etc). Units can optionally be specified (for \
    example: '1024 B', '1024 bytes', '1 KiB', '1 kibibyte', etc) and are \
    case-sensitive.";

/// Run real applications over simulated networks.
#[derive(Debug, Clone, Clap)]
#[clap(name = "Shadow", version = std::env!("CARGO_PKG_VERSION"), after_help = END_HELP_TEXT)]
pub struct CliOptions {
    /// Path to the Shadow configuration file. Use '-' to read from stdin
    #[clap(required_unless_present_any(&["show-build-info", "shm-cleanup"]))]
    config: Option<String>,

    /// Pause to allow gdb to attach
    #[clap(long, short = 'g')]
    gdb: bool,

    /// Exit after running shared memory cleanup routine
    #[clap(long, exclusive(true))]
    shm_cleanup: bool,

    /// Exit after printing build information
    #[clap(long, exclusive(true))]
    show_build_info: bool,

    /// Exit after printing the final configuration
    #[clap(long)]
    show_config: bool,

    #[clap(flatten)]
    general: GeneralOptions,

    #[clap(flatten)]
    network: NetworkOptions,

    #[clap(flatten)]
    host_defaults: HostDefaultOptions,

    #[clap(flatten)]
    experimental: ExperimentalOptions,
}

/// Options contained in a configuration file.
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct ConfigFileOptions {
    general: GeneralOptions,

    network: NetworkOptions,

    #[serde(default)]
    host_defaults: HostDefaultOptions,

    #[serde(default)]
    experimental: ExperimentalOptions,

    // we use a BTreeMap so that the hosts are sorted by their hostname (useful for determinism)
    hosts: BTreeMap<String, HostOptions>,
}

/// Shadow configuration options after processing command-line and configuration file options.
#[derive(Debug, Clone)]
pub struct ConfigOptions {
    general: GeneralOptions,

    network: NetworkOptions,

    experimental: ExperimentalOptions,

    // we use a BTreeMap so that the hosts are sorted by their hostname (useful for determinism)
    hosts: BTreeMap<String, HostOptions>,
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
        for (_, host) in &mut config_file.hosts {
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
#[derive(Debug, Clone, Clap, Serialize, Deserialize, Merge, JsonSchema)]
#[clap(help_heading = "GENERAL (Override configuration file options)")]
#[serde(deny_unknown_fields)]
pub struct GeneralOptions {
    /// The simulated time at which simulated processes are sent a SIGKILL signal
    #[clap(long, value_name = "seconds")]
    #[clap(about = GENERAL_HELP.get("stop_time").unwrap())]
    stop_time: Option<units::Time<units::TimePrefixUpper>>,

    /// Initialize randomness using seed N
    #[clap(long, value_name = "N")]
    #[clap(about = GENERAL_HELP.get("seed").unwrap())]
    #[serde(default = "default_some_1")]
    seed: Option<u32>,

    /// How many parallel threads to use to run the simulation. Optimal
    /// performance is usually obtained with `cores`, or sometimes `cores/2`
    /// with hyperthreading.
    #[clap(long, short = 'p', value_name = "cores")]
    #[clap(about = GENERAL_HELP.get("parallelism").unwrap())]
    #[serde(default = "default_some_nz_1")]
    parallelism: Option<NonZeroU32>,

    /// The simulated time that ends Shadow's high network bandwidth/reliability bootstrap period
    #[clap(long, value_name = "seconds")]
    #[clap(about = GENERAL_HELP.get("bootstrap_end_time").unwrap())]
    #[serde(default = "default_some_time_0")]
    bootstrap_end_time: Option<units::Time<units::TimePrefixUpper>>,

    /// Log level of output written on stdout. If Shadow was built in release mode, then log
    /// messages at level 'trace' will always be dropped
    #[clap(long, short = 'l', value_name = "level")]
    #[clap(about = GENERAL_HELP.get("log_level").unwrap())]
    #[serde(default = "default_some_info")]
    log_level: Option<LogLevel>,

    /// Interval at which to print heartbeat messages
    #[clap(long, value_name = "seconds")]
    #[clap(about = GENERAL_HELP.get("heartbeat_interval").unwrap())]
    #[serde(default = "default_some_time_1")]
    heartbeat_interval: Option<units::Time<units::TimePrefixUpper>>,

    /// Path to store simulation output
    #[clap(long, short = 'd', value_name = "path")]
    #[clap(about = GENERAL_HELP.get("data_directory").unwrap())]
    #[serde(default = "default_data_directory")]
    data_directory: Option<String>,

    /// Path to recursively copy during startup and use as the data-directory
    #[clap(long, short = 'e', value_name = "path")]
    #[clap(about = GENERAL_HELP.get("template_directory").unwrap())]
    #[serde(default)]
    template_directory: Option<String>,
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
#[derive(Debug, Clone, Clap, Serialize, Deserialize, Merge, JsonSchema)]
#[clap(help_heading = "NETWORK (Override network options)")]
#[serde(deny_unknown_fields)]
struct NetworkOptions {
    /// The network topology graph
    #[clap(skip)]
    graph: Option<GraphOptions>,

    /// When routing packets, follow the shortest path rather than following a direct
    /// edge between nodes. If false, the network graph is required to be complete.
    #[serde(default = "default_some_true")]
    #[clap(long, value_name = "bool")]
    #[clap(about = NETWORK_HELP.get("use_shortest_path").unwrap())]
    use_shortest_path: Option<bool>,
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

#[derive(Debug, Clone, Clap, Serialize, Deserialize, Merge, JsonSchema)]
#[clap(
    help_heading = "EXPERIMENTAL (Unstable and may change or be removed at any time, regardless of Shadow version)"
)]
#[serde(default, deny_unknown_fields)]
pub struct ExperimentalOptions {
    /// Use the SCHED_FIFO scheduler. Requires CAP_SYS_NICE. See sched(7), capabilities(7)
    #[clap(long, value_name = "bool")]
    #[clap(about = EXP_HELP.get("use_sched_fifo").unwrap())]
    use_sched_fifo: Option<bool>,

    /// Use performance workarounds for waitpid being O(n). Beneficial to disable if waitpid
    /// is patched to be O(1), if using one logical processor per host, or in some cases where
    /// it'd otherwise result in excessive detaching and reattaching
    #[clap(long, value_name = "bool")]
    #[clap(about = EXP_HELP.get("use_o_n_waitpid_workarounds").unwrap())]
    use_o_n_waitpid_workarounds: Option<bool>,

    /// Send message to plugin telling it to stop spinning when a syscall blocks
    #[clap(long, value_name = "bool")]
    #[clap(about = EXP_HELP.get("use_explicit_block_message").unwrap())]
    use_explicit_block_message: Option<bool>,

    /// Use seccomp to trap syscalls. Default is true for preload mode, false otherwise.
    #[clap(long, value_name = "bool")]
    #[clap(about = EXP_HELP.get("use_seccomp").unwrap())]
    use_seccomp: Option<bool>,

    /// Count the number of occurrences for individual syscalls
    #[clap(long, value_name = "bool")]
    #[clap(about = EXP_HELP.get("use_syscall_counters").unwrap())]
    use_syscall_counters: Option<bool>,

    /// Count object allocations and deallocations. If disabled, we will not be able to detect object memory leaks
    #[clap(long, value_name = "bool")]
    #[clap(about = EXP_HELP.get("use_object_counters").unwrap())]
    use_object_counters: Option<bool>,

    /// Preload our OpenSSL RNG library for all managed processes to mitigate non-deterministic use of OpenSSL.
    #[clap(long, value_name = "bool")]
    #[clap(about = EXP_HELP.get("use_openssl_rng_preload").unwrap())]
    use_openssl_rng_preload: Option<bool>,

    /// Max number of iterations to busy-wait on IPC semaphore before blocking
    #[clap(long, value_name = "iterations")]
    #[clap(about = EXP_HELP.get("preload_spin_max").unwrap())]
    preload_spin_max: Option<i32>,

    /// Use the MemoryManager. It can be useful to disable for debugging, but will hurt performance in
    /// most cases
    #[clap(long, value_name = "bool")]
    #[clap(about = EXP_HELP.get("use_memory_manager").unwrap())]
    use_memory_manager: Option<bool>,

    /// Use shim-side syscall handler to force hot-path syscalls to be handled via an inter-process syscall with Shadow
    #[clap(long, value_name = "bool")]
    #[clap(about = EXP_HELP.get("use_shim_syscall_handler").unwrap())]
    use_shim_syscall_handler: Option<bool>,

    /// Pin each thread and any processes it executes to the same logical CPU Core to improve cache affinity
    #[clap(long, value_name = "bool")]
    #[clap(about = EXP_HELP.get("use_cpu_pinning").unwrap())]
    use_cpu_pinning: Option<bool>,

    /// Which interposition method to use
    #[clap(long, value_name = "method")]
    #[clap(about = EXP_HELP.get("interpose_method").unwrap())]
    interpose_method: Option<InterposeMethod>,

    /// If set, overrides the automatically calculated minimum time workers may run ahead when sending events between nodes
    #[clap(long, value_name = "seconds")]
    #[clap(about = EXP_HELP.get("runahead").unwrap())]
    runahead: Option<units::Time<units::TimePrefix>>,

    /// The event scheduler's policy for thread synchronization
    #[clap(long, value_name = "policy")]
    #[clap(about = EXP_HELP.get("scheduler_policy").unwrap())]
    scheduler_policy: Option<SchedulerPolicy>,

    /// Initial size of the socket's send buffer
    #[clap(long, value_name = "bytes")]
    #[clap(about = EXP_HELP.get("socket_send_buffer").unwrap())]
    socket_send_buffer: Option<units::Bytes<units::SiPrefixUpper>>,

    /// Enable send window autotuning
    #[clap(long, value_name = "bool")]
    #[clap(about = EXP_HELP.get("socket_send_autotune").unwrap())]
    socket_send_autotune: Option<bool>,

    /// Initial size of the socket's receive buffer
    #[clap(long, value_name = "bytes")]
    #[clap(about = EXP_HELP.get("socket_recv_buffer").unwrap())]
    socket_recv_buffer: Option<units::Bytes<units::SiPrefixUpper>>,

    /// Enable receive window autotuning
    #[clap(long, value_name = "bool")]
    #[clap(about = EXP_HELP.get("socket_recv_autotune").unwrap())]
    socket_recv_autotune: Option<bool>,

    /// Size of the interface receive buffer that accepts incoming packets
    #[clap(long, value_name = "bytes")]
    #[clap(about = EXP_HELP.get("interface_buffer").unwrap())]
    interface_buffer: Option<units::Bytes<units::SiPrefixUpper>>,

    /// The queueing discipline to use at the network interface
    #[clap(long, value_name = "mode")]
    #[clap(about = EXP_HELP.get("interface_qdisc").unwrap())]
    interface_qdisc: Option<QDiscMode>,

    /// Create N worker threads. Note though, that `--parallelism` of them will
    /// be allowed to run simultaneously. If unset, will create a thread for
    /// each simulated Host. This is to work around limitations in ptrace, and
    /// may change in the future.
    #[clap(long, value_name = "N")]
    #[clap(about = EXP_HELP.get("worker_threads").unwrap())]
    worker_threads: Option<NonZeroU32>,

    /// Don't adjust the working directories of the plugins
    #[clap(long, value_name = "bool")]
    #[clap(about = EXP_HELP.get("use_legacy_working_dir").unwrap())]
    use_legacy_working_dir: Option<bool>,
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
            use_syscall_counters: Some(false),
            use_object_counters: Some(true),
            use_openssl_rng_preload: Some(true),
            preload_spin_max: Some(0),
            use_memory_manager: Some(true),
            use_shim_syscall_handler: Some(true),
            use_cpu_pinning: Some(true),
            interpose_method: Some(InterposeMethod::Ptrace),
            runahead: None,
            scheduler_policy: Some(SchedulerPolicy::Host),
            socket_send_buffer: Some(units::Bytes::new(131_072, units::SiPrefixUpper::Base)),
            socket_send_autotune: Some(true),
            socket_recv_buffer: Some(units::Bytes::new(174_760, units::SiPrefixUpper::Base)),
            socket_recv_autotune: Some(true),
            interface_buffer: Some(units::Bytes::new(1_024_000, units::SiPrefixUpper::Base)),
            interface_qdisc: Some(QDiscMode::Fifo),
            worker_threads: None,
            use_legacy_working_dir: Some(false),
        }
    }
}

/// Help messages used by Clap for command line arguments, combining the doc string with
/// the Serde default.
static HOST_HELP: Lazy<std::collections::HashMap<String, String>> =
    Lazy::new(|| generate_help_strs(schema_for!(HostDefaultOptions)));

#[derive(Debug, Clone, Clap, Serialize, Deserialize, Merge, JsonSchema)]
#[clap(help_heading = "HOST DEFAULTS (Default options for hosts)")]
#[serde(default, deny_unknown_fields)]
pub struct HostDefaultOptions {
    /// Log level at which to print node messages
    #[clap(long = "host-log-level", name = "host-log-level")]
    #[clap(value_name = "level")]
    #[clap(about = HOST_HELP.get("log_level").unwrap())]
    log_level: Option<LogLevel>,

    /// Log level at which to print host statistics
    #[clap(long = "host-heartbeat-log-level", name = "host-heartbeat-log-level")]
    #[clap(value_name = "level")]
    #[clap(about = HOST_HELP.get("heartbeat_log_level").unwrap())]
    heartbeat_log_level: Option<LogLevel>,

    /// List of information to show in the host's heartbeat message
    #[clap(long = "host-heartbeat-log-info", name = "host-heartbeat-log-info")]
    #[clap(parse(try_from_str = parse_set_log_info_flags))]
    #[clap(value_name = "options")]
    #[clap(about = HOST_HELP.get("heartbeat_log_info").unwrap())]
    heartbeat_log_info: Option<std::collections::HashSet<LogInfoFlag>>,

    /// Amount of time between heartbeat messages for this host
    #[clap(long = "host-heartbeat-interval", name = "host-heartbeat-interval")]
    #[clap(value_name = "seconds")]
    #[clap(about = HOST_HELP.get("heartbeat_interval").unwrap())]
    heartbeat_interval: Option<units::Time<units::TimePrefixUpper>>,

    /// Where to save the pcap files (relative to the host directory)
    #[clap(long, value_name = "path")]
    #[clap(about = HOST_HELP.get("pcap_directory").unwrap())]
    pcap_directory: Option<String>,

    /// IPv4 address hint for Shadow's name and routing system (ex: "100.0.0.1")
    #[clap(long, value_name = "ip")]
    #[clap(about = HOST_HELP.get("ip_address_hint").unwrap())]
    ip_address_hint: Option<String>,

    /// Country code hint for Shadow's name and routing system (ex: "US")
    #[clap(long, value_name = "country")]
    #[clap(about = HOST_HELP.get("country_code_hint").unwrap())]
    country_code_hint: Option<String>,

    /// City code hint for Shadow's name and routing system
    #[clap(long, value_name = "city")]
    #[clap(about = HOST_HELP.get("city_code_hint").unwrap())]
    city_code_hint: Option<String>,
}

impl HostDefaultOptions {
    pub fn new_empty() -> Self {
        Self {
            log_level: None,
            heartbeat_log_level: None,
            heartbeat_log_info: None,
            heartbeat_interval: None,
            pcap_directory: None,
            ip_address_hint: None,
            country_code_hint: None,
            city_code_hint: None,
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
            heartbeat_log_level: Some(LogLevel::Info),
            heartbeat_log_info: Some(std::array::IntoIter::new([LogInfoFlag::Node]).collect()),
            heartbeat_interval: Some(units::Time::new(1, units::TimePrefixUpper::Sec)),
            pcap_directory: None,
            ip_address_hint: None,
            country_code_hint: None,
            city_code_hint: None,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct ProcessOptions {
    path: std::path::PathBuf,

    /// Process arguments
    #[serde(default = "default_args_empty")]
    args: ProcessArgs,

    /// Environment variables passed when executing this process. Multiple variables can be
    /// specified by using a semicolon separator (ex: `ENV_A=1;ENV_B=2`)
    #[serde(default)]
    environment: String,

    /// The number of replicas of this process to execute
    #[serde(default)]
    quantity: Quantity,

    /// The simulated time at which to execute the process
    #[serde(default)]
    start_time: units::Time<units::TimePrefixUpper>,

    /// The simulated time at which to send a SIGKILL signal to the process
    #[serde(default)]
    stop_time: Option<units::Time<units::TimePrefixUpper>>,
}

#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct HostOptions {
    processes: Vec<ProcessOptions>,

    /// Number of hosts to start
    #[serde(default)]
    quantity: Quantity,

    /// Downstream bandwidth capacity of the host
    #[serde(default)]
    bandwidth_down: Option<units::BitsPerSec<units::SiPrefixUpper>>,

    /// Upstream bandwidth capacity of the host
    #[serde(default)]
    bandwidth_up: Option<units::BitsPerSec<units::SiPrefixUpper>>,

    #[serde(default = "HostDefaultOptions::new_empty")]
    options: HostDefaultOptions,
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

impl std::str::FromStr for LogLevel {
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

impl std::str::FromStr for InterposeMethod {
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

impl std::str::FromStr for SchedulerPolicy {
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
enum LogInfoFlag {
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

impl std::str::FromStr for LogInfoFlag {
    type Err = serde_yaml::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        serde_yaml::from_str(s)
    }
}

/// Parse a string as a set of `LogInfoFlag` values.
fn parse_set_log_info_flags(
    s: &str,
) -> Result<std::collections::HashSet<LogInfoFlag>, serde_yaml::Error> {
    let flags: Result<std::collections::HashSet<LogInfoFlag>, _> =
        s.split(",").map(|x| x.trim().parse()).collect();
    Ok(flags?)
}

#[derive(Debug, Clone, Copy, Hash, PartialEq, Eq, ArgEnum, Serialize, Deserialize, JsonSchema)]
#[serde(rename_all = "lowercase")]
#[repr(C)]
pub enum QDiscMode {
    Fifo,
    RoundRobin,
}

impl std::str::FromStr for QDiscMode {
    type Err = serde_yaml::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        serde_yaml::from_str(s)
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
#[serde(rename_all = "lowercase")]
enum CustomGraph {
    Path(String),
    Inline(String),
}

#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
#[serde(tag = "type", rename_all = "lowercase")]
enum GraphOptions {
    Gml(CustomGraph),
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

#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
#[serde(untagged)]
pub enum ProcessArgs {
    List(Vec<String>),
    Str(String),
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

/// Helper function for serde default `Some(1)` values.
fn default_some_1() -> Option<u32> {
    Some(1)
}

/// Helper function for serde default `Some(1)` values.
fn default_some_nz_1() -> Option<NonZeroU32> {
    Some(std::num::NonZeroU32::new(1).unwrap())
}

/// Helper function for serde default `Some(0)` values.
fn default_some_time_1() -> Option<units::Time<units::TimePrefixUpper>> {
    Some(units::Time::new(1, units::TimePrefixUpper::Sec))
}

/// Helper function for serde default `Some(LogLevel::Info)` values.
fn default_some_info() -> Option<LogLevel> {
    Some(LogLevel::Info)
}

// when updating this graph, make sure to also update the copy in docs/shadow_config_spec.md
const ONE_GBIT_SWITCH_GRAPH: &str = r#"graph [
  directed 0
  node [
    id 0
    ip_address "0.0.0.0"
    bandwidth_up "1 Gbit"
    bandwidth_down "1 Gbit"
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
            let space = if description.len() > 0 { " " } else { "" };
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

fn tilde_expansion(path: &str) -> std::path::PathBuf {
    // if the path begins with a "~"
    if let Some(x) = path.strip_prefix("~") {
        // get the tilde-prefix (everything before the first separator)
        let mut parts = x.splitn(2, "/");
        let (tilde_prefix, remainder) = (parts.next().unwrap(), parts.next().unwrap_or(""));
        assert!(parts.next().is_none());
        // we only support expansion for our own home directory
        // (nothing between the "~" and the separator)
        if tilde_prefix.is_empty() {
            if let Ok(ref home) = std::env::var("HOME") {
                return [&home, remainder].iter().collect::<std::path::PathBuf>();
            }
        }
    }

    // if we don't have a tilde-prefix that we support, just return the unmodified path
    std::path::PathBuf::from(path)
}

/// Parses a string as a list of arguments following the shell's parsing rules. This
/// uses `g_shell_parse_argv()` for parsing.
fn parse_string_as_args(args_str: &OsStr) -> Result<Vec<OsString>, String> {
    if args_str.len() == 0 {
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

        assert!(err_str.len() != 0);
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
    pub extern "C" fn clioptions_parse(
        argc: libc::c_int,
        argv: *const *const libc::c_char,
    ) -> *mut CliOptions {
        assert!(!argv.is_null());

        let args = (0..argc).map(|x| unsafe { CStr::from_ptr(*argv.add(x as usize)) });
        let args = args.map(|x| OsStr::from_bytes(x.to_bytes()));

        let opts = CliOptions::try_parse_from(args);
        let opts = match opts {
            Ok(x) => x,
            Err(e) => {
                if e.use_stderr() {
                    eprintln!("{}", e);
                } else {
                    println!("{}", e);
                }
                return std::ptr::null_mut();
            }
        };

        Box::into_raw(Box::new(opts))
    }

    #[no_mangle]
    pub extern "C" fn clioptions_free(options: *mut CliOptions) {
        assert!(!options.is_null());
        unsafe { Box::from_raw(options) };
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
    pub extern "C" fn configfile_parse(filename: *const libc::c_char) -> *mut ConfigFileOptions {
        assert!(!filename.is_null());
        let filename = OsStr::from_bytes(unsafe { CStr::from_ptr(filename).to_bytes() });

        let file = match std::fs::File::open(filename) {
            Ok(x) => x,
            Err(e) => {
                eprintln!("Could not open config file {:?}: {}", filename, e);
                return std::ptr::null_mut();
            }
        };

        let config: ConfigFileOptions = match serde_yaml::from_reader(file) {
            Ok(x) => x,
            Err(e) => {
                eprintln!("Could not parse yaml: {}", e);
                return std::ptr::null_mut();
            }
        };

        Box::into_raw(Box::new(config))
    }

    #[no_mangle]
    pub extern "C" fn configfile_free(config: *mut ConfigFileOptions) {
        assert!(!config.is_null());
        unsafe { Box::from_raw(config) };
    }

    #[no_mangle]
    pub extern "C" fn config_new(
        config_file: *const ConfigFileOptions,
        options: *const CliOptions,
    ) -> *mut ConfigOptions {
        assert!(!config_file.is_null());
        assert!(!options.is_null());

        let config_file = unsafe { &*config_file };
        let options = unsafe { &*options };

        let config = ConfigOptions::new(config_file.clone(), options.clone());

        Box::into_raw(Box::new(config))
    }

    #[no_mangle]
    pub extern "C" fn config_free(config: *mut ConfigOptions) {
        assert!(!config.is_null());
        unsafe { Box::from_raw(config) };
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

        config
            .general
            .heartbeat_interval
            .unwrap()
            .convert(units::TimePrefixUpper::Sec)
            .unwrap()
            .value()
            * SIMTIME_ONE_SECOND
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
    pub extern "C" fn config_getUseOpensslRNGPreload(config: *const ConfigOptions) -> bool {
        assert!(!config.is_null());
        let config = unsafe { &*config };
        config.experimental.use_openssl_rng_preload.unwrap()
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
            .value() as u64
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
            .value() as u64
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
    pub extern "C" fn config_getNetworkGraph(config: *const ConfigOptions) -> *mut libc::c_char {
        assert!(!config.is_null());
        let config = unsafe { &*config };

        let graph = match config.network.graph.as_ref().unwrap() {
            GraphOptions::Gml(CustomGraph::Path(f)) => std::fs::read_to_string(f).unwrap(),
            GraphOptions::Gml(CustomGraph::Inline(s)) => s.clone(),
            GraphOptions::OneGbitSwitch => ONE_GBIT_SWITCH_GRAPH.to_string(),
        };

        CString::into_raw(CString::new(graph).unwrap())
    }

    #[no_mangle]
    pub extern "C" fn config_getUseShortestPath(config: *const ConfigOptions) -> bool {
        assert!(!config.is_null());
        let config = unsafe { &*config };

        config.network.use_shortest_path.unwrap()
    }

    #[no_mangle]
    pub extern "C" fn config_iterHosts(
        config: *const ConfigOptions,
        f: unsafe extern "C" fn(
            *const libc::c_char,
            *const ConfigOptions,
            *const HostOptions,
            *mut libc::c_void,
        ),
        data: *mut libc::c_void,
    ) {
        assert!(!config.is_null());
        let config = unsafe { &*config };

        for (name, host) in &config.hosts {
            // bind the string to a local variable so it's not dropped before f() runs
            let name = CString::new(name.clone()).unwrap();
            unsafe {
                f(
                    name.as_c_str().as_ptr(),
                    config as *const _,
                    host as *const _,
                    data,
                )
            };
        }
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
    pub extern "C" fn hostoptions_getHeartbeatLogLevel(
        host: *const HostOptions,
    ) -> c_log::LogLevel {
        assert!(!host.is_null());
        let host = unsafe { &*host };

        match &host.options.heartbeat_log_level {
            Some(x) => x.to_c_loglevel(),
            None => c_log::_LogLevel_LOGLEVEL_UNSET,
        }
    }

    #[no_mangle]
    pub extern "C" fn hostoptions_getHeartbeatLogInfo(host: *const HostOptions) -> c::LogInfoFlags {
        assert!(!host.is_null());
        let host = unsafe { &*host };

        let mut flags = 0;

        for f in host.options.heartbeat_log_info.as_ref().unwrap() {
            flags |= f.to_c_loginfoflag();
        }

        flags
    }

    #[no_mangle]
    pub extern "C" fn hostoptions_getHeartbeatInterval(
        host: *const HostOptions,
    ) -> c::SimulationTime {
        assert!(!host.is_null());
        let host = unsafe { &*host };

        host.options
            .heartbeat_interval
            .unwrap()
            .convert(units::TimePrefixUpper::Sec)
            .unwrap()
            .value()
            * SIMTIME_ONE_SECOND
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
    pub extern "C" fn hostoptions_getIpAddressHint(host: *const HostOptions) -> *mut libc::c_char {
        assert!(!host.is_null());
        let host = unsafe { &*host };

        match host.options.ip_address_hint.as_ref() {
            Some(x) => CString::into_raw(CString::new(x.to_string()).unwrap()),
            None => std::ptr::null_mut(),
        }
    }

    #[no_mangle]
    pub extern "C" fn hostoptions_getCountryCodeHint(
        host: *const HostOptions,
    ) -> *mut libc::c_char {
        assert!(!host.is_null());
        let host = unsafe { &*host };

        match host.options.country_code_hint.as_ref() {
            Some(x) => CString::into_raw(CString::new(x.to_string()).unwrap()),
            None => std::ptr::null_mut(),
        }
    }

    #[no_mangle]
    pub extern "C" fn hostoptions_getCityCodeHint(host: *const HostOptions) -> *mut libc::c_char {
        assert!(!host.is_null());
        let host = unsafe { &*host };

        match host.options.city_code_hint.as_ref() {
            Some(x) => CString::into_raw(CString::new(x.to_string()).unwrap()),
            None => std::ptr::null_mut(),
        }
    }

    #[no_mangle]
    pub extern "C" fn hostoptions_getBandwidthDown(host: *const HostOptions) -> u64 {
        assert!(!host.is_null());
        let host = unsafe { &*host };

        match host.bandwidth_down {
            Some(x) => x.convert(units::SiPrefixUpper::Base).unwrap().value(),
            None => 0,
        }
    }

    #[no_mangle]
    pub extern "C" fn hostoptions_getBandwidthUp(host: *const HostOptions) -> u64 {
        assert!(!host.is_null());
        let host = unsafe { &*host };

        match host.bandwidth_up {
            Some(x) => x.convert(units::SiPrefixUpper::Base).unwrap().value(),
            None => 0,
        }
    }

    #[no_mangle]
    pub extern "C" fn hostoptions_iterProcesses(
        host: *const HostOptions,
        f: unsafe extern "C" fn(*const ProcessOptions, *mut libc::c_void),
        data: *mut libc::c_void,
    ) {
        assert!(!host.is_null());
        let host = unsafe { &*host };

        for proc in &host.processes {
            unsafe { f(proc as *const _, data) };
        }
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

        let expanded = tilde_expansion(&proc.path.to_str().unwrap());

        match expanded.canonicalize() {
            Ok(path) => CString::into_raw(CString::new(path.to_str().unwrap()).unwrap()),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => {
                log::warn!("Unable to find {:?}", &expanded);
                std::ptr::null_mut()
            }
            Err(_) => panic!(),
        }
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
