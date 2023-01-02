# Traffic Generation Example

_We recommend getting started with the [basic file
transfer](getting_started_basic.md) before running this example. It contains
some basics about running Shadow simulations that are not covered here._

During Shadow simulations, it is often useful to generate background traffic
flows between your simulated hosts. This example uses the [TGen traffic
generator](https://github.com/shadow/tgen) for this purpose.

TGen is capable of generating basic file transfers, where you can configure how
much data is transferred in each direction, how long to wait in between each
transfer, and how many transfers to perform. TGen also supports more complex
behavior models: you can use Markov models to configure a state machine with
precise inter-packet timing characteristics. We only make use of its basic
features in this example.

If you don't have it installed, you can follow the [instructions
here](https://github.com/shadow/tgen/#setup). The following example runs TGen
with 10 clients that each download 10 files from a server over a simple network
graph.

## A Shadow Simulation using TGen

The following examples simulates a network with 1 TGen server and 10 TGen clients
that are generating TCP traffic to and from the server.

### Configuring Shadow

The `shadow.yaml` file instructs Shadow how to model the network that is used to
carry the traffic between the hosts, and about the bandwidth available to each
of the hosts. It also specifies how many processes to run in the simulation, and
the configuration options for those applications.

`shadow.yaml`:

```yaml
general:
  stop_time: 10m
  # Needed to avoid deadlock in some configurations of tgen.
  # See below.
  model_unblocked_syscall_latency: true

network:
  graph:
    # a custom single-node graph
    type: gml
    inline: |
      graph [
        node [
          id 0
          host_bandwidth_down "140 Mbit"
          host_bandwidth_up "18 Mbit"
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
    network_node_id: 0
    processes:
    # Assumes `tgen` is on your shell's `PATH`.
    # Otherwise use an absolute path here.
    - path: tgen
      # The ../../../ prefix assumes that tgen.server.graph.xml in the same
      # directory as the data directory (specified with the -d CLI argument).
      # See notes below explaining Shadow's directory structure.
      args: ../../../tgen.server.graphml.xml
      start_time: 1s
  client:
    network_node_id: 0
    quantity: 10
    processes:
    - path: tgen
      args: ../../../tgen.client.graphml.xml
      start_time: 2s
```

We can see that Shadow will be running 11 processes in total, and that those
processes are configured using `graphml.xml` files (the configuration file
format for TGen) as arguments.

Each host directory is also the [working
directory](https://en.wikipedia.org/wiki/Working_directory) for the host's
processes, which is why we specified `../../../tgen.server.graphml.xml` as the
path to the TGen configuration in our Shadow configuration file
(`./shadow.data/hosts/server/../../../tgen.server.graphml.xml` →
`./tgen.server.graphml.xml`). The host directory structure is *stable*---it is
guaranteed not to change between minor releases, so the `../../../` prefix may
reliably be used to refer to files in the same directory as the data directory.

`model_unblocked_syscall_latency` is used to avoid deadlock in case tgen was
compiled with [libopenblas](compatibility_notes.md#libopenblas).

### Configuring TGen

Each TGen process requires an action-dependency graph in order to configure the
behavior of the clients and server. See the [TGen
documentation](https://github.com/shadow/tgen/tree/main/doc) for more
information about customizing TGen behaviors.

#### Our TGen Server

The main configuration here is the port number on which the server will listen.

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

#### Our TGen Clients

The client config specifies that we connect to the server using its name and
port `server:8888`, and that we download and upload `1 MiB` 10 times, pausing 1,
2, or 3 seconds between each transfer.

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

### Running the Simulation

With the above three files saved in the same directory, you can start a
simulation. Shadow stores simulation data to the `shadow.data/` directory by
default. We first remove this directory if it already exists, and then run
Shadow. This example may take a few minutes.

```bash
# delete any existing simulation data
rm -rf shadow.data/
shadow shadow.yaml > shadow.log
```

### Simulation Output

Shadow will write simulation output to the data directory `shadow.data/`. Each
host has its own directory under `shadow.data/hosts/`.

In the TGen process output, lines containing `stream-success` represent
completed downloads and contain useful timing statistics. From these lines we
should see that clients have completed a total of **100** streams:

```bash
for d in shadow.data/hosts/client*; do grep "stream-success" ${d}/*.stdout ; done | wc -l
```

We can also look at the transfers from the servers' perspective:

```bash
for d in shadow.data/hosts/server*; do grep "stream-success" ${d}/*.stdout ; done | wc -l
```

You can also parse the TGen output logged to the stdout files using the
`tgentools` program from the TGen repo, and plot the data in graphical format to
visualize the performance characteristics of the transfers. [This
page](https://github.com/shadow/tgen/blob/main/doc/Tools-Setup.md) describes how
to get started.
