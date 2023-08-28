#!/usr/bin/env bash

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

network=$1
client=$2
echo "TGen configuration: network=$network client=$client"

# read speed of server is the effective write speed of client
# read speed of client is the effective write speed of server
# tail -1 gets the fastest speed reached
server_Bps_read="$(grep -r --include="tgen.*.stdout" "driver-heartbeat" ./hosts/server/ \
    | cut -d' ' -f8 \
    | cut -d'[' -f2 \
    | cut -d',' -f1 \
    | cut -d'=' -f2 \
    | sort -n \
    | tail -1)"
client_Bps_read="$(grep -r --include="tgen.*.stdout" "driver-heartbeat" ./hosts/client/ \
    | cut -d' ' -f8 \
    | cut -d'[' -f2 \
    | cut -d',' -f1 \
    | cut -d'=' -f2 \
    | sort -n \
    | tail -1)"

echo "Server bytes per second=${server_Bps_read}"
echo "Client bytes per second=${client_Bps_read}"

# Test passes if tgen application achieves >= P% of configured speed, allowing for TCP overhead. TCP
# overhead includes non-payload bytes from packet headers and bytes from retransmitted packets. The
# latter tends to increase on very slow congested networks due to retransmission, meaning that TCP
# is less efficient when retransmitting many packets. Thus, our validation test progressively
# requires higher efficiency on faster networks where we expect fewer retransmissions.
expected=0
if [[ "${network}" == "1mbit"* ]]; then
    # 1 Mbit/s = 125000 bytes/s
    # 125000 bytes/s * 0.7 = 87500 bytes/s
    expected=87500
elif [[ "${network}" == "10mbit"* ]]; then
    # 10 Mbit/s = 1250000 bytes/s
    # 1250000 bytes/s * 0.88 = 1100000 bytes/s
    expected=1100000
elif [[ "${network}" == "100mbit"* ]]; then
    # 100 Mbit/s = 12500000 bytes/s
    # 12500000 bytes/s * 0.92 = 11500000 bytes/s
    expected=11500000
elif [[ "${network}" == "1gbit"* ]]; then
    # 1 Gbit/s = 125000000 bytes/s
    # 125000000 bytes/s * 0.96 = 120000000 bytes/s
    expected=120000000
else
    printf "Verification %bfailed%b: unable to determine expected connection speed\n" "$RED" "$NC"
    exit 1
fi

result="${network} net expected ${expected} got client=${client_Bps_read} server=${server_Bps_read}"
if [[ ${client_Bps_read} -lt ${expected} || ${server_Bps_read} -lt ${expected} ]]; then
    printf "Verification %bfailed%b: ${result}\n" "$RED" "$NC"
    exit 1
else
    printf "Verification %bsucceeded%b: ${result}; Yay :)\n" "$GREEN" "$NC"
    exit 0
fi
