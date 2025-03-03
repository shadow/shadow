#!/bin/bash

# Dependencies that depend on configuration beyond CONTAINER should be
# installed here.

set -euo pipefail

install_packages () {
    case "$CONTAINER" in
        ubuntu:*|debian:*)
            DEBIAN_FRONTEND=noninteractive apt-get install -y -- "$@"
            ;;
        fedora:*)
            dnf install --best -y "$@"
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

curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --no-modify-path --default-toolchain none --profile minimal
# We don't need shellcheck to validate this file.
# shellcheck disable=1091
source "$HOME/.cargo/env"

# Pin the Rust toolchain to our pinned stable version.
ln -s ci/rust-toolchain-stable.toml rust-toolchain.toml

# As of rustup 1.28.0 rustup will no longer automatically install the active
# toolchain if it is not installed, so we need to install it manually.
rustup toolchain install

# This forces installation of the toolchain required in Shadow's
# "rust-toolchain.toml" (if this script is run from the shadow directory). When
# used with Docker, this causes the rust installation to get "baked in" to the
# Docker image layer that runs this script, which is typically what we want.
cargo --version

# Install a version of the golang std library that supports dynamic linking
go install -buildmode=shared -linkshared std
