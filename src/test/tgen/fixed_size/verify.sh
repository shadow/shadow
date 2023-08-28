#!/usr/bin/env bash

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

network=$1
client=$2
echo "TGen configuration: network=$network client=$client"

# make sure all of the fixed-size transfers were successful
expected_count=$(echo "${client}" | cut -d'_' -f3 | cut -d'x' -f1)
actual_count=$(grep -r --include="tgen.*.stdout" "stream-success" ./hosts/client/ | wc -l)

echo "Successful tgen stream count: ${actual_count}/${expected_count}"

if [[ "${actual_count}" != "${expected_count}" ]]; then
    printf "Verification %bfailed%b: Not all tgen streams were successful :(\n" "$RED" "$NC"
    exit 1
fi

printf "Verification %bsucceeded%b: Yay :)\n" "$GREEN" "$NC"
