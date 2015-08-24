# "echo": a Shadow socket test

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

## copyright holders

Rob Jansen, (c)2010-2011.

## licensing deviations

No deviations from LICENSE.

