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
{{#include ../examples/docs/traffic-generation/shadow.yaml}}
```

We can see that Shadow will be running 6 processes in total, and that those
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
{{#include ../examples/docs/traffic-generation/tgen.server.graphml.xml}}
```

#### Our TGen Clients

The client config specifies that we connect to the server using its name and
port `server:8888`, and that we download and upload `1 MiB` 10 times, pausing 1,
2, or 3 seconds between each transfer.

`tgen.client.graphml.xml`:

```xml
{{#include ../examples/docs/traffic-generation/tgen.client.graphml.xml}}
```

### Running the Simulation

With the above three files saved in the same directory, you can start a
simulation. Shadow stores simulation data to the `shadow.data/` directory by
default. We first remove this directory if it already exists, and then run
Shadow. This example may take a few minutes.

```bash
{{#include ../examples/docs/traffic-generation/run.sh:body}}
```

### Simulation Output

Shadow will write simulation output to the data directory `shadow.data/`. Each
host has its own directory under `shadow.data/hosts/`.

In the TGen process output, lines containing `stream-success` represent
completed downloads and contain useful timing statistics. From these lines we
should see that clients have completed a total of **50** streams:

```text
$ {{#include ../examples/docs/traffic-generation/show.sh:body_1}}
{{#include ../examples/docs/traffic-generation/show.sh:output_1}}
```

We can also look at the transfers from the servers' perspective:

```text
$ {{#include ../examples/docs/traffic-generation/show.sh:body_2}}
{{#include ../examples/docs/traffic-generation/show.sh:output_2}}
```

You can also parse the TGen output logged to the stdout files using the
`tgentools` program from the TGen repo, and plot the data in graphical format to
visualize the performance characteristics of the transfers. [This
page](https://github.com/shadow/tgen/blob/main/tools/README.md) describes how
to get started.
