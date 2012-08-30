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

## testing the water

Shadow provides a virtual system and network that are used by plug-in applications. Fortunately, Shadow already contains several application plug-ins so you can get started without writing your own. Basic functionalities are tested using some static configurations of these plug-ins that are included in `shadow-bin`. Let's start by running those and make sure thing are working as they should.

### echo

The first example is the `echo` plug-in. This is essentially a small ping-pong test that ensures messages may be sent and received to and from nodes accross the simulated network. It tests every implemented communication mechanism, including:

+ pipes
+ socketpairs
+ reliable UDP channels
+ reliable UDP channels over loopback
+ reliable TCP channels
+ reliable TCP channels over loopback
+ unreliable TCP channels (includes packet dropping)
+ unreliable TCP channels over loopback (includes packet dropping)

Run the test like this:

```bash
shadow --echo
```

A message should be printed for each of these channels, stating either `consistent echo received` or `inconsistent echo received`. When things are working properly, the result of the following command should be **8**:

```bash
shadow --echo | grep " consistent echo received" | wc -l
```

## slowly wading in

The next step is to run experiments that fully utilize the virtual network. 

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
grep "fileserver stats" filetests.log
```

### torrent

The `torrent` plug-in built-in example configures a BitTorrent-like P2P swarm to share an 8 MiB file between all of 10 clients. A torrent 'authority' represents a tracker and assists clients in joining the swarm, and the file is shared in 16 KiB chunks. This example will also take a few minutes, and redirecting output is advised:

```bash
shadow --torrent > torrenttest.log
```

Useful statistics here are contained in messages labeled with `client-block-complete`, which is printed for each node upon the completion of each 16 KiB block, and `client-complete`, which is printed when the transfer finishes. An 8 MiB file should contain 512 blocks, so for all 10 clients there should be **5120** blocks total:

**NOTE** - _[a bug in the torrent plug-in](https://github.com/shadow/shadow/issues/82) is currently causing an incorrect number of blocks from being downloaded_
```bash
grep "client-block-complete" torrenttest.log | wc -l
```

And all **10** clients should have completed:

```bash
grep "client-complete" torrenttest.log | wc -l
```

## treading water

Great, all of the built-in tests seem to be working. We now need to know more about the configuration process, as this is a major part of running Shadow experiments.

Shadow requires **XML input files** to configure an experiment. These files are used to describe the structure of the network topology, the network hosts that should be started, and application configuration options for each host. Although static configurations were used for the above examples, customizable examples are also included.

### file transfer, round 2

Lets take a look at another `filetransfer` example:

```bash
cd resource/filetransfer-example
shadow example.topology.xml example.hosts.xml
```

Shadow requires at least one XML file, and accepts additional files. Shadow parses these files to create the internal representation of the network, plug-ins, and hosts. You should examine these files and understand how they are used. For example, you might try changing the quantity of clients in `example.hosts.xml`, or the bandwidth of the clusters or the latency of the links in `example.topology.xml` to see how download times are affected.

Although you may want to configure your own network characteristics, Shadow already includes an extensive **pre-built topology file** installed to `~/.shadow/share/topology.xml` (or `/share` in your custom prefix). It contains a **cluster** for every country in the world, and **links** between each pair of those clusters. The topology characteristics were generated from real network metrics gathered from [iPlane](http://iplane.cs.washington.edu/) and large-scale PlanetLab experiments. For more information, check out the [peer-reviewed publication on network modeling](http://www-users.cs.umn.edu/~jansen/papers/tormodel-cset2012.pdf).

Using the included topology file means we are only left with configuring the hosts:

```bash
cd resource/filetransfer-example
shadow ~/.shadow/share/topology.xml example.hosts.xml
```

The format of all the attributes and acceptable values is described on the [[Topology format]] page.

## diving into the deep end

We now hopefully understand what makes Shadow grrr. Now its time for more complicated experiments.

### scallion

`scallion` is a plug-in that runs the [Tor anonymity software](https://www.torproject.org/), allowing us to configure a private Tor networks on our machine and transfer data through it. Scallion requires an additional library in `LD_PRELOAD` and also requires setting some extra environment variables. Because of these complexities, a python helper script called `scallion` is also installed in `~/.shadow/bin` (or `/bin` in your custom prefix). This script is a wrapper for the `shadow` wrapper script.

Tor requires other configuration files and keys to function. A very small example can be run as follows:

```bash
cd resource/scallion-example
scallion -i scallion.xml
```

Scallion notes:
+ Each Tor node type may be configured in the `*torrc` files
+ The `scallion` script automatically redirects all output from Tor into `./data`
+ The `./data` directory contains the private data directories from each Tor instance running in the experiment
+ All Shadow and Tor logging for every node is redirected to the `./data/scallion.log` file
+ If `dstat` is installed, it output is redirected to `./data/dstat.log`
+ The `scallion` script automatically redirects all output from Tor into `./data`

The above toy example is not realistic for research purposes. More realistic network configurations can be found, compressed, in `resource/scallion-hosts`. To run one of these experiments:

```bash
cd resource/scallion-hosts
tar xaf tiny-m1.large.tar.xz
cd tiny-m1.large
scallion -i hosts.xml
```

Note that these experiments will take on the order of 30 minutes to several hours, and consume ~4 to ~64 GiB of RAM, depending which size you run (tiny, small, medium, large). See [[Using the scallion plug-in]] for more information, [[Analyzing results]] for help parsing the output, and [[Generating network configurations]] for details on generating your own Scallion `hosts.xml` file.