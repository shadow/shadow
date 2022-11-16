# Overview

This file provides a brief overview of the tgen tests which are defined in this
directory and used to test shadow network performance.

We currently have two types of tgen tests: "fixed duration: and "fixed size"
tests. More information about the tgen config files can be found in the
`gen_conf.py` scripts in the respective directories.

## Fixed duration

In the fixed duration tests, two hosts connect to each other and transfer as
much as possible for a fixed duration. This is like a standard speed test.

Our tests vary parameters along two main axes:

- Client behavior: we configure the client to create a different number of
  streams in parallel to the server, and upload and download effectively
  infinitely sized files. As an example, the config file name
  `client.10streams.graphml` encodes that 10 parallel streams are used.

- Network: we configure networks of varying quality, from slower networks (less
  bandwidth) with higher latency, to faster networks (more bandwidth) with lower
  latency. As an example, the config file name `10mbit_200ms.yaml` encodes
  that the bandwidth is set to `10 Mbit` and the latency to `200 ms`.

We run the speed test over all combinations of client and network. The
`verify.sh` script that runs after each test checks that we are able to reach
within a percent threshold of the expected transfer speed as configured in the
network host bandwdith part of the shadow config file.

## Fixed size

In the fixed size tests, two hosts connect to each other and perform a number of
transfers of a fixed size.

Our tests vary parameters along two main axes:

- Client behavior: we configure the client to create a different number of
  streams in parallel to the server, differently sized bidirational transfers,
  and a different number of total tranfers. As an example, the config file name
  `client.1stream_1b_1000x.graphml` encodes that 1 parallel stream is used, each
  transfer is 1 byte, and we repeat 1000 times.
- Network: we configure networks of varying quality, from slower networks (less
  bandwidth) with higher latency, to faster networks (more bandwidth) with lower
  latency. The config file name is the same as described above in the fixed
  duration section.

Again, we run all combinations of clients and networks. Our choice of client
parameters is such that we test both streams in parallel and streams in serial,
both over different network types. The `verify.sh` script that runs after each
test checks that we successfully completed all transfers.
