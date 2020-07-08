#!/bin/bash

# Runs shadow's CI for a single configuration.
# Requires env parameters:
#   CONTAINER: container-name to use
#   CC: C compiler to use (cc or clang)
#   BUILDTYPE: release, debug, or coverage
#
# Example:
#   sudo CONTAINER=centos:8 CC=clang BUILDTYPE=debug ci/run_one.sh

set -euo pipefail

TAG="shadow:${CONTAINER/:/-}-$CC-$BUILDTYPE"
echo "Running $TAG"

# Build and tag a Docker image with the given configuration.
docker build -t "$TAG" -f- . <<EOF
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

# Run the tests inside the image we just built.
docker run --shm-size=1g $TAG /bin/bash ci/container_scripts/test.sh

# TODO: Add support for running the images created above to do incremental builds.
# This is a little tricky because when running the docker image, we want to update
# the source tree already baked into the image, without updating timestamps for
# unmodified files.
#
# I think we'll need to mount the host shadow directory as a (read-only) volume,
# (e.g. `--mount src=`realpath .`,target=/mnt/shadow,type=bind,readonly`)
# and then rsync it into the source directory already baked into the image.
