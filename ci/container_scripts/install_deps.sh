#!/bin/bash

# Dependencies that depend only on CONTAINER should be installed here.

set -euo pipefail

APT_PACKAGES="
  cmake
  curl
  findutils
  libc-dbg
  libglib2.0-0
  libglib2.0-dev
  libigraph0-dev
  libigraph0v5
  libprocps-dev
  make
  python3
  python3-pip
  xz-utils
  "

RPM_PACKAGES="
  cmake
  curl
  findutils
  glib2
  glib2-devel
  igraph
  igraph-devel
  make
  procps-devel
  python3
  python3-pip
  xz
  xz-devel
  yum-utils
  diffutils
  "

case "$CONTAINER" in
    ubuntu:*|debian:*)
        sed -i '/deb-src/s/^# //' /etc/apt/sources.list
        DEBIAN_FRONTEND=noninteractive apt-get update
        DEBIAN_FRONTEND=noninteractive apt-get install -y $APT_PACKAGES
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
        dnf install --best -y $RPM_PACKAGES
        ;;
    centos:7)
        yum install -y https://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm
        RPM_PACKAGES=${RPM_PACKAGES/cmake/cmake3}
        yum install -y $RPM_PACKAGES
        alternatives --install /usr/local/bin/cmake cmake /usr/bin/cmake3 20 \
            --slave /usr/local/bin/ctest ctest /usr/bin/ctest3 \
            --slave /usr/local/bin/cpack cpack /usr/bin/cpack3 \
            --slave /usr/local/bin/ccmake ccmake /usr/bin/ccmake3 \
            --family cmake

        ;;
    centos:8)
        dnf remove -y procps-ng procps-ng-devel
        dnf install -y http://vault.centos.org/centos/7.7.1908/os/x86_64/Packages/procps-ng-3.3.10-26.el7.x86_64.rpm
        dnf install -y http://vault.centos.org/centos/7.7.1908/os/x86_64/Packages/procps-ng-devel-3.3.10-26.el7.x86_64.rpm
        RPM_PACKAGES=${RPM_PACKAGES/procps-devel}

        dnf install -y https://dl.fedoraproject.org/pub/archive/epel/7.7/x86_64/Packages/i/igraph-0.7.1-12.el7.x86_64.rpm
        dnf install -y https://dl.fedoraproject.org/pub/archive/epel/7.7/x86_64/Packages/i/igraph-devel-0.7.1-12.el7.x86_64.rpm
        RPM_PACKAGES=${RPM_PACKAGES/igraph-devel}
        RPM_PACKAGES=${RPM_PACKAGES/igraph}

        dnf install -y ${RPM_PACKAGES}
        ;;
    *)
        echo "Unhandled container $CONTAINER"
        exit 1
        ;;
esac

