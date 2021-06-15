# Shadow 2.x Design

_TODO: This document should be expanded._

## Overview

Shadow directly executes real applications using native OS processes and co-opts
them into a high-performance discrete-event network simulation. Shadow enables
realistic and scalable private network experiments that can be scientifically
controlled and deterministically replicated.

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
