#!/usr/bin/env bash
set -euo pipefail

COUNT="$(
# ANCHOR: body_1
for d in shadow.data/hosts/client*; do grep "stream-success" "${d}"/*.stdout ; done | wc -l
# ANCHOR_END: body_1
)"

echo "Client count: ${COUNT}"

if [ "${COUNT}" -ne 50 ]; then
	echo "Unexpected client count ${COUNT} != 50"
	exit 1
fi

: <<OUTPUT
# ANCHOR: output_1
50
# ANCHOR_END: output_1
OUTPUT

COUNT="$(
# ANCHOR: body_2
for d in shadow.data/hosts/server*; do grep "stream-success" "${d}"/*.stdout ; done | wc -l
# ANCHOR_END: body_2
)"

echo "Server count: ${COUNT}"

if [ "${COUNT}" -ne 50 ]; then
	echo "Unexpected server count ${COUNT} != 50"
	exit 1
fi

: <<OUTPUT
# ANCHOR: output_2
50
# ANCHOR_END: output_2
OUTPUT
