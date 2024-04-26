#!/bin/bash

# Runs the full CI locally in Docker containers.

set -euo pipefail

DRYTAG=0
BUILD_IMAGE=0
PUSH=0
NOCACHE=
REPO=shadowsim/shadow-ci

CONTAINER=ubuntu:24.04
CC=gcc
BUILDTYPE=debug

show_help () {
    cat <<EOF
Usage: $0 ...
  -h
  -?             Show help
  -i             Build image
  -c CONTAINER   Set container
  -C CC          Set C compiler
  -b BUILDTYPE   Set build-types
  -n             nocache when building Docker images
  -p             push image to dockerhub
  -r             set Docker repository
  -t             just get the image tag for the requested configuration

On the first run, you should use the '-i' flag to build the image.

Run default configuration:

  $0

EOF
}

while getopts "h?ipc:C:b:nr:t" opt; do
    case "$opt" in
    h|\?)
        show_help
        exit 0
        ;;
    i)  BUILD_IMAGE=1
        ;;
    c)  CONTAINER="$OPTARG"
        ;;
    C)  CC="$OPTARG"
        ;;
    b)  BUILDTYPE="$OPTARG"
        ;;
    n)  NOCACHE=--no-cache
        ;;
    p)  PUSH=1
        ;;
    r)  REPO="$OPTARG"
        ;;
    t)  DRYTAG=1
        ;;
    esac
done

run_one () {
    # Replace the single ':' with a '-'
    CONTAINER_FOR_TAG=${CONTAINER/:/-}
    # Replace all forward slashes with '-'
    CONTAINER_FOR_TAG="${CONTAINER_FOR_TAG//\//-}"

    TAG="$REPO:$CONTAINER_FOR_TAG-$CC-$BUILDTYPE"

    if [ "${DRYTAG}" == "1" ]; then
        echo "$TAG"
        return 0
    fi

    if [ "${BUILD_IMAGE}" == "1" ]; then
        echo "Building $TAG"

        # Build and tag a Docker image with the given configuration.
        docker build -t "$TAG" $NOCACHE -f- . <<EOF
        FROM $CONTAINER

        ENV CARGO_TERM_COLOR=always

        ENV CONTAINER "$CONTAINER"
        SHELL ["/bin/bash", "-c"]

        # Install base dependencies before adding CC or BUILTYPE to
        # the environment, allowing this layer to be cached and reused
        # across other images for this CONTAINER.
        COPY ci/container_scripts/install_deps.sh /root/install_deps.sh
        RUN /root/install_deps.sh

        # Now install any CC/BUILDTYPE-specific dependencies.
        ENV CC="$CC" BUILDTYPE="$BUILDTYPE"
        COPY ci/container_scripts/install_extra_deps.sh /root/install_extra_deps.sh
        # Set the default rust toolchain to be installed by the script.
        #
        # Copying just these config files before we copy the whole shadow source
        # allows us to reuse this image layer when iterating locally,
        # saving us from necessarily reinstalling the rust toolchain.
        #
        # In an incremental build with an updated rust-toolchain.toml, the
        # specified rust version will still correctly be installed and used
        # at run-time.
        COPY ci/rust-toolchain-*.toml ci/
        RUN /root/install_extra_deps.sh
        ENV PATH /root/.cargo/bin:\$PATH

        # Copy the local source into the container.
        RUN mkdir /root/shadow
        COPY . /root/shadow
        WORKDIR /root/shadow

        # Build and install. We do this as part of building the Docker image to support
        # reusing such images for incremental builds.
        RUN ci/container_scripts/build_and_install.sh
EOF
    fi

    # Run the tests inside the image we just built
    echo "Testing $TAG"

    # Start the container and copy the most recent code.
    DOCKER_CREATE_FLAGS=()
    # Shadow needs some extra space allocated to /dev/shm
    DOCKER_CREATE_FLAGS+=("--shm-size=1024g")
    # Docker's default seccomp policy disables the `personality` syscall, which
    # shadow uses to disable ASLR. This causes shadow's determinism tests to fail.
    # https://github.com/moby/moby/issues/43011
    # It also appears to cause a substantial (~3s) pause when the syscall fails,
    # causing the whole test suite to take much longer, and some tests to time out.
    #
    # If we remove this flag we then need `--cap-add=SYS_PTRACE`, which causes
    # Docker's seccomp policy to allow `ptrace`, `process_vm_readv`, and
    # `process_vm_writev`.
    DOCKER_CREATE_FLAGS+=("--security-opt=seccomp=unconfined")
    # Mount the source directory. This allows us to perform incremental builds in
    # a locally modified source dir without rebuilding the docker container.
    DOCKER_CREATE_FLAGS+=("--mount=src=$(realpath .),target=/mnt/shadow,type=bind,readonly")
    # In most cases running install_deps.sh and install_extra_deps.sh isn't
    # needed, since we already ran them when building the base image. Running
    # them here allows tests to pass in cases where new system dependencies have
    # been added without having to rebuild the base images. In the common case
    # where nothing new is installed they are usually fairly fast no-ops.
    CONTAINER_ID=$(docker create "${DOCKER_CREATE_FLAGS[@]}" "${TAG}" /bin/bash -c \
        "echo '' \
         && echo 'Changes (see https://stackoverflow.com/a/36851784 for details):' \
         && rsync --delete --exclude-from=.dockerignore --itemize-changes -c -rlpgoD --no-owner --no-group /mnt/shadow/ . \
         && echo '' \
         && ci/container_scripts/install_deps.sh \
         && ci/container_scripts/install_extra_deps.sh \
         && ci/container_scripts/build_and_install.sh \
         && ci/container_scripts/test.sh")

    # Start the container (build, install, and test)
    RV=0
    docker start --attach "${CONTAINER_ID}" || RV=$?

    # On failure, copy build directory out of container
    if [ "$RV" != 0 ]
    then
        docker cp "${CONTAINER_ID}":/root/shadow/build ./ci/
    fi

    # Remove the container, even if it failed
    echo -n "Removing container: "
    docker rm -f "${CONTAINER_ID}"

    if [ "${RV}" != "0" ]; then
        echo "Exiting due to docker container failure (${TAG})"
        exit 1
    fi

    if [ "${PUSH}" == "1" ]; then
        docker push "${TAG}"
    fi
}

run_one
