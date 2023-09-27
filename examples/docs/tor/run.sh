#!/usr/bin/env bash

# Run the Tor minimal test and store output in shadow.log
shadow --template-directory shadow.data.template --use-memory-manager=false tor-minimal.yaml > shadow.log
