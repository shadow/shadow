use core::convert::{TryFrom, TryInto};
use std::collections::hash_map::Entry;
use std::collections::HashMap;
use std::error::Error;
use std::hash::Hash;

use crate::core::support::configuration::{
    self, Compression, FileSource, GraphOptions, GraphSource,
};
use crate::core::support::{units, units::Unit};
use crate::routing::petgraph_wrapper::GraphWrapper;

use log::*;
use petgraph::graph::NodeIndex;
use petgraph::visit::EdgeRef;
use rayon::iter::{IntoParallelIterator, ParallelIterator};

/// A graph node.
#[derive(Debug, PartialEq)]
pub struct ShadowNode {
    pub id: u32,
    pub bandwidth_down: Option<units::BitsPerSec<units::SiPrefixUpper>>,
    pub bandwidth_up: Option<units::BitsPerSec<units::SiPrefixUpper>>,
}

impl TryFrom<gml_parser::gml::Node<'_>> for ShadowNode {
    type Error = String;

    fn try_from(mut gml_node: gml_parser::gml::Node) -> Result<Self, Self::Error> {
        Ok(Self {
            id: gml_node.id.ok_or("Node 'id' was not provided")?,
            bandwidth_down: gml_node
                .other
                .remove("host_bandwidth_down")
                .map(|bandwidth| {
                    bandwidth
                        .as_str()
                        .ok_or("Node 'host_bandwidth_down' is not a string")?
                        .parse()
                        .map_err(|e| {
                            format!("Node 'host_bandwidth_down' is not a valid unit: {}", e)
                        })
                })
                .transpose()?,
            bandwidth_up: gml_node
                .other
                .remove("host_bandwidth_up")
                .map(|bandwidth| {
                    bandwidth
                        .as_str()
                        .ok_or("Node 'host_bandwidth_up' is not a string")?
                        .parse()
                        .map_err(|e| format!("Node 'host_bandwidth_up' is not a valid unit: {}", e))
                })
                .transpose()?,
        })
    }
}

/// A graph edge.
#[derive(Debug, PartialEq)]
pub struct ShadowEdge {
    pub source: u32,
    pub target: u32,
    pub latency: units::Time<units::TimePrefix>,
    pub packet_loss: f32,
}

impl TryFrom<gml_parser::gml::Edge<'_>> for ShadowEdge {
    type Error = String;

    fn try_from(mut gml_edge: gml_parser::gml::Edge) -> Result<Self, Self::Error> {
        let rv = Self {
            source: gml_edge.source,
            target: gml_edge.target,
            latency: gml_edge
                .other
                .remove("latency")
                .ok_or("Edge 'latency' was not provided")?
                .as_str()
                .ok_or("Edge 'latency' is not a string")?
                .parse()
                .map_err(|e| format!("Edge 'latency' is not a valid unit: {}", e))?,
            packet_loss: match gml_edge.other.remove("packet_loss") {
                Some(x) => x.as_float().ok_or("Edge 'packet_loss' is not a float")?,
                None => 0.0,
            },
        };

        if rv.packet_loss < 0f32 || rv.packet_loss > 1f32 {
            Err("Edge 'packet_loss' is not in the range [0,1]")?;
        }

        Ok(rv)
    }
}

/// A network graph containing the petgraph graph and a map from gml node ids to petgraph node
/// indexes.
#[derive(Debug)]
pub struct NetworkGraph {
    graph: GraphWrapper<ShadowNode, ShadowEdge, u32>,
    node_id_to_index_map: HashMap<u32, NodeIndex>,
}

impl NetworkGraph {
    pub fn graph(&self) -> &GraphWrapper<ShadowNode, ShadowEdge, u32> {
        &self.graph
    }

    pub fn node_id_to_index(&self, id: u32) -> Option<&NodeIndex> {
        self.node_id_to_index_map.get(&id)
    }

    pub fn node_index_to_id(&self, index: NodeIndex) -> Option<u32> {
        self.graph.node_weight(index).map(|w| w.id)
    }

    pub fn parse(graph_text: &str) -> Result<Self, Box<dyn Error>> {
        let gml_graph = gml_parser::parse(graph_text)?;

        let mut g = match gml_graph.directed {
            true => GraphWrapper::Directed(
                petgraph::graph::Graph::<_, _, petgraph::Directed, _>::with_capacity(
                    gml_graph.nodes.len(),
                    gml_graph.edges.len(),
                ),
            ),
            false => {
                GraphWrapper::Undirected(
                    petgraph::graph::Graph::<_, _, petgraph::Undirected, _>::with_capacity(
                        gml_graph.nodes.len(),
                        gml_graph.edges.len(),
                    ),
                )
            }
        };

        // map from GML id to petgraph id
        let mut id_map = HashMap::new();

        for x in gml_graph.nodes.into_iter() {
            let x: ShadowNode = x.try_into()?;
            let gml_id = x.id;
            let petgraph_id = g.add_node(x);
            id_map.insert(gml_id, petgraph_id);
        }

        for x in gml_graph.edges.into_iter() {
            let x: ShadowEdge = x.try_into()?;

            let source = *id_map
                .get(&x.source)
                .ok_or(format!("Edge source {} doesn't exist", x.source))?;
            let target = *id_map
                .get(&x.target)
                .ok_or(format!("Edge target {} doesn't exist", x.target))?;

            g.add_edge(source, target, x);
        }

        Ok(Self {
            graph: g,
            node_id_to_index_map: id_map,
        })
    }

    pub fn compute_shortest_paths(
        &self,
        nodes: &[NodeIndex],
    ) -> HashMap<(NodeIndex, NodeIndex), PathProperties> {
        let start = std::time::Instant::now();

        // calculate shortest paths
        let mut paths: HashMap<(_, _), PathProperties> = nodes
            .into_par_iter()
            .flat_map(|src| {
                match &self.graph {
                    GraphWrapper::Directed(graph) => {
                        petgraph::algo::dijkstra(&graph, *src, None, |e| e.weight().into())
                    }
                    GraphWrapper::Undirected(graph) => {
                        petgraph::algo::dijkstra(&graph, *src, None, |e| e.weight().into())
                    }
                }
                .into_iter()
                // ignore nodes that aren't in use
                .filter(|(dst, _)| nodes.contains(dst))
                // include the src node
                .map(|(dst, path)| ((*src, dst), path))
                .collect::<HashMap<(_, _), _>>()
            })
            .collect();

        // make sure the shortest path includes at least one edge
        for node in nodes {
            // the dijkstra shortest path from node -> node will always be 0
            assert_eq!(paths[&(*node, *node)], PathProperties::default());

            // function for finding the shortest path weight between two specific nodes
            let get_shortest_path = |src, dst| {
                // we may already have a known value if the node is in `nodes`
                if let Some(val) = paths.get(&(src, dst)) {
                    // returned cached value
                    val.clone()
                } else {
                    // compute shortest path
                    let weights: HashMap<_, PathProperties> = match &self.graph {
                        GraphWrapper::Directed(graph) => {
                            petgraph::algo::dijkstra(&graph, src, Some(dst), |e| e.weight().into())
                        }
                        GraphWrapper::Undirected(graph) => {
                            petgraph::algo::dijkstra(&graph, src, Some(dst), |e| e.weight().into())
                        }
                    };
                    // return the one that we're interested in
                    weights[&dst]
                }
            };

            // get a new path for node -> node that includes at least one edge
            let new_path = match &self.graph {
                GraphWrapper::Directed(graph) => graph
                    .edges(*node)
                    // add the edge path and the shortest path back to the original node
                    .map(|e| {
                        PathProperties::from(e.weight()) + get_shortest_path(e.target(), *node)
                    })
                    .min_by(|w_1, w_2| PathProperties::partial_cmp(w_1, w_2).unwrap())
                    .unwrap(),
                GraphWrapper::Undirected(graph) => graph
                    .edges(*node)
                    // add the edge path and the shortest path back to the original node
                    .map(|e| {
                        PathProperties::from(e.weight()) + get_shortest_path(e.target(), *node)
                    })
                    .min_by(|w_1, w_2| PathProperties::partial_cmp(w_1, w_2).unwrap())
                    .unwrap(),
            };

            // if there is a self-loop, 'e.target()' will be 'node'
            // and 'get_shortest_path_weight(e.target(), *node)' will be
            // 'PathProperties::default()', so the new path will only include the self-loop

            paths.insert((*node, *node), new_path);
        }

        assert_eq!(paths.len(), nodes.len().pow(2));

        debug!(
            "Finished dijkstra: {} seconds, {} entries",
            (std::time::Instant::now() - start).as_secs(),
            paths.len()
        );

        paths
    }

    pub fn get_direct_paths(
        &self,
        nodes: &[NodeIndex],
    ) -> Result<HashMap<(NodeIndex, NodeIndex), PathProperties>, Box<dyn Error>> {
        nodes
            .iter()
            .flat_map(|src| nodes.iter().map(move |dst| (src.clone(), dst.clone())))
            .map(|(src, dst)| {
                let src_id = self.node_index_to_id(src).unwrap();
                let dst_id = self.node_index_to_id(dst).unwrap();
                match &self.graph {
                    GraphWrapper::Directed(graph) => {
                        // we require the graph to be connected with exactly one edge between any two nodes
                        let mut edges = graph.edges_connecting(src, dst);
                        let edge = edges
                            .next()
                            .ok_or(format!("No edge connecting {} to {}", src_id, dst_id))?;
                        if edges.count() != 0 {
                            Err(format!(
                                "More than one edge connecting {} to {}",
                                src_id, dst_id
                            ))?
                        }
                        Ok(((src, dst), edge.weight().into()))
                    }
                    GraphWrapper::Undirected(graph) => {
                        // we require the graph to be connected with exactly one edge between any two nodes
                        let mut edges = graph.edges_connecting(src, dst);
                        let edge = edges
                            .next()
                            .ok_or(format!("No edge connecting {} to {}", src_id, dst_id))?;
                        if edges.count() != 0 {
                            Err(format!(
                                "More than one edge connecting {} to {}",
                                src_id, dst_id
                            ))?
                        }
                        Ok(((src, dst), edge.weight().into()))
                    }
                }
            })
            .collect()
    }
}

/// Network characteristics for a path between two nodes.
#[derive(Debug, Default, Clone, Copy)]
pub struct PathProperties {
    /// Latency in nanoseconds.
    pub latency_ns: u64,
    /// Packet loss as fraction.
    pub packet_loss: f32,
}

impl PartialOrd for PathProperties {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        // order by lowest latency first, then by lowest packet loss
        match self.latency_ns.cmp(&other.latency_ns) {
            std::cmp::Ordering::Equal => self.packet_loss.partial_cmp(&other.packet_loss),
            x => Some(x),
        }
    }
}

impl PartialEq for PathProperties {
    fn eq(&self, other: &Self) -> bool {
        // PartialEq must be consistent with PartialOrd
        self.partial_cmp(other) == Some(std::cmp::Ordering::Equal)
    }
}

impl core::ops::Add for PathProperties {
    type Output = Self;

    fn add(self, other: Self) -> Self::Output {
        Self {
            latency_ns: self.latency_ns + other.latency_ns,
            packet_loss: 1f32 - (1f32 - self.packet_loss) * (1f32 - other.packet_loss),
        }
    }
}

impl std::convert::From<&ShadowEdge> for PathProperties {
    fn from(e: &ShadowEdge) -> Self {
        Self {
            latency_ns: e.latency.convert(units::TimePrefix::Nano).unwrap().value(),
            packet_loss: e.packet_loss,
        }
    }
}

/// Tool for assigning IP addresses to graph nodes.
#[derive(Debug)]
pub struct IpAssignment<T: Copy + Eq + Hash + std::fmt::Display> {
    /// A map of host IP addresses to node ids.
    map: HashMap<std::net::IpAddr, T>,
    /// The last dynamically assigned address.
    last_assigned_addr: std::net::IpAddr,
}

impl<T: Copy + Eq + Hash + std::fmt::Display> IpAssignment<T> {
    pub fn new() -> Self {
        Self {
            map: HashMap::new(),
            last_assigned_addr: std::net::IpAddr::V4(std::net::Ipv4Addr::new(11, 0, 0, 0)),
        }
    }

    /// Get an unused address and assign it to a node.
    pub fn assign(&mut self, node_id: T) -> std::net::IpAddr {
        // loop until we find an unused address
        loop {
            let ip_addr = Self::increment_address(&self.last_assigned_addr);
            self.last_assigned_addr = ip_addr;
            if !self.map.contains_key(&ip_addr) {
                self.map.insert(ip_addr, node_id);
                break ip_addr;
            }
        }
    }

    /// Assign an address to a node.
    pub fn assign_ip(&mut self, node_id: T, ip_addr: std::net::IpAddr) -> Result<(), String> {
        let entry = self.map.entry(ip_addr.clone());
        if let Entry::Occupied(entry) = &entry {
            let prev_node_id = entry.get();
            return Err(format!(
                "IP {} assigned to both nodes {} and {}",
                ip_addr, prev_node_id, node_id
            ));
        }
        entry.or_insert(node_id);
        Ok(())
    }

    /// Get the node that an address is assigned to.
    pub fn get_node(&self, ip_addr: std::net::IpAddr) -> Option<T> {
        self.map.get(&ip_addr).copied()
    }

    /// Get all nodes with assigned addresses.
    pub fn get_nodes(&self) -> std::collections::HashSet<T> {
        self.map.values().copied().collect()
    }

    fn increment_address(addr: &std::net::IpAddr) -> std::net::IpAddr {
        match addr {
            std::net::IpAddr::V4(mut x) => loop {
                // increment the address
                x = std::net::Ipv4Addr::from(u32::from(x.clone()) + 1);
                match x.octets()[3] {
                    // if the address ends in ".0" or ".255" (broadcast), try the next
                    0 | 255 => {}
                    _ => break std::net::IpAddr::V4(x),
                }
            },
            std::net::IpAddr::V6(_) => unimplemented!(),
        }
    }
}

/// Routing information for paths between nodes.
pub struct RoutingInfo<T: Eq + Hash + std::fmt::Display + Clone + Copy> {
    paths: HashMap<(T, T), PathProperties>,
    packet_counters: std::sync::RwLock<HashMap<(T, T), u64>>,
}

impl<T: Eq + Hash + std::fmt::Display + Clone + Copy> RoutingInfo<T> {
    pub fn new(paths: HashMap<(T, T), PathProperties>) -> Self {
        Self {
            paths,
            packet_counters: std::sync::RwLock::new(HashMap::new()),
        }
    }

    /// Get properties for the path from one node to another.
    pub fn path(&self, start: T, end: T) -> Option<PathProperties> {
        self.paths.get(&(start, end)).copied()
    }

    /// Increment the number of packets sent from one node to another.
    pub fn increment_packet_count(&self, start: T, end: T) {
        let key = (start, end);
        let mut packet_counters = self.packet_counters.write().unwrap();
        match packet_counters.get_mut(&key) {
            Some(x) => *x = x.saturating_add(1),
            None => assert!(packet_counters.insert(key, 1).is_none()),
        }
    }

    /// Log the number of packets sent between nodes.
    pub fn log_packet_counts(&self) {
        // only logs paths that have transmitted at least one packet
        for ((start, end), count) in self.packet_counters.read().unwrap().iter() {
            let path = self.paths.get(&(*start, *end)).unwrap();
            log::debug!(
                "Found path {}->{}: latency={}ns, packet_loss={}, packet_count={}",
                start,
                end,
                path.latency_ns,
                path.packet_loss,
                count,
            );
        }
    }
}

/// Read and decompress a file.
fn read_xz(filename: &str) -> Result<String, Box<dyn Error>> {
    let mut f = std::io::BufReader::new(std::fs::File::open(filename)?);

    let mut decomp: Vec<u8> = Vec::new();
    lzma_rs::xz_decompress(&mut f, &mut decomp)?;
    decomp.shrink_to_fit();

    Ok(String::from_utf8(decomp)?)
}

/// Get the network graph as a string.
pub fn load_network_graph(graph_options: &GraphOptions) -> Result<String, Box<dyn Error>> {
    Ok(match graph_options {
        GraphOptions::Gml(GraphSource::File(FileSource {
            compression: None,
            path: f,
        })) => std::fs::read_to_string(f)?,
        GraphOptions::Gml(GraphSource::File(FileSource {
            compression: Some(Compression::Xz),
            path: f,
        })) => read_xz(f)?,
        GraphOptions::Gml(GraphSource::Inline(s)) => s.clone(),
        GraphOptions::OneGbitSwitch => configuration::ONE_GBIT_SWITCH_GRAPH.to_string(),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_path_add() {
        let p1 = PathProperties {
            latency_ns: 23,
            packet_loss: 0.35,
        };
        let p2 = PathProperties {
            latency_ns: 11,
            packet_loss: 0.85,
        };

        let p3 = p1 + p2;
        assert_eq!(p3.latency_ns, 34);
        assert!((p3.packet_loss - 0.9025).abs() < 0.01);
    }

    #[test]
    fn test_nonexistent_id() {
        for id in &[2, 3] {
            let graph = format!(
                r#"graph [
                node [
                  id 1
                ]
                node [
                  id 3
                ]
                edge [
                  source 1
                  target {}
                  latency "1 ns"
                ]
            ]"#,
                id
            );

            if *id == 3 {
                NetworkGraph::parse(&graph).unwrap();
            } else {
                NetworkGraph::parse(&graph).unwrap_err();
            }
        }
    }

    #[test]
    fn test_shortest_path() {
        for directed in &[true, false] {
            let graph = format!(
                r#"graph [
                  directed {}
                  node [
                    id 0
                  ]
                  node [
                    id 1
                  ]
                  node [
                    id 2
                  ]
                  edge [
                    source 0
                    target 1
                    latency "3 ns"
                  ]
                  edge [
                    source 1
                    target 0
                    latency "5 ns"
                  ]
                  edge [
                    source 0
                    target 2
                    latency "7 ns"
                  ]
                  edge [
                    source 2
                    target 1
                    latency "11 ns"
                  ]
                ]"#,
                if *directed { 1 } else { 0 }
            );
            let graph = NetworkGraph::parse(&graph).unwrap();
            let node_0 = *graph.node_id_to_index(0).unwrap();
            let node_1 = *graph.node_id_to_index(1).unwrap();
            let node_2 = *graph.node_id_to_index(2).unwrap();

            let shortest_paths = graph.compute_shortest_paths(&[node_0, node_1, node_2]);

            let lookup_latency = |a, b| shortest_paths.get(&(a, b)).unwrap().latency_ns;

            if *directed {
                assert_eq!(lookup_latency(node_0, node_0), 8);
                assert_eq!(lookup_latency(node_0, node_1), 3);
                assert_eq!(lookup_latency(node_0, node_2), 7);
                assert_eq!(lookup_latency(node_1, node_0), 5);
                assert_eq!(lookup_latency(node_1, node_1), 8);
                assert_eq!(lookup_latency(node_1, node_2), 12);
                assert_eq!(lookup_latency(node_2, node_0), 16);
                assert_eq!(lookup_latency(node_2, node_1), 11);
                assert_eq!(lookup_latency(node_2, node_2), 23);
            } else {
                assert_eq!(lookup_latency(node_0, node_0), 6);
                assert_eq!(lookup_latency(node_0, node_1), 3);
                assert_eq!(lookup_latency(node_0, node_2), 7);
                assert_eq!(lookup_latency(node_1, node_0), 3);
                assert_eq!(lookup_latency(node_1, node_1), 6);
                assert_eq!(lookup_latency(node_1, node_2), 10);
                assert_eq!(lookup_latency(node_2, node_0), 7);
                assert_eq!(lookup_latency(node_2, node_1), 10);
                assert_eq!(lookup_latency(node_2, node_2), 14);
            }
        }
    }
}

mod export {
    use super::*;

    #[no_mangle]
    pub extern "C" fn networkgraph_load(
        config: *const configuration::ConfigOptions,
    ) -> *mut NetworkGraph {
        let config = unsafe { config.as_ref() }.unwrap();

        match load_network_graph(config.network.graph.as_ref().unwrap()) {
            Ok(graph_str) => Box::into_raw(Box::new(NetworkGraph::parse(&graph_str).unwrap())),
            Err(err) => {
                error!("{}", err);
                std::ptr::null_mut()
            }
        }
    }

    #[no_mangle]
    pub extern "C" fn networkgraph_free(graph: *mut NetworkGraph) {
        assert!(!graph.is_null());
        unsafe { Box::from_raw(graph) };
    }

    /// Get the downstream bandwidth of the graph node if it exists. A non-zero return value means
    /// that the node did not have a downstream bandwidth and that `bandwidth_down` was not updated.
    #[no_mangle]
    #[must_use]
    pub extern "C" fn networkgraph_nodeBandwidthDownBits(
        graph: *mut NetworkGraph,
        node_id: u32,
        bandwidth_down: *mut u64,
    ) -> libc::c_int {
        let graph = unsafe { graph.as_ref() }.unwrap();
        let bandwidth_down = unsafe { bandwidth_down.as_mut() }.unwrap();

        let node = graph.node_id_to_index(node_id).unwrap();

        match graph.graph().node_weight(*node).unwrap().bandwidth_down {
            Some(x) => {
                *bandwidth_down = x.convert(units::SiPrefixUpper::Base).unwrap().value();
                return 0;
            }
            None => return -1,
        }
    }

    /// Get the upstream bandwidth of the graph node if it exists. A non-zero return value means
    /// that the node did not have an upstream bandwidth and that `bandwidth_up` was not updated.
    #[no_mangle]
    #[must_use]
    pub extern "C" fn networkgraph_nodeBandwidthUpBits(
        graph: *mut NetworkGraph,
        node_id: u32,
        bandwidth_up: *mut u64,
    ) -> libc::c_int {
        let graph = unsafe { graph.as_ref() }.unwrap();
        let bandwidth_up = unsafe { bandwidth_up.as_mut() }.unwrap();

        let node = graph.node_id_to_index(node_id).unwrap();

        match graph.graph().node_weight(*node).unwrap().bandwidth_up {
            Some(x) => {
                *bandwidth_up = x.convert(units::SiPrefixUpper::Base).unwrap().value();
                return 0;
            }
            None => return -1,
        }
    }

    #[no_mangle]
    pub extern "C" fn ipassignment_new() -> *mut IpAssignment<u32> {
        Box::into_raw(Box::new(IpAssignment::new()))
    }

    #[no_mangle]
    pub extern "C" fn ipassignment_free(ip_assignment: *mut IpAssignment<u32>) {
        assert!(!ip_assignment.is_null());
        unsafe { Box::from_raw(ip_assignment) };
    }

    /// Get an unused address and assign it to a node.
    #[no_mangle]
    #[must_use]
    pub extern "C" fn ipassignment_assignHost(
        ip_assignment: *mut IpAssignment<u32>,
        node_id: u32,
        ip_addr: *mut libc::in_addr_t,
    ) -> libc::c_int {
        let ip_assignment = unsafe { ip_assignment.as_mut() }.unwrap();
        let ip_addr = unsafe { ip_addr.as_mut() }.unwrap();

        *ip_addr = match ip_assignment.assign(node_id) {
            std::net::IpAddr::V4(x) => u32::to_be(x.into()),
            _ => unimplemented!("Assigned a host to an IPv6 address, but not supported from C"),
        };

        return 0;
    }

    /// Assign an address to a node.
    #[no_mangle]
    #[must_use]
    pub extern "C" fn ipassignment_assignHostWithIp(
        ip_assignment: *mut IpAssignment<u32>,
        node_id: u32,
        ip_addr: libc::in_addr_t,
    ) -> libc::c_int {
        let ip_assignment = unsafe { ip_assignment.as_mut() }.unwrap();
        let ip_addr = std::net::IpAddr::V4(u32::from_be(ip_addr).into());

        match ip_assignment.assign_ip(node_id, ip_addr) {
            Ok(()) => return 0,
            Err(e) => {
                error!("{}", e);
                return -1;
            }
        }
    }

    #[no_mangle]
    pub extern "C" fn routinginfo_new(
        graph: *mut NetworkGraph,
        ip_assignment: *mut IpAssignment<u32>,
        use_shortest_paths: bool,
    ) -> *mut RoutingInfo<u32> {
        let graph = unsafe { graph.as_ref() }.unwrap();
        let ip_assignment = unsafe { ip_assignment.as_mut() }.unwrap();

        let nodes = ip_assignment.get_nodes();
        let nodes: Vec<_> = nodes
            .iter()
            .map(|x| *graph.node_id_to_index(*x).unwrap())
            .collect();

        let to_ids = |((src, dst), path)| {
            let src = graph.node_index_to_id(src).unwrap();
            let dst = graph.node_index_to_id(dst).unwrap();
            ((src, dst), path)
        };

        let paths = if use_shortest_paths {
            graph
                .compute_shortest_paths(&nodes[..])
                .into_iter()
                .map(to_ids)
                .collect()
        } else {
            match graph.get_direct_paths(&nodes[..]) {
                Ok(x) => x.into_iter().map(to_ids).collect(),
                Err(e) => {
                    error!("{}", e);
                    return std::ptr::null_mut();
                }
            }
        };

        Box::into_raw(Box::new(RoutingInfo::new(paths)))
    }

    #[no_mangle]
    pub extern "C" fn routinginfo_free(routing_info: *mut RoutingInfo<u32>) {
        assert!(!routing_info.is_null());

        unsafe { &*routing_info }.log_packet_counts();
        unsafe { Box::from_raw(routing_info) };
    }

    /// Checks if the addresses are assigned to hosts, and if so it must be routable since the
    /// graph is connected.
    #[no_mangle]
    pub extern "C" fn routinginfo_isRoutable(
        ip_assignment: *const IpAssignment<u32>,
        src: libc::in_addr_t,
        dst: libc::in_addr_t,
    ) -> bool {
        let ip_assignment = unsafe { ip_assignment.as_ref() }.unwrap();

        if ip_assignment
            .get_node(std::net::IpAddr::V4(u32::from_be(src).into()))
            .is_none()
        {
            return false;
        }

        if ip_assignment
            .get_node(std::net::IpAddr::V4(u32::from_be(dst).into()))
            .is_none()
        {
            return false;
        }

        // the network graph is required to be a connected graph, so they must be routable
        return true;
    }

    /// Get the packet latency from one host to another. The given addresses must be assigned to
    /// hosts.
    #[no_mangle]
    pub extern "C" fn routinginfo_getLatencyNs(
        routing_info: *const RoutingInfo<u32>,
        ip_assignment: *const IpAssignment<u32>,
        src: libc::in_addr_t,
        dst: libc::in_addr_t,
    ) -> u64 {
        let routing_info = unsafe { routing_info.as_ref() }.unwrap();
        let ip_assignment = unsafe { ip_assignment.as_ref() }.unwrap();
        let src = ip_assignment
            .get_node(std::net::IpAddr::V4(u32::from_be(src).into()))
            .unwrap();
        let dst = ip_assignment
            .get_node(std::net::IpAddr::V4(u32::from_be(dst).into()))
            .unwrap();

        routing_info.path(src, dst).unwrap().latency_ns
    }

    /// Get the packet reliability from one host to another. The given addresses must be assigned
    /// to hosts.
    #[no_mangle]
    pub extern "C" fn routinginfo_getReliability(
        routing_info: *const RoutingInfo<u32>,
        ip_assignment: *const IpAssignment<u32>,
        src: libc::in_addr_t,
        dst: libc::in_addr_t,
    ) -> f32 {
        let routing_info = unsafe { routing_info.as_ref() }.unwrap();
        let ip_assignment = unsafe { ip_assignment.as_ref() }.unwrap();
        let src = ip_assignment
            .get_node(std::net::IpAddr::V4(u32::from_be(src).into()))
            .unwrap();
        let dst = ip_assignment
            .get_node(std::net::IpAddr::V4(u32::from_be(dst).into()))
            .unwrap();

        1.0 - routing_info.path(src, dst).unwrap().packet_loss
    }

    /// Increment the number of packets sent from one host to another. The given addresses must be
    /// assigned to hosts.
    #[no_mangle]
    pub extern "C" fn routinginfo_incrementPacketCount(
        routing_info: *mut RoutingInfo<u32>,
        ip_assignment: *const IpAssignment<u32>,
        src: libc::in_addr_t,
        dst: libc::in_addr_t,
    ) {
        let routing_info = unsafe { routing_info.as_mut() }.unwrap();
        let ip_assignment = unsafe { ip_assignment.as_ref() }.unwrap();
        let src = ip_assignment
            .get_node(std::net::IpAddr::V4(u32::from_be(src).into()))
            .unwrap();
        let dst = ip_assignment
            .get_node(std::net::IpAddr::V4(u32::from_be(dst).into()))
            .unwrap();

        routing_info.increment_packet_count(src, dst)
    }
}
