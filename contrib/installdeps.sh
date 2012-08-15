#!/bin/bash

## This script downloads OpenSSL and Libevent2, build them, and installs them
## to ~/.shadow. This must be done to use the Scallion plugin, since we need
## to pass special flags so that linking to Tor and running in Shadow both
## work properly.

PREFIX=${PREFIX-${HOME}/.shadow}
echo "Installing to $PREFIX"

D=`pwd`
mkdir -p build
cd build

wget https://www.openssl.org/source/openssl-1.0.1c.tar.gz
wget https://www.openssl.org/source/openssl-1.0.1c.tar.gz.asc
gpg --verify openssl-1.0.1c.tar.gz.asc

if [ $? -eq 0 ]
then
    echo Signature is well.
else
    echo "Problem with openssl signature. Edit the installdeps.sh script if you want to avoid checking the signature."
    exit -1
fi

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

gpg --verify libevent-2.0.19-stable.tar.gz.asc

if [ $? -eq 0 ]
then
    echo Signature is well.
else
    echo "Problem with libevent signature. Edit the installdeps.sh script if you want to avoid checking the signature."
    exit -1
fi

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

