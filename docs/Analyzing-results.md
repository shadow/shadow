This section discusses the format of Shadow's log files, and how to analyze the results contained therein.

## the log file

Shadow produces log messages in the following format:

```
real-time [thread-id] virtual-time [logdomain-loglevel] [hostname-ip] [function-name] MESSAGE
```

+ **real-time**: the wall clock time since the start of the experiment, represented as `hours:minutes:seconds:nanoseconds`
+ **thread-id**: the ID of the worker thread that generated the message
+ **virtual-time**: the simulated time since the start of the experiment, represented as `hours:minutes:seconds:nanoseconds`
+ **logdomain**: either `shadow` or the name of one of the plug-ins as specified in the _id_ tag of the _plugin_ element in the XML file (e.g., `scallion`, `filetransfer`, `echoplugin`, `torrent`, `browser`)
+ **loglevel**: one of `error` < `critical` < `warning` < `message` < `info` < `debug`, in that order
+ **hostname**: the name of the node as specified in the _id_ tag of the _node_ element in the XML file
+ **ip**: the IP address of the node as specified in the _ip_ tag of the _node_ element in the XML file, or a random IP address if one is not specified  
**NOTE**: _all IP addresses are random until [this feature](https://github.com/shadow/shadow/issues/39) is completed_
+ **function-name**: the name of the function logging the message
+ **MESSAGE**: the actual message to be logged

By default, Shadow only prints plug-in and core messages at or below the `message` log level. This behavior can be changed using the Shadow option `-l` or `--log-level`. **NOTE**: _in addition to Shadow's log level, Scallion experiments should also change Tor's log level in the *torrc files if lower level Tor messages are desired_

## gathering statistics

Shadow includes a heartbeat message that contains some useful system information for each virtual node in the experiment. This information is contained in messages containing the string `shadow-heartbeat` and includes:

+ CPU _value_ %: the percentage of time spent executing code _inside_ the plug-in over the previous interval  
**NOTE**: _this value will be 0 if using `--cpu-threshold=-1`__
+ MEM _value_ KiB: the total memory currently consumed by the node's plug-in, in Kibibytes
+ interval _value_ seconds: the number of seconds used to calculate interval statistics
+ alloc _value_ KiB: the amount of memory allocated (i.e. malloced) in the last interval, in Kibibytes
+ dealloc _value_ KiB: the amount of memory de-allocated (i.e. freed) in the last interval, in Kibibytes
+ Rx _value_ B: the amount of network data received in the last interval, in Bytes
+ Tx _value_ B: the amount of network data sent in the last interval, in Bytes

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

As a quick example of how to use the script, consider a set of experiments where we would like to analyze the effect of changing the size of our nodes' network interface receive buffer. We run the following 3 experiments:

```bash
shadow --interface-buffer=128000 --file > buffer-128kb.log
shadow --interface-buffer=1024000 --file > buffer-1mb.log
shadow --interface-buffer=2024000 --file > buffer-2mb.log
```

To parse these log files, we use the `contrib/analyze.py` script as follows:

```bash
python contrib/analyze.py parse --cutoff=0 --output=buffer-128kb buffer-128kb.log
python contrib/analyze.py parse --cutoff=0 --output=buffer-1mb buffer-1mb.log
python contrib/analyze.py parse --cutoff=0 --output=buffer-2mb buffer-2mb.log
```

Each of the directories `buffer-128kb/`, `buffer-1mb/`, and `buffer-2mb/` now contain data statistics files extracted from the log files. We can now combine and visualize these results by plotting them with pylab:

```bash
python contrib/analyze.py plot --title "Shadow Interface Receive Buffer Test" --prefix "buffer" --data buffer-128kb/ "128 KB" --data buffer-1mb/ "1 MB" --data buffer-2mb/ "2 MB"
```

See any of the graphs in `./graphs`, or if you have `pdftk` installed, you can simply view the `buffer-combined.pdf` file.