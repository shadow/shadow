# Parallel simulations

Some care must be taken when running multiple Shadow simulations on the same
hardware at the same time. By default, Shadow pins threads to specific CPUs to
avoid CPU migrations. The CPU selection logic isn't aware of other processes
that may be using substantial CPU time and/or pinning, *including other Shadow
simulations*. i.e. without some care, multiple Shadow simulations running on the
same machine at the same time will generally end up trying to use the same set
of CPUs, even if other CPUs on the machine are idle.

## Disabling pinning

The simplest solution is to disable CPU pinning entirely. This has a substantial
performance penalty (with some reports as high as 3x), but can be a reasonable
solution for small simulations. Pinning can be disabled by passing
`--use-cpu-pinning=false` to Shadow.

## Setting an initial CPU affinity

Shadow checks the initial CPU affinity assigned to it, and only assigns to CPUs
within that set. The easiest way to run Shadow with a subset of CPUs is with the
`taskset` utility. e.g. to start one Shadow simulation using CPUs 0-9 and
another using CPUs 10-19, you could use:

```
$ (cd sim1 && taskset --cpu-list 0-9 shadow sim1config.yml) &
$ (cd sim2 && taskset --cpu-list 10-19 shadow sim2config.yml) &
```

Shadow similarly avoids trying to pin to CPUs outside of its cgroup cpuset (see
cpuset(7)). This allows Shadow to work correctly in such scenarios (such as
running in a container on a shared machine that only has access to some CPUs),
but is generally more complex and requires higher privilege than setting the CPU
affinity with `taskset`.

## Choosing a CPU set

When assigning Shadow a subset of CPUs, some care must be taken to get optimal
performance. You can use the `lscpu` utility to see the layout of the CPUs on
your machine.

* Avoid using multiple CPUs on the same core (aka hyperthreading). Such CPUs
  compete with each-other for resources.
* Prefer CPUs on the same socket and (NUMA) node. Such CPUs share cache, which
  is typically beneficial in Shadow simulations.

For example, given the `lscpu` output:

```
$ lscpu --parse=cpu,core,socket,node
# The following is the parsable format, which can be fed to other
# programs. Each different item in every column has an unique ID
# starting from zero.
# CPU,Core,Socket,Node
0,0,0,0
1,1,1,1
2,2,0,0
3,3,1,1
4,4,0,0
5,5,1,1
6,6,0,0
7,7,1,1
8,8,0,0
9,9,1,1
10,10,0,0
11,11,1,1
12,12,0,0
13,13,1,1
14,14,0,0
15,15,1,1
16,16,0,0
17,17,1,1
18,18,0,0
19,19,1,1
20,20,0,0
21,21,1,1
22,22,0,0
23,23,1,1
24,24,0,0
25,25,1,1
26,26,0,0
27,27,1,1
28,28,0,0
29,29,1,1
30,30,0,0
31,31,1,1
32,32,0,0
33,33,1,1
34,34,0,0
35,35,1,1
36,36,0,0
37,37,1,1
38,38,0,0
39,39,1,1
40,0,0,0
41,1,1,1
42,2,0,0
43,3,1,1
44,4,0,0
45,5,1,1
46,6,0,0
47,7,1,1
48,8,0,0
49,9,1,1
50,10,0,0
51,11,1,1
52,12,0,0
53,13,1,1
54,14,0,0
55,15,1,1
56,16,0,0
57,17,1,1
58,18,0,0
59,19,1,1
60,20,0,0
61,21,1,1
62,22,0,0
63,23,1,1
64,24,0,0
65,25,1,1
66,26,0,0
67,27,1,1
68,28,0,0
69,29,1,1
70,30,0,0
71,31,1,1
72,32,0,0
73,33,1,1
74,34,0,0
75,35,1,1
76,36,0,0
77,37,1,1
78,38,0,0
79,39,1,1
```

A reasonable configuration for two simulations might be `taskset --cpu-list
0-39:2` (CPUs 0,2,...,38) and `taskset --cpu-list 1-39:2`. (CPUs 1,3,...,39).
This assignment leaves CPUs 40-79 idle, since those share the same physical
cores at CPUs 0-39, puts the first simulation on socket 0 and numa node 0, and
the second simulation on socket 1 and numa node 1.