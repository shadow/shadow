**PLEASE NOTE** - _this page is lacking information and is in progress of being updated. browse with caution!_
## running with shadow and shadow-bin

The Shadow wrapper script, `shadow`, is used to handle some environmental variables that
need to get set before running `shadow-bin`. This is particularly useful if a
plug-in is using `LD_PRELOAD` to intercept certain function calls from its
user-space application, or for properly running Shadow under valgrind. To get
usage information for the wrapper script:
```bash
shadow --usage
```

The wrapper script forwards all unhandled arguments to `shadow-bin`, which handles
most of the configuration. For `shadow-bin` usage information:
```bash
shadow --help
```

## built-in example experiments

Of potential interest are several built-in plug-ins which `shadow-bin` may run 
automatically. Use the following for help regarding the built-in plug-ins:
```bash
shadow --help-plug-ins
```
For example, `shadow --file` will run a basic file transfer experiment.

When developing and running Shadow plug-ins, one or more valid XML files 
describing the experiment must be provided. Examples of XML files can be found 
in the `resource/` directory. To run an example experiment using those files:
```bash
shadow resource/example.topology.xml resource/example.hosts.xml
```

## topology and hosts XML files

Shadow includes a pre-built topology file generated from real network
metrics gathered from large-scale planetlab experiments. This is decompressed 
during the build process and installed to `~/.shadow/share` (or `/share` in your custom prefix).

This may prevent the need to generate a synthetic topology and should make 
running experiments easier by only requiring changes to the hosts.xml file. An 
experiment using the included topology may then be run:
```bash
shadow resource/topology.xml resource/example.hosts.xml
```