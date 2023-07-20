#!/usr/bin/env bash

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

EXPECTED_BOOTSTRAP_COUNT=11
bootstrapped_count="$(grep -r --include="tor.*.stdout" "Bootstrapped 100" | wc -l)"
printf "Bootstrapped count: ${bootstrapped_count}/$EXPECTED_BOOTSTRAP_COUNT\n"
if [ "${bootstrapped_count}" != "$EXPECTED_BOOTSTRAP_COUNT" ]; then
    printf "Verification ${RED}failed${NC}: Not all tor processes bootstrapped :(\n"
    exit 1
fi

check_host () {
    local NAME=$1
    local MINIMUM_STREAMS=$2
    local TOTAL_STREAMS=$3
    # Careful to declare the local separately from assigning the value;
    # otherwise we miss errors from `grep` such as if the files don't exist.
    # https://unix.stackexchange.com/a/343259
    local stream_count
    stream_count=$(grep -c stream-success hosts/$NAME/tgen.*.stdout)
    printf "Successful $NAME stream count: ${stream_count}/$TOTAL_STREAMS (minimum $MINIMUM_STREAMS)\n"
    if [ "${stream_count}" -lt "$MINIMUM_STREAMS" ]; then
        printf "Verification ${RED}failed${NC}: Not enough $NAME streams were successful :(\n"
        exit 1
    fi
}

check_host client 10 10
check_host torclient 10 10
check_host torbridgeclient 10 10
check_host fileserver 30 30

# Only require half of the streams to succeed, to mitigate
# https://github.com/shadow/shadow/issues/2544
check_host torhiddenclient 5 10
check_host torhiddenserver 5 10

printf "Verification ${GREEN}succeeded${NC}: Yay :)\n"
