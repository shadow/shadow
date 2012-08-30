**NOTE** - _this page is currently incomplete, its content is here for historical reasons_

## Topology File Format

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
