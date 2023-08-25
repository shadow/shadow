# Installing Dependencies

### Required:
  + gcc, gcc-c++
  + python (version >= 3.6)
  + glib (version >= 2.58.0)
  + cmake (version >= 3.13.4)
  + make
  + pkg-config
  + xz-utils
  + lscpu
  + rustup (version \~ latest)
  + libclang (version >= 9)

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
    python3-networkx \
    xz-utils \
    util-linux \
    gcc \
    g++

# rustup: https://rustup.rs
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```

On older versions of Debian or Ubuntu, the default version of libclang is too
old, which may cause bindgen to have errors finding system header files,
particularly when compiling with gcc. In this case you will need to explicitly
install a newer-than-default version of libclang. e.g. on `debian-10` install
`libclang-13-dev`.

## DNF (Fedora):

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
    python3-networkx \
    xz \
    xz-devel \
    yum-utils \
    diffutils \
    util-linux \
    gcc \
    gcc-c++

# rustup: https://rustup.rs
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```
