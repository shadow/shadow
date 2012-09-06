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

The _cdf_ element instructs Shadow to either generate an empirical Cumulative Distribution Function, or load the representation of one from a file. 

If no _path_ is given, it will generate a CDF using _center_ - _width_ as the 10th percentile, _center_ as the 80th percentile, _center_ + _width_ as the 90th percentile, and _center_ + _width_ + _tail_ as the 95th percentile.

If _path_ is given, it should specify the location of a file from which Shadow should extract a CDF. The file should be in the following format:

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

```xml
<kill time="INTEGER" />
```
**Required**: _time_  

```xml
<link clusters="" latency="INTEGER" jitter="INTEGER" packetloss="FLOAT" />
```
**Required**: _clusters_, _latency_  
**Optional**: _jitter_, _packetloss_

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

```xml
<software id="STRING" plugin="STRING" time="INTEGER" arguments="STRING" />
```
**Required**: _id_, _plugin_, _time_, _arguments_  
