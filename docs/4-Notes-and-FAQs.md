- [Shadow is running at 100% CPU. Is that normal?](#shadow-is-running-at-100-cpu-is-that-normal)
- [Is Shadow multi-threaded?](#is-shadow-multi-threaded)
- [Is it possible to achieve deterministic experiments, so that every time I run Shadow with the same configuration file, I get the same results?](#is-it-possible-to-achieve-deterministic-experiments-so-that-every-time-i-run-shadow-with-the-same-configuration-file-i-get-the-same-results)
- [Can I use Shadow/Scallion with my custom Tor modifications?](#can-i-use-shadowscallion-with-my-custom-tor-modifications)
- [My OS does not include the correct Clang/LLVM CMake modules. How do I build Clang/LLVM from source?](#my-os-does-not-include-the-correct-clangllvm-cmake-modules-how-do-i-build-clangllvm-from-source)
- [Why don't the consensus values from a v3bw file for the torflowauthority show up in the directory authority's `cached-consenus` file?](#why-dont-the-consensus-values-from-a-v3bw-file-for-the-torflowauthority-show-up-in-the-directory-authoritys-cached-consenus-file)
- [How can I build Shadow directly using cmake instead of the setup script?](#how-can-i-build-shadow-directly-using-cmake-instead-of-the-setup-script)
- [How can I build the traffic generator (TGen) for use outside of Shadow, e.g. over the Internet?](#how-can-i-build-the-traffic-generator-tgen-for-use-outside-of-shadow-eg-over-the-internet)
- [How can I stop Shadow from forking?](#how-can-i-stop-shadow-from-forking)
- [Is Shadow the right tool for my research question?](#is-shadow-the-right-tool-for-my-research-question)

#### Shadow is running at 100% CPU. Is that normal?

Yes. In single-thread mode, Shadow runs at 100% CPU because its continuously processing simulation events as fast as possible. All other things constant, an experiment will finish quicker with a faster CPU. Due to node dependencies, thread CPU utilization will be less than 100% in multi-thread mode.

#### Is Shadow multi-threaded?

Yes. Shadow can run with _N_ worker threads by specifying `-w N` or `--workers=N` on the command line. Note that virtual nodes depend on network packets that can potentially arrive from other virtual nodes. Therefore, each worker can only advance according to the propagation delay to avoid dependency violations.

#### Is it possible to achieve deterministic experiments, so that every time I run Shadow with the same configuration file, I get the same results?

Yes. You need to use the "--cpu-threshold=-1" flag when running Shadow to disable the CPU model, as it introduces non-determinism into the experiment in exchange for more realistic CPU behaviors. (See also: `shadow --help-all`)

#### Can I use Shadow/Scallion with my custom Tor modifications?

Yes. You'll need to build Shadow with the `--tor-prefix` option set to the path of your Tor source directory. Then, every time you make Tor modifications, you need to rebuild and reinstall Shadow and Scallion, again using the `--tor-prefix` option.

#### My OS does not include the correct Clang/LLVM CMake modules. How do I build Clang/LLVM from source?

Clang/LLVM are no longer required to build Shadow as of Shadow v1.12.0.

Older versions of the **clang/llvm** OS packages do not include the shared CMake module files Shadow requires. Bug reports have been filed for ~~[Fedora](https://bugzilla.redhat.com/show_bug.cgi?id=914713)~~ and ~~[Debian](http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=701153)~~). You can get these by building Clang/LLVM from source as follows.

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

For Shadow v1.12.0 or newer, use:
```bash
mkdir -p build/shadow; cd build/shadow
cmake ../..
make && make install
```

For Shadow versions before v1.12.0:

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

When running the `shadow-plugin-tor` minimal example, stop Shadow from forking by doing something like the following:

```bash
cd shadow-plugin-tor/resource/minimal
rm -rf data
cp -R initdata data
LD_PRELOAD=/home/rob/.shadow/lib/libshadow-interpose.so:/home/rob/.shadow/lib/libshadow-preload-tor.so EVENT_NOSELECT=1 EVENT_NOPOLL=1 EVENT_NOKQUEUE=1 EVENT_NODEVPOLL=1 EVENT_NOEVPORT=1 EVENT_NOWIN32=1 OPENSSL_ia32cap=~0x200000200000000 shadow shadow.config.xml
```

You may choose to export the env variables in your bash session (e.g., `export LD_PRELOAD=...`) to avoid declaring them every time.

#### Is Shadow the right tool for my research question?

Shadow is a network simulator/emulator hybrid. It runs real applications, but it simulates network and system functions thereby emulating the kernel to the application. The suitability of Shadow to your problem depends upon what exactly you are trying to measure. If you are interested in analyzing changes in application behavior, e.g. application layer queuing, failure modes, or design changes, and how those changes affect the operation of the system and  network performance, then Shadow seems like a very good choice (especially if you want to minimize work on your end). If your research relies on, e.g., the accuracy of specific kernel features or kernel parameter settings, or dynamic changes in Internet routing, then Shadow may not be the right choice as it does not precisely model these behaviors. Shadow is also not the best at measuring cryptographic overhead, so if that is desired then it should probably be done more directly as a separate research component.