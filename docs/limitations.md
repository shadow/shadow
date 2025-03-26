# Limitations and workarounds

Shadow can typically run applications without modification, but there are a few
limitations to be aware of.

If you are severely affected by one of these limitations (or another not listed
here) let us know, as this can help us prioritize our improvements to Shadow.
You may reach out in our
[discussions](https://github.com/shadow/shadow/discussions) or
[issues](https://github.com/shadow/shadow/issues).

## Unimplemented system calls and options

When Shadow encounters a syscall or a syscall option that it hasn't implemented,
it will generally return `ENOSYS` and log at `warn` level or higher. In many
such cases the application is able to recover, and this has little or no effect
on the ultimate results of the simulation.

There are some syscalls that shadow doesn't quite emulate faithfully, but has a
"best effort" implementation. As with unimplemented sysalls, shadow logs at
`warn` level when encountering such a syscall.

### vfork

A notable example of a not-quite faithfully implemented syscall is
[`vfork`](https://www.man7.org/linux/man-pages/man2/vfork.2.html), which shadow
effectively implements as a synonym for `fork`. Usage of `vfork` that is
compliant with the POSIX.1 specification that "behavior is undefined if the
process created by vfork() either modifies any data other than a variable of
type pid_t used to store the return value...". However, usage that relies on
specific Linux implementation details of `vfork` (e.g. that a write to a global
variable from the child will be observed by the parent) won't work correctly.

As in other such cases, shadow logs a warning when it encounters `vfork`, so
that users can identify it as the potential source of problems if a simulation
doesn't work as expected.

## IPv6

Shadow does not yet implement IPv6. Most applications can be configured to use IPv4
instead. Tracking issue: [#2216](https://github.com/shadow/shadow/issues/2216]).

## Statically linked executables

Shadow relies on `LD_PRELOAD` to inject code into the managed processes. This
doesn't work for statically linked executables. Tracking issue:
[#1839](https://github.com/shadow/shadow/issues/1839).

Most applications can be dynamically linked, though occasionally you may need to
edit build scripts and/or recompile.

### golang

`golang` typically defaults to producing statically linked executables, unless
the application uses `cgo`. Using the networking functionality of `golang`'s
standard library usually pulls in `cgo` by default and thus results in a
dynamically linked executable.

You can also explicitly force `go` to produce a dynamically linked executable. e.g.

```
# Install a dynamically linked `std`
go install -buildmode=shared std
# Build your application with dynamic linking
go build -linkshared myapp.go
```

## Busy loops

By default, Shadow runs each thread of managed processes until it's blocked by a
syscall such as `nanosleep`,  `read`, `select`, `futex`, or `epoll`. Likewise,
time only moves forward when Shadow is blocked on such a call - Shadow
effectively models the CPU as being infinitely fast. This model is generally
sufficient for modeling non-CPU-bound network applications.

Unfortunately this model can lead to deadlock in the case of "busy loops", where
a thread repeatedly checks for something to happen indefinitely or until some
amount of wall-clock-time has passed. e.g., a worker thread might repeatedly
check whether work is available for some amount of time before going to sleep on
a `futex`, to avoid the latency of going to sleep and waking back up in cases
where work arrives quickly. However since Shadow normally doesn't advance time
when making non-blocking syscalls or allow other threads to run, such a loop can
run indefinitely, deadlocking the whole simulation.

### Ideal solution: modify the managed program

Even outside of Shadow, it's usually good practice for such loops to have a
bound on the number of iterations instead of or in addition to a bound on
wallclock time. When feasible, modifying the relevant loops to do this, and
better yet upstreaming that modification, is typically the ideal solution.

### Workaround: have Shadow model unblocked syscall latency

For cases where modifying the loop is infeasible, and the busy loop has a bound
on wallclock time or contains some other syscall (such as `sched_yield`), Shadow
provides the option `--model-unblocked-syscall-latency`. When this option is
enabled, Shadow moves time forward a small amount on *every* syscall (and VDSO
function call, and time-check via `rdtsc` instruction), and switches to another
thread if one becomes runnable in the meantime (e.g. because network data
arrived when the clock moved forward, unblocking it).

This feature should only be used when it's needed to get around such loops. Some
limitations:

* It may cause the simulation to run slower.

  * Enabling this feature forces Shadow to switch between threads more
  frequently, which is costly and hurts cache performance. We have minimized
  this effect to the extent that we can, but it can especially hurt performance
  when there are multiple unblocked threads on a single simulated Host, forcing
  Shadow to keep switching between them to keep the simulated time synchronized.

  * Busy loops intrinsically waste some CPU cycles. Outside of Shadow this can
  be a tradeoff for improved latency by avoiding a thread switch. However, in a
  Shadow simulation this latency isn't modeled, so busy-looping instead of
  blocking immediately has no benefit to simulated performance; only cost to
  simulation performance. If feasible, changing the busy-loop to block
  immediately instead of spinning should improve simulation performance without
  substantially affecting simulation results.

* It's not meant as an accurate model of syscall latency. It generally models
syscalls as being somewhat faster than they would be on a real system to minimize
the impact on simulation results.

* Nonetheless it *does* affect simulation results. Arguably this model
is more accurate, since syscalls on real systems *do* take non-zero time, but it
makes the time model more complex to understand and reason about.

### Workaround: have Shadow preempt CPU-only busy-loops{#cpu-busy-loops}

In cases where enabling `--model-unblocked-syscall-latency` doesn't get the
simulation out of the busy loop, it may be because the busy-loop makes no syscalls or
time-checks at all, and instead is waiting indefinitely for another thread to
modify memory (e.g. to flip a "ready" flag). In Shadow's default mode of
operation, it will never regain control from such loops, and hence can't move
the simulation forward.

Such loops can be escaped by enabling the experimental option
`--native-preemption-enabled`. In this mode of operation, Shadow uses a native
Linux timer to preempt the thread in such situations, moving simulated time
forward and allowing other threads in the process to run.

Drawbacks:

* Loss of simulation determinism. In different runs of the simulation, the native timer
  may fire at different times. In cases where it is truly interrupting a
  CPU-only-busy-loop that would otherwise run indefinitely, this is unlikely to
  ultimately change the results of the simulation, but *could*, e.g. if the busy
  loop counts how many times it iterates and that count is used later. Worse,
  the native timer may fire during long CPU-only operations that *would* finish
  on their own (e.g. copying, encrypting, or decrypting large chunks of data in
  memory), firing at different points in some runs of the simulation, or not at
  all in some runs of the simulation.

* It causes the simulation to run slower.

  * Even when the timer never fires, there is substantial additional overhead
    for the additional bookkeeping and management of the native timers.

  * As with `--model-unblocked-syscall-latency`, when this feature actually does
    cause a thread to be rescheduled, there is some performance overhead for that
    rescheduling.

* It's not meant as an accurate model of CPU-time spent; for that, see
  [#2060](https://github.com/shadow/shadow/issues/2060). e.g. CPU-time consumed
  between syscalls still takes 0 simulation time, if not enough time passes for
  the native timer to fire.

* Nonetheless it *does* affect simulation results (in simulations where the
  timer actually triggers a preemption). Arguably this makes the simulation
  somewhat more accurate, since it at least causes some CPU-heavy sections of
  code that make no syscalls to take some non-zero amount of simulated time
  instead of zero simulated time, but it also makes the passing of simulated
  time more complex to understand (and potentially non-deterministic, as noted
  above).

### Further discussion

For more about this topic, see [#1792](https://github.com/shadow/shadow/issues/1792).
