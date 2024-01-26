# Configuration options

Shadow's configuration options are generally tuned for optimal performance
using Tor benchmarks, but not all system architectures and simulation workloads
are the same. Shadow has several configuration options that may improve the
simulation performance. Many of these options are considered "experimental",
which means that they may be changed or removed at any time. If you find any of
these options useful, [let us know][discussions].

Be careful as these options may also *worsen* the simulation performance.

[discussions]: https://github.com/shadow/shadow/discussions

### [`bootstrap_end_time`][bootstrap_end_time]

Shadow supports an optional "bootstrapping period" of high network
bandwidth and reliability for simulations which require network-related
bootstrapping (for example Tor). While the network performance characteristics
will be unrealistic during this time period, it can significantly reduce the
simulation's wall clock time. After this bootstrapping period ends, the network
bandwidth/reliability is reverted back to the values specified in the
simulation and network configuration.

[bootstrap_end_time]: https://shadow.github.io/docs/guide/shadow_config_spec.html#generalbootstrap_end_time

### [`heartbeat_interval`][heartbeat_interval] and [`host_heartbeat_interval`][host_heartbeat_interval]

Shadow logs simulation statistics at given simulation time intervals. If any of
these time intervals are small relative to the [total time][stop_time] of the
simulation, a large number of log lines will be written. If the log is being
written to disk, this increased disk I/O may slow down the simulation
dramatically.

[heartbeat_interval]: https://shadow.github.io/docs/guide/shadow_config_spec.html#generalheartbeat_interval
[host_heartbeat_interval]: https://shadow.github.io/docs/guide/shadow_config_spec.html#experimentalhost_heartbeat_interval
[stop_time]: https://shadow.github.io/docs/guide/shadow_config_spec.html#generalstop_time

### [`parallelism`][parallelism]

Simulations with multiple hosts can be parallelized across multiple threads. By
default Shadow tries to choose an optimal number of threads to run in parallel,
but a different number of threads may yield better run time performance.

[parallelism]: https://shadow.github.io/docs/guide/shadow_config_spec.html#generalparallelism

### [`use_cpu_pinning`][use_cpu_pinning]

CPU pinning is enabled by default and should improve the simulation
performance, but in shared computing environments it might be beneficial to
disable this option.

[use_cpu_pinning]: https://shadow.github.io/docs/guide/shadow_config_spec.html#experimentaluse_cpu_pinning

### [`scheduler`][scheduler]

Shadow supports two different types of work schedulers. The default
`thread_per_core` scheduler has been found to be significantly faster on most
machines, but may perform worse than the `thread_per_host` scheduler in rare
circumstances.

[scheduler]: https://shadow.github.io/docs/guide/shadow_config_spec.html#experimentalscheduler

### [`use_memory_manager`][use_memory_manager]

Shadow supports a memory manager that uses shared memory maps to reduce the
overhead of accessing a managed process' data from Shadow's main process, but
this is disabled by default as it does not support other Shadow features such
as emulating the fork/exec syscalls. If you do not need support for these
features, enabling this memory manager may slightly improve simulation
performance.

[use_memory_manager]: https://shadow.github.io/docs/guide/shadow_config_spec.html#experimentaluse_memory_manager

### [`use_worker_spinning`][use_worker_spinning]

Shadow's thread-per-core scheduler uses a spinloop by default. While this
results in significant performance improvements in our benchmarks, it may be
worth testing Shadow's performance with this disabled.

[use_worker_spinning]: https://shadow.github.io/docs/guide/shadow_config_spec.html#experimentaluse_worker_spinning

### [`max_unapplied_cpu_latency`][max_unapplied_cpu_latency]

If [`model_unblocked_syscall_latency`][model_unblocked_syscall_latency] is
enabled, increasing the max unapplied CPU latency may improve the simulation
run time performance.

[max_unapplied_cpu_latency]: https://shadow.github.io/docs/guide/shadow_config_spec.html#experimentalmax_unapplied_cpu_latency
[model_unblocked_syscall_latency]: https://shadow.github.io/docs/guide/shadow_config_spec.html#generalmodel_unblocked_syscall_latency

### [`runahead`][runahead]

This option effectively sets a minimum network latency. Increasing this value
will allow for better simulation parallelisation and possibly better run time
performance, but will affect the network characteristics of the simulation.

[runahead]: https://shadow.github.io/docs/guide/shadow_config_spec.html#experimentalrunahead
