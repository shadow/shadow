#!/bin/bash

set -euo pipefail

if [ "$BUILDTYPE" = "coverage" ]
then
    # Preload tests are broken in coverage builds: https://github.com/shadow/shadow/issues/867
    EXCLUDE="shadow-preload"
else
    EXCLUDE=""
fi

# Try rerunning failed tests once.
# TODO: We should only do this for an allowed-list of known-flaky tests,
# and there should be issues filed for each such test.
./setup test -j4 -- -E "$EXCLUDE" || ./setup test -j4 --rerun-failed
