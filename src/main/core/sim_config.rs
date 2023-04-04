use std::collections::{HashMap, HashSet};
use std::ffi::{OsStr, OsString};
use std::hash::{Hash, Hasher};
use std::os::unix::fs::MetadataExt;
use std::path::PathBuf;
use std::time::Duration;

use anyhow::Context;
use rand::{Rng, SeedableRng};
use rand_xoshiro::Xoshiro256PlusPlus;
use shadow_shim_helper_rs::simulation_time::SimulationTime;

use crate::core::support::configuration::Flatten;
use crate::core::support::configuration::{
    parse_string_as_args, ConfigOptions, HostOptions, LogInfoFlag, LogLevel, ProcessArgs,
    ProcessOptions, QDiscMode,
};
use crate::core::support::units::{self, Unit};
use crate::network::graph::{load_network_graph, IpAssignment, NetworkGraph, RoutingInfo};
use crate::utility::tilde_expansion;

/// The simulation configuration after processing the configuration options and network graph.
pub struct SimConfig {
    // deterministic source of randomness for the simulation
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

impl SimConfig {
    pub fn new(config: &ConfigOptions, hosts_to_debug: &HashSet<String>) -> anyhow::Result<Self> {
        // Xoshiro256PlusPlus is not ideal when a seed with many zeros is used, but
        // 'seed_from_u64()' uses SplitMix64 to derive the actual seed, so we are okay here
        let seed = config.general.seed.unwrap();
        let mut random = Xoshiro256PlusPlus::seed_from_u64(seed.into());

        // this should be the same for all hosts
        let randomness_for_seed_calc = random.gen();

        // build the host list
        let mut hosts = vec![];
        for (name, host_options) in &config.hosts {
            let mut new_hosts = build_host(
                config,
                host_options,
                name,
                randomness_for_seed_calc,
                hosts_to_debug,
            )
            .with_context(|| format!("Failed to configure host '{name}'"))?;
            hosts.append(&mut new_hosts);
        }
        if hosts.is_empty() {
            return Err(anyhow::anyhow!(
                "The configuration did not contain any hosts"
            ));
        }

        // load and parse the network graph
        let graph: String = load_network_graph(config.network.graph.as_ref().unwrap())
            .map_err(|e| anyhow::anyhow!(e))
            .context("Failed to load the network graph")?;
        let graph = NetworkGraph::parse(&graph)
            .map_err(|e| anyhow::anyhow!(e))
            .context("Failed to parse the network graph")?;

        // check that each node ID is valid
        for host in &hosts {
            if graph.node_id_to_index(host.network_node_id).is_none() {
                return Err(anyhow::anyhow!(
                    "The network node id {} for host '{}' does not exist",
                    host.network_node_id,
                    host.name
                ));
            }
        }

        // assign a bandwidth to every host
        for host in &mut hosts {
            let node_index = graph.node_id_to_index(host.network_node_id).unwrap();
            let node = graph.graph().node_weight(*node_index).unwrap();

            let graph_bw_down_bits = node
                .bandwidth_down
                .map(|x| x.convert(units::SiPrefixUpper::Base).unwrap().value());
            let graph_bw_up_bits = node
                .bandwidth_up
                .map(|x| x.convert(units::SiPrefixUpper::Base).unwrap().value());

            host.bandwidth_down_bits = host.bandwidth_down_bits.or(graph_bw_down_bits);
            host.bandwidth_up_bits = host.bandwidth_up_bits.or(graph_bw_up_bits);

            // check if no bandwidth was provided in the host options or graph node
            if host.bandwidth_down_bits.is_none() {
                return Err(anyhow::anyhow!(
                    "No downstream bandwidth provided for host '{}'",
                    host.name
                ));
            }
            if host.bandwidth_up_bits.is_none() {
                return Err(anyhow::anyhow!(
                    "No upstream bandwidth provided for host '{}'",
                    host.name
                ));
            }
        }

        // check if any hosts in 'hosts_to_debug' don't exist
        for hostname in hosts_to_debug {
            if !hosts.iter().any(|y| &y.name == hostname) {
                return Err(anyhow::anyhow!(
                    "The host to debug '{hostname}' doesn't exist"
                ));
            }
        }

        // assign IP addresses to hosts and graph nodes
        let ip_assignment = assign_ips(&mut hosts)?;

        // generate routing info between every pair of in-use nodes
        let routing_info = generate_routing_info(
            &graph,
            &ip_assignment.get_nodes(),
            config.network.use_shortest_path.unwrap(),
        )?;

        // get all host bandwidths
        let host_bandwidths = hosts
            .iter()
            .map(|host| {
                // we made sure above that every host has a bandwidth set
                let bw = Bandwidth {
                    up_bytes: host.bandwidth_up_bits.unwrap() / 8,
                    down_bytes: host.bandwidth_down_bits.unwrap() / 8,
                };

                (host.ip_addr.unwrap(), bw)
            })
            .collect();

        Ok(Self {
            random,
            ip_assignment,
            routing_info,
            host_bandwidths,
            hosts,
        })
    }
}

#[derive(Clone)]
pub struct HostInfo {
    pub name: String,
    pub processes: Vec<ProcessInfo>,
    pub seed: u64,
    pub network_node_id: u32,
    pub pause_for_debugging: bool,
    pub cpu_threshold: Option<SimulationTime>,
    pub cpu_precision: Option<SimulationTime>,
    pub bandwidth_down_bits: Option<u64>,
    pub bandwidth_up_bits: Option<u64>,
    pub ip_addr: Option<std::net::IpAddr>,
    pub log_level: Option<LogLevel>,
    pub pcap_config: Option<PcapConfig>,
    pub heartbeat_log_level: Option<LogLevel>,
    pub heartbeat_log_info: HashSet<LogInfoFlag>,
    pub heartbeat_interval: Option<SimulationTime>,
    pub send_buf_size: u64,
    pub recv_buf_size: u64,
    pub autotune_send_buf: bool,
    pub autotune_recv_buf: bool,
    pub qdisc: QDiscMode,
}

#[derive(Clone)]
pub struct ProcessInfo {
    pub plugin: PathBuf,
    pub start_time: SimulationTime,
    pub stop_time: Option<SimulationTime>,
    pub args: Vec<OsString>,
    pub env: String,
}

#[derive(Debug, Clone)]
pub struct Bandwidth {
    pub up_bytes: u64,
    pub down_bytes: u64,
}

#[derive(Debug, Clone, Copy)]
pub struct PcapConfig {
    pub capture_size: u64,
}

/// For a host entry in the configuration options, build a list of `HostInfo` objects.
fn build_host(
    config: &ConfigOptions,
    host: &HostOptions,
    hostname: &str,
    randomness_for_seed_calc: u64,
    hosts_to_debug: &HashSet<String>,
) -> anyhow::Result<Vec<HostInfo>> {
    let quantity = *host.quantity;

    // make sure we're not trying to set a single address for multiple hosts
    // this should be caught later anyways, but the check here gives a useful error message
    if host.ip_addr.is_some() && quantity > 1 {
        return Err(anyhow::anyhow!(
            "Host has an IP address set and a quantity {quantity} greater than 1",
        ));
    }

    let mut hosts = Vec::with_capacity(quantity.try_into().unwrap());

    for host_index in 0..quantity {
        let hostname = if quantity > 1 {
            format!("{}{}", hostname, host_index + 1)
        } else {
            hostname.to_string()
        };

        // hostname hash is used as part of the host's seed
        let hostname_hash = {
            let mut hasher = std::collections::hash_map::DefaultHasher::new();
            hostname.hash(&mut hasher);
            hasher.finish()
        };

        let pause_for_debugging = hosts_to_debug.contains(&hostname);

        let mut processes = vec![];
        for proc in &host.processes {
            let mut new_processes = build_process(proc).with_context(|| {
                format!("Failed to configure process '{}'", proc.path.display())
            })?;
            processes.append(&mut new_processes);
        }

        hosts.push(HostInfo {
            name: hostname,
            processes,

            seed: randomness_for_seed_calc ^ hostname_hash,
            network_node_id: host.network_node_id,
            pause_for_debugging,

            cpu_threshold: None,
            cpu_precision: Some(SimulationTime::from_nanos(200)),

            bandwidth_down_bits: host
                .bandwidth_down
                .map(|x| x.convert(units::SiPrefixUpper::Base).unwrap().value()),
            bandwidth_up_bits: host
                .bandwidth_down
                .map(|x| x.convert(units::SiPrefixUpper::Base).unwrap().value()),

            ip_addr: host.ip_addr.map(|x| x.into()),
            log_level: host.options.log_level.flatten(),
            pcap_config: host.options.pcap_enabled.unwrap().then_some(PcapConfig {
                capture_size: host
                    .options
                    .pcap_capture_size
                    .unwrap()
                    .convert(units::SiPrefixUpper::Base)
                    .unwrap()
                    .value(),
            }),

            // some options come from the config options and not the host options
            heartbeat_log_level: config.experimental.host_heartbeat_log_level,
            heartbeat_log_info: config
                .experimental
                .host_heartbeat_log_info
                .clone()
                .unwrap_or_default(),
            heartbeat_interval: config
                .experimental
                .host_heartbeat_interval
                .flatten()
                .map(|x| Duration::from(x).try_into().unwrap()),
            send_buf_size: config
                .experimental
                .socket_send_buffer
                .unwrap()
                .convert(units::SiPrefixUpper::Base)
                .unwrap()
                .value(),
            recv_buf_size: config
                .experimental
                .socket_recv_buffer
                .unwrap()
                .convert(units::SiPrefixUpper::Base)
                .unwrap()
                .value(),
            autotune_send_buf: config.experimental.socket_send_autotune.unwrap(),
            autotune_recv_buf: config.experimental.socket_recv_autotune.unwrap(),
            qdisc: config.experimental.interface_qdisc.unwrap(),
        });
    }

    Ok(hosts)
}

/// For a process entry in the configuration options, build a list of `ProcessInfo` objects.
fn build_process(proc: &ProcessOptions) -> anyhow::Result<Vec<ProcessInfo>> {
    let start_time = Duration::from(proc.start_time).try_into().unwrap();
    let stop_time = proc
        .stop_time
        .map(|x| Duration::from(x).try_into().unwrap());

    if let Some(stop_time) = stop_time {
        if start_time >= stop_time {
            return Err(anyhow::anyhow!(
                "Process has a start time '{}' greater than the stop time '{}'",
                proc.start_time,
                proc.stop_time.unwrap(),
            ));
        }
    }

    let mut args = match &proc.args {
        ProcessArgs::List(x) => x.iter().map(|y| OsStr::new(y).to_os_string()).collect(),
        ProcessArgs::Str(x) => parse_string_as_args(OsStr::new(&x.trim()))
            .map_err(|e| anyhow::anyhow!(e))
            .with_context(|| format!("Failed to parse arguments: {x}"))?,
    };

    let expanded_path = tilde_expansion(proc.path.to_str().unwrap());

    // We currently use `which::which`, which searches the `PATH` similarly to a
    // shell.
    let canonical_path: PathBuf = which::which(&expanded_path)
        .map_err(anyhow::Error::from)
        // `which` returns an absolute path, but it may still contain
        // symbolic links, .., etc.
        .and_then(|p| Ok(p.canonicalize()?))
        .with_context(|| format!("Failed to resolve plugin path '{:?}'", expanded_path))?;

    verify_plugin_path(&canonical_path)
        .with_context(|| format!("Failed to verify plugin path '{:?}'", canonical_path))?;
    log::info!(
        "Resolved binary path {:?} to {:?}",
        proc.path,
        canonical_path
    );

    // set argv[0] as the user-provided expanded string, not the canonicalized version
    args.insert(0, expanded_path.into());

    Ok(vec![
        ProcessInfo {
            plugin: canonical_path,
            start_time,
            stop_time,
            args,
            env: proc.environment.clone(),
        };
        (*proc.quantity).try_into().unwrap()
    ])
}

/// Generate an IP assignment map using hosts' configured IP addresses and graph node IDs. For hosts
/// without IP addresses, they will be assigned an arbitrary IP address.
fn assign_ips(hosts: &mut [HostInfo]) -> anyhow::Result<IpAssignment<u32>> {
    let mut ip_assignment = IpAssignment::new();

    // first register hosts that have a specific IP address
    for host in hosts.iter().filter(|x| x.ip_addr.is_some()) {
        let ip = host.ip_addr.unwrap();
        let hostname = &host.name;
        let node_id = host.network_node_id;
        ip_assignment.assign_ip(node_id, ip).with_context(|| {
            format!("Failed to assign IP address {ip} for host '{hostname}' to node '{node_id}'")
        })?;
    }

    // then register remaining hosts
    for host in hosts.iter_mut().filter(|x| x.ip_addr.is_none()) {
        let ip = ip_assignment.assign(host.network_node_id);
        // assign the new IP to the host
        host.ip_addr = Some(ip);
    }

    Ok(ip_assignment)
}

/// Generate a map containing routing information (latency, packet loss, etc) for each pair of
/// nodes.
fn generate_routing_info(
    graph: &NetworkGraph,
    nodes: &std::collections::HashSet<u32>,
    use_shortest_paths: bool,
) -> anyhow::Result<RoutingInfo<u32>> {
    // convert gml node IDs to petgraph indexes
    let nodes: Vec<_> = nodes
        .iter()
        .map(|x| *graph.node_id_to_index(*x).unwrap())
        .collect();

    // helper to convert petgraph indexes back to gml node IDs
    let to_ids = |((src, dst), path)| {
        let src = graph.node_index_to_id(src).unwrap();
        let dst = graph.node_index_to_id(dst).unwrap();
        ((src, dst), path)
    };

    let paths = if use_shortest_paths {
        graph
            .compute_shortest_paths(&nodes[..])
            .map_err(|e| anyhow::anyhow!(e))
            .context("Failed to compute shortest paths between graph nodes")?
            .into_iter()
            .map(to_ids)
            .collect()
    } else {
        graph
            .get_direct_paths(&nodes[..])
            .map_err(|e| anyhow::anyhow!(e))
            .context("Failed to get the direct paths between graph nodes")?
            .into_iter()
            .map(to_ids)
            .collect()
    };

    Ok(RoutingInfo::new(paths))
}

/// Check that the plugin path is valid.
fn verify_plugin_path(path: impl AsRef<std::path::Path>) -> anyhow::Result<()> {
    let path = path.as_ref();
    let metadata = std::fs::metadata(path)?;

    if !metadata.is_file() {
        return Err(anyhow::anyhow!("The path is not a file"));
    }

    // this mask doesn't guarantee that we can execute the file (the file might have S_IXUSR
    // but be owned by a different user), but it should catch most errors
    let mask = libc::S_IXUSR | libc::S_IXGRP | libc::S_IXOTH;
    if (metadata.mode() & mask) == 0 {
        return Err(anyhow::anyhow!("The path is not executable"));
    }

    Ok(())
}
