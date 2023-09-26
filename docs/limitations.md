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

When feasible, it's usually good practice to modify such loops to have a bound
on the number of iterations instead of or in addition to a bound on wallclock
time.

For cases where modifying the loop is infeasible, Shadow provides the option
`--model-unblocked-syscall-latency`. When this option is enabled, Shadow moves
time forward a small amount on *every* syscall (and VDSO function call), and
switches to another thread if one becomes runnable in the meantime (e.g. because
network data arrived when the clock moved forward, unblocking it).

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

* It still doesn't account for time spent by the CPU executing code,
which also means that a busy-loop that makes no syscalls at all can still lead
to deadlock. Fortunately such busy loops are rare and are generally agreed upon
to be bugs, since they'd also potentially monopolize a CPU indefinitely when run
natively.

For more about this topic, see [#1792](https://github.com/shadow/shadow/issues/1792).
