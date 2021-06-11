# The Shadow Simulator

[![Shadow Tests](https://github.com/shadow/shadow/actions/workflows/run_tests.yml/badge.svg?branch=dev&event=push)](https://github.com/shadow/shadow/actions/workflows/run_tests.yml?query=branch:dev+event:push)
[![Tor Tests](https://github.com/shadow/shadow/actions/workflows/run_tor.yml/badge.svg?branch=dev&event=push)](https://github.com/shadow/shadow/actions/workflows/run_tor.yml?query=branch:dev+event:push)

## TL;DR

Shadow directly executes real applications using native OS processes and co-opts
them into a high-performance discrete-event network simulation. Shadow enables
realistic and scalable private network experiments that can be scientifically
controlled and deterministically replicated.

Build, test, and install Shadow into `~/.local`:
```
$ ./setup build --clean
$ ./setup test
$ ./setup install
```

And then [learn more](https://shadow.github.io/docs/guide) or get started with
[a simple example simulation](docs/getting_started_basic.md) or [a slightly more
complex Tor simulation](docs/getting_started_tor.md).

## Overview

Shadow directly executes **real applications** in **simulated networks**.

#### Real Applications

Shadow directly executes unmodified, real application code using native OS
processes. Shadow co-opts the native processes into a discrete-event simulation
by interposing itself at the system call API; the necessary systems calls are
emulated such that the applications need not be aware that they are running in a
Shadow simulation.

#### Simulated Networks

Shadow constructs a private, virtual network through which the managed processes
can communicate. To enable network communication, Shadow internally implements
simulated versions of common network protocols (e.g., TCP and UDP) and models
network routing characteristics (e.g., path latency and packet loss) using a
configurable network graph.

This architecture enables Shadow to simulate distributed systems of
network-connected processes in a **realistic** and **scalable** private network
experiment that can be scientifically **controlled** and deterministically
**replicated**.

#### Caveats

Shadow currently supports or partially supports **over 150 systems calls**, and
we continue to extend support for these and other system calls. Applications
that make basic use of TCP or UDP functionality should work out of the box.
However, Shadow does not fully support all features for these systems calls, and
support is not yet provided for many less common or more complex functions (such
as `fork()`). You may find that your application does not yet function correctly
when running it in Shadow.

That being said, we are particularly motivated to run large-scale [Tor
Network](https://www.torproject.org) simulations and are eager to add necessary
functionality in support of this use-case. We may consider extending support for
other novel and interesting applications, particularly if the required effort is
low.

## Rationale

What about ns-3? [ns-3](https://www.nsnam.org) is a network simulator that is
designed to replicate network-layer protocol behavior with very high fidelity.
It contains accurate reimplementations of many network layer protocols and
communication substrates, and is thus targeted primarily for use by researchers
designing new network-layer protocols or protocol features. It does not (really)
support running unmodified, real applications, leaving users to implement
synthetic application abstraction models in place of real application code.

What about mininet? [mininet](http://mininet.org) is a network emulator that is
designed to run real kernel, switch, and application code in real time. The real
time requirement severely limits the number of processes that can be run in a
mininet experiment: time distortion can occur if the processes exceed a
computational threshold, which can result in undefined behavior and artifacts
that lead to untrustworthy results.

Shadow aims to fill the gap between these tools. Like mininet, Shadow directly
executes real applications in order to faithfully reproduce application
behavior. But like ns-3, Shadow runs a discrete-event simulation in order to
scientifically control and deterministically replicate network experiments.
Shadow uniquely targets experiments with large-scale distributed systems and
thus our simulator design prioritizes high-performance computing.

## More Information

Homepage:
  + https://shadow.github.io

Detailed Documentation:
  + [Local user documentation in docs/README.md](docs/README.md)
  + [Online user documentation](https://shadow.github.io/docs/guide)
  + [Online developer documentation](https://shadow.github.io/docs/rust)

Shadow Project Development:
  + https://github.com/shadow
        
Community Support:
  + https://github.com/shadow/shadow/discussions

Bug Reports:
  + https://github.com/shadow/shadow/issues
