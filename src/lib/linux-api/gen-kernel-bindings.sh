#!/bin/bash

# Checks out and builds Linux kernel headers,
# and uses `bindgen` to generate Rust bindings.
#
# We don't do this from the crate build script to avoid requiring a checkout of
# the Linux kernel source to build Shadow.
#
# We check out the kernel source and build at a fixed version for reproducible
# results. While the kernel ABI is stable, there are source-level changes across
# versions that can change the generated bindings.

set -euo pipefail

# Generally it makes sense to use the most recent stable version of the kernel,
# since the ABI is stable. i.e. managed programs compiled against an older
# version of the kernel headers should still get ABI-compatible definitions.
LINUX_TAG=v6.2

LINUX_REPO=https://github.com/torvalds/linux.git
ARCH=x86_64

BUILDDIR="$(pwd)/bindings-build"
LINUX_SRC_DIR="$BUILDDIR/linux-src"
LINUX_INSTALL_DIR="$BUILDDIR/linux-install"

if [ ! -d "$BUILDDIR" ]; then
  mkdir "$BUILDDIR"
fi

if [ ! -d "$LINUX_SRC_DIR" ]; then
  git clone -b "$LINUX_TAG" --depth=1 "$LINUX_REPO" "$LINUX_SRC_DIR"
fi
git -C "$LINUX_SRC_DIR" checkout "$LINUX_TAG"

# https://www.kernel.org/doc/html/latest/kbuild/headers_install.html
make -C "$LINUX_SRC_DIR" headers_install ARCH="$ARCH" INSTALL_HDR_PATH="$LINUX_INSTALL_DIR"

bindgen_flags=()

# Use core instead of std, for no_std compatibility.
bindgen_flags+=("--use-core")

# We seem to also need this to prevent using ctypes from std.
bindgen_flags+=("--ctypes-prefix=::core::ffi")

# Metadata about how we're generating the file
bindgen_flags+=("--raw-line=/* Build script: $0 */")
bindgen_flags+=("--raw-line=/* Kernel tag: $LINUX_TAG */")

# Errno values. This is a bit overly broad, but we can't really do
# better without either enumerating all the values here or splitting
# into multiple bindgen invocations.
bindgen_flags+=("--allowlist-var=E.*")

# Signal names
bindgen_flags+=("--allowlist-var=SIG.*")

# Signal codes
bindgen_flags+=("--allowlist-var=SI_.*")
bindgen_flags+=("--allowlist-var=ILL_.*")
bindgen_flags+=("--allowlist-var=FPE_.*")
bindgen_flags+=("--allowlist-var=SEGV_.*")
bindgen_flags+=("--allowlist-var=BUS_.*")
bindgen_flags+=("--allowlist-var=TRAP_.*")
bindgen_flags+=("--allowlist-var=CLD_.*")
bindgen_flags+=("--allowlist-var=POLL_.*")
bindgen_flags+=("--allowlist-var=SYS_SECCOMP")

# sigaction flags
bindgen_flags+=("--allowlist-var=SA_.*")

# Signal types
bindgen_flags+=("--allowlist-type=sigset_t")
bindgen_flags+=("--allowlist-type=siginfo_t")
bindgen_flags+=("--allowlist-type=sigaction")

# Time types
bindgen_flags+=("--allowlist-type=__kernel_clockid_t")
bindgen_flags+=("--allowlist-type=timespec")

# Clock types
bindgen_flags+=("--allowlist-var=CLOCK_.*")

# Clone flags
bindgen_flags+=("--allowlist-var=CLONE_.*")

# Output
bindgen_flags+=("-o" "$BUILDDIR/bindings.rs")

# Derive Eq and PartialEq when possible
bindgen_flags+=("--with-derive-eq" "--with-derive-partialeq")

# --- Begin positional params ---

# Input (positional)
bindgen_flags+=("./bindings-wrapper.h")

# Following flags are passed to clang
bindgen_flags+=("--")

# Override the system's linux headers (if any) with our own.
bindgen_flags+=("-I" "$LINUX_INSTALL_DIR/include")

bindgen "${bindgen_flags[@]}"
./rename.py < "$BUILDDIR/bindings.rs" > src/bindings.rs