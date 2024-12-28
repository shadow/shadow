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
        OPTIONS=()
        ;;
    "debug")
        OPTIONS=(--debug)
        ;;
    *)
        echo "Unknown BUILDTYPE $BUILDTYPE"
        exit 1
        ;;
esac

./setup build -j4 --test --extra --werror "${OPTIONS[@]}"
./setup install
