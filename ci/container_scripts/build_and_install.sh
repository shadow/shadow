#!/bin/bash

set -euo pipefail

case "$CC" in
gcc)
  export CXX="g++"
  ;;
clang)
  export CXX="clang++"
  ;;
clang-11)
  export CXX="clang++-11"
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
    "use-c-syscalls")
        OPTIONS="--debug --use-c-syscalls"
        ;;
    "coverage")
        OPTIONS="--debug --coverage"
        ;;
    *)
        echo "Unknown BUILDTYPE $BUILDTYPE"
        exit 1
        ;;
esac

./setup build -j4 --test --werror $OPTIONS
./setup install
