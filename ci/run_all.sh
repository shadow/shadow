#!/bin/bash

# Runs the full CI locally in Docker containers.

set -euo pipefail

for CONTAINER in 'ubuntu:16.04' 'ubuntu:18.04' 'ubuntu:20.04' 'debian:10-slim' 'fedora:32' 'centos:7' 'centos:8'; do
for CC in gcc clang; do
for BUILDTYPE in debug release; do
    CONTAINER=$CONTAINER CC=$CC BUILDTYPE=$BUILDTYPE ci/run_one.sh
done
done
done

CONTAINER='ubuntu:18.04' CC=clang BUILDTYPE=coverage ci/run_one.sh
