So you've got Shadow installed and your machine configured. Its time to see what Shadow can do!

## logistical crash course

Before getting started, there are a couple of important points to be aware of. When installing Shadow, the following two main executables were placed in `/bin` in your install prefix (`~/.shadow/bin` by default). As a reminder, it would be helpful if this location was included in your environment `PATH`.

`shadow-bin` is the main Shadow binary. It contains most of the simulator's code, including events and the event engine, the network stack, and the routing topology configuration.

`shadow` is a python wrapper to `shadow-bin` and is used to set some environmental variables that are required to run the simulator. In particular, it sets `LD_PRELOAD` to our custom _function interposition_ library that makes it possible to intercept real operating system functions and manage them in the simulation environment. The `shadow` script also assists with running `valgrind`, mostly for debugging and development purposes.

For our purposes, we need only concern ourselves with the `shadow` script, as all unknown arguments are passed to `shadow-bin`. For more information:

```bash
shadow --usage
shadow --help
```

## performing some basic functional tests

Shadow provides a virtual system and network that are used by plug-in applications. Fortunately, Shadow already contains several application plug-ins so you can get started without writing your own. Basic functionalities are tested using some static configurations of these plug-ins that are included in `shadow-bin`. Let's start by running those and make sure thing are working as they should.

### file transfer

The `filetransfer` plug-in built-in example will set up 1 file server that will serve `/bin/ls` to 1000 clients 10 times each (for a total of 10,000 transfers). This example will take a few minutes, and you probably want to redirect the output to a log file:

```bash
shadow --file > filetest.log
```

Now you can browse through filetest.log to get a feel for Shadow's logging style and format. More information on logging and analyzing results can be found on [[the analysis page|Analyzing results]].

For now, we are most interested in lines containing `fg-download-complete`, since those represent completed downloads and contain useful timing statistics. Overall, we hope all **10,000** downloads completed:

```bash
grep "fg-download-complete" filetest.log | wc -l
```

We can also look at the fileserver statistics:

```bash
grep "fileserver stats" filetest.log
```

We now need to know more about the configuration process, as this is a major part of running Shadow experiments.

### file transfer, round 2

Shadow requires **XML input files** to configure an experiment. These files are used to describe the structure of the network topology, the network hosts that should be started, and application configuration options for each host. Although static configurations were used for the above examples, customizable examples are also included.

Lets take a look at another `filetransfer` example:

```bash
cd resource/examples/filetransfer/
shadow shadow.config.xml
```

Shadow requires at least one XML file, and accepts additional files. Shadow parses these files to create the internal representation of the network, plug-ins, and hosts. You should examine these files and understand how they are used. For example, you might try changing the quantity of clients, or the bandwidth of the network vertices or the latency of the network edges to see how download times are affected.

Although you may want to configure your own network characteristics, Shadow already includes an extensive **pre-built topology file** installed to `~/.shadow/share/topology.graphml.xml` (or `your/prefix/share`). It contains **vertices** and **edges** as specified in the [[Topology Format]].

You may modify `shadow.config.xml` to use the path to `~/.shadow/share/topology.graphml.xml` instead of embedding a topology as is done in `resource/examples/filetransfer/shadow.config.xml`.

The format of all the attributes and acceptable values for the topology is described on the [[Topology format]] page.