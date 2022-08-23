#!/bin/bash

set -euxo pipefail

# One of the tests is designed to leak memory, so we need to tell miri to ignore leaks altogether.
RUST_BACKTRACE=1 MIRIFLAGS=-Zmiri-ignore-leaks cargo miri test
RUST_BACKTRACE=1 cargo miri test --examples