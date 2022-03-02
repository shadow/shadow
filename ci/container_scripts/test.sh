#!/bin/bash

set -euo pipefail

if [ "$BUILDTYPE" = "coverage" ]
then
    # Some preload tests are broken in coverage builds:
    # https://github.com/shadow/shadow/issues/867
    EXCLUDE="determinism.*shadow-preload|phold-parallel-shadow-preload"
else
    EXCLUDE=""
fi

# Array of flags to be passed on to setup script
FLAGS=()

# Run as many tests in parallel as we have cores.
FLAGS+=("-j$(nproc)")

# Run extra tests that we don't generally require (for
# example tests that require additional dependencies).
FLAGS+=("--extra")

# Following flags passed through to ctest
FLAGS+=("--")

# We exclude some tests in some configurations.
FLAGS+=("--exclude-regex" "$EXCLUDE")

# Exclude tor tests as we test them in a different workflow
FLAGS+=("--label-exclude" "tor")

FLAGS+=("--output-on-failure")

./setup test "${FLAGS[@]}"
