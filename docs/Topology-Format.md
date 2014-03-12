## Topology Description

The topology files included in Shadow were generated from a variety of data sources, including [Net Index](http://www.netindex.com/), and [CAIDA](http://www.caida.org/). The data is represented in a standard graphml format so that it is easy to swap out specific measurements of latency, jitter, packetloss, etc, if desired. For more information, check out some [recent](http://www-users.cs.umn.edu/~jansen/papers/tormodel-cset2012.pdf) [work](https://security.cs.georgetown.edu/~msherr/papers/tor-relaystudy.pdf) on Tor network modeling.

_TODO_: describe PoIs, PoPs, the various hints and how they are used in the topology, the method for assigning hosts to network vertices, routing, etc.

## Topology Format

Shadow uses a graphml format to represent a network topology. The python module [networkx](http://networkx.github.io/) can be used to manipulate such a format.

### Vertices

All vertices must have the following attributes: _id_ and _type_. _type_ is currently one of client, relay, or server.

In addition, all _point of interest_ (poi) vertices must have the following attributes: _ip_, _geocode_, _bandwidthup_, _bandwidthdown_, _packetloss_. The _asn_ attribute is optional.

_Points of Interest_ are special vertices that represent a collection of Internet routers that are very close to each other in terms of network distance. These vertices also represent end-points in the network where virtual hosts may be attached. Shadow does this attachment using the typehint, iphint, and geocodehint as specified in the [[Shadow config format]].

### Edges

All edges must have the following attributes: _from_, _to_, _latency_, _jitter_, _packetloss_
