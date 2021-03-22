#!/usr/bin/env bash

bootstrapped_count="$(grep -r --include="*.tor.*.stdout" "Bootstrapped 100" | wc -l)"
echo "Bootstrapped count: ${bootstrapped_count}"

if [ "${bootstrapped_count}" != "9" ]; then
    echo "Not all tor processes bootstrapped"
    exit 1
fi

stream_count="$(grep -r --include="*.tgen.*.stdout" "stream-success" | wc -l)"
echo "Successful tgen stream count: ${stream_count}"

if [ "${stream_count}" != "80" ]; then
    echo "Not all tgen streams were successful"
    exit 1
fi
