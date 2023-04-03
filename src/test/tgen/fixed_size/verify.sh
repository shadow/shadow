#!/usr/bin/env bash

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

printf "TGen configuration: network=$1 client=$2\n"
network=$1
client=$2

# make sure all of the fixed-size transfers were successful
expected_count="$(echo ${client} | cut -d'_' -f3 | cut -d'x' -f1)"
actual_count="$(grep -r --include="tgen.*.stdout" "stream-success" ./hosts/client/ | wc -l)"

printf "Successful tgen stream count: ${actual_count}/${expected_count}\n"

if [[ "${actual_count}" != "${expected_count}" ]]; then
    printf "Verification ${RED}failed${NC}: Not all tgen streams were successful :(\n"
    exit 1
fi

printf "Verification ${GREEN}succeeded${NC}: Yay :)\n"
