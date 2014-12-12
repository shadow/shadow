So you've got Shadow installed and your machine configured. Its time to see what Shadow can do!

## Logistics

When installing Shadow, the main executable was placed in `/bin` in your install prefix (`~/.shadow/bin` by default). As a reminder, it would be helpful if this location was included in your environment `PATH`.

`shadow` is the main Shadow binary executable. It contains most of the simulator's code, including events and the event engine, the network stack, and the routing logic.

The `shadow` binary is capable of appending custom **function interposition** libraries to the `LD_PRELOAD`  environment variable to make it possible to intercept real operating system functions and manage them in the simulation environment. The `shadow` binary also assists with running `valgrind`, mostly for debugging and development purposes. For more information:

```bash
shadow --help
```

## Shadow plug-ins

Generic applications may be run in Shadow. The most important required features of the application code to enable this are:
 + completely non-blocking I/O and non-blocking system calls
 + polling I/O events using the `epoll` interface (see `$ man epoll`)  
   _NOTE_: [libevent](http://libevent.org/) is also supported through its use of `epoll`.
 + no process forking or thread creation, or a mode that allows the application to run in a single thread

The [shadow-plugin-extras repository](https://github.com/shadow/shadow-plugin-extras) contains a useful basic "hello world" example that illustrates how a program running outside of Shadow may also be run inside of Shadow. The example provides useful comments and a general structure that will be useful to understand when writing your own plug-ins.

## Basic functional tests

Shadow provides a virtual system and network that are used by plug-in applications. Fortunately, Shadow already contains a traffic generator application plug-in so you can get started without writing your own. 

The following example runs 10 clients that each download 10 files from a set of 5 servers over a simple network topology using Shadow's traffic generator plug-in. The example could take a few minutes, and you probably want to redirect the output to a log file:

```bash
cd shadow/resource/examples
shadow shadow.config.xml > shadow.log
```
Once it finishes, you can browse through shadow.log to get a feel for Shadow's logging style and format. For now, we are most interested in lines containing `transfer-complete`, since those represent completed downloads and contain useful timing statistics. The clients should have completed a total of **100** transfers:

```bash
cat shadow.log | grep "transfer-complete" | grep "GET" | wc -l
```

We can also look at the transfers from the servers' perspective:

```bash
cat shadow.log | grep "transfer-complete" | grep "PUT" | wc -l
```

We now need to know more about the configuration process, as this is a major part of running Shadow experiments.

## Configuration

Shadow requires **XML input files** to configure an experiment. These files are used to describe the structure of the network topology, the network hosts that should be started, and application configuration options for each host. The network, node, and application configuration is specified in the `shadow.config.xml` file; the client behavior models (traffic generator configurations) are specified in the `tgen.*.graphml.xml` files.

Lets take a look at another `filetransfer` example:

```bash
cd resource/examples/filetransfer/
shadow shadow.config.xml
```

Shadow requires at least one XML file, and accepts additional files. Shadow parses these files to create the internal representation of the network, plug-ins, and hosts. You should examine these files and understand how they are used. For example, you might try changing the quantity of clients, or the bandwidth of the network vertices or the latency of the network edges to see how download times are affected.

Although you may want to configure your own network characteristics, Shadow already includes an extensive **pre-built topology file** installed to `~/.shadow/share/topology.graphml.xml` (or `your/prefix/share`). It contains **vertices** and **edges** as specified in the [[Topology Format]].

You may modify `shadow.config.xml` to use the path to `~/.shadow/share/topology.graphml.xml` instead of embedding a topology as is done in `resource/examples/filetransfer/shadow.config.xml`.

The format of all the attributes and acceptable values for the topology is described on the [[Topology format]] page.

# analyze

This section discusses the format of Shadow's log files, and how to analyze the results contained therein.

## the log file

Shadow produces log messages in the following format:

```text
real-time [thread-id] virtual-time [logdomain-loglevel] [hostname-ip] [function-name] MESSAGE
```

+ _real-time_:  
the wall clock time since the start of the experiment, represented as `hours:minutes:seconds:microseconds`
+ _thread-id_:  
the ID of the worker thread that generated the message
+ _virtual-time_:  
the simulated time since the start of the experiment, represented as `hours:minutes:seconds:nanoseconds`
+ _logdomain_:  
either `shadow` or the name of one of the plug-ins as specified in the _id_ tag of the _plugin_ element in the XML file (e.g., `scallion`, `filetransfer`, `echoplugin`, `torrent`, `browser`)
+ _loglevel_:  
one of `error` < `critical` < `warning` < `message` < `info` < `debug`, in that order
+ _hostname_:  
the name of the node as specified in the _id_ tag of the _node_ element in the XML file
+ _ip_:  
the IP address of the node as specified in the _ip_ tag of the _node_ element in the XML file, or a random IP address if one is not specified  
**NOTE**: _all IP addresses are random until [this feature](https://github.com/shadow/shadow/issues/39) is completed_
+ _function-name_:  
the name of the function logging the message
+ _MESSAGE_:  
the actual message to be logged

By default, Shadow only prints plug-in and core messages at or below the `message` log level. This behavior can be changed using the Shadow option `-l` or `--log-level`.  
**NOTE**: _in addition to Shadow's log level, Scallion experiments should also change Tor's log level in the *torrc files if lower level Tor messages are desired_

## gathering statistics

Shadow includes a heartbeat message that contains some useful system information for each virtual node in the experiment. This information is contained in messages containing the string `shadow-heartbeat` and includes:

+ CPU _value_ %:  
the percentage of time spent executing code _inside_ the plug-in over the previous interval  
**NOTE**: _this value will be 0 if using `--cpu-threshold=-1`_
+ MEM _value_ KiB:  
the total memory currently consumed by the node's plug-in, in Kibibytes
+ interval _value_ seconds:  
the number of seconds used to calculate interval statistics
+ alloc _value_ KiB:  
the amount of memory allocated (i.e. malloced) in the last interval, in Kibibytes
+ dealloc _value_ KiB:  
the amount of memory de-allocated (i.e. freed) in the last interval, in Kibibytes
+ Rx _value_ B:  
the amount of network data received in the last interval, in Bytes
+ Tx _value_ B:  
the amount of network data sent in the last interval, in Bytes

These heartbeats are logged at the `message` level every `60` seconds by default.  The heartbeat log level can be changed with the option `-g` or `--stat-log-level` and the heartbeat interval set with the option `-h` or `--stat-interval`.

Each plug-in also generally prints useful statistics, such as file download size and timing information. See the plug-in usage pages for an explanation of what each provides.

## parsing and plotting results

Shadow includes a python script `contrib/analyze.py` that can parse a log file and extract these important statistics. This script can also plot the results using python's pylab module.

The script is meant to be generic enough to parse any Shadow output and extract whatever information it knows about. This includes network throughput over time, client download statistics, and client load statistics (some of these are gathered from plug-in log messages).

For more information:
```bash
python contrib/analyze.py --help
python contrib/analyze.py parse --help
python contrib/analyze.py plot --help
```

**NOTE**: _the analyze.py script requires some python modules, most notably the `pylab` module_

## examples

Here are some quick examples of how to use the `analyze.py` script.

### filetransfer
Consider a set of experiments where we would like to analyze the effect of changing the size of our nodes' network interface receive buffer. We run the following 2 experiments:

```bash
shadow --tcp-windows=1 --file > window1.log
shadow --tcp-windows=1000 --file > window1000.log
```

To parse these log files, we use the `contrib/analyze.py` script as follows:

```bash
python contrib/analyze.py parse --cutoff=0 --output=window1 window1.log
python contrib/analyze.py parse --cutoff=0 --output=window1000 window1000.log
```

Each of the directories `window1/` and `window1000/` now contain data statistics files extracted from the log files. We can now combine and visualize these results by plotting them with pylab:

```bash
python contrib/analyze.py plot --title "Shadow TCP Window Test" --prefix "window" --data window1/ "1 packet" --data window1000/ "1000 packets"
```

See any of the graphs in `./graphs`, or if you have `pdftk` installed, you can simply view the `window-combined.pdf` file.