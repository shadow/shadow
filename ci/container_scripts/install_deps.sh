#!/bin/bash

# Dependencies that depend only on CONTAINER should be installed here.

set -euo pipefail

APT_PACKAGES="
  cmake
  findutils
  golang-go
  libc-dbg
  libglib2.0-0
  libglib2.0-dev
  make
  python3
  python3-pip
  xz-utils
  util-linux
  "

# packages that are only required for our CI environment
APT_CI_PACKAGES="
  curl
  rsync
  "

RPM_PACKAGES="
  cmake
  findutils
  glib2
  glib2-devel
  golang
  make
  python3
  python3-pip
  xz
  xz-devel
  yum-utils
  diffutils
  util-linux
  "

# packages that are only required for our CI environment
RPM_CI_PACKAGES="
  curl
  rsync
  "

PYTHON_PACKAGES="
  PyYaml
  networkx>=2.5
  "

case "$CONTAINER" in
    ubuntu:*|debian:*)
        sed -i '/deb-src/s/^# //' /etc/apt/sources.list
        DEBIAN_FRONTEND=noninteractive apt-get update
        DEBIAN_FRONTEND=noninteractive apt-get install -y $APT_PACKAGES $APT_CI_PACKAGES

        # Handle dict ordering of src/tools/convert.py and allow diff on its tests
        # Before Python3.6, dict ordering was not predictable
        if [[ `python3 --version` == *" 3.5"* ]]; then
          apt-get install -y software-properties-common
          add-apt-repository -y ppa:deadsnakes/ppa
          apt-get update
          apt-get install -y python3.6
          unlink /usr/bin/python3
          ln -s /usr/bin/python3.6 /usr/bin/python3
        fi
        ;;
    fedora:*)
        dnf install --best -y $RPM_PACKAGES $RPM_CI_PACKAGES
        ;;
    *centos:stream8)
        dnf install -y ${RPM_PACKAGES} ${RPM_CI_PACKAGES}
        ;;
    *)
        echo "Unhandled container $CONTAINER"
        exit 1
        ;;
esac


python3 -m pip install $PYTHON_PACKAGES