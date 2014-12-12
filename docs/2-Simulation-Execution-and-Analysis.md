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

Included with Shadow is a traffic generator plug-in that is capable of modeling generic behaviors represented using an action-dependency graph and the standard graphml xml format. This powerful plug-in means different behavior models can be implemented by simply writing a python script to generate new graphml files rather than modifying simulator code or writing new plug-ins. More information about customizing behaviors is [also on the wiki](3-Simulation-Customization#Traffic-generator-configuration).

Existing plug-ins for Shadow also include [shadow-plugin-tor](https://github.com/shadow/shadow-plugin-tor) for running Tor anonymity networks and [shadow-plugin-bitcoin](https://github.com/shadow/shadow-plugin-bitcoin) for running Bitcoin cryptocurrency networks. Other useful plug-ins exist in the [shadow-plugin-extras repository](https://github.com/shadow/shadow-plugin-extras), including an HTML-supported web browser and server combo.

To write your own plug-in, start by inspecting the [hello plug-in from the extras repository](https://github.com/shadow/shadow-plugin-extras/tree/master/hello): it contains a useful basic "hello world" example that illustrates how a program running outside of Shadow may also be run inside of Shadow. The example provides useful comments and a general structure that will be useful to understand when writing your own plug-ins.

## Basic functional tests

Shadow provides a virtual system and network that are used by plug-in applications. Fortunately, Shadow already contains a traffic generator application ("tgen") so you can get started without writing your own. 

The following example runs tgen with 10 clients that each download 10 files from a set of 5 servers over a simple network topology. The example could take a few minutes, and you probably want to redirect the output to a log file:

```bash
cd resource/examples
shadow shadow.config.xml > shadow.log
```
Once it finishes, you can browse through shadow.log to get a feel for Shadow's logging style and format. For now, we are most interested in lines containing `transfer-complete`, since those represent completed downloads and contain useful timing statistics. The clients should have completed a total of **100** transfers:

```bash
cat shadow.log | grep "transfer-complete" | grep "GET" > clients.log
cat clients.log | wc -l
```

We can also look at the transfers from the servers' perspective:

```bash
cat shadow.log | grep "transfer-complete" | grep "PUT" > servers.log
cat servers.log | wc -l
```

We now need to know more about the configuration process, as this is a major part of running Shadow experiments.

## Configuration

Shadow requires **XML input files** to configure an experiment. These files are used to describe the structure of the network topology, the network hosts that should be started, and application configuration options for each host. The network, node, and application configuration is specified in the `shadow.config.xml` file; the client behavior models (traffic generator configurations) are specified in the `tgen.*.graphml.xml` files.

Lets take a look at another `tgen` example:

```bash
cd resource/examples
shadow shadow.config.xml > shadow.log
```

Shadow requires an XML file. Shadow parses the file and create the internal representation of the network, loads the plug-ins, and generates the virtual hosts. You should examine these files and understand how they are used. For example, you might try changing the quantity of clients, or the bandwidth of the network vertices or the latency of the network edges to see how download times are affected.

Shadow includes a **pre-built topology file** installed to `~/.shadow/share/topology.graphml.xml` (or `your/prefix/share`). You may modify `shadow.config.xml` to use the path to `~/.shadow/share/topology.graphml.xml` instead of embedding a topology as is done in `resource/examples/shadow.config.xml`.

You may want to customize the topology **vertices** and **edges** to include your own network characteristics. The format of all of the attributes and acceptable values for the topology is described on the [network configuration](3-Simulation-Customization#Network-configuration) page.

## The log file

Shadow produces log messages in the following format:

```text
real-time [thread-id] virtual-time [logdomain-loglevel] [hostname~ip] [function-name] MESSAGE
```

+ _real-time_:  
the wall clock time since the start of the experiment, represented as `hours:minutes:seconds:microseconds`
+ _thread-id_:  
the ID of the worker thread that generated the message
+ _virtual-time_:  
the simulated time since the start of the experiment, represented as `hours:minutes:seconds:nanoseconds`
+ _logdomain_:  
either `shadow` or the name of one of the plug-ins as specified in the _id_ tag of the _plugin_ element in the XML file (e.g., `tgen`, `tor`, `bitcoin`)
+ _loglevel_:  
one of `error` < `critical` < `warning` < `message` < `info` < `debug`, in that order
+ _hostname_:  
the name of the node as specified in the _id_ tag of the _node_ element in the XML file
+ _ip_:  
the IP address of the node as specified in the _ip_ tag of the _node_ element in the XML file, or a random IP address if one is not specified  
+ _function-name_:  
the name of the function logging the message
+ _MESSAGE_:  
the actual message to be logged

By default, Shadow only prints plug-in and core messages at or below the `message` log level. This behavior can be changed using the Shadow option `-l` or `--log-level`.  

## Gathering statistics

Shadow logs heartbeat messages that contain useful system information for each virtual node in the experiment, in messages containing the string `shadow-heartbeat`:

+ CPU _value_ %:  
the percentage of time spent executing code _inside_ the plug-in over the previous interval
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

These heartbeats are logged at the `message` level every `60` seconds by default. The heartbeat log level can be changed with the option `-g` or `--stat-log-level` and the heartbeat interval set with the option `-h` or `--stat-interval`.

Each plug-in also generally logs useful statistics, such as file download size and timing information. This information can be parsed from the Shadow log file.

## Parsing and plotting results

Shadow includes a python script `tools/parse-shadow.py` that can parse a log file and extract some important statistics, including network throughput over time, client download statistics, and client load statistics (some of these are gathered from plug-in log messages). The data from the files produced by the script can then be visualized using the `tools/plot-shadow.py` script.

```bash
python parse-shadow.py --help
python plot-shadow.py --help
python parse-shadow.py --prefix results shadow.log
python plot-shadow.py --data results "example-plots"
```

Then open the PDF file that was created. Note that these scripts may require some additional python modules.

## Example experiment

Consider a set of experiments where we would like to analyze the effect of changing the size of our nodes' network interface receive buffer. We run the following 2 experiments:

```bash
cd resource/examples/
shadow --tcp-windows=1 shadow.config.xml > window1.log
shadow --tcp-windows=1000 shadow.config.xml > window1000.log
```

To parse these log files, we use the `parse-shadow.py` script as follows:

```bash
python ../../tools/parse-shadow.py --prefix=window1 window1.log
python contrib/analyze.py parse --prefix=window1000 window1000.log
```

Each of the directories `window1/` and `window1000/` now contain data statistics files extracted from the log files. We can now combine and visualize these results with the `parse-shadow.py` script:

```bash
python ../../tools/plot-shadow.py --prefix "window" --data window1/ "1 packet" --data window1000/ "1000 packets"
```

Then open the PDF file that was created.