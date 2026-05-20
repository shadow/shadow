# Network Graph Specification

The [network graph overview](network_graph_overview.md) provides a general
summary of Shadow's use of a network graph to abstractly model network position
and to connect virtual hosts in a network topology while enforcing network
characteristics on paths between hosts. This page describes the specific
attributes that can be configured in the network graph, and the effect that each
attribute has on the simulation.

### Example Graph

Below is an example of a simple network graph in the Shadow-supported GML format
(note that GML calls graph _vertices_ as _nodes_, but these terms are generally
interchangeable).

```gml
graph [
  directed 0
  node [
    id 0
    label "node at 1.2.3.4"
    host_bandwidth_down "100 Mbit"
    host_bandwidth_up "100 Mbit"
  ]
  edge [
    source 0
    target 0
    label "path from 1.2.3.4 to 1.2.3.4"
    latency "10 ms"
    jitter "0 ms"
    packet_loss 0.0
  ]
]
```

### Configurable Attributes

- [`graph.directed`](#graphdirected)
- [`node.id`](#nodeid)
- [`node.label`](#nodelabel)
- [`node.host_bandwidth_down`](#nodehost_bandwidth_down)
- [`node.host_bandwidth_up`](#nodehost_bandwidth_up)
- [`edge.source`](#edgesource)
- [`edge.target`](#edgetarget)
- [`edge.label`](#edgelabel)
- [`edge.latency`](#edgelatency)
- [`edge.jitter`](#edgejitter)
- [`edge.packet_loss`](#edgepacket_loss)
 - [`edge.edge_bandwidth_down`](#edgeedge_bandwidth_down)
 - [`edge.edge_bandwidth_up`](#edgeedge_bandwidth_up)

#### `graph.directed`

Required: False  
Default: `0`  
Type: Integer

Specifies the symmetry of the edges in the graph. If set to `0` (the default),
the graph is an [undirected
graph](https://en.wikipedia.org/wiki/Graph_(discrete_mathematics)): an edge
between node `u` and node `v` is symmetric and can be used to construct a
path both from `u` to `v` and from `v` to `u`. If set to `1`, the graph is a
[directed graph](https://en.wikipedia.org/wiki/Directed_graph): an edge from
node `u` to node `v` is assymmetric and can only be used to construct a path
from `u` to `v` (a separate edge from `v` to `u` must be specified to compose a
path in the reverse direction).

#### `node.id`

Required: True  
Type: Integer

A unique integer identifier for a given node.

#### `node.label`

Required: False  
Default: n/a  
Type: String

An optional, human-meaningful string description of the node. The string may
be used in log messages printed by Shadow.

#### `node.host_bandwidth_down`

Required: True  
Type: String

A string defining the downstream (receive) bandwidth that will be allowed for
any host attached to this node. Hosts may individually override this value in
[the Shadow config file](shadow_config_spec.md#hostshostnamebandwidth_down).
The format of the string specifies the bandwidth and its unit as described in
the [config documentation](shadow_config_spec.md), e.g., `10 Mbit`. Note that
this bandwidth is allowed for every host that is attached to this node; it is
**not** the total bandwidth logically available at the node (which is not
defined).

#### `node.host_bandwidth_up`

Required: True  
Type: String

A string defining the upstream (send) bandwidth that will be allowed for any
host attached to this node. Hosts may individually override this value in [the
Shadow config file](shadow_config_spec.md#hostshostnamebandwidth_up). The
format of the string specifies the bandwidth and its unit as described in the
[config documentation](shadow_config_spec.md), e.g., `10 Mbit`. Note that
this bandwidth is allowed for every host that is attached to this node; it is
**not** the total bandwidth logically available at the node (which is not
defined).

#### `edge.source`

Required: True  
Type: Integer

The unique integer identifier of the first of two nodes of the edge. The
node must exist in the graph. If the graph is directed, this node is treated
as the source or start of the edge.

#### `edge.target`

Required: True  
Type: Integer

The unique integer identifier of the second of two nodes of the edge. The
node must exist in the graph. If the graph is directed, this node is treated
as the target or end of the edge.

#### `edge.label`

Required: False  
Default: n/a  
Type: String

An optional, human-meaningful string description of the edge. The string may be
used in log messages printed by Shadow.

#### `edge.latency`

Required: True  
Type: String

The latency that will be added to packets traversing this edge. This value is
used as a weight while running Dijkstra's shortest path algorithm. The format of
the string specifies the latency and its unit, e.g., `10 ms`. If a unit is not
specified, it will be assumed that it is in the base unit of "seconds". The
latency must not be 0.

#### `edge.jitter`

Required: False  
Default: n/a  
Type: String

This keyword is allowed but currently nonfunctional; it is reserved for future
use.

#### `edge.packet_loss`

Required: True  
Type: Float

A fractional value between 0 and 1 representing the chance that a packet
traversing this edge will get dropped.

#### `edge.edge_bandwidth_down`

Required: False  
Default: n/a  
Type: String

Optional downstream capacity limit for traffic flowing from `source` to `target`
on this edge. The format matches other unit strings (e.g., `64 Kbit`, `10 Mbit`).

Used only when [edge bandwidth limiting](shadow_config_spec.md#experimentaledge_bandwidth_limiting_enabled)
is enabled. If unset, this hop is treated as unlimited in the source→target
direction.

#### `edge.edge_bandwidth_up`

Required: False  
Default: n/a  
Type: String

Optional upstream capacity limit for traffic flowing from `target` to `source`
on this edge. The format matches other unit strings (e.g., `64 Kbit`, `10 Mbit`).

Used only when [edge bandwidth limiting](shadow_config_spec.md#experimentaledge_bandwidth_limiting_enabled)
is enabled. If unset, this hop is treated as unlimited in the target→source
direction.
