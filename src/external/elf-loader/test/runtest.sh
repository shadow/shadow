#!/bin/bash

testn=$1
srcdir=$2

mkdir -p output 2>/dev/null

cp ${testn} ${testn}-ldso
../elfedit ${testn}-ldso ../ldso

echo "running 'LD_LIBRARY_PATH=.:../ ./${testn}-ldso > output/${testn}'"
LD_LIBRARY_PATH=.:../ ./${testn}-ldso > output/${testn} 2> /dev/null || true;

diff -q output/${testn} ${srcdir}/output/${testn}.ref > /dev/null

if test $? -eq 0; then
    echo "***** PASS ${testn} *****"
    exit 0
else
    echo "***** FAIL ${testn} *****"
    exit 1
fi

