#!/bin/bash

# Dependencies that depend only on CONTAINER should be installed here.

set -euo pipefail

APT_PACKAGES="
  cmake
  findutils
  libc-dbg
  libglib2.0-0
  libglib2.0-dev
  libprocps-dev
  make
  python3
  python3-pip
  xz-utils
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
  make
  procps-devel
  python3
  python3-pip
  xz
  xz-devel
  yum-utils
  diffutils
  libarchive
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
        # These packages aren't available on centos stream 8; we need to use centos 7's.
        dnf remove -y procps-ng procps-ng-devel
        dnf install -y http://vault.centos.org/centos/7.7.1908/os/x86_64/Packages/procps-ng-3.3.10-26.el7.x86_64.rpm
        dnf install -y http://vault.centos.org/centos/7.7.1908/os/x86_64/Packages/procps-ng-devel-3.3.10-26.el7.x86_64.rpm
        RPM_PACKAGES=${RPM_PACKAGES/procps-devel}

        dnf install -y ${RPM_PACKAGES} ${RPM_CI_PACKAGES}
        ;;
    *)
        echo "Unhandled container $CONTAINER"
        exit 1
        ;;
esac


python3 -m pip install $PYTHON_PACKAGES
