#!/bin/bash

# Runs the full CI locally in Docker containers.

set -euo pipefail

CONTAINERS=(
    ubuntu:16.04
    ubuntu:18.04
    ubuntu:20.04
    debian:10-slim
    fedora:32
    centos:7
    centos:8
    )

CCS=(
    gcc
    clang
    )

BUILDTYPES=(
    debug
    release
    )

EXTRAS=("ubuntu:18.04;clang;coverage")

NOCACHE=

show_help () {
    cat <<EOF
Usage: $0 ...
  -h
  -?             Show help
  -c CONTAINERS  Set containers used in test matrix
  -C CCS         Set C compilers used in test matrix
  -b BUILDTYPES  Set build-types used in test matrix
  -e EXTRAS      Set extra configurations to run
  -o             Set only configurations to run
  -n             nocache when building Docker images

Run all default configurations:

  $0

Run all default configurations, but restrict C compilers to gcc:

  $0 -C gcc

Run all default configurations, but restrict C compilers to gcc,
and containers to ubuntu:16.04 and centos:7:

  $0 -C gcc -c "ubuntu:16.04 centos:7"

Set "extra" configurations to ubuntu:18.04;clang;coverage
and debian:10-slim;gcc;coverage

  $0 -e "ubuntu:18.04;clang;coverage debian:10-slim;gcc;coverage"

Set *only* configurations to run:

  $0 -o "ubuntu:18.04;clang;coverage debian:10-slim;gcc;coverage"
EOF
}

while getopts "h?c:C:b:e:o:n" opt; do
    case "$opt" in
    h|\?)
        show_help
        exit 0
        ;;
    c)  read -ra CONTAINERS <<< "$OPTARG"
        ;;
    C)  read -ra CCS <<< "$OPTARG"
        ;;
    b)  read -ra BUILDTYPES <<< "$OPTARG"
        ;;
    e)  read -ra EXTRAS <<< "$OPTARG"
        ;;
    o)  CONTAINERS=()
        CCS=()
        BUILDTYPES=()
        read -ra EXTRAS <<< "$OPTARG"
        ;;
    n)  NOCACHE=--no-cache
        ;;
    esac
done

run_one () {
    CONTAINER=$1
    CC=$2
    BUILDTYPE=$3

    TAG="shadow:${CONTAINER/:/-}-$CC-$BUILDTYPE"
    echo "Running $TAG"

    # Build and tag a Docker image with the given configuration.
    docker build -t "$TAG" $NOCACHE -f- . <<EOF
    FROM $CONTAINER

    ENV CONTAINER $CONTAINER
    SHELL ["/bin/bash", "-c"]

    # Install base dependencies before adding CC or BUILTYPE to
    # the environment, allowing this layer to be cached and reused
    # across other images for this CONTAINER.
    COPY ci/container_scripts/install_deps.sh /root/install_deps.sh
    RUN /root/install_deps.sh

    # Now install any CC/BUILDTYPE-specific dependencies.
    ENV CC=$CC BUILDTYPE=$BUILDTYPE
    COPY ci/container_scripts/install_extra_deps.sh /root/install_extra_deps.sh
    RUN /root/install_extra_deps.sh
    ENV PATH /root/.cargo/bin:\$PATH

    # Copy the local source into the container.
    RUN mkdir /root/shadow
    COPY . /root/shadow
    WORKDIR /root/shadow

    # Build and install. We do this as part of building the Docker image to support
    # reusing such images for incremental builds (TBD). 
    RUN ci/container_scripts/build_and_install.sh
EOF
    echo "Testing $TAG"

    # Run the tests inside the image we just built.
    docker run --rm --shm-size=1g $TAG /bin/bash ci/container_scripts/test.sh
}

for CONTAINER in ${CONTAINERS[*]}; do
for CC in ${CCS[*]}; do
for BUILDTYPE in ${BUILDTYPES[*]}; do
    run_one $CONTAINER $CC $BUILDTYPE
done
done
done

for EXTRA in ${EXTRAS[*]}; do
    # Split on ';'
    IFS=';' read -ra args <<< $EXTRA
    run_one ${args[0]} ${args[1]} ${args[2]}
done

# TODO: Add support for running the images created above to do incremental builds.
# This is a little tricky because when running the docker image, we want to update
# the source tree already baked into the image, without updating timestamps for
# unmodified files.
#
# I think we'll need to mount the host shadow directory as a (read-only) volume,
# (e.g. `--mount src=`realpath .`,target=/mnt/shadow,type=bind,readonly`)
# and then rsync it into the source directory already baked into the image.
