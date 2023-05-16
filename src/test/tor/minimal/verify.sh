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

EXPECTED_STREAM_COUNT=80
stream_count="$(grep -r --include="tgen.*.stdout" "stream-success" | wc -l)"
printf "Successful tgen stream count: ${stream_count}/$EXPECTED_STREAM_COUNT\n"

if [ "${stream_count}" != "$EXPECTED_STREAM_COUNT" ]; then
    printf "Verification ${RED}failed${NC}: Not all tgen streams were successful :(\n"
    exit 1
fi

printf "Verification ${GREEN}suceeded${NC}: Yay :)\n"
