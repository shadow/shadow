#!/bin/bash

# Dependencies that depend on configuration beyond CONTAINER should be
# installed here.

set -euo pipefail

install_packages () {
    case "$CONTAINER" in
        ubuntu:*|debian:*)
            DEBIAN_FRONTEND=noninteractive apt-get install -y $@
            ;;
        centos:7)
            yum install -y $@
            ;;
        centos:*)
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
        if [ "$CONTAINER" = "centos:7" ]
        then
            # centos 7's clang requires gcc
            install_packages gcc gcc-c++
        fi
        install_packages clang
        ;;
    clang-12)
        install_packages clang-12
        ;;
    *)
        echo "Unhandled cc $CC"
        exit 1
        ;;
esac

if [ "${BUILDTYPE:-}" = coverage ]
then
    RUST_TOOLCHAIN=nightly-2021-05-06
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
