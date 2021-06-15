# Getting Started Tor

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
shadow --template-directory shadow.data.template tor-minimal.yaml > shadow.log
./verify.sh
```

After the experiment, have a look in the `shadow.data/host/*` directories to
inspect the individual log files from the Tor relays and TGen clients.

You can use the [tornettools
toolkit](https://github.com/shadow/tornettools) to run larger, more
complex Tor networks that are meant to more accurately resemble the
characteristics and state of the public Tor network.
