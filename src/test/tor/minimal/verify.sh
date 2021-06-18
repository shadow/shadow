#!/usr/bin/env bash

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

bootstrapped_count="$(grep -r --include="*.tor.*.stdout" "Bootstrapped 100" | wc -l)"
printf "Bootstrapped count: ${bootstrapped_count}/9\n"

if [ "${bootstrapped_count}" != "9" ]; then
    printf "Verification ${RED}failed${NC}: Not all tor processes bootstrapped :(\n"
    exit 1
fi

stream_count="$(grep -r --include="*.tgen.*.stdout" "stream-success" | wc -l)"
printf "Successful tgen stream count: ${stream_count}/80\n"

if [ "${stream_count}" != "80" ]; then
    printf "Verification ${RED}failed${NC}: Not all tgen streams were successful :(\n"
    exit 1
fi

printf "Verification ${GREEN}suceeded${NC}: Yay :)\n"
