#### Shadow is running at 100% CPU. Is that normal?

Yes. In single-thread mode, Shadow runs at 100% CPU because its continuously processing simulation events as fast as possible. All other things constant, an experiment will finish quicker with a faster CPU. Due to node dependencies, thread CPU utilization will be less than 100% in multi-thread mode.

#### Is Shadow multi-threaded?

Yes. Shadow can run with _N_ worker threads by specifying `-w N` or `--workers=N` on the command line. Note that virtual nodes depend on network packets that can potentially arrive from other virtual nodes. Therefore, each worker can only advance according to the propagation delay to avoid dependency violations.

#### Is it possible to achieve deterministic experiments, so that every time I run Shadow with the same configuration file, I get the same results?

Yes. You need to use the "--cpu-threshold=-1" flag when running Shadow to disable the CPU model, as it introduces non-determinism into the experiment in exchange for more realistic CPU behaviors. (See also: `shadow --help-all`)

#### Can I use Shadow/Scallion with my custom Tor modifications?

Yes. You'll need to build Shadow with the `--tor-prefix` option set to the path of your Tor source directory. Then, every time you make Tor modifications, you need to rebuild and reinstall Shadow and Scallion, again using the `--tor-prefix` option.

#### My OS does not include the correct Clang/LLVM CMake modules. How do I build Clang/LLVM from source?

Older versions of the **clang/llvm** OS packages do not include the shared CMake module files Shadow requires. ug reports have been filed for ~~[Fedora](https://bugzilla.redhat.com/show_bug.cgi?id=914713)~~ and ~~[Debian](http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=701153)~~). You can get these by building Clang/LLVM from source as follows.

```bash
wget http://www.llvm.org/releases/3.2/llvm-3.2.src.tar.gz
wget http://www.llvm.org/releases/3.2/clang-3.2.src.tar.gz
tar xaf llvm-3.2.src.tar.gz
tar xaf clang-3.2.src.tar.gz
cp -R clang-3.2.src llvm-3.2.src/tools/clang
cd llvm-3.2.src
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/home/${USER}/.shadow ../.
make -jN # Replace 'N' with the number of threads for a parallel build
make install
export PATH=${PATH}:/home/${USER}/.shadow/bin
```

You should then add `/home/${USER}/.shadow/bin` to your shell setup for the PATH environment variable (e.g., in `~/.bashrc` or `~/.bash_profile`).

#### Why don't the consensus values from a v3bw file for the torflowauthority show up in the directory authority's `cached-consenus` file?

Tor currently requires 3 directory authorities to be configured in order to accept values from a v3bw file; otherwise the directory authorities use relays' advertised bandwidth when creating the consensus and the v3bw file entries are ignored.

#### How can I build Shadow directly using cmake instead of the setup script?

```bash
mkdir -p build/shadow; cd build/shadow
CC=`which clang` CXX=`which clang++` cmake ../..
make && make install
```

#### How can I build the traffic generator (TGen) for use outside of Shadow, e.g. over the Internet?

The traffic generator currently exists in the Shadow simulator repository, but we can build tgen as an external tool and skip building both the full simulator and the plug-in part of tgen:

```bash
git clone https://github.com/shadow/shadow.git
cd shadow/src/plugin/shadow-plugin-tgen
mkdir build
cd build
cmake .. -DSKIP_SHADOW=ON -DCMAKE_MODULE_PATH=`pwd`/../../../../cmake/
make
```
#### How can I stop Shadow from forking?

In order to run Shadow, the `LD_PRELOAD` environmental variable must be set to the location of `libshadow-interpose.so`. If this is not done, recent versions of Shadow will attempt to do this on behalf of the user, and then fork itself once the environment is set up properly. To avoid the fork, simply run shadow like:

```bash
LD_PRELOAD=/home/rob/.shadow/lib/libshadow-interpose.so shadow ...
```

Similarly, when running `shadow-plugin-tor`, the `shadow-tor` command also sets up some required variables for the user. When running the `shadow-plugin-tor` minimal example, stop Shadow from forking by doing something like the following instead of using the `shadow-tor` command:

```bash
cd shadow-plugin-tor/resource/minimal
rm -rf data
cp -R initdata data
LD_PRELOAD=/home/rob/.shadow/lib/libshadow-interpose.so:/home/rob/.shadow/lib/libshadow-preload-tor.so EVENT_NOSELECT=1 EVENT_NOPOLL=1 EVENT_NOKQUEUE=1 EVENT_NODEVPOLL=1 EVENT_NOEVPORT=1 EVENT_NOWIN32=1 OPENSSL_ia32cap=~0x200000200000000 shadow shadow.config.xml
```

You may choose to export the env variables in your bash session (e.g., `export LD_PRELOAD=...`) to avoid declaring them every time.

#### How to debug Shadow using GDB

When debugging, it will be helpful to use the Shadow option `--cpu-threshold=-1`. It disable the automatic virtual CPU delay measurement feature. This feature may introduce non-deterministic behaviors, even when running the exact same experiment twice, by the re-ordering of events that occurs due to how the kernel schedules the physical CPU of the experiment machine. Disabling the feature with the above option will ensure a deterministic experiment, making debugging easier.

Build Shadow with debugging symbols by using the `-g` flag. See the help menu with `python setup.py build --help`.

The easiest way to debug is to run shadow with the `-d` flag, which will pause shadow after startup and print the `PID`. You can then simply attach gdb to shadow in a new terminal and continue the experiment:
```
shadow -d shadow.config.xml
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

#### How to trace Shadow using Valgrind

If you want to be able to run Shadow through valgrind and the application you 
are running in Shadow uses OpenSSL (i.e. the Scallion plug-in), you should configure OpenSSL with the 
additional option: `-DPURIFY`. This fixes OpenSSL so it doesn't break valgrind.
You may also want to ensure that debugging symbols are included in the GLib
that Shadow links to, and any library used by the plug-in. This can be achieved
with the compiler flag `-g` when manually building a local version of GLib.

__NOTE__: Currently, symbols from plug-ins are not accessible via GDB until [this issue](https://github.com/shadow/shadow/issues/101) gets fixed. See [this thread](http://mailman.cs.umn.edu/archives/shadow-dev/2013-September/000066.html) for more information.

#### How to generate documentation

Documentation may be generated by running doxygen:
```bash
doxygen Doxyfile
```

Then check the `doc/` directory. Documentation is currently sparse.

#### How new Shadow releases are tagged

The following commands can be used to tag a new version of Shadow, after which an
archive will be available on github's [releases page][https://github.com/shadow/shadow/releases].

```bash
git tag -s v1.10.0
git push origin v1.10.0
git checkout release
git merge -Xtheirs v1.10.0
git rm {DELETED-FILE-NAMES}
```

Note that `git merge -Xtheirs v1.5.0` will assume all conflicts can be fixed by taking the remote tag/branch that you are merging into the current branch. This will keep our releases following master.

#### How to create a signed archive of a release

```bash
git archive --prefix=shadow-v1.10.0/ --format=tar v1.10.0 | gzip > shadow-v1.10.0.tar.gz
gpg -a -b shadow-v1.10.0.tar.gz
gpg --verify shadow-v1.10.0.tar.gz.asc
```

#### Is Shadow the right tool for my research question?

Shadow is a network simulator/emulator hybrid. It runs real applications, but it simulates network and system functions thereby emulating the kernel to the application. The suitability of Shadow to your problem depends upon what exactly you are trying to measure. If you are interested in analyzing changes in application behavior, e.g. application layer queuing, failure modes, or design changes, and how those changes affect the operation of the system and  network performance, then Shadow seems like a very good choice (especially if you want to minimize work on your end). If your research relies on, e.g., the accuracy of specific kernel features or kernel parameter settings, or dynamic changes in Internet routing, then Shadow may not be the right choice as it does not precisely model these behaviors. Shadow is also not the best at measuring cryptographic overhead, so if that is desired then it should probably be done more directly as a separate research component.