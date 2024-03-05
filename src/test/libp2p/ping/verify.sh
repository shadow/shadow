#!/usr/bin/env bash

set -euo pipefail

echo "Test libp2p ping"

text="Dialed /ip4/1.1.1.1/tcp/9001"
cat ./hosts/peer2/*.stdout | grep "$text"
text="Listening on \"/ip4/127.0.0.1/tcp/9001\""
cat ./hosts/peer1/*.stdout | grep "$text"
cat ./hosts/peer2/*.stdout | grep "$text"
text="Listening on \"/ip4/1.1.1.1/tcp/9001\""
cat ./hosts/peer1/*.stdout | grep "$text"
text="Listening on \"/ip4/1.1.1.2/tcp/9001\""
cat ./hosts/peer2/*.stdout | grep "$text"

expected_ping_count=20
regex="Event { peer: PeerId(.*), connection: ConnectionId(.*), result: Ok(.*) }"

actual_ping_count=$(cat ./hosts/peer1/*.stdout | grep "$regex" | wc -l)
if [[ "${actual_ping_count}" != "${expected_ping_count}" ]]; then
    printf "Peer1 doesn't have the expected number of pings"
    exit 1
fi
actual_ping_count=$(cat ./hosts/peer2/*.stdout | grep "$regex" | wc -l)
if [[ "${actual_ping_count}" != "${expected_ping_count}" ]]; then
    printf "Peer2 doesn't have the expected number of pings"
    exit 1
fi

echo "Verification succeeded"
