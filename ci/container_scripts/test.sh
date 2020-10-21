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


# Try rerunning failed tests once.
# TODO: We should only do this for an allowed-list of known-flaky tests,
# and there should be issues filed for each such test.
./setup test -j2 -- -E "$EXCLUDE" -C "$CONFIG" --output-on-failure || ./setup test -j2 -- -E "$EXCLUDE" -C "$CONFIG" --output-on-failure --rerun-failed
