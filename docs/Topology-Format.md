**NOTE** - _this page is currently in progress - the description is out of date_

## Topology Description

The topology was generated from the PlanetLab experiments from the Shadow design
paper. The format is general enough to easily swap out specific measurements of 
latency, jitter, packetloss, etc, once we have a better data source without
changing the format. There are two main elements:

```xml
<cluster id="BGBG" bandwidthdown="22429" bandwidthup="11805"/>
```

For US and CA, the cluster ID is the two letter country code followed by
the two letter state/territory code. For other countries, the two letter
country code is given twice. This gives a unique ID for each cluster.
Bandwidth down and up is taken from netindex data.

```xml
<link id="link1" clusters="ARAR BGBG" latency="145" jitter="13" packetloss="0.026"/>
```

Links are directed, connecting the given clusters. The link ID is
meaningless other than to provide uniqueness. Latency was computed as
the median of our measurements from inter-node planetlab pings between
the given clusters. Packetloss was taken from netindex data. Jitter was
computed as the average difference of the first and third quartiles from
the median, i.e.:

```python
q1, latency, q3 = getQuartiles()
jitter = ((latency-q1) + (q3-latency)) / 2.0
if jitter >= latency: jitter = min(latency-q1, q3-latency)
assert jitter < latency
```

I believe this covers every country/state for which there is a code.

## Topology Format

The following are valid elements and their attributes:

```xml
<cdf id="STRING" path="STRING" center="INTEGER" width="INTEGER" tail="INTEGER" />
```
**Required**: _id_, _path_ or _center_  
**Optional**: _width_, _tail_

The _cdf_ element instructs Shadow to either generate an empirical Cumulative Distribution Function, or load the representation of one from a file. The _id_ attribute must be a string that is unique among all _id_ attributes for any element in the XML file.

If no _path_ is given, it will generate a CDF using _center_ - _width_ as the 10th percentile, _center_ as the 80th percentile, _center_ + _width_ as the 90th percentile, and _center_ + _width_ + _tail_ as the 95th percentile.

If _path_ is given, it should specify the location of a file from which Shadow should extract a CDF. If _path_ begins with `~/`, the path will be considered relative to the current user's home directory. The file should be in the following format:

```text
1000.000 0.2000000000
2000.000 0.4000000000
3000.000 0.6000000000
4000.000 0.8000000000
5000.000 1.0000000000
```

Where the first column represents the value, and the second represents the percentile.

```xml
<cluster id="STRING" bandwidthdown="INTEGER" bandwidthup="INTEGER" packetloss="FLOAT" />
```
**Required**: _id_, _bandwidthdown_, _bandwidthup_  
**Optional**: _packetloss_

The _cluster_ element represents a vertex in the network topology, i.e., the entity to which we will attach virtual hosts. The _id_ attribute must be a string that is unique among all _id_ attributes for any element in the XML file. _bandwidthdown_ and _bandwidthup_ represent the default downstream and upstream bandwidth capacities for hosts attached to this cluster. The default values are used for _nodes_ that do not supply their own bandwidth overrides. The _packetloss_ attribute is optional and represents the percentage chance that any given packet traveling through this cluster will be lost (independent of _link_ _packetloss_ rates, i.e., there are 3 end-to-end chances to drop a packet).

```xml
<kill time="INTEGER" />
```
**Required**: _time_  

The _time_ attribute represents the number of virtual seconds to simulate, after which the experiment will be killed and resources released.

```xml
<link clusters="STRING STRING" latency="INTEGER" jitter="INTEGER" packetloss="FLOAT" />
```
**Required**: _clusters_, _latency_  
**Optional**: _jitter_, _packetloss_

The _link_ element represents an edge in the network topology, and is used to connect two _clusters_. The _clusters_ attribute is a string that specifies the _ids_ of the two clusters the link is connecting, separated by a space. So, to connect `<cluster id="c1" ...` and `<cluster id="c2" ...`, you would set `clusters="c1 c2"`. The packet delay across this _link_ in is specified with the _latency_ attribute, and the average variation in packet delay is specified with the _jitter_ attribute. Both _latency_ and _jitter_ are specified in milliseconds. The _packetloss_ attribute is optional and represents the percentage chance that any given packet traveling through this link will be lost (independent of _cluster_ _packetloss_ rates, i.e., there are 3 end-to-end chances to drop a packet).

```xml
<node id="STRING" software="STRING" clusters="STRING" quantity="INTEGER" bandwidthdown="INTEGER" bandwidthup="INTEGER" loglevel="STRING" heartbeatloglevel="STRING" heartbeatfrequency="INTEGER" cpufrequency="INTEGER" logpcap="STRING" pcapdir="STRING" />
```
**Required**: _id_, _software_  
**Optional**: _clusters_, _quantity_, _bandwidthdown_, _bandwidthup_, _loglevel_, _heartbeatloglevel_, _heartbeatfrequency_, _cpufrequency_, _logpcap_, _pcapdir_

logpcap is a case insenstive boolean string (e.h. "true")

```xml
<plugin id="STRING" path="STRING" />
```
**Required**: _id_, _path_  

The _plugin_ element represents a library plug-in that Shadow should dynamically load. The _id_ attribute must be a string that is unique among all _id_ attributes for any element in the XML file. The _path_ attribute holds the system path to the plug-in `*.so` library. If _path_ begins with `~/`, the path will be considered relative to the current user's home directory.

```xml
<software id="STRING" plugin="STRING" time="INTEGER" arguments="STRING" />
```
**Required**: _id_, _plugin_, _time_, _arguments_  