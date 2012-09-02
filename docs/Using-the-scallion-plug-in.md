**NOTE** - _This page is incomplete_

The `resource/scallion-hosts` directory contains sample network configurations to get started with some Tor experiments. The maximum memory requirements are given below. Note that the smallest possible EC2 instance required to run these configurations are also given, but the memory requirement guidelines hold for any machine.

The memory requirements of the included topologies, and the EC2 instances supporting those memory requirements are given below. For instructions on running Shadow in the cloud, visit [[Running Shadow on EC2]].

```
Size    (# Nodes)                   RAM (GiB)   EC2 Instance
----    ---------                   ---------   ------------
tiny    20 relays, 200 clients      < 4         m1.large
small   50 relays, 500 clients      < 16        m1.xlarge or m2.xlarge
medium  100 relays, 1000 clients    < 32        m2.2xlarge
large   250 relays, 2500 clients    < 64        m2.4xlarge
```