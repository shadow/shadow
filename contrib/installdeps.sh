#!/bin/bash

## This script downloads OpenSSL and Libevent2, build them, and installs them
## to ~/.shadow. This must be done to use the Scallion plugin, since we need
## to pass special flags so that linking to Tor and running in Shadow both
## work properly.

check_signature() {
  local PKGNAME=`echo $1 | cut -d '-' -f1`
  read -r -p "Do you want to use the included public keys to verify the signature of $PKGNAME? [Y/n] " RESPONSE

  if [[ $RESPONSE =~ ^([nn][oo]|[nn])$ ]]; then
    gpg --verify $1.asc
  else
    gpg --keyring $KEYRING --verify $1.asc
  fi

  if [ $? -eq 0 ]; then
    echo Signature is well.
  else
    echo "Problem with $PKGNAME signature. Edit the installdeps.sh script if you want to avoid checking the signature."
    exit -1
  fi
}

download() {
  read -r -p "${1} (${2}) [Y/n] " RESPONSE
  if [[ $RESPONSE =~ ^([nn][oo]|[nn])$ ]]; then
    exit -1
  else
    wget $2
  fi
}

PREFIX=${PREFIX-${HOME}/.shadow}
echo "Installing to $PREFIX"

D=`pwd`
KEYRING=$( cd "$( dirname "$0" )" && pwd )/deps_keyring.gpg
mkdir -p build
cd build

opensslversion=openssl-1.0.1c

download "May we download openssl?" https://www.openssl.org/source/${opensslversion}.tar.gz
download "May we download the openssl signature?" https://www.openssl.org/source/${opensslversion}.tar.gz.asc

check_signature "${opensslversion}.tar.gz"

tar xaf ${opensslversion}.tar.gz
cd ${opensslversion}/

## use ONE of the following:

## for debugging and profiling (you probably want to enable -g and -pg independently)
#./config --prefix=${PREFIX} no-shared threads -fPIC -g -pg -DPURIFY -Bsymbolic

## for normal use
./config --prefix=${PREFIX} shared threads -fPIC

make
make install

cd ../

libeventversion=libevent-2.0.19-stable

download "May we download libevent?" https://github.com/downloads/libevent/libevent/${libeventversion}.tar.gz
download "May we download the libevent signature?" https://github.com/downloads/libevent/libevent/${libeventversion}.tar.gz.asc

check_signature "${libeventversion}.tar.gz"

tar xaf ${libeventversion}.tar.gz
cd ${libeventversion}/

## use ONE of the following:

## for debugging and profiling (you probably want to enable -g and -pg independently)
#./configure --prefix=${PREFIX} --enable-shared=no CFLAGS="-fPIC -I${PREFIX} -g -pg" LDFLAGS="-L${PREFIX}" CPPFLAGS="-DUSE_DEBUG"

## for normal use
./configure --prefix=${PREFIX} --enable-shared CFLAGS="-fPIC -I${PREFIX}" LDFLAGS="-L${PREFIX}"

make
make install

cd ${D}

