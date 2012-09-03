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



## parsing and plotting results