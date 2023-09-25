# Running Shadow

When installing Shadow, the main executable was placed in `/bin` in your install
prefix (`~/.local/bin` by default). As a reminder, it would be helpful if this
location was included in your environment `PATH`.

The main Shadow binary executable, `shadow`, contains most of the simulator's
code, including events and the event engine, the network stack, and the routing
logic. Shadow's event engine supports multi-threading using the `-p` or
`--parallelism` flags (or their corresponding [configuration file
option](shadow_config_spec.md#generalparallelism)) to simulate multiple hosts
in parallel.

In the following sections we provide some examples to help you get started, but
Shadow's configuration format is entirely specified in the ["Shadow Config
Specification"](shadow_config_spec.md) and ["Network Graph
Specification"](network_graph_spec.md) documents. You will find these useful
once you begin writing your own simulations.
