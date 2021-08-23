use petgraph::graph::{EdgeIndex, Graph, IndexType, NodeIndex};
use petgraph::{Directed, Undirected};

#[derive(Debug)]
pub enum GraphWrapper<N, E, Ix: IndexType> {
    Directed(Graph<N, E, Directed, Ix>),
    Undirected(Graph<N, E, Undirected, Ix>),
}

// for functions that are the same for both directed and undirected graphs, we can added
// wrapper functions
#[allow(dead_code)]
impl<N, E, Ix: IndexType> GraphWrapper<N, E, Ix> {
    enum_passthrough!(self, (weight), Directed, Undirected;
        pub fn add_node(&mut self, weight: N) -> NodeIndex<Ix>
    );
    enum_passthrough!(self, (a, b, weight), Directed, Undirected;
        pub fn add_edge(&mut self, a: NodeIndex<Ix>, b: NodeIndex<Ix>, weight: E) -> EdgeIndex<Ix>
    );
    enum_passthrough!(self, (node), Directed, Undirected;
        pub fn node_weight(&self, node: NodeIndex<Ix>) -> Option<&N>
    );
    enum_passthrough!(self, (edge), Directed, Undirected;
        pub fn edge_weight(&self, edge: EdgeIndex<Ix>) -> Option<&E>
    );
    enum_passthrough!(self, (a, b), Directed, Undirected;
        pub fn find_edge(&self, a: NodeIndex<Ix>, b: NodeIndex<Ix>) -> Option<EdgeIndex<Ix>>
    );
}
