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
    *)
        echo "Unhandled cc $CC"
        exit 1
        ;;
esac

if [ "$BUILDTYPE" = coverage ]
then
    # using an older rust nightly until https://github.com/shadow/shadow/issues/941 is resolved
    RUST_TOOLCHAIN=nightly-2020-08-20
else
    RUST_TOOLCHAIN=stable
fi
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --no-modify-path --default-toolchain "$RUST_TOOLCHAIN"
PATH=$HOME/.cargo/bin:$PATH
# Force cargo to download its package index
cargo search foo
