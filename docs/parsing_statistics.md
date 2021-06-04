# Parsing Statistics

Shadow logs simulator heartbeat messages that contain useful system information for each virtual node in the experiment. For example, Shadow logs the number of bytes sent/received, number of bytes allocated/deallocated, CPU usage, etc. You can parse these heartbeat log messages to get insight into the simulation. Details of these heartbeat messages can be found [here](log_format.md#heartbeat-messages).
 
## Generating Traffic

This example uses the [TGen traffic generator](https://github.com/shadow/tgen) to generate network traffic between hosts, and then shows results from Shadow's heartbeat messages.

TGen is capable of modeling generic behaviors with an action-dependency graph in the standard GraphML format. If you don't have it installed, you can follow the [instructions here](https://github.com/shadow/tgen/#setup). The following example runs TGen with 10 clients that each download 10 files from a server over a simple network topology.

`shadow.yaml`:

```yaml
general:
  # stop after 1 simulated hour
  stop_time: 3600

network:
  graph:
    # use a built-in network graph containing
    # a single vertex with a bandwidth of 1 Gbit
    type: 1_gbit_switch

hosts:
  # a host with the hostname 'server'
  server:
    processes:
    - path: ~/.local/bin/tgen
      args: ../../../tgen.server.graphml.xml
      start_time: 1
  # a host with the hostname 'client'
  client:
    count: 10
    processes:
    - path: ~/.local/bin/tgen
      args: ../../../tgen.client.graphml.xml
      start_time: 2
```

TGen requires an action-dependency graph for the client and server. See the [TGen documentation](https://github.com/shadow/tgen/tree/main/doc) for more information about customizing TGen behaviors.

`tgen.server.graphml.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?><graphml xmlns="http://graphml.graphdrawing.org/xmlns" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:schemaLocation="http://graphml.graphdrawing.org/xmlns http://graphml.graphdrawing.org/xmlns/1.0/graphml.xsd">
  <key attr.name="serverport" attr.type="string" for="node" id="d1" />
  <key attr.name="loglevel" attr.type="string" for="node" id="d0" />
  <graph edgedefault="directed">
    <node id="start">
      <data key="d0">info</data>
      <data key="d1">8888</data>
    </node>
  </graph>
</graphml>
```

`tgen.client.graphml.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?><graphml xmlns="http://graphml.graphdrawing.org/xmlns" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:schemaLocation="http://graphml.graphdrawing.org/xmlns http://graphml.graphdrawing.org/xmlns/1.0/graphml.xsd">
  <key attr.name="recvsize" attr.type="string" for="node" id="d5" />
  <key attr.name="sendsize" attr.type="string" for="node" id="d4" />
  <key attr.name="count" attr.type="string" for="node" id="d3" />
  <key attr.name="time" attr.type="string" for="node" id="d2" />
  <key attr.name="peers" attr.type="string" for="node" id="d1" />
  <key attr.name="loglevel" attr.type="string" for="node" id="d0" />
  <graph edgedefault="directed">
    <node id="start">
      <data key="d0">info</data>
      <data key="d1">server:8888</data>
    </node>
    <node id="pause">
      <data key="d2">1,2,3</data>
    </node>
    <node id="end">
      <data key="d3">10</data>
    </node>
    <node id="stream">
      <data key="d4">1 MiB</data>
      <data key="d5">1 MiB</data>
    </node>
    <edge source="start" target="stream" />
    <edge source="pause" target="start" />
    <edge source="end" target="pause" />
    <edge source="stream" target="end" />
  </graph>
</graphml>
```

With those three files saved in the same directory, you can start a simulation. This example may take a few minutes.

```bash
# delete any existing simulation data
rm -rf shadow.data
shadow shadow.yaml > shadow.log
```

In the TGen process output, lines containing `stream-success` represent completed downloads and contain useful timing statistics. From these lines we should see that clients have completed a total of **100** streams:

```bash
for d in shadow.data/hosts/client*; do grep "stream-success" ${d}/* ; done | tee clients.log | wc -l
```

We can also look at the transfers from the servers' perspective:

```bash
for d in shadow.data/hosts/server*; do grep "stream-success" ${d}/* ; done | tee servers.log | wc -l
```

## Parsing and Plotting Results

Shadow includes some Python scripts that can parse important statistics from the Shadow log messages, including network throughput over time, client download statistics, and client load statistics, and then visualize the results. The following will parse and plot the output produced from the above experiment:

```bash
# parse the shadow output file
src/tools/parse-shadow.py --help
src/tools/parse-shadow.py --prefix results shadow.log
# plot the results
src/tools/plot-shadow.py --help
src/tools/plot-shadow.py --data results "example-plots"
```

The `parse-*.py` scripts generate `stats.*.json.xz` files. The (heavily trimmed) contents of `stats.shadow.json` look like the following:

```json
$ xzcat results/stats.shadow.json.xz
{
  "nodes": {
    "client:11.0.0.1": {
      "recv": {
        "bytes_control_header": {
          "0": 0,
          "1": 0,
          "2": 0,
          ...
          "3599": 0
        },
        "bytes_control_header_retrans": { ... },
        "bytes_data_header": { ... },
        "bytes_data_header_retrans": { ... },
        "bytes_data_payload": { ... },
        "bytes_data_payload_retrans": { ... },
        "bytes_total": { ... }
      },
      "send": { ... }
    },
    "server:11.0.0.2": { ... }
  },
  "ticks": {
    "2": {
      "maxrss_gib": 0.162216,
      "time_seconds": 0.070114
    },
    "3": { ... },
    ...
    "3599": { ... }
  }
}
```

The `plot-*.py` scripts generate graphs. Open the PDF file that was created to see the graphed results.

You can also parse and plot the TGen output using the `tgentools` program from the TGen repo. [This page](https://github.com/shadow/tgen/blob/main/doc/Tools-Setup.md) describes how to get started.

### Combining Simulation Data

Consider a set of experiments where we would like to analyze the effect of changing our nodes' traffic queueing disciplines. We run the following 2 experiments:

```bash
# delete any existing simulation data and post-processing
rm -rf shadow.data shadow.log qdisc-fifo.data qdisc-fifo.log qdisc-rr.data qdisc-rr.log
shadow --interface-qdisc fifo       --data-directory qdisc-fifo.data shadow.yaml > qdisc-fifo.log
shadow --interface-qdisc roundrobin --data-directory qdisc-rr.data   shadow.yaml > qdisc-rr.log
```

To parse these log files, we use the following scripts:

```bash
src/tools/parse-shadow.py --prefix=qdisc-fifo.results qdisc-fifo.log
src/tools/parse-shadow.py --prefix=qdisc-rr.results   qdisc-rr.log
```

Each of the directories `qdisc-fifo.results/` and `qdisc-rr.results/` now contain data statistics files extracted from the log files. We can now combine and visualize these results with the `plot-shadow.py` script:

```bash
src/tools/plot-shadow.py --prefix "qdisc" --data qdisc-fifo.results/ "fifo" --data qdisc-rr.results/ "round-robin"
```

Open the PDF file that was created to compare results from the experiments.
