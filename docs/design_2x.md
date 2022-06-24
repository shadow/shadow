# Shadow Design Overview

Shadow is a multi-threaded network experimentation tool that is designed as a
hybrid between simulation and emulation architectures: it directly executes
applications as Linux processes, but runs them in the context of a
discrete-event network simulation.

Shadow's version 2 design is summarized in the following sections. Please see
the end of this document for references to published design articles with more
details.

## Executing Applications

Shadow directly executes real, unmodified application binaries natively in Linux
as standard OS processes (using `vfork()` and `execvpe()`): we call these
processes executed by Shadow _managed processes_. When executing each managed
 process, Shadow dynamically injects a shim library using preloading (via the
`LD_PRELOAD` environment variable) and establishes an inter-process control
channel using shared memory and semaphores. The control channel enables Shadow
to exchange messages with the shim and to instruct the shim to perform actions
in the managed process space.

## Intercepting System Calls

The shim co-opts each running managed process into the simulation environment by
intercepting all system calls they make rather than allowing them to be handled
by the Linux kernel. System call interception happens through two methods: first
via preloading and second via a seccomp filter.

- Preloading: Because the shim is preloaded, the shim will be the first library
that is searched when attempting to dynamically resolve symbols. We use the shim
to override functions in other shared libraries (e.g., system call wrapper
functions from libc) by supplying identically named functions with alternative
implementations inside the shim. Note that preloading works on dynamically
linked function calls (e.g., to libc system call wrappers), but not on
statically linked function calls (e.g. those made from inside of libc) or system
calls made using a `syscall` instruction.

- seccomp: System calls that are not interceptable via preloading are
intercepted using the kernel's seccomp facility. The shim of each managed
process installs a seccomp filter that traps all system calls (except those made
from the shim) and a handler function to handle the trapped system calls. This
facility has a very small overhead because it involves running the installed
filter in kernel mode, but we infrequently incur this overhead in practice since
most system calls are interceptable via the more efficient preloading method.

## Emulating System Calls

System calls that are intercepted by the shim (using either preloading or
seccomp) are emulated by Shadow. Hot-path system calls (e.g., time-related
system calls) are handled directly in the shim by using state that is stored in
shared memory. Other system calls are sent from the shim to Shadow via the
control channel and handled in Shadow (the shim sends the system call number and
argument registers). While the shim is waiting for a system call to be serviced
by Shadow, the managed process is blocked; this allows Shadow to precisely
control the running state of each process.

Shadow emulates system calls using its simulated kernel. The simulated kernel
(re)implements (i.e., simulates) important system functionality, including: the
passage of time; input and output operations on file, socket, pipe, timer, and
event descriptors; signals; packet transmissions with respect to transport layer
protocols such as TCP and UDP; and aspects of computer networking including
routing, queuing, and bandwidth limits. Thus, Shadow establishes a private,
simulated network environment that is completely isolated from the real network,
but is internally interoperable and entirely controllable.

Care is taken to ensure that all random bytes that are needed during the
simulation are initiated from a seeded pseudorandom source, including during the
emulation of system calls such as `getrandom()` and when emulating reads from
files like `/dev/*random`. This enables Shadow to produce deterministic
simulations, i.e., running a simulation twice using the same inputs and the same
seed should produce the same sequence of operations in the managed process.

## Managing Memory

Some system calls pass dynamically allocated memory addresses (e.g., the buffer
address in the `sendto()` system call). To handle this system call in Shadow,
this shim sends the buffer address but not the buffer contents to Shadow. Shadow
uses an inter-process memory access manager to directly and efficiently read and
write the memory of each managed process without extraneous data copies or
control messages. Briefly, the memory manager (re)maps the memory of each
managed process into a shared memory file that is accessible by both Shadow and
the managed process. When Shadow needs to copy data from a memory address passed
to it by the shim, the memory manager translates the managed process's memory
address to a shared memory address and brokers requested data copies. This
approach minimizes the number of data copies and system calls needed to transfer
the buffer contents from the managed process to Shadow.

## Scheduling

Shadow is designed to be high performance: it uses a thread for every virtual
host configured in an experiment while only allowing a number of threads equal
to the number of available CPU cores to run in parallel to avoid performance
degradation caused by CPU oversubscription. Work stealing is used to ensure that
each core is always running a worker thread as long as remaining work exists.
Shadow also effectively uses CPU pinning to reduce the frequency of cache
misses, CPU migrations, and context switches.

# Research

Shadow's design is based on the following published research articles. Please
cite our work when using Shadow in your projects.

## Shadow version 2 (latest)

This is the latest v2 design described above:

Co-opting Linux Processes for High-Performance Network Simulation  
by Rob Jansen, Jim Newsome, and Ryan Wails  
in the 2022 USENIX Annual Technical Conference, 2022.

```
@inproceedings{netsim-atc2022,
  author = {Rob Jansen and Jim Newsome and Ryan Wails},
  title = {Co-opting Linux Processes for High-Performance Network Simulation},
  booktitle = {USENIX Annual Technical Conference},
  year = {2022},
  note = {See also \url{https://netsim-atc2022.github.io}},
}
```

## Shadow version 1 (original)

This is the original v1 design, using plugins loaded into the Shadow process
rather than independent processes:

Shadow: Running Tor in a Box for Accurate and Efficient Experimentation  
by Rob Jansen and Nicholas Hopper  
in the Symposium on Network and Distributed System Security, 2012.

```
@inproceedings{shadow-ndss12,
  title = {Shadow: Running Tor in a Box for Accurate and Efficient Experimentation},
  author = {Rob Jansen and Nicholas Hopper},
  booktitle = {Symposium on Network and Distributed System Security},
  year = {2012},
  note = {See also \url{https://shadow.github.io}},
}
```
