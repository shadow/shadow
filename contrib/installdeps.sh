#!/bin/bash

## This script downloads OpenSSL and Libevent2, build them, and installs them
## to ~/.shadow. This must be done to use the Scallion plugin, since we need
## to pass special flags so that linking to Tor and running in Shadow both
## work properly.

check_signature() {
  local pkgname=`echo $1 | cut -d '-' -f1`
  local keyring=./signing_keys
  read -r -p "do you want me to import the public key to check the signature of $pkgname? [Y/n] " response

  if [[ $response =~ ^([nn][oo]|[nn])$ ]]; then
    gpg --verify $1.asc
  else
    gpg --no-default-keyring --keyring $keyring --keyserver pgp.mit.edu --recv $2
    gpg --keyring $keyring --verify $1.asc
  fi

  if [ $? -eq 0 ]
  then
    echo Signature is well.
  else
    echo "Problem with $pkgname signature. Edit the installdeps.sh script if you want to avoid checking the signature."
    exit -1
  fi
}


PREFIX=${PREFIX-${HOME}/.shadow}
echo "Installing to $PREFIX"

D=`pwd`
mkdir -p build
cd build

wget https://www.openssl.org/source/openssl-1.0.1c.tar.gz
wget https://www.openssl.org/source/openssl-1.0.1c.tar.gz.asc

check_signature "openssl-1.0.1c.tar.gz" "F295C759"

tar xaf openssl-1.0.1c.tar.gz
cd openssl-1.0.1c/

## use ONE of the following:

## for debugging and profiling (you probably want to enable -g and -pg independently)
#./config --prefix=${PREFIX} no-shared threads -fPIC -g -pg -DPURIFY -Bsymbolic

## for normal use
./config --prefix=${PREFIX} shared threads -fPIC

make
make install

cd ../

wget https://github.com/downloads/libevent/libevent/libevent-2.0.19-stable.tar.gz
wget https://github.com/downloads/libevent/libevent/libevent-2.0.19-stable.tar.gz.asc

check_signature "libevent-2.0.19-stable.tar.gz" "8D29319A"

tar xaf libevent-2.0.19-stable.tar.gz
cd libevent-2.0.19-stable/

## use ONE of the following:

## for debugging and profiling (you probably want to enable -g and -pg independently)
#./configure --prefix=${PREFIX} --enable-shared=no CFLAGS="-fPIC -I${PREFIX} -g -pg" LDFLAGS="-L${PREFIX}" CPPFLAGS="-DUSE_DEBUG"

## for normal use
./configure --prefix=${PREFIX} --enable-shared CFLAGS="-fPIC -I${PREFIX}" LDFLAGS="-L${PREFIX}"

make
make install

cd ${D}

