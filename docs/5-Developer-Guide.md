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

If debugging plugins in shadow instead of shadow itself, some extra commands are helpful. Because of performance problems in gdb, shadow (via elf-loader) prevents debug symbols for plugins from loading by default. To load all debug symbols in gdb, stop the experiment after the relevant plugins have been loaded, then run:
```
> p vdl_linkmap_abi_update()
```
However, unless the experiment is very small, this will take too long to feasibly run. Instead, individual plugins can have their debug symbols loaded by calling:
```
> p vdl_linkmapabi_from_addr(addr)
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

Some other functions elf-loader provides that could potentially be useful are `vdl_linkmap_shadow_print()`, which prints all shared object files in all namespaces available for loading the debug symbols of, and `vdl_linkmap_abi_print()`, which prints all the shared ojbect files in all namespaces that should already have their debug symbols loaded by gdb.

### Tracing Shadow using Valgrind

If you want to be able to run Shadow through valgrind and the application you 
are running in Shadow uses OpenSSL (i.e. the Scallion plug-in), you should configure OpenSSL with the 
additional option: `-DPURIFY`. This fixes OpenSSL so it doesn't break valgrind.
You may also want to ensure that debugging symbols are included in the GLib
that Shadow links to, and any library used by the plug-in. This can be achieved
with the compiler flag `-g` when manually building a local version of GLib.

### Profiling Shadow

##### Profiling with `gprof`

This method only provides profiling info for the core of Shadow, not for elf-loader, plug-ins, or other libraries. Also, the profiling info is limited since gprof only measures active CPU usage and function call counts and misses performance related to blocking IO and barrier waits.

```bash
./setup build -cgo
./setup install
cd resource/examples
shadow shadow.config.xml > shadow.log
gprof `which shadow` gmon.out > analysis.txt
less analysis.txt
```

##### Profiling with `perf`

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

### Tagging Shadow releases

The following commands can be used to tag a new version of Shadow, after which an
archive will be available on github's [releases page](https://github.com/shadow/shadow/releases).

```bash
git checkout master
git tag -s v1.10.0
git push origin v1.10.0
```
Our releases will then be tagged off of the master branch. Once tagged, a signed archive of a release can be created like this:

```bash
git archive --prefix=shadow-v1.10.0/ --format=tar v1.10.0 | gzip > shadow-v1.10.0.tar.gz
gpg -a -b shadow-v1.10.0.tar.gz
gpg --verify shadow-v1.10.0.tar.gz.asc
```