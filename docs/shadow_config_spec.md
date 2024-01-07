# Shadow Configuration Specification

Shadow uses the standard [YAML 1.2](https://yaml.org/spec/1.2.2/) format to
accept configuration options, with the following extensions:

* [merge keys](https://yaml.org/type/merge.html)
* [extension fields](https://docs.docker.com/compose/compose-file/compose-file-v3/#extension-fields)

The following describes Shadow's YAML format and all of the options that Shadow
supports that can be used to customize a simulation.

Example:

```yaml
general:
  stop_time: 2 min
network:
  graph:
    type: gml
    inline: |
      graph [
        node [
          id 0
          host_bandwidth_down "140 Mbit"
          host_bandwidth_up "18 Mbit"
        ]
        edge [
          source 0
          target 0
          latency "50 ms"
          packet_loss 0.01
        ]
      ]
hosts:
  server:
    network_node_id: 0
    processes:
    - path: /usr/sbin/nginx
      args: -c ../../../nginx.conf -p .
      start_time: 1
      expected_final_state: running
  client1: &client_host
    network_node_id: 0
    host_options:
      log_level: debug
    processes:
    - path: /usr/bin/curl
      args: server --silent
      start_time: 5
  client2: *client_host
  client3: *client_host
```

## Contents:

- [`general`](#general)
- [`general.bootstrap_end_time`](#generalbootstrap_end_time)
- [`general.data_directory`](#generaldata_directory)
- [`general.heartbeat_interval`](#generalheartbeat_interval)
- [`general.log_level`](#generallog_level)
- [`general.model_unblocked_syscall_latency`](#generalmodel_unblocked_syscall_latency)
- [`general.parallelism`](#generalparallelism)
- [`general.progress`](#generalprogress)
- [`general.seed`](#generalseed)
- [`general.stop_time`](#generalstop_time)
- [`general.template_directory`](#generaltemplate_directory)
- [`network`](#network)
- [`network.graph`](#networkgraph)
- [`network.graph.type`](#networkgraphtype)
- [`network.graph.<file|inline>`](#networkgraphfileinline)
- [`network.graph.file.path`](#networkgraphfilepath)
- [`network.graph.file.compression`](#networkgraphfilecompression)
- [`network.use_shortest_path`](#networkuse_shortest_path)
- [`experimental`](#experimental)
- [`experimental.host_heartbeat_interval`](#experimentalhost_heartbeat_interval)
- [`experimental.host_heartbeat_log_info`](#experimentalhost_heartbeat_log_info)
- [`experimental.host_heartbeat_log_level`](#experimentalhost_heartbeat_log_level)
- [`experimental.interface_qdisc`](#experimentalinterface_qdisc)
- [`experimental.max_unapplied_cpu_latency`](#experimentalmax_unapplied_cpu_latency)
- [`experimental.runahead`](#experimentalrunahead)
- [`experimental.scheduler`](#experimentalscheduler)
- [`experimental.socket_recv_autotune`](#experimentalsocket_recv_autotune)
- [`experimental.socket_recv_buffer`](#experimentalsocket_recv_buffer)
- [`experimental.socket_send_autotune`](#experimentalsocket_send_autotune)
- [`experimental.socket_send_buffer`](#experimentalsocket_send_buffer)
- [`experimental.strace_logging_mode`](#experimentalstrace_logging_mode)
- [`experimental.unblocked_syscall_latency`](#experimentalunblocked_syscall_latency)
- [`experimental.unblocked_vdso_latency`](#experimentalunblocked_vdso_latency)
- [`experimental.use_cpu_pinning`](#experimentaluse_cpu_pinning)
- [`experimental.use_dynamic_runahead`](#experimentaluse_dynamic_runahead)
- [`experimental.use_memory_manager`](#experimentaluse_memory_manager)
- [`experimental.use_new_tcp`](#experimentaluse_new_tcp)
- [`experimental.use_object_counters`](#experimentaluse_object_counters)
- [`experimental.use_preload_libc`](#experimentaluse_preload_libc)
- [`experimental.use_preload_openssl_crypto`](#experimentaluse_preload_openssl_crypto)
- [`experimental.use_preload_openssl_rng`](#experimentaluse_preload_openssl_rng)
- [`experimental.use_sched_fifo`](#experimentaluse_sched_fifo)
- [`experimental.use_syscall_counters`](#experimentaluse_syscall_counters)
- [`experimental.use_worker_spinning`](#experimentaluse_worker_spinning)
- [`experimental.log_errors_to_stderr`](#experimentallog_errors_to_stderr)
- [`host_option_defaults`](#host_option_defaults)
- [`host_option_defaults.log_level`](#host_option_defaultslog_level)
- [`host_option_defaults.pcap_capture_size`](#host_option_defaultspcap_capture_size)
- [`host_option_defaults.pcap_enabled`](#host_option_defaultspcap_enabled)
- [`hosts`](#hosts)
- [`hosts.<hostname>.bandwidth_down`](#hostshostnamebandwidth_down)
- [`hosts.<hostname>.bandwidth_up`](#hostshostnamebandwidth_up)
- [`hosts.<hostname>.ip_addr`](#hostshostnameip_addr)
- [`hosts.<hostname>.network_node_id`](#hostshostnamenetwork_node_id)
- [`hosts.<hostname>.host_options`](#hostshostnamehost_options)
- [`hosts.<hostname>.processes`](#hostshostnameprocesses)
- [`hosts.<hostname>.processes[*].args`](#hostshostnameprocessesargs)
- [`hosts.<hostname>.processes[*].environment`](#hostshostnameprocessesenvironment)
- [`hosts.<hostname>.processes[*].expected_final_state`](#hostshostnameprocessesexpected_final_state)
- [`hosts.<hostname>.processes[*].path`](#hostshostnameprocessespath)
- [`hosts.<hostname>.processes[*].shutdown_signal`](#hostshostnameprocessesshutdown_signal)
- [`hosts.<hostname>.processes[*].shutdown_time`](#hostshostnameprocessesshutdown_time)
- [`hosts.<hostname>.processes[*].start_time`](#hostshostnameprocessesstart_time)

#### `general`

*Required*

General experiment settings.

#### `general.bootstrap_end_time`

Default: "0 sec"  
Type: String OR Integer

The simulated time that ends Shadow's high network bandwidth/reliability
bootstrap period.

If the bootstrap end time is greater than 0, Shadow uses a simulation
bootstrapping period where hosts have unrestricted network bandwidth and no
packet drop. This can help to bootstrap large networks quickly when the network
hosts have low network bandwidth or low network reliability.

#### `general.data_directory`

Default: "shadow.data"  
Type: String

Path to store simulation output.

#### `general.heartbeat_interval`

Default: "1 sec"  
Type: String OR Integer OR null

Interval at which to print simulation heartbeat messages.

#### `general.log_level`

Default: "info"  
Type: "error" OR "warning" OR "info" OR "debug" OR "trace"

Log level of output written on stdout. If Shadow was built in release mode, then
messages at level 'trace' will always be dropped.

#### `general.model_unblocked_syscall_latency`

Default: false  
Type: Bool

Whether to model syscalls and VDSO functions that don't block as having some
latency. This should have minimal effect on typical simulations, but can be
helpful for programs with "busy loops" that otherwise deadlock under Shadow.

#### `general.parallelism`

Default: 0  
Type: Integer

How many parallel threads to use to run the simulation. Optimal performance is usually obtained with
the number of *physical* CPU cores (`nproc` without hyperthreading or `nproc`/2 with
hyperthreading).

A value of 0 will allow Shadow to choose the number of threads, typically the number of physical CPU
cores available in the current CPU affinity mask and cgroup.

Virtual hosts depend on network packets that can potentially arrive from other
virtual hosts, so each worker can only advance according to the propagation
delay to avoid dependency violations. Therefore, not all threads will have 100%
CPU utilization.

#### `general.progress`

Default: false  
Type: Bool

Show the simulation progress on stderr.

When running in a tty, the progress will be updated every second and shown at
the bottom of the terminal. Otherwise the progress will be printed without ANSI
escape codes at intervals which increase as the simulation progresses.

#### `general.seed`

Default: 1  
Type: Integer

Initialize randomness using seed N.

#### `general.stop_time`

*Required*  
Type: String OR Integer

The simulated time at which the simulation ends.

#### `general.template_directory`

Default: null  
Type: String OR null

Path to recursively copy during startup and use as the data-directory.

#### `network`

*Required*

Network settings.

#### `network.graph`

*Required*

The network topology graph.

A network topology represented by a connected graph with certain attributes
specified on the network nodes and edges. For more information on how to
structure this data, see the [Network Graph Overview](network_graph_overview.md).

Example:

```yaml
network:
  graph:
    type: gml
    inline: |
      graph [
        ...
      ]
```

#### `network.graph.type`

*Required*  
Type: "gml" OR "1\_gbit\_switch"

The network graph can be specified in the GML format, or a built-in
"1\_gbit\_switch" graph with a single network node can be used instead.

The built-in "1\_gbit\_switch" graph contains the following:

```text
graph [
  directed 0
  node [
    id 0
    host_bandwidth_up "1 Gbit"
    host_bandwidth_down "1 Gbit"
  ]
  edge [
    source 0
    target 0
    latency "1 ms"
    packet_loss 0.0
  ]
]
```

#### `network.graph.<file|inline>`

*Required if `network.graph.type` is "gml"*  
Type: Object OR String

If the network graph type is not a built-in network graph, the graph data can be
specified as a path to an external file, or as an inline string.

#### `network.graph.file.path`

*Required*  
Type: String

The path to the file.

If the path begins with `~/`, it will be considered relative to the current
user's home directory. No other shell expansion is performed on the path.

#### `network.graph.file.compression`

Default: null  
Type: "xz" OR null

The file's compression format.

#### `network.use_shortest_path`

Default: true  
Type: Bool

When routing packets, follow the shortest path rather than following a direct
edge between network nodes. If false, the network graph is required to be
complete (including self-loops) and to have exactly one edge between any two
nodes.

#### `experimental`

Experimental experiment settings. Unstable and may change or be removed at any
time, regardless of Shadow version.

#### `experimental.host_heartbeat_interval`

Default: "1 sec"  
Type: String OR Integer OR null

Amount of time between host heartbeat messages.

#### `experimental.host_heartbeat_log_info`

Default: ["node"]  
Type: Array of ("node" OR "socket" OR "ram")

List of information to show in the host's heartbeat message.

#### `experimental.host_heartbeat_log_level`

Default: "info"  
Type: "error" OR "warning" OR "info" OR "debug" OR "trace"

Log level at which to print host heartbeat messages.

#### `experimental.interface_qdisc`

Default: "fifo"  
Type: "fifo" OR "round-robin"

The queueing discipline to use at the network interface.

#### `experimental.max_unapplied_cpu_latency`

Default: "1 microsecond"  
Type: String

Max amount of execution-time latency allowed to accumulate before the clock is
moved forward. Moving the clock forward is a potentially expensive operation, so
larger values reduce simulation overhead, at the cost of coarser time jumps.

Note also that accumulated-but-unapplied latency is discarded when a thread is
blocked on a syscall.

Ignored when
[`general.model_unblocked_syscall_latency`](#generalmodel_unblocked_syscall_latency)
is false.

#### `experimental.runahead`

Default: "1 ms"  
Type: String OR null

If set, overrides the automatically calculated minimum time workers may run
ahead when sending events between virtual hosts.

#### `experimental.scheduler`

Default: "thread-per-core"  
Type: "thread-per-core" OR "thread-per-host"

The host scheduler implementation, which decides how to assign hosts to threads
and threads to CPU cores.

#### `experimental.socket_recv_autotune`

Default: true  
Type: Bool

Enable receive window autotuning.

#### `experimental.socket_recv_buffer`

Default: "174760 B"  
Type: String OR Integer

Initial size of the socket's receive buffer.

#### `experimental.socket_send_autotune`

Default: true  
Type: Bool

Enable send window autotuning.

#### `experimental.socket_send_buffer`

Default: "131072 B"  
Type: String OR Integer

Initial size of the socket's send buffer.

#### `experimental.strace_logging_mode`

Default: "off"  
Type: "off" OR "standard" OR "deterministic"

Log the syscalls for each process to individual "strace" files.

The mode determines the format that the syscalls are logged in. For example,
the "deterministic" mode will avoid logging memory addresses or potentially
uninitialized memory.

The logs will be stored at
`shadow.data/hosts/<hostname>/<procname>.<pid>.strace`.

Limitations:

- Syscalls run natively will not log the syscall arguments or return value (for
  example `SYS_getcwd`).
- Syscalls processed within Shadow's C code will not log the syscall arguments.
- Syscalls that are interrupted by a signal may not be logged (for example
  `SYS_read`).
- Syscalls that are interrupted by a signal may be logged inaccurately. For
  example, the log may show `syscall(...) = -1 (EINTR)`, but the managed
  process may not actually see this return value. Instead the syscall may be
  restarted.

#### `experimental.unblocked_syscall_latency`

Default: "1 microseconds"  
Type: String

The simulated latency of an unblocked syscall. For simulation efficiency, this
latency is only added when `max_unapplied_cpu_latency` is reached.

Ignored when
[`general.model_unblocked_syscall_latency`](#generalmodel_unblocked_syscall_latency)
is false.

#### `experimental.unblocked_vdso_latency`

Default: "10 nanoseconds"  
Type: String

The simulated latency of an unblocked vdso function. For simulation efficiency, this
latency is only added when `max_unapplied_cpu_latency` is reached.

Ignored when
[`general.model_unblocked_syscall_latency`](#generalmodel_unblocked_syscall_latency)
is false.

#### `experimental.use_cpu_pinning`

Default: true  
Type: Bool

Pin each thread and any processes it executes to the same logical CPU Core to
improve cache affinity.

#### `experimental.use_dynamic_runahead`

Default: false  
Type: Bool

Update the minimum runahead dynamically throughout the simulation.

#### `experimental.use_memory_manager`

Default: false  
Type: Bool

Use the MemoryManager in memory-mapping mode. This can improve
performance, but disables support for dynamically spawning processes
inside the simulation (e.g. the `fork` syscall).

#### `experimental.use_new_tcp`

Default: false  
Type: Bool

Use the rust TCP implementation.

#### `experimental.use_object_counters`

Default: true  
Type: Bool

Count object allocations and deallocations. If disabled, we will not be able to
detect object memory leaks.

#### `experimental.use_preload_libc`

Default: true  
Type: Bool

Preload our libc library for all managed processes for fast syscall
interposition when possible.

#### `experimental.use_preload_openssl_crypto`

Default: false  
Type: Bool

Preload our OpenSSL crypto library for all managed processes to skip some AES
crypto operations, which may speed up simulation if your CPU lacks AES-NI
support. However, it changes the behavior of your application and can cause bugs
in OpenSSL that are hard to notice. You should probably not use this option
unless you really know what you're doing.

#### `experimental.use_preload_openssl_rng`

Default: true  
Type: Bool

Preload our OpenSSL RNG library for all managed processes to mitigate
non-deterministic use of OpenSSL.

#### `experimental.use_sched_fifo`

Default: false  
Type: Bool

Use the `SCHED_FIFO` scheduler. Requires `CAP_SYS_NICE`. See sched(7),
capabilities(7).

#### `experimental.use_syscall_counters`

Default: true  
Type: Bool

Count the number of occurrences for individual syscalls.

#### `experimental.use_worker_spinning`

Default: true  
Type: Bool

Each worker thread will spin in a `sched_yield` loop while waiting for a new task. This is ignored
if not using the thread-per-core scheduler.

This may improve runtime performance in some environments.

#### `experimental.log_errors_to_tty`

Default: true  
Type: Bool

Log `Error`-level log lines to shadow's `stderr` in addition to `stdout`, if
`stdout` is not a tty but `stderr` is.

#### `host_option_defaults`

Default options for all hosts. These options can also be overridden for each
host individually in the host's [`hosts.<hostname>.host_options`](#hostshostnamehost_options)
section.

#### `host_option_defaults.log_level`

Default: null  
Type: "error" OR "warning" OR "info" OR "debug" OR "trace" OR null

Log level at which to print host log messages.

#### `host_option_defaults.pcap_capture_size`

Default: "65535 B"  
Type: String OR Integer

How much data to capture per packet (header and payload) if pcap logging is
enabled.

The default of 65535 bytes is the maximum length of an IP packet.

#### `host_option_defaults.pcap_enabled`

Default: false  
Type: Bool

Should Shadow generate pcap files?

Logs all network input and output for this host in PCAP format (for viewing in
e.g. wireshark). The pcap files will be stored in the host's data directory,
for example `shadow.data/hosts/myhost/eth0.pcap`.

#### `hosts`

*Required*  
Type: Object

The simulated hosts which execute processes. Each field corresponds to a host
configuration, with the field name being used as the network hostname. A
hostname must follow the character requirements of
[hostname(7)](https://man7.org/linux/man-pages/man7/hostname.7.html).

Shadow assigns each host to a network node in the [network graph](network_graph_overview.md).

In Shadow, each host is given an RNG whose seed is derived from the global seed
([`general.seed`](#generalseed)) and the hostname. This means that changing a
host's name will change that host's RNG seed, subtly affecting the simulation
results.

#### `hosts.<hostname>.bandwidth_down`

Default: null  
Type: String OR Integer OR null

Downstream bandwidth capacity of the host.

Overrides any default bandwidth values set in the assigned network graph
node.

#### `hosts.<hostname>.bandwidth_up`

Default: null  
Type: String OR Integer OR null

Upstream bandwidth capacity of the host.

Overrides any default bandwidth values set in the assigned network graph
node.

#### `hosts.<hostname>.ip_addr`

Default: null  
Type: String OR null

IP address to assign to the host.

This IP address must not conflict with the address of any other host (two hosts
must not have the same IP address).

#### `hosts.<hostname>.network_node_id`

*Required*  
Type: Integer

Network graph node ID to assign the host to.

#### `hosts.<hostname>.host_options`

See [`host_option_defaults`](#host_option_defaults) for supported fields.

Example:

```yaml
hosts:
  client:
    ...
    host_options:
      log_level: debug
```

#### `hosts.<hostname>.processes`

*Required*  
Type: Array

Virtual software processes that the host will run.

#### `hosts.<hostname>.processes[*].args`

Default: ""  
Type: String OR Array of String

Process arguments.

The arguments can be specified as a string in a shell command-line format:

```yaml
args: "--user-agent 'Mozilla/5.0 (compatible; ...)' http://myserver:8080"
```

Or as an array of strings:

```yaml
args: ['--user-agent', 'Mozilla/5.0 (compatible; ...)', 'http://myserver:8080']
```

Shell expansion (which includes `~/` expansion) is not performed on either
format. In the command-line format, the string is parsed as an argument vector
following typical shell quotation parsing rules.

#### `hosts.<hostname>.processes[*].environment`

Default: ""  
Type: Object

Environment variables passed when executing this process.

Shell expansion (which includes `~/` expansion) is not performed on any fields.

Examples:

```yaml
environment:
  ENV_A: "1"
  ENV_B: foo
```

```yaml
environment: { ENV_A: "1", ENV_B: foo }
```

#### `hosts.<hostname>.processes[*].expected_final_state`

Default: \{exited: 0\}  
Type: \{"exited": \<Integer\>\} OR \{"signaled": [Unix Signal](./shadow_config_overview.md#unix-signals)\} OR "running"

The expected state of the process at the end of the simulation. If the process
exits before the end of the simulation with an unexpected state, or is still running
at the end of the simulation when this was not `running`, shadow will log an error
and return a non-zero status for the simulation.

Use `exited` to indicate that a process should have exited normally; e.g. by returning
from `main` or calling `exit`.

Use `signaled` to indicate that a process should have been killed by a signal.

Use `running` for a process expected to still be running at the end of the simulation,
such as a server process that you didn't arrange to shutdown before the end of the simulation.
(All processes will be killed by Shadow when the simulation ends).

Examples:

- `{exited: 0}`
- `{exited: 1}`
- `{signaled: SIGINT}`
- `{signaled: 9}`
- `running`

Only processes started directly from the configuration have an
`expected_final_state`. Processes that *those* processes start (e.g. via `fork`
in C, or running an executable in a shell script) don't have one. Generally it's
the parent process's responsibility to do any necessary validation of the exit
status of its children (e.g. via `waitpid` in C, or checking `$?` in a bash
script).

#### `hosts.<hostname>.processes[*].path`

*Required*  
Type: String

If the path begins with `~/`, it will be considered relative to the current
user's home directory. No other shell expansion is performed on the path.

Bare file basenames like `sleep` will be located using Shadow's `PATH`
environment variable (e.g. to `/usr/bin/sleep`).

#### `hosts.<hostname>.processes[*].shutdown_signal`

Default: "SIGTERM"  
Type: [Unix Signal](./shadow_config_overview.md#unix-signals)

The signal that will be sent to the process at
[`hosts.<hostname>.processes[*].shutdown_time`](#hostshostnameprocessesshutdown_time).
Signals specified by name should be all-caps and include the SIG prefix; e.g.
"SIGTERM".

Many long-running processes support exiting cleanly when sent `SIGTERM` or
`SIGINT`.

If the process is expected to be killed directly by the signal instead of
catching it and exiting cleanly, you can set
[`expected_final_state`](#hostshostnameprocessesexpected_final_state) to prevent
Shadow from interpreting this as an error. e.g. `SIGKILL` cannot be caught, so
will always result in an end state of `{signaled: SIGKILL}` if the process didn't
already exit before the signal was sent.

```yaml
path: sleep
args: "1000"
start_time: 1s
shutdown_time: 2s
shutdown_signal: SIGKILL
expected_final_state: {signaled: SIGKILL}
```

#### `hosts.<hostname>.processes[*].shutdown_time`

Default: null  
Type: String OR Integer OR null

The simulated time at which to send
[`hosts.<hostname>.processes[*].shutdown_signal`](#hostshostnameprocessesshutdown_signal)
to the process. This must be before [`general.stop_time`](#generalstop_time).

#### `hosts.<hostname>.processes[*].start_time`

Default: "0 sec"  
Type: String OR Integer

The simulated time at which to execute the process. This must be before
[`general.stop_time`](#generalstop_time).
