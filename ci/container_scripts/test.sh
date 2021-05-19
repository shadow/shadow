#!/bin/bash

set -euo pipefail

if [ "$BUILDTYPE" = "coverage" ]
then
    # Preload tests are broken in coverage builds:
    # https://github.com/shadow/shadow/issues/867
    #
    # Hybrid mode is currently very slow, leading to timeouts in coverage builds.
    # https://github.com/shadow/shadow/issues/1168
    EXCLUDE="shadow-preload|shadow-hybrid"
else
    EXCLUDE=""
fi

EXTRA_FLAGS=""
if [ "$CONTAINER" = "centos:7" ]
then
    # On centos:7 we enable extra tests that currently require a patched libc.
    # https://github.com/shadow/shadow/issues/892
    CONFIG="ilibc"
else
    # On all other platforms, we run extra tests that we don't generally require (for
    # example tests that require additional dependencies).
    CONFIG="extra"
fi

# Array of flags to be passed on to setup script
FLAGS=()

# Run as many tests in parallel as we have cores.
FLAGS+=("-j$(nproc)")

# Following flags passed through to ctest
FLAGS+=("--")

# We exclude some tests in some configurations.
FLAGS+=("--exclude-regex" "$EXCLUDE")

# Pass through an optional config-name, which can enable more tests
FLAGS+=("--build-config" "$CONFIG")

# Exclude tor tests as we test them in a different workflow
FLAGS+=("--label-exclude" "tor")

FLAGS+=("--output-on-failure")

./setup test "${FLAGS[@]}"
