#!/usr/bin/env bash

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

printf "TGen configuration: network=$1 client=$2\n"
network=$1
client=$2

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

printf "Server bytes per second=${server_Bps_read}\n"
printf "Client bytes per second=${client_Bps_read}\n"

# Test passes if tgen application achieves >= 85% of configured speed, allowing
# for TCP overhead. We use a 89% threshold since it's the highest that allowed
# all tgen tests to pass at time of writing. We can increase this in the future
# as we improve our stack.
#
# The 111250 value below comes from:
#   1Mbit/s = 125000 bytes/s
#   125000 bytes/s * 0.85 = 106250 bytes/s
expected=0
if [[ "${network}" == "1mbit"* ]]; then
    expected=106250
elif [[ "${network}" == "10mbit"* ]]; then
    expected=1062500
elif [[ "${network}" == "100mbit"* ]]; then
    expected=10625000
elif [[ "${network}" == "1gbit"* ]]; then
    expected=106250000
else
    printf "Verification ${RED}failed${NC}: unable to determine expected connection speed\n"
    exit 1
fi

result="${network} net expected ${expected} got client=${client_Bps_read} server=${server_Bps_read}"
if [[ ${client_Bps_read} < ${expected} || ${server_Bps_read} < ${expected} ]]; then
    printf "Verification ${RED}failed${NC}: ${result}\n"
    exit 1
else
    printf "Verification ${GREEN}succeeded${NC}: ${result}; Yay :)\n"
    exit 0
fi
