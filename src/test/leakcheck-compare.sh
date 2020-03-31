#!/bin/bash

set -euo pipefail

input=$1
target=$(<$2)

nfails=0
while read line || [[ -n $line ]]
do
    l=`/usr/bin/env echo "$line" | /usr/bin/env tr -d [:space:]`
    t=`/usr/bin/env echo "$target" | /usr/bin/env tr -d [:space:]`
    if [ "$l" != "$t" ]
    then
        # a counter diff line does not match target
        nfails=$((nfails+1))
    fi
done < $input

exit $nfails
