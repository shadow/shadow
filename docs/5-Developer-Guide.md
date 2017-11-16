### Debugging Shadow using GDB

When debugging, it will be helpful to use the Shadow option `--cpu-threshold=-1`. It disable the automatic virtual CPU delay measurement feature. This feature may introduce non-deterministic behaviors, even when running the exact same experiment twice, by the re-ordering of events that occurs due to how the kernel schedules the physical CPU of the experiment machine. Disabling the feature with the above option will ensure a deterministic experiment, making debugging easier.

Build Shadow with debugging symbols by using the `-g` flag. See the help menu with `python setup.py build --help`.

The easiest way to debug is to run shadow with the `-g` flag, which will pause shadow after startup and print the `PID`. You can then simply attach gdb to shadow in a new terminal and continue the experiment:
```
shadow -g shadow.config.xml
# new terminal
gdb --pid=PID
> continue
```

To do run shadow in gdb manually instead of attaching after initial execution, you will need to set `LD_PRELOAD` inside of gdb. Its value should contain a colon separated list of every 'preload' library (generally installed to `~/.shadow/lib`):
```
cd shadow/resource/examples
gdb shadow
> set environment LD_PRELOAD=/home/rob/.shadow/lib/libshadow-interpose.so
> set args shadow.config.xml
> run
```

If this doesn't work and you just see "exited with code 1", instead set
`LD_PRELOAD` in gdb as follows:
```
> set exec-wrapper env LD_PRELOAD=/home/rob/.shadow/lib/libshadow-interpose.so
```

The following example shows how to manually run a `shadow-plugin-tor` experiment in gdb instead of attaching to the shadow process:

```
cd shadow-plugin-tor/resource/minimal
gdb shadow
> set env EVENT_NOSELECT=1
> set env EVENT_NOPOLL=1
> set env EVENT_NOKQUEUE=1
> set env EVENT_NODEVPOLL=1
> set env EVENT_NOEVPORT=1
> set env EVENT_NOWIN32=1
> set env OPENSSL_ia32cap=~0x200000200000000
> set env LD_PRELOAD=/home/rob/.shadow/lib/libshadow-interpose.so:/home/rob/.shadow/lib/libshadow-preload-tor.so
> set args shadow.config.xml
> run
```

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

TODO.

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