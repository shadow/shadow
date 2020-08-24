#!/bin/bash

set -euo pipefail

case "$CC" in
gcc)
  export CXX=g++
  ;;
clang)
  export CXX=clang++
  ;;
*)
  echo "Unknown cc $CC"
  exit 1
  ;;
esac

case "$BUILDTYPE" in
    release)
        OPTIONS=""
        rustup default stable
        ;;
    debug)
        OPTIONS="--debug"
        rustup default stable
        ;;
    coverage)
        OPTIONS="--debug --coverage"
        # using an older rust nightly until https://github.com/shadow/shadow/issues/941 is resolved
        rustup default nightly-2020-08-20
        ;;
    *)
        echo "Unknown BUILDTYPE $BUILDTYPE"
        exit 1
        ;;
esac

./setup build -j4 --test --werror $OPTIONS
./setup install
