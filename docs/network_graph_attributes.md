### Network Graph Attributes

The [network graph overview](3.2-Network-Config.md) provides a general summary of Shadow's use of a network graph to abstractly model network position and to connect virtual hosts in a network topology while enforcing network characteristics on paths between hosts. This page describes the specific attributes that can be configured in the network graph, and the effect that each attribute has on the simulation.

### Example Graph

Below is an example of a simple network graph in the Shadow-supported GML format (note that GML calls _vertices_ as _nodes_).

```gml
graph [
  directed 0
  node [
    id 0
    label "node at 1.2.3.4"
    country_code "US"
    city_code "Portland"
    ip_address "1.2.3.4"
    bandwidth_down "100 Mbit"
    bandwidth_up "100 Mbit"
  ]
  edge [
    source 0
    target 0
    label "path from 1.2.3.4 to 1.2.3.4"
    latency 10.0
    jitter 0.0
    packet_loss 0.0
  ]
]
```

### Configurable Attributes

- [`graph.directed`](#graphdirected)
- [`vertex.id`](#vertexid)
- [`vertex.label`](#vertexlabel)
- [`vertex.country_code`](#vertexcountry_code)
- [`vertex.city_code`](#vertexcity_code)
- [`vertex.ip_address`](#vertexip_address)
- [`vertex.bandwidth_down`](#vertexbandwidth_down)
- [`vertex.bandwidth_up`](#vertexbandwidth_up)
- [`edge.source`](#edgesource)
- [`edge.target`](#edgetarget)
- [`edge.label`](#edgelabel)
- [`edge.latency`](#edgelatency)
- [`edge.jitter`](#edgejitter)
- [`edge.packet_loss`](#edgepacket_loss)

#### `graph.directed`

Required: False  
Default: `0`  
Type: Integer

Specifies the symmetry of the edges in the graph. If set to `0` (the default), the graph is an [undirected graph](https://en.wikipedia.org/wiki/Graph_(discrete_mathematics)): an edge between vertex `u` and vertex `v` is symmetric and can be used to construct a path both from `u` to `v` and from `v` to `u`. If set to `1`, the graph is a [directed graph](https://en.wikipedia.org/wiki/Directed_graph): an edge from vertex `u` to vertex `v` is assymmetric and can only be used to construct a path from `u` to `v` (a separate edge from `v` to `u` must be specified to compose a path in the reverse direction).

#### `vertex.id`

Required: True  
Type: Integer

A unique integer identifier for a given vertex.

#### `vertex.label`

Required: False  
Default: n/a  
Type: String

An optional, human-meaningful string description of the vertex. The string may be used in log messages printed by Shadow.

#### `vertex.country_code`

Required: False  
Default: n/a  
Type: String

A code for the country in which the node represented by this vertex is located. This code can be used to control the placement of hosts in the network: when attaching a specific host into the network, we ignore any vertex whose `country_code` does not match the host's [`country_code_hint` host configuration value](3.1-Shadow-Config.md#host_defaultscountry_code_hint) (if one is configured).

#### `vertex.city_code`

Required: False  
Default: n/a  
Type: String

A code for the city in which the node represented by this vertex is located. This code can be used to control the placement of hosts in the network: when attaching a specific host into the network, we ignore any vertex whose `city_code` does not match the host's [`city_code_hint` host configuration value](3.1-Shadow-Config.md#host_defaultscity_code_hint) (if one is configured).

#### `vertex.ip_address`

Required: False  
Default: n/a
Type: String

An IP address at which the node represented by this vertex is located. This address can be used to control the placement of hosts in the network: after filtering vertices based on the city and country codes (as described above), we perform a [longest prefix match](https://en.wikipedia.org/wiki/Longest_prefix_match) on the remaining vertices by comparing the vertex `ip_address` with the host's [`ip_address_hint` host configuration value](3.1-Shadow-Config.md#host_defaultsip_address_hint) (if one is configured) and attach the host to the vertex with the closest match. We assign the host the address specified in `ip_address_hint` as long as that address has not yet been assigned to another host, otherwise we choose a unique address nearby to the requested address.

#### `vertex.bandwidth_down`

Required: True  
Type: String

A string defining the downstream (receive) bandwidth that will be allowed for any host attached to this vertex. Hosts may individually override this value in [the Shadow config file](3.1-Shadow-Config.md#hostshostnamebandwidth_down). The format of the string specifies the bandwidth and its unit as described in the [config documentation](3.1-Shadow-Config.md), e.g., `10 Mbit`. Note that this bandwidth is allowed for every host that is attached to this vertex; it is **not** the total bandwidth logically available at the node (which is not defined).

#### `vertex.bandwidth_up`

Required: True  
Type: String

A string defining the upstream (send) bandwidth that will be allowed for any host attached to this vertex. Hosts may individually override this value in [the Shadow config file](3.1-Shadow-Config.md#hostshostnamebandwidth_up). The format of the string specifies the bandwidth and its unit as described in the [config documentation](3.1-Shadow-Config.md), e.g., `10 Mbit`. Note that this bandwidth is allowed for every host that is attached to this vertex; it is **not** the total bandwidth logically available at the node (which is not defined).

#### `edge.source`

Required: True  
Type: Integer

The unique integer identifier of the first of two vertices of the edge. The vertex must exist in the graph. If the graph is directed, this vertex is treated as the source or start of the edge.

#### `edge.target`

Required: True  
Type: Integer

The unique integer identifier of the second of two vertices of the edge. The vertex must exist in the graph. If the graph is directed, this vertex is treated as the target or end of the edge.

#### `edge.label`

Required: False  
Default: n/a  
Type: String

An optional, human-meaningful string description of the edge. The string may be used in log messages printed by Shadow.

#### `edge.latency`

Required: True  
Type: Float

The latency that will be added to packets traversing this edge. This value is used as a weight while running Dijkstra's shortest path algorithm.

#### `edge.jitter`

Required: False  
Default: n/a  
Type: Float

This keyword is allowed but currently nonfunctional; it is reserved for future use.

#### `edge.packet_loss`

Required: True  
Type: Float

A fractional value between 0 and 1 representing the chance that a packet traversing this edge will get dropped.
