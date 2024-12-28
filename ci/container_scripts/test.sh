#!/bin/bash

set -euo pipefail

# Array of flags to be passed on to setup script
FLAGS=()

# Run as many tests in parallel as we have cores.
FLAGS+=("-j$(nproc)")

# Run extra tests that we don't generally require (for
# example tests that require additional dependencies).
FLAGS+=("--extra")

# Following flags passed through to ctest
FLAGS+=("--")

# * Exclude tgen and tor tests as we test them in a different workflow.
# * Exclude examples as we don't have all the required dependencies.
# * Exclude flaky tests.
FLAGS+=("--label-exclude" "tgen|tor|example|flaky")

FLAGS+=("--output-on-failure")

./setup test "${FLAGS[@]}"
