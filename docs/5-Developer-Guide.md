## Debugging

### Debugging Shadow using GDB

When debugging, it will be helpful to use the Shadow option `--cpu-threshold=-1`. It disables the automatic virtual CPU delay measurement feature. This feature may introduce non-deterministic behaviors, even when running the exact same experiment twice, by the re-ordering of events that occurs due to how the kernel schedules the physical CPU of the experiment machine. Disabling the feature with the above option will ensure a deterministic experiment, making debugging easier.

Build Shadow with debugging symbols by using the `-g` flag. See the help menu with `python setup.py build --help`.

These days, shadow can typically be run directly from gdb:
```
gdb shadow
> run shadow.config.xml
```

An alternative is to run shadow with the `-g` flag, which will pause shadow after startup and print the `PID`. You can then simply attach gdb to shadow in a new terminal and continue the experiment:
```
shadow -g shadow.config.xml
# new terminal
gdb --pid=PID
> continue
```

#### Debugging plugins with gdb

If debugging plugins in shadow instead of shadow itself, some extra commands are helpful. Because of performance problems in gdb, shadow (via elf-loader) prevents debug symbols for plugins from loading by default. (Note: The following commands include `set scheduler-locking on` to prevent gdb from running other threads while executing the requested command. This should not be done until needed and should be turned back off if you wish to run Shadow again or from the current state.) To load all debug symbols in gdb, stop the experiment after the relevant plugins have been loaded, then run:
```
> set scheduler-locking on
> p vdl_linkmap_abi_update()
> set scheduler-locking off
```
However, unless the experiment is very small, this will take too long to feasibly run. Instead, individual plugins can have their debug symbols loaded by calling:
```
> set scheduler-locking on
> p vdl_linkmap_abi_from_addr(addr)
> set scheduler-locking off
```
where `addr` is an address in a loaded elf file, e.g. from a backtrace.
This process can be automated in gdb by copying and pasting the commands below into gdb before running/continuing:
```
py
def bt_load():
  frame=gdb.newest_frame()
  frameaddrs=""
  count=0
  while(frame):
    frameaddrs += ", " + (str(frame.pc()))
    count += 1
    frame=frame.older()
  command = "p vdl_linkmap_abi_from_addrs(" + str(count) + frameaddrs + ")"
  gdb.execute(command)
end
catch signal SIGILL SIGFPE SIGSEGV SIGSYS
commands
set scheduler-locking on
py bt_load()
end
```
where the above catches any of `SIGILL SIGFPE SIGSEGV SIGSYS` (illegal instructions, arithmetic errors, segfaults, and improper syscalls) and loads the debug symbols from every file in the backtrace. You can also load the debug symbols from the current backtrace yourself by running `py bt_load()` if you define it as above.

Some other functions elf-loader provides that could potentially be useful are:  
  * `vdl_linkmap_shadow_print()`  
     prints all shared object files in all namespaces for which we can load debug symbols, and
  * `vdl_linkmap_abi_print()`  
     prints all shared ojbect files in all namespaces that should already have their debug symbols loaded by gdb.

### Tracing Shadow using Valgrind

If you want to be able to run Shadow through valgrind and the application you 
are running in Shadow uses OpenSSL (i.e. the Scallion plug-in), you should configure OpenSSL with the 
additional option: `-DPURIFY`. This fixes OpenSSL so it doesn't break valgrind.
You may also want to ensure that debugging symbols are included in the GLib
that Shadow links to, and any library used by the plug-in. This can be achieved
with the compiler flag `-g` when manually building a local version of GLib.

### Profiling Shadow

#### Profiling with `gprof`

This method only provides profiling info for the core of Shadow, not for elf-loader, plug-ins, or other libraries. Also, the profiling info is limited since gprof only measures active CPU usage and function call counts and misses performance related to blocking IO and barrier waits.

```bash
./setup build -cgo
./setup install
cd resource/examples
shadow shadow.config.xml > shadow.log
gprof `which shadow` gmon.out > analysis.txt
less analysis.txt
```

#### Profiling with `perf`

Either run perf when starting Shadow:

```bash
perf record shadow shadow.config.xml > shadow.log
```

Or, connect to a running Shadow process:

```bash
perf record -p <PID>
```

Either of the above two options will write a `perf.data` file when you press control-c, or Shadow ends. You can then analyze the report:

```bash
perf report
```

Perf is extremely powerful with many options. See `man perf` or [the perf wiki](https://perf.wiki.kernel.org/index.php/Tutorial) for more info.

Note that any time an example uses the `-g` option in `perf record`, you should use `--call-graph dwarf` instead. (The `-g` option defaults to stack frames for traces, which elf-loader and certain optimizations can break. If you see absurdly tall or small call graphs, this is probably what happened.)

### Testing for Deterministic Behavior

If you run Shadow twice with the same seed (the `-s` or `--seed` command line options), then it _should_ produce deterministic results (it's a bug if it doesn't).

A good way to check this is to compare the log output of an application that was run in Shadow. For example, after running two TGen experiments where the results are in the `shadow.data.1` and `shadow.data.2` directories, you could run something like the following bash script:

```bash
#!/bin/bash

found_difference=0

for SUFFIX in \
    hosts/fileserver/stdout-fileserver.tgen.1000.log \
    hosts/client/stdout-client.tgen.1000.log
do
    ## ignore memory addresses in log file with `sed 's/0x[0-9a-f]*/HEX/g' FILENAME`
    sed -i 's/0x[0-9a-f]*/HEX/g' shadow.data.1/${SUFFIX}
    sed -i 's/0x[0-9a-f]*/HEX/g' shadow.data.2/${SUFFIX}

    diff --brief shadow.data.1/${SUFFIX} shadow.data.2/${SUFFIX}
    exit_code=$?

    if (($exit_code != 0)); then
      found_difference=1
    fi
done

if (($found_difference == 1)); then
  echo -e "\033[0;31mDetected difference in output (Shadow may be non-deterministic).\033[0m"
else
  echo -e "\033[0;32mDid not detect difference in Shadow output (Shadow may be deterministic).\033[0m"
fi
```

If you find non-deterministic behavior in your Shadow experiment, please consider helping to diagnose the problem by opening a [new issue](https://github.com/shadow/shadow/issues/new).

