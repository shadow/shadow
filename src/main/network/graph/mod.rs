mod petgraph_wrapper;

use std::collections::HashMap;
use std::collections::hash_map::Entry;
use std::error::Error;
use std::hash::Hash;

use anyhow::Context;
use log::*;
use petgraph::graph::NodeIndex;
use rayon::iter::{IntoParallelIterator, ParallelIterator};

use crate::core::configuration::{self, Compression, FileSource, GraphOptions, GraphSource};
use crate::network::graph::petgraph_wrapper::GraphWrapper;
use crate::utility::tilde_expansion;
use crate::utility::units::{self, Unit};

type NetGraphError = Box<dyn Error + Send + Sync + 'static>;

/// A graph node.
#[derive(Debug, PartialEq, Eq)]
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
                        .map_err(|e| format!("Node 'host_bandwidth_down' is not a valid unit: {e}"))
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
                        .map_err(|e| format!("Node 'host_bandwidth_up' is not a valid unit: {e}"))
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
    pub jitter: units::Time<units::TimePrefix>,
    pub packet_loss: f32,
    // Optional per-edge bandwidth limits
    pub bandwidth_down: Option<units::BitsPerSec<units::SiPrefixUpper>>,
    pub bandwidth_up: Option<units::BitsPerSec<units::SiPrefixUpper>>,
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
                .map_err(|e| format!("Edge 'latency' is not a valid unit: {e}"))?,
            jitter: match gml_edge.other.remove("jitter") {
                Some(x) => x
                    .as_str()
                    .ok_or("Edge 'jitter' is not a string")?
                    .parse()
                    .map_err(|e| format!("Edge 'jitter' is not a valid unit: {e}"))?,
                None => units::Time::new(0, units::TimePrefix::Milli),
            },
            packet_loss: match gml_edge.other.remove("packet_loss") {
                Some(x) => x.as_float().ok_or("Edge 'packet_loss' is not a float")?,
                None => 0.0,
            },
            bandwidth_down: gml_edge
                .other
                .remove("edge_bandwidth_down")
                .map(|bandwidth| {
                    bandwidth
                        .as_str()
                        .ok_or("Edge 'edge_bandwidth_down' is not a string")?
                        .parse()
                        .map_err(|e| format!("Edge 'edge_bandwidth_down' is not a valid unit: {e}"))
                })
                .transpose()?,
            bandwidth_up: gml_edge
                .other
                .remove("edge_bandwidth_up")
                .map(|bandwidth| {
                    bandwidth
                        .as_str()
                        .ok_or("Edge 'edge_bandwidth_up' is not a string")?
                        .parse()
                        .map_err(|e| format!("Edge 'edge_bandwidth_up' is not a valid unit: {e}"))
                })
                .transpose()?,
        };

        if rv.packet_loss < 0f32 || rv.packet_loss > 1f32 {
            return Err("Edge 'packet_loss' is not in the range [0,1]".into());
        }

        if rv.latency.value() == 0 {
            return Err("Edge 'latency' must not be 0".into());
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

    pub fn parse(graph_text: &str) -> Result<Self, NetGraphError> {
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
    ) -> Result<HashMap<(NodeIndex, NodeIndex), PathProperties>, NetGraphError> {
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

        // use the self-loop for paths from a node to itself
        for node in nodes {
            // the dijkstra shortest path from node -> node will always be 0
            assert_eq!(paths[&(*node, *node)], PathProperties::default());

            // there must be a single self-loop for each node
            paths.insert((*node, *node), self.get_edge_weight(node, node)?.into());
        }

        assert_eq!(paths.len(), nodes.len().pow(2));

        debug!(
            "Finished computing shortest paths: {} seconds, {} entries",
            (std::time::Instant::now() - start).as_secs(),
            paths.len()
        );

        Ok(paths)
    }

    pub fn get_direct_paths(
        &self,
        nodes: &[NodeIndex],
    ) -> Result<HashMap<(NodeIndex, NodeIndex), PathProperties>, NetGraphError> {
        let start = std::time::Instant::now();

        let paths: HashMap<_, _> = nodes
            .iter()
            .flat_map(|src| nodes.iter().map(move |dst| (*src, *dst)))
            // we require the graph to be connected with exactly one edge between any two nodes
            .map(|(src, dst)| Ok(((src, dst), self.get_edge_weight(&src, &dst)?.into())))
            .collect::<Result<_, NetGraphError>>()?;

        assert_eq!(paths.len(), nodes.len().pow(2));

        debug!(
            "Finished computing direct paths: {} seconds, {} entries",
            (std::time::Instant::now() - start).as_secs(),
            paths.len()
        );

        Ok(paths)
    }

    /// Get the weight for the edge between two nodes. Returns an error if there
    /// is not exactly one edge between them.
    fn get_edge_weight(
        &self,
        src: &NodeIndex,
        dst: &NodeIndex,
    ) -> Result<&ShadowEdge, NetGraphError> {
        let src_id = self.node_index_to_id(*src).unwrap();
        let dst_id = self.node_index_to_id(*dst).unwrap();
        match &self.graph {
            GraphWrapper::Directed(graph) => {
                let mut edges = graph.edges_connecting(*src, *dst);
                let edge = edges
                    .next()
                    .ok_or(format!("No edge connecting node {src_id} to {dst_id}"))?;
                if edges.count() != 0 {
                    return Err(
                        format!("More than one edge connecting node {src_id} to {dst_id}").into(),
                    );
                }
                Ok(edge.weight())
            }
            GraphWrapper::Undirected(graph) => {
                let mut edges = graph.edges_connecting(*src, *dst);
                let edge = edges
                    .next()
                    .ok_or(format!("No edge connecting node {src_id} to {dst_id}"))?;
                if edges.count() != 0 {
                    return Err(
                        format!("More than one edge connecting node {src_id} to {dst_id}").into(),
                    );
                }
                Ok(edge.weight())
            }
        }
    }

    /// Get a reference to the edge between two nodes.
    pub fn get_edge(&self, src: NodeIndex, dst: NodeIndex) -> Result<&ShadowEdge, NetGraphError> {
        self.get_edge_weight(&src, &dst)
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

#[derive(Debug)]
pub struct IpPreviouslyAssignedError;
impl std::error::Error for IpPreviouslyAssignedError {}

impl std::fmt::Display for IpPreviouslyAssignedError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "IP address has already been assigned")
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
            if let std::collections::hash_map::Entry::Vacant(e) = self.map.entry(ip_addr) {
                e.insert(node_id);
                break ip_addr;
            }
        }
    }

    /// Assign an address to a node.
    pub fn assign_ip(
        &mut self,
        node_id: T,
        ip_addr: std::net::IpAddr,
    ) -> Result<(), IpPreviouslyAssignedError> {
        let entry = self.map.entry(ip_addr);
        if let Entry::Occupied(_) = &entry {
            return Err(IpPreviouslyAssignedError);
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
            std::net::IpAddr::V4(x) => {
                let addr_bits = u32::from(*x);
                let mut increment = 1;
                loop {
                    // increment the address
                    let next_addr = std::net::Ipv4Addr::from(addr_bits + increment);
                    match next_addr.octets()[3] {
                        // if the address ends in ".0" or ".255" (broadcast), try the next
                        0 | 255 => increment += 1,
                        _ => break std::net::IpAddr::V4(next_addr),
                    }
                }
            }
            std::net::IpAddr::V6(_) => unimplemented!(),
        }
    }
}

impl<T: Copy + Eq + Hash + std::fmt::Display> Default for IpAssignment<T> {
    fn default() -> Self {
        Self::new()
    }
}

/// Routing information for paths between nodes.
#[derive(Debug)]
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

    pub fn get_smallest_latency_ns(&self) -> Option<u64> {
        self.paths.values().map(|x| x.latency_ns).min()
    }
}

/// Read and decompress a file.
fn read_xz<P: AsRef<std::path::Path>>(path: P) -> Result<String, NetGraphError> {
    let path = path.as_ref();

    let mut f = std::io::BufReader::new(
        std::fs::File::open(path).with_context(|| format!("Failed to open file: {path:?}"))?,
    );

    let mut decomp: Vec<u8> = Vec::new();
    lzma_rs::xz_decompress(&mut f, &mut decomp).context("Failed to decompress file")?;
    decomp.shrink_to_fit();

    Ok(String::from_utf8(decomp)?)
}

/// Get the network graph as a string.
pub fn load_network_graph(graph_options: &GraphOptions) -> Result<String, NetGraphError> {
    Ok(match graph_options {
        GraphOptions::Gml(GraphSource::File(FileSource {
            compression: None,
            path: f,
        })) => std::fs::read_to_string(tilde_expansion(f))
            .with_context(|| format!("Failed to read file: {f}"))?,
        GraphOptions::Gml(GraphSource::File(FileSource {
            compression: Some(Compression::Xz),
            path: f,
        })) => read_xz(tilde_expansion(f))?,
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
                  target {id}
                  latency "1 ns"
                ]
            ]"#,
            );

            if *id == 3 {
                NetworkGraph::parse(&graph).unwrap();
            } else {
                NetworkGraph::parse(&graph).unwrap_err();
            }
        }
    }

    // disabled under miri due to https://github.com/rayon-rs/rayon/issues/952
    #[test]
    #[cfg_attr(miri, ignore)]
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
                    target 0
                    latency "3333 ns"
                  ]
                  edge [
                    source 1
                    target 1
                    latency "5555 ns"
                  ]
                  edge [
                    source 2
                    target 2
                    latency "7777 ns"
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

            let shortest_paths = graph
                .compute_shortest_paths(&[node_0, node_1, node_2])
                .unwrap();

            let lookup_latency = |a, b| shortest_paths.get(&(a, b)).unwrap().latency_ns;

            if *directed {
                assert_eq!(lookup_latency(node_0, node_0), 3333);
                assert_eq!(lookup_latency(node_0, node_1), 3);
                assert_eq!(lookup_latency(node_0, node_2), 7);
                assert_eq!(lookup_latency(node_1, node_0), 5);
                assert_eq!(lookup_latency(node_1, node_1), 5555);
                assert_eq!(lookup_latency(node_1, node_2), 12);
                assert_eq!(lookup_latency(node_2, node_0), 16);
                assert_eq!(lookup_latency(node_2, node_1), 11);
                assert_eq!(lookup_latency(node_2, node_2), 7777);
            } else {
                assert_eq!(lookup_latency(node_0, node_0), 3333);
                assert_eq!(lookup_latency(node_0, node_1), 3);
                assert_eq!(lookup_latency(node_0, node_2), 7);
                assert_eq!(lookup_latency(node_1, node_0), 3);
                assert_eq!(lookup_latency(node_1, node_1), 5555);
                assert_eq!(lookup_latency(node_1, node_2), 10);
                assert_eq!(lookup_latency(node_2, node_0), 7);
                assert_eq!(lookup_latency(node_2, node_1), 10);
                assert_eq!(lookup_latency(node_2, node_2), 7777);
            }
        }
    }

    #[test]
    fn test_increment_address_skip_broadcast() {
        let addr = std::net::IpAddr::V4(std::net::Ipv4Addr::new(11, 0, 0, 254));
        let incremented = IpAssignment::<i32>::increment_address(&addr);
        assert!(incremented > addr);
        assert_ne!(
            incremented,
            std::net::IpAddr::V4(std::net::Ipv4Addr::new(11, 0, 0, 255))
        );
    }
}
