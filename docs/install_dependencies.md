# Installing Dependencies

### Required:
  + gcc, gcc-c++ (or clang, clang++)
  + python (version >= 3.6)
  + glib (version >= 2.32.0)
  + cmake (version >= 3.2)
  + make
  + pkg-config
  + xz-utils
  + lscpu
  + cargo, rustc (version \~ latest)
  + libclang (version >= 9)

Notice: Clang 13.0 is unsupported as it has a miscompilation bug that affects
        Shadow (see [issue #1741](https://github.com/shadow/shadow/issues/1741)).

### Recommended Python Modules (for helper/analysis scripts):
  + numpy, scipy, matplotlib, networkx, lxml, pyyaml

### Recommended System Tools:
  + git, dstat, htop, tmux

## APT (Debian/Ubuntu):

```bash
# required dependencies
sudo apt-get install -y \
    cmake \
    findutils \
    libclang-dev \
    libc-dbg \
    libglib2.0-0 \
    libglib2.0-dev \
    make \
    python3 \
    python3-pip \
    xz-utils \
    util-linux \
    gcc \
    g++

# rustup: https://rustup.rs
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh

# optional python modules
sudo apt-get install -y \
    python3-numpy \
    python3-lxml \
    python3-matplotlib \
    python3-networkx \
    python3-scipy \
    python3-yaml

# optional tools
sudo apt-get install -y \
    dstat \
    git \
    htop \
    tmux
```

On older versions of Debian or Ubuntu, the default version of libclang is too
old, which may cause bindgen to have errors finding system header files,
particularly when compiling with gcc. In this case you will need to explicitly
install a newer-than-default version of libclang. e.g. on `debian-10` install
`libclang-13-dev`, and on `ubuntu-18.04` install `libclang-9-dev`.

## YUM (Fedora/CentOS):

**Warning:** `dnf` often installs 32-bit (`i686`) versions of
libraries. You may want to use the `--best` option to make sure you're
installing the 64-bit (`x86_64`) versions, which are required by Shadow.

```bash
# required dependencies
sudo dnf install -y \
    cmake \
    findutils \
    clang-devel \
    glib2 \
    glib2-devel \
    make \
    python3 \
    python3-pip \
    xz \
    xz-devel \
    yum-utils \
    diffutils \
    util-linux \
    gcc \
    gcc-c++

# rustup: https://rustup.rs
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh

# optional python modules
sudo dnf install -y \
    python3-numpy \
    python3-lxml \
    python3-matplotlib \
    python3-networkx \
    python3-scipy \
    python3-yaml

# optional tools
sudo dnf install -y \
    dstat \
    git \
    htop \
    tmux
```
