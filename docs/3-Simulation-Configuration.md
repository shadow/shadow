## Shadow Application Configuration

Shadow uses a standard XML format to accept configuration options from users, and uses [GLib's XML parser](https://developer.gnome.org/glib/stable/glib-Simple-XML-Subset-Parser.html) to parse the simple structure. Examples of a Shadow configuration file, `shadow.config.xml`, are distributed with the release. The following describes valid XML elements and attributes that Shadow will accept from a `shadow.config.xml` file and that can be used to customize a simulation.

### The _kill_ element
```xml
<kill time="INTEGER" />
```
**Required attribute**: _time_  

The _time_ attribute represents the number of virtual seconds to simulate, after which the experiment will be killed and resources released.

### The _plugin_ element
```xml
<plugin id="STRING" path="STRING" />
```
**Required attributes**: _id_, _path_  

The _plugin_ element represents a library plug-in that Shadow should dynamically load. The _id_ attribute identifies this _plugin_ and must be a string that is unique among all _id_ attributes for any element in the XML file. 

The _path_ attribute holds the system path to the plug-in `*.so` library. If _path_ begins with `~/`, the path will be considered relative to the current user's home directory.

### The _topology_ element
```xml
<topology path="STRING" />
```
**Required attributes**:  
**Optional attributes**: _path_  
**Optional child element**: `<![CDATA[TEXT]]>`

The _topology_ element must hold either a collection of vertices and edges that represent a network topology as the TEXT in a `<![CDATA[TEXT]]>` style element, or a _path_ to a file holding such data. The TEXT data should be in graphml format, and hold an undirected, complete, connected graph with certain attributes specified on the vertices and links. For more information on how to structure this data, see the [Topology Format](#Network-Topology-Configuration).

### The _node_ element
```xml
<node id="STRING" iphint="STRING" geocodehint="STRING" typehint="STRING" quantity="INTEGER" bandwidthdown="INTEGER" bandwidthup="INTEGER" interfacebuffer="INTEGER" socketrecvbuffer="INTEGER" socketsendbuffer="INTEGER" loglevel="STRING" heartbeatloglevel="STRING" heartbeatloginfo="STRING" heartbeatfrequency="INTEGER" cpufrequency="INTEGER" logpcap="STRING" pcapdir="STRING">
  <application ... />
  ...
</node>
```
**Required attributes**: _id_  
**Optional attributes**: _iphint_, _geocodehint_, _typehint_, _quantity_, _bandwidthdown_, _bandwidthup_, _interfacebuffer_, _socketrecvbuffer_, _socketsendbuffer_, _loglevel_, _heartbeatloglevel_, _heartbeatloginfo_, _heartbeatfrequency_, _cpufrequency_, _logpcap_, _pcapdir_  
**Required child element**: \<application\>  

The _node_ element represents a node or virtual host in the simulation. The _id_ attribute identifies this _node_ and must be a string that is unique among all _id_ attributes for any element in the XML file. _id_ will also be used as the network hostname of this _node_.

The _iphint_, _geocodehint_, and _typehint_ attributes provide hints to Shadow's name and routing system on where to attach this host to the topology. These hints will be matched based on the values of the _ip_, _geocode_, and _type_ of the vertices as specified in the topology file.

The _quantity_ attribute specifies the number of hosts of this type to start. If _quantity_ is greater than 1, each host's hostname will be prefixed with a counter. For example, a _node_ with an _id_ of `host` and _quantity_=2 would produce nodes with hostnames `1.host` and `2.host`.

_bandwidthdown_ and _bandwidthup_ optionally specify the downstream and upstream bandwidth capacities for this _node_, and override any default bandwidth values set in the _cluster_ element corresponding to the _cluster_ attribute. If not given, the default bandwidth values from the assigned _cluster_ element are used.

_interfacebuffer_ controls the size of the interface receive buffer that accepts packets from the network. _socketrecvbuffer_ and _socketsendbuffer_ control the initial size of the socket buffers that hold packets to and from the application. Note that these sizes may be adjusted by auto-tuning, in order fill the channel capacity as defined by the bandwidth-delay product between two nodes. These values can instead be set globally for all nodes with the Shadow command line options `--interface-buffer`, `--socket-recv-buffer`, and `--socket-send-buffer` (see `shadow --help-network` for more info).

_loglevel_ and _heartbeatloglevel_ are node-specific overrides for the simulator default log levels (the defaults are adjustable with shadow arguments `--log-level` and `--heartbeat-log-level`). Valid strings include 'error', 'critical', 'warning', 'message', 'info', and 'debug'. _heartbeatloginfo_ is a node-specific override for the type of information that will get logged for this node during the heartbeat. Valid values are 'node', 'socket', and 'ram'. _heartbeatfrequency_ is a node-specific override for the default number of seconds between which heartbeat messages are logged (the default is adjustable with shadow argument `--heartbeat-frequency`). Each heartbeat message contains useful statistics about the _node_.

_cpufrequency_ is the speed of this _node's_ virtual CPU in kilohertz. Along with the CPU processing requirements of the plug-in application, this determines how often events for this _node_ are delayed during simulation.

_logpcap_ is a case insenstive boolean string (e.g. "true") that specifies that Shadow should log all network input and output for this _node_ in PCAP format (for viewing in e.g. wireshark). _pcapdir_ is the directory to which the logs should be saved for this _node_.

Nodes must have at least one child \<application\> (see below), and may have more than one.

### The _application_ element
```xml
<application plugin="STRING" starttime="INTEGER" stoptime="INTEGER" arguments="STRING" />
```
**Required attributes**: _plugin_, _starttime_, _arguments_  
**Optional attributes**: _stoptime_  
**Required parent element**: \<node\>

The _application_ element represents an application the node will run. The _plugin_ attribute should be set to the _id_ of the _plugin_ element that represents the plug-in that should be used to launch this application at _starttime_ virtual seconds from the beginning of the simulation. The application will be stopped at _stoptime_ virtual seconds if given.

The _arguments_ attribute should be set to a string holding the required plug-in arguments. This string will be passed to the plug-in in an `argv`-style array, similar to how arguments are passed to the main function in a `C` program. Please see the plug-in documentation for usage and format of the argument string.

## Network Topology Configuration

Shadow uses a graphml format to represent a network topology. The python module [networkx](http://networkx.github.io/) can be used to manipulate such a format. Shadow is distributed with some graphml topology files that were generated using data from a variety of sources, including [Net Index](http://www.netindex.com/), and [CAIDA](http://www.caida.org/). Because the topology is represented in a standard graphml format, it is easy to swap out specific measurements of latency, jitter, packetloss, etc, if desired.

For more information on modeling topologies, check out [some](http://www.robgjansen.com/publications/tormodel-cset2012.pdf) [recent](https://security.cs.georgetown.edu/~msherr/papers/tor-relaystudy.pdf) [work](http://www.robgjansen.com/publications/kist-sec2014.pdf) on Tor network modeling.

### Vertices

All vertices must have the following explicit attributes (in addition to the default _id_ attribute): _type_. _type_ is currently one of client, relay, or server, and is used to help determine where to attach virtual hosts to the topology.

In addition, all _point of interest_ (poi) vertices must have the following attributes: _ip_, _geocode_, _bandwidthup_, _bandwidthdown_, _packetloss_. The _asn_ attribute is optional.

_Points of Interest_ are special vertices that represent a collection of Internet routers that are very close to each other in terms of network distance. These vertices also represent end-points in the network where virtual hosts may be attached. Shadow does this attachment using the _typehint_, _iphint_, and _geocodehint_ attributes to the _node_ element as specified in a [Shadow config file](#Shadow-Application-Configuration). Hosts are always attached to the closest match to the best known location following the hinted restrictions.

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