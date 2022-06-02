# Profiling

## Profiling with `gprof`

This method only provides profiling info for the core of Shadow, not for
plug-ins, or other libraries. Also, the profiling info is limited since gprof
only measures active CPU usage and function call counts and misses performance
related to blocking IO and barrier waits.

```bash
./setup build -cgo
./setup install
cd resource/examples
shadow shadow.config.yaml > shadow.log
gprof `which shadow` gmon.out > analysis.txt
less analysis.txt
```

## Profiling with `perf`

Either run perf when starting Shadow:

```bash
perf record shadow shadow.config.yaml > shadow.log
```

Or, connect to a running Shadow process:

```bash
perf record -p <PID>
```

Either of the above two options will write a `perf.data` file when you press
control-c, or Shadow ends. You can then analyze the report:

```bash
perf report
```

Perf is extremely powerful with many options. See `man perf` or [the perf
wiki](https://perf.wiki.kernel.org/index.php/Tutorial) for more info.

Note that any time an example uses the `-g` option in `perf record`, you should
use `--call-graph dwarf` instead. (The `-g` option defaults to stack frames for
traces, which elf-loader and certain optimizations can break. If you see
absurdly tall or small call graphs, this is probably what happened.)
