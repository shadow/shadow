# Installing Dependencies

### Required:
  + gcc, gcc-c++ (or clang, clang++)
  + python (version >= 3.6)
  + glib (version >= 2.32.0)
  + cmake (version >= 3.2)
  + make
  + xz-utils
  + procps
  + cargo, rustc (version \~ latest)

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
    libprocps-dev \
    make \
    python3 \
    python3-pip \
    xz-utils \
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

Before running these commands, please check any platform-specific
requirements below.

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
    procps-devel \
    python3 \
    python3-pip \
    xz \
    xz-devel \
    yum-utils \
    diffutils \
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

### CentOS Stream 8

As procps-ng-devel is not available on CentOS Stream 8, you must install it manually.

```bash
dnf remove -y procps-ng procps-ng-devel
dnf install -y http://vault.centos.org/centos/7.7.1908/os/x86_64/Packages/procps-ng-3.3.10-26.el7.x86_64.rpm
dnf install -y http://vault.centos.org/centos/7.7.1908/os/x86_64/Packages/procps-ng-devel-3.3.10-26.el7.x86_64.rpm
```

Due to [a bug](https://bugs.centos.org/view.php?id=18212) in the CentOS 8 CMake
package, you must also install libarchive manually.

```bash
dnf install -y libarchive
```
