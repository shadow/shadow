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

 + polls I/O events using one of the `epoll`, `poll`, or `select` interfaces (see, e.g., `$ man epoll`)
 + doesn't fork/exec process (can run in single process mode)

Included with Shadow is a traffic generator plug-in that is capable of modeling generic behaviors represented using an action-dependency graph and the standard graphml xml format. With this powerful plug-in, different behavior models can be implemented by simply writing a python script to generate new graphml files rather than modifying simulator code or writing new plug-ins. More information about customizing behaviors is [also on the wiki](3-Simulation-Customization#Traffic-generator-configuration).

Existing plug-ins for Shadow also include [shadow-plugin-tor](https://github.com/shadow/shadow-plugin-tor) for running Tor anonymity networks and [shadow-plugin-bitcoin](https://github.com/shadow/shadow-plugin-bitcoin) for running Bitcoin cryptocurrency networks. Other useful plug-ins exist in the [shadow-plugin-extras repository](https://github.com/shadow/shadow-plugin-extras), including an HTML-supported web browser and server combo.

To write your own plug-in, start by inspecting the [hello plug-in from the extras repository](https://github.com/shadow/shadow-plugin-extras/tree/master/hello): it contains a useful basic "hello world" example that illustrates how a program running outside of Shadow may also be run inside of Shadow. The example provides useful comments and a general structure that will be useful to understand when writing your own plug-ins.

## Basic functional tests

Shadow provides a virtual system and network that are used by plug-in applications. Fortunately, Shadow already contains a traffic generator application (tgen) so you can get started without writing your own. 

The following example runs tgen with 10 clients that each download 10 files from a set of 5 servers over a simple network topology. The example could take a few minutes, and you probably want to redirect the output to a log file:

```bash
cd resource/examples
shadow shadow.config.xml > shadow.log
```

Once it finishes, you will notice:

  + a new `shadow.log` file, which contains the simulator log messages that we redirected above;
  + a new `shadow.data` directory, which contains output from the virtual hosts in your simulation.

You can browse through `shadow.log` to get a feel for Shadow's logging style and format, and each `shadow.data/hosts/<hostname>` directory contains the standard output and standard error for each virtual process that ran in the simulation.

For now, we are most interested in the tgen virtual process output, and the lines containing `transfer-complete`, since those represent completed downloads and contain useful timing statistics. The clients should have completed a total of **1000** transfers:

```bash
for d in shadow.data/hosts/*client*; do grep "transfer-complete" ${d}/* ; done | tee clients.log | wc -l
```

We can also look at the transfers from the servers' perspective:

```bash
for d in shadow.data/hosts/*server*; do grep "transfer-complete" ${d}/* ; done | tee servers.log | wc -l
```

We now need to know more about the configuration process, as this is a major part of running Shadow experiments.

## Configuration

Shadow requires **XML input files** to configure an experiment. These files are used to describe the structure of the network topology, the network hosts that should be started, and application configuration options for each host. The network, node, and application configuration is specified in the `shadow.config.xml` file; the client behavior models (traffic generator configurations) are specified in the `tgen.*.graphml.xml` files.

Lets take another look at the `tgen` example from above, the configuration for which can be found in the `resource/examples/shadow.config.xml` file. After parsing this file, Shadow creates the internal representation of the network, loads the plug-ins, and generates the virtual hosts. You should examine these configuration files and understand how they are used. For example, you might try changing the quantity of clients, or the bandwidth of the network vertices or the latency of the network edges to see how download times are affected.

The network topology used for the simulation is also configured inside of the `shadow.config.xml` file. In the example above, the network topology was embedded as CDATA inside of the `<topology>` element. This network topology is itself XML in the standard graphml format, and can be stored in a separate file instead of embedding it. You may then modify `shadow.config.xml` to reference the external graphml topology file rather than embedding it with something like `<topology path="~/.shadow/share/topology.graphml.xml" />.

Shadow includes a **pre-built topology file** installed to `~/.shadow/share/topology.graphml.xml` (or `your/prefix/share`), which you can include as described above. You may want to customize the topology **vertices** and **edges** to include your own network characteristics, as the included topology is very basic and quite outdated. The format of all of the attributes and acceptable values for the topology is described on the [network configuration](3-Simulation-Customization#Network-configuration) page.

## The log file

Shadow produces simulator log messages (from the `shadow.log` file above) in the following format:

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

By default, Shadow only prints core messages at or below the `message` log level. This behavior can be changed using the Shadow option `-l` or `--log-level` to increase or decrease the verbosity of the output. As mentioned in the example from the previous section, the output from each virtual process (i.e. plug-in) is stored in separate log files beneath the `shadow.data` directory, and the format of those log files is application-specific (i.e., Shadow writes application output _directly_ to file).  

## Gathering statistics

Shadow logs simulator heartbeat messages that contain useful system information for each virtual node in the experiment, in messages containing the string `shadow-heartbeat`. By default, these heartbeats are logged once per second, but the frequency can be changed using the `--heartbeat-frequency` option to Shadow (see `shadow --help`).

There are currently three heartbeat statistic subsystems: `node`, `socket`, and `ram`. For each subsystem that is enabled, Shadow will print a 'header' message followed by regular message every frequency interval. The 'header' messages generally describe the statistics that are printed in the regular messages for that subsystem.

The following are examples of the statistics that are available for each subsystem:

Node:

```
[node-header] interval-seconds,recv-bytes,send-bytes,cpu-percent,delayed-count,avgdelay-milliseconds;inbound-localhost-counters;outbound-localhost-counters;inbound-remote-counters;outbound-remote-counters where counters are: packets-total,bytes-total,packets-control,bytes-control-header,packets-control-retrans,bytes-control-header-retrans,packets-data,bytes-data-header,bytes-data-payload,packets-data-retrans,bytes-data-header-retrans,bytes-data-payload-retrans
```

Socket:

```
[socket-header] descriptor-number,protocol-string,hostname:port-peer;inbuflen-bytes,inbufsize-bytes,outbuflen-bytes,outbufsize-bytes;recv-bytes,send-bytes;inbound-localhost-counters;outbound-localhost-counters;inbound-remote-counters;outbound-remote-counters|...where counters are: packets-total,bytes-total,packets-control,bytes-control-header,packets-control-retrans,bytes-control-header-retrans,packets-data,bytes-data-header,bytes-data-payload,packets-data-retrans,bytes-data-header-retrans,bytes-data-payload-retrans
```

Ram:

```
[ram-header] interval-seconds,alloc-bytes,dealloc-bytes,total-bytes,pointers-count,failfree-count
```

Only the `node` subsystem is on by default; be aware that the other subsystems track a lot of information and may significantly increase the amount of output that Shadow produces.

The tgen plug-in also logs generally useful statistics, such as file download size and timing information. This information can be parsed from the corresponding log files in the virtual process data directories.

## Parsing and plotting results

Shadow includes some python scripts that can parse some important statistics from the Shadow and TGen messages, including network throughput over time, client download statistics, and client load statistics, and then visualize the results. The following will parse and plot the output produced from the above experiment:

```bash
# start in the base shadow/ directory
cd ../..
# parse the shadow output file
python src/tools/parse-shadow.py --help
python src/tools/parse-shadow.py --prefix results resource/examples/shadow.log
# parse tgen output files from all hosts
python src/tools/plot-tgen.py --help
python src/tools/parse-tgen.py --prefix results resource/examples/shadow.data/hosts/
# plot the results!
python src/tools/plot-shadow.py --help
python src/tools/plot-shadow.py --data results "example-plots"
```

The `parse-*.py` scripts generate `stats.*.json.xz` files. The (heavily trimmed) contents of `stats.shadow.json` look a little like this.

        nodes:
            4uthority:
                recv:
                    bytes_control_header:
                        0: 0
                        1: 0
                        2: 0
                    bytes_control_header_retrans:
                    bytes_data_header:
                    bytes_data_header_retrans:
                    bytes_data_payload:
                    bytes_data_payload_retrans:
                    bytes_total:
                send:
            relay1:
            relay2:
        ticks:
            0:
                maxrss_gib: 0.010578
                time_seconds: 5.003849
            1:
            2:

The `plot-*.py` scripts generate graphs. Open the PDF file that was created to see the graphed results.

## Example experiment

Consider a set of experiments where we would like to analyze the effect of changing the size of our nodes' initial TCP window. We run the following 2 experiments:

```bash
cd resource/examples/
rm -rf shadow.data shadow.log
shadow --tcp-windows=1 shadow.config.xml > window1.log
mv shadow.data windows1.data
shadow --tcp-windows=1000 shadow.config.xml > window1000.log
mv shadow.data window1000.data
```

To parse these log files, we use the following scripts:

```bash
python ../../src/tools/parse-shadow.py --prefix=window1.results window1.log
python ../../src/tools/parse-tgen.py --prefix=window1.results window1.data/hosts
python ../../src/tools/parse-shadow.py --prefix=window1000.results window1000.log
python ../../src/tools/parse-tgen.py --prefix=window1000.results window1000.data/hosts
```

Each of the directories `window1.results/` and `window1000.results/` now contain data statistics files extracted from the log files. We can now combine and visualize these results with the `plot-shadow.py` script:

```bash
python ../../src/tools/plot-shadow.py --prefix "window" --data window1.results/ "1 packet" --data window1000.results/ "1000 packets"
```

Then open the PDF file that was created to compare results from the experiments.