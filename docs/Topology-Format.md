## Topology Description

The topology files included in Shadow were generated from a variety of data sources, including [Net Index](http://www.netindex.com/), and [CAIDA](http://www.caida.org/). The data is represented in a standard graphml format so that it is easy to swap out specific measurements of latency, jitter, packetloss, etc, if desired. For more information, check out some [recent](http://www-users.cs.umn.edu/~jansen/papers/tormodel-cset2012.pdf) [work](https://security.cs.georgetown.edu/~msherr/papers/tor-relaystudy.pdf) on Tor network modeling.

_TODO_: describe PoIs, PoPs, the various hints and how they are used in the topology, the method for assigning hosts to network vertices, routing, etc.

## Topology Format

Shadow uses a graphml format to represent a network topology. The python module [networkx](http://networkx.github.io/) can be used to manipulate such a format.

### Vertices

All vertices must have the following explicit attributes (in addition to the default _id_ attribute): _type_. _type_ is currently one of client, relay, or server, and is used to help determine where to attach virtual hosts to the topology.

In addition, all _point of interest_ (poi) vertices must have the following attributes: _ip_, _geocode_, _bandwidthup_, _bandwidthdown_, _packetloss_. The _asn_ attribute is optional.

_Points of Interest_ are special vertices that represent a collection of Internet routers that are very close to each other in terms of network distance. These vertices also represent end-points in the network where virtual hosts may be attached. Shadow does this attachment using the _typehint_, _iphint_, and _geocodehint_ attributes to the _node_ element as specified in the [[Shadow config format]]. Hosts are always attached to the closest match to the best known location following the hinted restrictions.

### Edges

All edges must have the following explicit attributes (in addition to the default _source_ and _target_ attributes): _latency_, _jitter_, _packetloss_. These are used when computing paths and routing packets between virtual hosts.

### Routing

If the topology is a complete graph, Shadow uses the single link between each vertex as the path. Otherwise, a routing path is approximated using Dijkstra's shortest path algorithm.

### Example

The following is an example of a properly-formed graphml file for Shadow:

```xml
<?xml version="1.0" encoding="utf-8"?><graphml xmlns="http://graphml.graphdrawing.org/xmlns" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:schemaLocation="http://graphml.graphdrawing.org/xmlns http://graphml.graphdrawing.org/xmlns/1.0/graphml.xsd">
  <key attr.name="packetloss" attr.type="double" for="edge" id="d9" />
  <key attr.name="jitter" attr.type="double" for="edge" id="d8" />
  <key attr.name="latency" attr.type="double" for="edge" id="d7" />
  <key attr.name="asn" attr.type="int" for="node" id="d6" />
  <key attr.name="type" attr.type="string" for="node" id="d5" />
  <key attr.name="bandwidthup" attr.type="int" for="node" id="d4" />
  <key attr.name="bandwidthdown" attr.type="int" for="node" id="d3" />
  <key attr.name="geocode" attr.type="string" for="node" id="d2" />
  <key attr.name="ip" attr.type="string" for="node" id="d1" />
  <key attr.name="packetloss" attr.type="double" for="node" id="d0" />
  <graph edgedefault="undirected">
    <node id="poi-1">
      <data key="d0">0.0</data>
      <data key="d1">0.0.0.0</data>
      <data key="d2">US</data>
      <data key="d3">10240</data>
      <data key="d4">10240</data>
      <data key="d5">net</data>
      <data key="d6">0</data>
    </node>
    <edge source="poi-1" target="poi-1">
      <data key="d7">50.0</data>
      <data key="d8">0.0</data>
      <data key="d9">0.0</data>
    </edge>
  </graph>
</graphml>
```
