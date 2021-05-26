# Using a recompiled libc

With a stock libc library, it's difficult to interpose every syscall via
`LD_PRELOAD`. Once code is executing inside a libc function such as `fwrite`,
it typically makes non-PLT calls and uses inline assembly to make system calls,
leaving no opportunity to intercept the system call itself via `LD_PRELOAD`.
For more about this problem see
<https://www.jimnewsome.net/posts/interposing-internal-libc-calls/>.

One solution is to use `ptrace` instead, which can be done in shadow using
`--interpose-method=ptrace`. This approach can have some drawbacks though, such
as not being able to attach to the child process with gdb at runtime.

Another solution is to reimplement every entry point into libc, including
reimplementing e.g. `fwrite`, to ensure that they internally end up calling
Shadow's syscall implementation. For complete coverage of libc though, this
ends up requiring reimplementing a substantial chunk of the library. Shadow
does this for some of libc, but currently has some gaps, and probably always
will.

We can greatly improve our coverage of the libc API by patching a full
implementation of libc to make system calls via an interposable call to the
`syscall` function rather than using the `syscall` instruction inline. Then in
the Shadow shim we only need to interpose the `syscall` function.  For more
about how this works, and a proof of concept, see
<https://www.jimnewsome.net/posts/patching-glibc-to-make-syscalls-interposable/>.

For this to work reliably though, we have to be careful that our libc is
compatible with the system's libc headers. Therefore what we actually want to
do is patch the libc source from our system's package manager, and configure
and compile it with the same options that were used with the system's packaged
libc.

## Compiling on Ubuntu 18.04

 * Enable source repos in `/etc/apt/sources.list`
 * Run `apt source glibc-source` to get the system's glibc source.
 * Patch it (see below).
 * Run `dpkg-source --commit` to locally "commit" the patch.
 * Run `dpkg-buildpackage` to build the patched source along with Ubuntu's
   additional patches and configuration options. For me this fails in testing,
but still produces the library binaries.

To patch the source:

```shell
# Save the source directory for future reference. Your version number may
# be different.
cd glibc-2.27
SYSTEM_LIBC_SOURCE=`pwd`

# Grab the patched glibc. This has to be outside of SYSTEM_LIBC_SOURCE.
cd ..
git clone https://github.com/sporksmith/glibc.git interposable-glibc
cd interposable-glibc

# Switch to the patched branch. interpose-syscalls-2.27 is based on 2.27.
# interpose-syscalls is baed on 2.31 (dev).
git checkout origin/interpose-syscalls-2.27

# Generate patches
git format-patch glibc-2.27

# Apply the patches
cd $SYSTEM_LIBC_SOURCE
patch -p1 ../interposable-glibc/*.patch
```

## Compiling on CentOS 7

Install prerequisites:

```shell
sudo yum install -y \
    gcc \
    git \
    make \
    redhat-rpm-config \
    rpm-build
sudo yum-builddep -y glibc
```

Set up rpm build environment, following
<https://wiki.centos.org/HowTos/SetupRpmBuildEnvironment>:

```shell
mkdir -p ~/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
echo '%_topdir %(echo $HOME)/rpmbuild' > ~/.rpmmacros
```

Get and unpack centos7's glibc package:

```shell
cd ~ && yumdownloader --source glibc
cd ~ && rpm -ivh glibc-2.17-307.el7.1.src.rpm
```

Get custom patched source, and use it to generate the patch:

```shell
cd ~ && git clone --depth=2 -b interpose-syscalls-2.17-centos7 https://github.com/sporksmith/glibc.git
cd ~/glibc && git diff glibc-2.17-centos7 > ~/rpmbuild/SOURCES/use-syscall-function.patch
```

Next you'll need to edit `~/rpmbuild/SPECS/glibc.spec` file to tell it to apply the patch.
You'll need to add a line like:

```
PatchNNNN: use-syscall-function.patch
```

and another like:

```
%patchNNNN -p1
```

For more on editing the spec file, see <https://blog.packagecloud.io/eng/2015/04/20/working-with-source-rpms/#modifying-the-source-and-applying-patches>.

```shell
# (Re)unpack, patch, and build
cd ~/rpmbuild/SPECS && rpmbuild -ba glibc.spec
```

## Using the compiled libc

Our patched libc can be injected via `LD_LIBRARY_PATH` or `LD_PRELOAD`.
I've been using `LD_LIBRARY_PATH` because there are actually multiple libraries
compiled, and we'll want those others to be preferred as well (e.g. `libpthread`).
In shadow's configuration file you can set the environment variable for all
loaded plugins with the `environment` attribute of the `shadow` tag. e.g.:

```
<shadow environment="LD_LIBRARY_PATH=/path/to/glibc-2.27/build-tree/amd64-libc">
```

