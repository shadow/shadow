# "echo": a Shadow plug-in

A _client_ sends a message through a _server_; the message is then echoed back to the _client_.

The echo plug-in is essentially a small ping-pong test that ensures messages may be sent and received to and from nodes accross the simulated network. It tests every implemented communication mechanism, including:

+ pipes
+ socketpairs
+ reliable UDP channels
+ reliable UDP channels over loopback
+ reliable TCP channels
+ reliable TCP channels over loopback
+ unreliable TCP channels (includes packet dropping)
+ unreliable TCP channels over loopback (includes packet dropping)

## copyright holders

Rob Jansen, (c)2010-2011.

## licensing deviations

No deviations from LICENSE.

## last known working version

This plug-in was last tested and known to work with 
Shadow v1.9.0
commit 2fb316ad84801434c4b5e0536740807774c732fd
Date:   Tue Mar 11 18:20:36 2014 -0400

## usage

The _echo_ plug-in may be used to test multiple socket mechanisms.
The following are all valid arguments for echo applications.

**NOTE**: clients and servers must be paired together, but loopback, socketpair,
and pipe modes stand on their own.

 + `tcp client SERVERIP`
 + `tcp server`
 + `tcp loopback`
 + `tcp socketpair`
 + `udp client SERVERIP`
 + `udp server`
 + `udp loopback`
 + `pipe`

## example

Run the test from this directory like this:

```bash
shadow example.xml
```

A message should be printed for each of these channels, stating either `consistent echo received` or `inconsistent echo received`. When things are working properly, the result of the following command should be **8**:

```bash
shadow --echo | grep " consistent echo received" | wc -l
```

