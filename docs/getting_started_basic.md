# Basic File Transfer Example

Here we present a basic example that simulates the network traffic of an HTTP
server with 3 clients, each running on different virtual hosts. If you do not
have Python or cURL installed, you can download them through your distribution's
package manager.

## Configuring the Simulation

Each client uses cURL to make an HTTP request to a basic Python HTTP server.

Shadow requires a configuration file that specifies information about the
network graph and the processes to run within the simulation. This example uses
a [built-in network graph](shadow_config_spec.md#networkgraphtype) for
simplicity.

`shadow.yaml`:

```yaml
{{#include ../examples/docs/basic-file-transfer/shadow.yaml}}
```

## Running the Simulation

Shadow stores simulation data to the `shadow.data/` directory by default. We
first remove this directory if it already exists, and then run Shadow.

```bash
{{#include ../examples/docs/basic-file-transfer/run.sh:body}}
```

This small Shadow simulation should complete almost immediately.

## Viewing the Simulation Output

Shadow will write simulation output to the data directory `shadow.data/`. Each
host has its own directory under `shadow.data/hosts/`. For example:

```bash
$ {{#include ../examples/docs/basic-file-transfer/show.sh:body_1}}
{{#include ../examples/docs/basic-file-transfer/show.sh:output_1}}
```

Each host directory contains the output for each process running on that host.
For example:

```bash
$ {{#include ../examples/docs/basic-file-transfer/show.sh:body_2}}
{{#include ../examples/docs/basic-file-transfer/show.sh:output_2}}

$ {{#include ../examples/docs/basic-file-transfer/show.sh:body_3}}
{{#include ../examples/docs/basic-file-transfer/show.sh:output_3}}
```
