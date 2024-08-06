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
LINUX_TAG=v6.10

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

# rustdoc tries to interpret some of the comments from the C code as formatted
# doc comments and thinks that some of the docs are rust code blocks, which
# fail to compile when performing 'cargo test'.
# See: https://github.com/rust-lang/rust-bindgen/issues/1313
bindgen_flags+=("--no-doc-comments")

# We seem to also need this to prevent using ctypes from std.
bindgen_flags+=("--ctypes-prefix=::core::ffi")

# Metadata about how we're generating the file
bindgen_flags+=("--raw-line=/* Build script: $0 */")
bindgen_flags+=("--raw-line=/* Kernel tag: $LINUX_TAG */")

# Allow variables by default. We end up pulling in most of them anyway,
# and pulling in some extra ones shouldn't hurt anything.
bindgen_flags+=("--allowlist-var=.*")
# SS_AUTODISARM is defined in the kernel headers as a u64,
# (`(1U << 31)`), but is stuffed into an i32 field in `linux_stack_t`.
# Avoid pulling it in at all for now.
bindgen_flags+=("--blocklist-item=SS_AUTODISARM")

# Signal types
bindgen_flags+=("--allowlist-type=sigset_t")
bindgen_flags+=("--allowlist-type=siginfo_t")
bindgen_flags+=("--allowlist-type=sigaction")
bindgen_flags+=("--allowlist-type=stack_t")
bindgen_flags+=("--allowlist-type=ucontext")

# Time types (time.h)
bindgen_flags+=("--allowlist-type=__kernel_clockid_t")
bindgen_flags+=("--allowlist-type=timespec")
bindgen_flags+=("--allowlist-type=itimerspec")
bindgen_flags+=("--allowlist-type=itimerval")

# More time types (time_types.h)
bindgen_flags+=("--allowlist-type=__kernel_timespec")
bindgen_flags+=("--allowlist-type=__kernel_old_timeval")

# Sched types
bindgen_flags+=("--allowlist-type=clone_args")

# rseq types
bindgen_flags+=("--allowlist-type=rseq")

# in.h types
bindgen_flags+=("--allowlist-type=sockaddr_in")

# fcntl.h
bindgen_flags+=("--allowlist-type=flock")
bindgen_flags+=("--allowlist-type=flock64")

# epoll.h
bindgen_flags+=("--allowlist-type=epoll_event")

# resource.h
bindgen_flags+=("--allowlist-type=rusage")
bindgen_flags+=("--allowlist-type=rlimit")
bindgen_flags+=("--allowlist-type=rlimit64")

# utsname.h
bindgen_flags+=("--allowlist-type=new_utsname")

# futex.h
bindgen_flags+=("--allowlist-type=robust_list_head")

# For poll
bindgen_flags+=("--allowlist-type=pollfd")

# Misc integer-ish types
bindgen_flags+=("--allowlist-type=__kernel_.*_t")

# For pselect6
bindgen_flags+=("--allowlist-type=__kernel_fd_set")

# netlink.h
bindgen_flags+=("--allowlist-type=nlmsghdr")

# select.h
bindgen_flags+=("--allowlist-type=stat")

# non-exposed socket types
bindgen_flags+=("--allowlist-type=sock_shutdown_cmd")

# Output
bindgen_flags+=("-o" "$BUILDDIR/bindings.rs")

# Derive Eq and PartialEq when possible
bindgen_flags+=("--with-derive-eq" "--with-derive-partialeq")

# Create a Debug impl if it can't be derived
bindgen_flags+=("--impl-debug")

# --- Begin positional params ---

# Input (positional)
bindgen_flags+=("./bindings-wrapper.h")

# Following flags are passed to clang
bindgen_flags+=("--")

# Override the system's linux headers (if any) with our own.
bindgen_flags+=("-I" "$LINUX_INSTALL_DIR/include")

bindgen "${bindgen_flags[@]}"
./rename.py < "$BUILDDIR/bindings.rs" > src/bindings.rs
rustfmt src/bindings.rs
