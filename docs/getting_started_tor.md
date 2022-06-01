# Simple Tor Network Example

_We recommend getting started with the [basic file
transfer](getting_started_basic.md) and [traffic
generation](getting_started_tgen.md) examples to orient yourself with Shadow
before running this slightly more complex Tor simulation._

This example requires that you have installed (or linked) a Tor executable in
`~/.local/bin/tor` (see [the Tor install
README](https://github.com/torproject/tor/blob/main/README)). You also need to
install (or link) a TGen executable in `~/.local/bin/tgen` (see [the TGen
installation guide](https://github.com/shadow/tgen)).

Once Shadow, Tor, and TGen are installed, you can quickly get started running a
very simple Tor network:

```bash
cd shadow/src/test/tor/minimal
./run.sh
./verify.sh
```

The [`run.sh` script](../src/test/tor/minimal/run.sh) launches Shadow with a
config that runs a minimal Tor network. The [`verify.sh`
script](../src/test/tor/minimal/verify.sh) checks that all Tor processes
bootstrapped correctly and that all TGen file transfer attempts succeeded. Note
that these steps can also be launched as a test case using `./setup test --
--build-config extra --label tor`.

After the experiment, have a look in the `shadow.data/host/*` directories to
inspect the individual log files from the Tor relays and TGen clients.

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
