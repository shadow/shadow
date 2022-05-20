#!/bin/bash

# Runs the full CI locally in Docker containers.

set -euo pipefail

BUILD_IMAGES=0

CONTAINERS=(
    ubuntu:18.04
    ubuntu:20.04
    ubuntu:21.10
    ubuntu:22.04
    debian:10-slim
    debian:11-slim
    fedora:34
    fedora:35
    quay.io/centos/centos:stream8
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
  -i             Build images
  -c CONTAINERS  Set containers used in test matrix
  -C CCS         Set C compilers used in test matrix
  -b BUILDTYPES  Set build-types used in test matrix
  -e EXTRAS      Set extra configurations to run
  -o             Set only configurations to run
  -n             nocache when building Docker images

On the first run, you should use the '-i' flag to build the images.

Run all default configurations:

  $0

Run all default configurations, but restrict C compilers to gcc:

  $0 -C gcc

Run all default configurations, but restrict C compilers to gcc,
and containers to ubuntu:18.04 and fedora:35:

  $0 -C gcc -c "ubuntu:18.04 fedora:35"

Set "extra" configurations to ubuntu:18.04;clang;coverage
and debian:11-slim;gcc;coverage

  $0 -e "ubuntu:18.04;clang;coverage debian:11-slim;gcc;coverage"

Set *only* configurations to run:

  $0 -o "ubuntu:18.04;clang;coverage debian:11-slim;gcc;coverage"
EOF
}

while getopts "h?ic:C:b:e:o:n" opt; do
    case "$opt" in
    h|\?)
        show_help
        exit 0
        ;;
    i)  BUILD_IMAGES=1
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
    CONTAINER="$1"
    CC="$2"
    BUILDTYPE="$3"

    # Replace the single ':' with a '-'
    CONTAINER_FOR_TAG=${CONTAINER/:/-}
    # Replace all forward slashes with '-'
    CONTAINER_FOR_TAG="${CONTAINER_FOR_TAG//\//-}"

    TAG="shadow:$CONTAINER_FOR_TAG-$CC-$BUILDTYPE"

    if [ "${BUILD_IMAGES}" == "1" ]; then
        echo "Building $TAG"

        # Build and tag a Docker image with the given configuration.
        docker build -t "$TAG" $NOCACHE -f- . <<EOF
        FROM $CONTAINER

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
    # Shadow needs some space allocated to /dev/shm.
    DOCKER_CREATE_FLAGS+=("--shm-size=1g")
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
    CONTAINER_ID="$(docker create ${DOCKER_CREATE_FLAGS[@]} ${TAG} /bin/bash -c \
        "echo '' \
         && echo 'Changes (see https://stackoverflow.com/a/36851784 for details):' \
         && rsync --delete --exclude-from=.dockerignore --itemize-changes -a --no-owner --no-group /mnt/shadow/ . \
         && echo '' \
         && ci/container_scripts/build_and_install.sh \
         && ci/container_scripts/test.sh")"

    # Start the container (build, install, and test)
    RV=0
    docker start --attach "${CONTAINER_ID}" || RV=$?

    # Remove the container, even if it failed
    echo -n "Removing container: "
    docker rm -f "${CONTAINER_ID}"

    if [ "${RV}" != "0" ]; then
        echo "Exiting due to docker container failure (${TAG})"
        exit 1
    fi
}

# Array indexing follows the syntax of:
# https://stackoverflow.com/a/61551944
# to handle potentially empty arrays

for CONTAINER in ${CONTAINERS[@]+"${CONTAINERS[@]}"}; do
for CC in ${CCS[@]+"${CCS[@]}"}; do
for BUILDTYPE in ${BUILDTYPES[@]+"${BUILDTYPES[@]}"}; do
    run_one "$CONTAINER" "$CC" "$BUILDTYPE"
done
done
done

for EXTRA in ${EXTRAS[@]+"${EXTRAS[@]}"}; do
    # Split on ';'
    IFS=';' read -ra args <<< "$EXTRA"
    run_one "${args[0]}" "${args[1]}" "${args[2]}"
done
