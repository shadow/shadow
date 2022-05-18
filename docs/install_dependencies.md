# Installing Dependencies

### Required:
  + gcc, gcc-c++ (or clang, clang++)
  + python (version >= 3.6)
  + glib (version >= 2.32.0)
  + cmake (version >= 3.2)
  + make
  + xz-utils
  + lscpu
  + cargo, rustc (version \~ latest)

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

## YUM (Fedora/CentOS):

**Warning:** `dnf` often installs 32-bit (`i686`) versions of
libraries. You may want to use the `--best` option to make sure you're
installing the 64-bit (`x86_64`) versions, which are required by Shadow.

```bash
# required dependencies
sudo dnf install -y \
    cmake \
    findutils \
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
