#!/bin/bash

set -euo pipefail

case "$CC" in
gcc)
  export CXX="g++"
  ;;
clang)
  export CXX="clang++"
  ;;
clang-12)
  export CXX="clang++-12"
  ;;
*)
  echo "Unknown cc $CC"
  exit 1
  ;;
esac

case "$BUILDTYPE" in
    "release")
        OPTIONS=""
        ;;
    "debug")
        OPTIONS="--debug"
        ;;
    "coverage")
        OPTIONS="--debug --coverage"
        ;;
    *)
        echo "Unknown BUILDTYPE $BUILDTYPE"
        exit 1
        ;;
esac

# We *want* word splitting
# shellcheck disable=2086
./setup build -j4 --test --extra --werror $OPTIONS
./setup install
