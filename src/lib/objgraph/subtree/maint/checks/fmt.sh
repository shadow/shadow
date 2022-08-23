#!/bin/bash

set -euxo pipefail

cargo fmt --all -- --check
