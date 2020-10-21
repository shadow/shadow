#!/bin/bash

set -euo pipefail

if [ "$BUILDTYPE" = "coverage" ]
then
    # Preload tests are broken in coverage builds: https://github.com/shadow/shadow/issues/867
    EXCLUDE="shadow-preload"
else
    EXCLUDE=""
fi

# On centos:7 we enable extra tests that currently require a patched libc.
# https://github.com/shadow/shadow/issues/892
EXTRA_FLAGS=""
if [ "$CONTAINER" = "centos:7" ]
then
    CONFIG="ilibc"
else
    CONFIG=""
fi

# Array of flags to be passed on to setup script
FLAGS=()

# Run as many tests in parallel as we have cores.
FLAGS+=("-j$(nproc)")

# Following flags passed through to ctest
FLAGS+=("--")

# We exclude some tests in some configurations.
FLAGS+=("-E" "$EXCLUDE")

# Pass through an optional config-name, which can enable more tests
FLAGS+=("-C" "$CONFIG")

FLAGS+=("--output-on-failure")

# Try any that failed once more.
# TODO: We should only do this for an allowed-list of known-flaky tests,
# and there should be issues filed for each such test.
./setup test "${FLAGS[@]}" || ./setup test "${FLAGS[@]}" --rerun-failed
