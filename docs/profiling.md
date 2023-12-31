# Profiling

Profiling can be useful for improving the performance of experiments, either as
improvements to the implementation of Shadow itself, or in altering the
configuration of the experiments you are running.

## Profiling with `top`/`htop`

Tools like `top` and `htop` will give good first-order approximations for what
Shadow is doing. While they can only give system-wide to thread-level
granularity, this can often still tell you important details such as whether
Shadow, the simulated processes, or the kernel are consuming memory and
processor cycles. E.g., if you're running into memory constraints, the `RES` or
`MEM` column of these tools can tell you where to start looking for ways to
address that. If execution time is too long, sorting by `CPU` or `TIME` can
provide insight into where that time is being spent.

One limitation to note is that Shadow relies on spinlocks in barriers for some
of its operation. Especially when running with many threads, these spinlocks
will show as consuming most of the CPU anytime the simulation is bottlenecked
on few simulated processes. Telling when this is happening can be difficult in
these tools, because no symbol information is available.

## Profiling with `perf`

The `perf` tool is a powerful interface to the Linux kernel's performance
counter subsystem. See `man perf` or [the perf
wiki](https://perf.wiki.kernel.org/index.php/Tutorial) for full details on how
to use it, but some highlights most relevant to Shadow execution time are given
here.

Regardless of how you are using `perf`, the aforementioned complication of
spinlocks in Shadow apply. Namely, when there is any bottleneck on the barrier,
the symbols associated with the spinlocks will dominate the sample
counts. Improving the performance of the spinlocks will not improve the
performance of the experiment, but improving the performance of whatever is
causing the bottleneck (likely something towards the top of non-spinlock
symbols) can.

### `perf top`

The `perf top` command will likely be the most practical mode of
`perf` for profiling all parts of a Shadow experiment. It requires one
of: root access, appropriately set up Linux capabilities, or a system
configured to allow performance monitoring (similar to attaching to
processes with `gdb`), so isn't always available, but is very simple
when it is. The interface is similar to `top`'s, but provides
information on the granularity of symbols, across the entire
system. This means you will be able to tell which specific functions
in Shadow, the simulated processes, and the kernel are consuming CPU
time.

When `perf top` can't find symbol information for a process, it will display
the offset of the instruction as hex instead. (Note this means it will be
ranked by instruction, rather than the entire function.) If you know where the
respective executable or shared object file is, you can look up the name of the
symbol for that instruction's function by opening the file with `gdb` and
running `info symbol [ADDRESS]`. If `gdb` can't find the symbols either, you
can look it up manually using `readelf -s` and finding the symbol with the
largest address smaller than the offset you are looking for (note that
`readelf` does not output the symbols in order of address; you can pipe the
output to `awk '{$1=""; print $0}' | sort` to get a sorted list).

Details on more options (e.g., for filtering the sampled CPUs or processes) can
be found in `man perf top`.

### `perf record`

If you know which particular process you wish to profile, `perf record` can
give far greater detail than other options. To use it for Shadow, either run it
when starting Shadow:

```bash
perf record shadow shadow.config.yaml > shadow.log
```

Or, attach to a running Shadow process:

```bash
perf record -p <PID>
```

Attaching to a process requires similar permissions as `perf top`, but can be
used to profile any process, including the simulated processes launched by
Shadow.

The `perf record` process will write a `perf.data` file when you press Ctrl-c,
or Shadow ends. You can then analyze the report:

```bash
perf report
```

More details are available in `man perf record` and `man perf report`.
