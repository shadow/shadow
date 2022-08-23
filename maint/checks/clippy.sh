#!/bin/bash

set -euxo pipefail

cargo clippy --all-features --all-targets -- -D warnings