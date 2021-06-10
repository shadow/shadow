# Parsing Statistics

Shadow logs simulator heartbeat messages that contain useful system information for each virtual node in the experiment. For example, Shadow logs the number of bytes sent/received, number of bytes allocated/deallocated, CPU usage, etc. You can parse these heartbeat log messages to get insight into the simulation. Details of these heartbeat messages can be found [here](log_format.md#heartbeat-messages).
 
## Generating Traffic

This example uses the [TGen traffic generator](https://github.com/shadow/tgen) to generate network traffic between hosts, and then shows results from Shadow's heartbeat messages.

TGen is capable of modeling generic behaviors with an action-dependency graph in the standard GraphML format. If you don't have it installed, you can follow the [instructions here](https://github.com/shadow/tgen/#setup). The following example runs TGen with 10 clients that each download 10 files from a server over a simple network topology.

`shadow.yaml`:

```yaml
general:
  stop_time: 10m

network:
  graph:
    # a custom single-node topology
    type: gml
    inline: |
      graph [
        node [
          id 0
          bandwidth_down "140 Mbit"
          bandwidth_up "18 Mbit"
        ]
        edge [
          source 0
          target 0
          latency "50 ms"
          packet_loss 0.01
        ]
      ]
hosts:
  server:
    processes:
    - path: ~/.local/bin/tgen
      args: ../../../tgen.server.graphml.xml
      start_time: 1s
  client:
    quantity: 10
    processes:
    - path: ~/.local/bin/tgen
      args: ../../../tgen.client.graphml.xml
      start_time: 2s
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
for d in shadow.data/hosts/client*; do grep "stream-success" ${d}/*.stdout ; done | wc -l
```

We can also look at the transfers from the servers' perspective:

```bash
for d in shadow.data/hosts/server*; do grep "stream-success" ${d}/*.stdout ; done | wc -l
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

```text
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
          "599": 0
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
    "599": { ... }
  }
}
```

The `plot-*.py` scripts generate graphs. Open the PDF file that was created to see the graphed results.

You can also parse and plot the TGen output using the `tgentools` program from the TGen repo. [This page](https://github.com/shadow/tgen/blob/main/doc/Tools-Setup.md) describes how to get started.

### Combining Simulation Data

Consider a set of experiments where we would like to analyze the effect of changing our nodes' socket receive buffer sizes. We run the following 2 experiments:

```bash
# delete any existing simulation data and post-processing
rm -rf shadow.{data,log} 10KiB.{data,results,log} 100KiB.{data,results,log} *.results.pdf
shadow --socket-recv-buffer  10KiB --socket-recv-autotune false \
       --data-directory  10KiB.data shadow.yaml >  10KiB.log
shadow --socket-recv-buffer 100KiB --socket-recv-autotune false \
       --data-directory 100KiB.data shadow.yaml > 100KiB.log
```

To parse these log files, we use the following scripts:

```bash
src/tools/parse-shadow.py --prefix=10KiB.results   10KiB.log
src/tools/parse-shadow.py --prefix=100KiB.results 100KiB.log
```

Each of the directories `10KiB.results/` and `100KiB.results/` now contain data statistics files extracted from the log files. We can now combine and visualize these results with the `plot-shadow.py` script:

```bash
src/tools/plot-shadow.py --prefix "recv-buffer" --data 10KiB.results/ "10 KiB" --data 100KiB.results/ "100 KiB"
```

Open the PDF file that was created to compare results from the experiments.
