#!/usr/bin/env bash

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

EXPECTED_BOOTSTRAP_COUNT=8
bootstrapped_count="$(grep -r --include="tor.*.stdout" "Bootstrapped 100" | wc -l)"
echo "Bootstrapped count: ${bootstrapped_count}/$EXPECTED_BOOTSTRAP_COUNT"
if [ "${bootstrapped_count}" != "$EXPECTED_BOOTSTRAP_COUNT" ]; then
    printf "Verification %bfailed%b: Not all tor processes bootstrapped :(\n" "$RED" "$NC"
    exit 1
fi

check_host () {
    local NAME=$1
    local MINIMUM_STREAMS=$2
    local TOTAL_STREAMS=$3
    # Careful to declare the local separately from assigning the value;
    # otherwise we miss errors from `grep` such as if the files don't exist.
    # https://unix.stackexchange.com/a/343259
    local stream_success_count
    stream_success_count=$(grep -c stream-success hosts/"$NAME"/tgen.*.stdout)
    echo "Successful $NAME stream count: ${stream_success_count}/$TOTAL_STREAMS (minimum $MINIMUM_STREAMS)"
    if [ "${stream_success_count}" -lt "$MINIMUM_STREAMS" ]; then
        printf "Verification %bfailed%b: Not enough $NAME streams were successful :(\n" "$RED" "$NC"
        exit 1
    fi
}

check_host client 10 10
check_host torclient 10 10
check_host fileserver 20 20

printf "Verification %bsucceeded%b: Yay :)\n" "$GREEN" "$NC"
