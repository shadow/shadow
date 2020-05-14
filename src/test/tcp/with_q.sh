#!/bin/bash

# Catch failures
set -euo pipefail

# Create Q, and export into environment of child processes
export QUEUE=`ipcmk --queue | sed 's/[^0-9]*//g'`
