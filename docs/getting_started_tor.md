# Simple Tor Network Example

_We recommend getting started with the [basic file
transfer](getting_started_basic.md) and [traffic
generation](getting_started_tgen.md) examples to orient yourself with Shadow
before running this slightly more complex Tor simulation._

This example requires that you have installed:
* [`tor`](https://github.com/torproject/tor/blob/main/README); can typically be installed
via your system package manager.
* [`tgen`](https://github.com/shadow/tgen); will most likely need to be built from source.

## Configuring Shadow

This simulation again uses `tgen` as both client and server. In addition to a
`tor`-oblivious client and server, we add a `tor` network and a client that uses
`tor` to connect to the server.

`shadow.yaml`:

```yaml
{{#include ../examples/docs/tor/shadow.yaml}}
```

## Running the Simulation

We run this example similarly as before. Here we use an additional command-line
flag `--template-directory` to copy a template directory layout containing each
host's `tor` configuraton files into its host directory before the simulation
begins.

For brevity we omit the contents of our template directory, and configuration files that are referenced from it, but you can find them at [`examples/docs/tor/shadow.data.template/`](https://github.com/shadow/shadow/blob/main/examples/docs/tor/shadow.data.template) and [`examples/docs/tor/conf/`](https://github.com/shadow/shadow/blob/main/examples/docs/tor/conf).

```bash
{{#include ../examples/docs/tor/run.sh:body}}
```

## Simulation Output

As before, Shadow will write simulation output to the data directory
`shadow.data/`. Each host has its own directory under `shadow.data/hosts/`.

In the TGen process output, lines containing `stream-success` represent
completed downloads and contain useful timing statistics. From these lines we
should see that clients have completed a total of **20** streams:

```text
$ {{#include ../examples/docs/tor/show.sh:body_1}}
{{#include ../examples/docs/tor/show.sh:output_1}}
```

We can also look at the transfers from the servers' perspective:

```text
$ {{#include ../examples/docs/tor/show.sh:body_2}}
{{#include ../examples/docs/tor/show.sh:output_2}}
```

You can also parse the TGen output logged to the stdout files using the
`tgentools` program from the TGen repo, and plot the data in graphical format to
visualize the performance characteristics of the transfers. [This
page](https://github.com/shadow/tgen/blob/main/doc/Tools-Setup.md) describes how
to get started.

## More Realistic Simulations

You can use the [tornettools
toolkit](https://github.com/shadow/tornettools) to run larger, more
complex Tor networks that are meant to more accurately resemble the
characteristics and state of the public Tor network.

## Determinism

To improve determinism for your simulation, Shadow preloads an auxiliary
library, libshadow\_openssl\_rng, which override's some of openssl's RNG
routines. This is enabled by default, but can be controlled using the
experimental
[use\_openssl\_rng\_preload](shadow_config_spec.md#experimentaluse_openssl_rng_preload)
option.
