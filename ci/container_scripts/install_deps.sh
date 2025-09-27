#!/bin/bash

# Dependencies that depend only on CONTAINER should be installed here.

set -euo pipefail

APT_PACKAGES=(
  cmake
  findutils
  golang-go
  iproute2
  libc-dbg
  libclang-dev
  libglib2.0-0
  libglib2.0-dev
  make
  netbase
  pkg-config
  python3
  python3-networkx
  python3-yaml
  xz-utils
  util-linux
  )

# packages that are only required for our CI environment
APT_CI_PACKAGES=(
  curl
  rsync
  )

RPM_PACKAGES=(
  clang-devel
  cmake
  findutils
  glib2
  glib2-devel
  golang
  iproute
  make
  pkg-config
  python3
  python3-networkx
  python3-yaml
  xz
  xz-devel
  yum-utils
  diffutils
  util-linux
  glibc-static
  )

# packages that are only required for our CI environment
RPM_CI_PACKAGES=(
  curl
  rsync
  )

case "$CONTAINER" in
    ubuntu:*|debian:*)
        # Try to avoid downloading source packages, which we don't need.
        # TODO: Update to work with deb822 format, as used in bookworm?
        # https://manpages.debian.org/bookworm/apt/sources.list.5.en.html#DEB822-STYLE_FORMAT
        sed -i '/deb-src/s/^# //' /etc/apt/sources.list || true

        DEBIAN_FRONTEND=noninteractive apt-get update
        DEBIAN_FRONTEND=noninteractive apt-get install -y -- "${APT_PACKAGES[@]}" "${APT_CI_PACKAGES[@]}"

        # Handle dict ordering of src/tools/convert.py and allow diff on its tests
        # Before Python3.6, dict ordering was not predictable
        if [[ $(python3 --version) == *" 3.5"* ]]; then
          apt-get install -y software-properties-common
          add-apt-repository -y ppa:deadsnakes/ppa
          apt-get update
          apt-get install -y python3.6
          unlink /usr/bin/python3
          ln -s /usr/bin/python3.6 /usr/bin/python3
        fi
        ;;
    fedora:*)
        dnf install --best -y "${RPM_PACKAGES[@]}" "${RPM_CI_PACKAGES[@]}"
        ;;
    *)
        echo "Unhandled container $CONTAINER"
        exit 1
        ;;
esac
