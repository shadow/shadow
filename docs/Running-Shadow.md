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
shadow topology.xml hosts.xml
```

Shadow requires at least one XML file, and accepts additional files. Shadow parses these files to create the internal representation of the network, plug-ins, and hosts. You should examine these files and understand how they are used. For example, you might try changing the quantity of clients in `hosts.xml`, or the bandwidth of the clusters or the latency of the links in `topology.xml` to see how download times are affected.

Although you may want to configure your own network characteristics, Shadow already includes an extensive **pre-built topology file** installed to `~/.shadow/share/topology.xml` (or `your/prefix/share`). It contains a **cluster** for every country (and US state / Canadian Province) in the world, and **links** between each pair of those clusters. The topology characteristics were generated from real network metrics gathered from [iPlane](http://iplane.cs.washington.edu/) and large-scale PlanetLab experiments. For more information, check out the [peer-reviewed publication on network modeling](http://www-users.cs.umn.edu/~jansen/papers/tormodel-cset2012.pdf).

Using the included topology file means we are only left with configuring the hosts:

```bash
cd resource/examples/filetransfer/
shadow ~/.shadow/share/topology.xml hosts.xml
```

The format of all the attributes and acceptable values is described on the [[Topology format]] page.

### scallion

`scallion` is a plug-in that runs the [Tor anonymity software](https://www.torproject.org/), allowing us to configure a private Tor networks on our machine and transfer data through it. Scallion requires an additional library in `LD_PRELOAD` and also requires setting some extra environment variables. Because of these complexities, a python helper script called `scallion` is also installed in `~/.shadow/bin` (or `your/prefix/bin`). This script is a wrapper for the `shadow` wrapper script.

Tor requires other configuration files and keys to function. A very small example can be run as follows:

```bash
cd resource/examples/scallion/
tar xaf minimal.tar.xz
cd minimal
scallion -i hosts.xml
```

Scallion notes:
+ Each Tor node type may be configured in the `*torrc` files
+ The `scallion` script automatically redirects all output from Tor into `./data`
+ The `./data` directory contains the private data directories from each Tor instance running in the experiment
+ All Shadow and Tor logging for every node is redirected to the `./data/scallion.log` file
+ If `dstat` is installed, its output is redirected to `./data/dstat.log`

The above toy example is not realistic for research purposes. More realistic network configurations can be found in the other compressed files in `resource/examples/scallion/`. To run one of these experiments with 2 worker threads enabled:

```bash
cd resource/examples/scallion/
tar xaf tiny-m1.large.tar.xz
cd tiny-m1.large
scallion -i hosts.xml -w 2
```

Note that these experiments will take on the order of 30 minutes to several hours, and consume ~4 to ~64 GiB of RAM, depending which size you run (tiny, small, medium, large). See [[Using the scallion plug-in]] for more information and for details on generating your own Scallion `hosts.xml` file, and [[Analyzing results]] for help parsing the output.
