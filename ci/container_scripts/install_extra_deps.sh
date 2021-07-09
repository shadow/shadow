#!/bin/bash

# Dependencies that depend on configuration beyond CONTAINER should be
# installed here.

set -euo pipefail

install_packages () {
    case "$CONTAINER" in
        ubuntu:*|debian:*)
            DEBIAN_FRONTEND=noninteractive apt-get install -y $@
            ;;
        *centos:*)
            dnf install -y $@
            ;;
        fedora:*)
            dnf install --best -y $@
            ;;
        *)
            echo "Unhandled container $CONTAINER"
            exit 1
            ;;
    esac
}

case "$CC" in
    gcc)
        case "$CONTAINER" in
            ubuntu:*|debian:*)
                install_packages gcc g++
                ;;
            *)
                install_packages gcc gcc-c++
                ;;
        esac
        ;;
    clang)
        install_packages clang
        ;;
    clang-12)
        case "$CONTAINER" in
            ubuntu:*|debian:*)
                curl https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --dearmor > /usr/share/keyrings/llvm-archive-keyring.gpg
        esac
        case "$CONTAINER" in
            ubuntu:20.04)
                echo "deb [signed-by=/usr/share/keyrings/llvm-archive-keyring.gpg] http://apt.llvm.org/focal/ llvm-toolchain-focal-12 main" \
                  >> /etc/apt/sources.list
        esac
        case "$CONTAINER" in
            ubuntu:*|debian:*)
                apt-get update
        esac
        install_packages clang-12
        ;;
    *)
        echo "Unhandled cc $CC"
        exit 1
        ;;
esac

if [ "${BUILDTYPE:-}" = coverage ]
then
    RUST_TOOLCHAIN=nightly-2021-05-12
else
    RUST_TOOLCHAIN=stable
fi

if [ -n "${RUSTPROFILE:+x}" ]
then
    RUSTPROFILE="--profile=${RUSTPROFILE}"
fi

curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --no-modify-path --default-toolchain "$RUST_TOOLCHAIN" ${RUSTPROFILE:+"$RUSTPROFILE"}
PATH="$HOME/.cargo/bin:$PATH"
rustup default "${RUST_TOOLCHAIN}"

# Force cargo to download its package index
cargo search foo
