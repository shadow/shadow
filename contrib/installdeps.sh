#!/bin/bash

D=`pwd`
mkdir -p build
cd build

wget http://www.openssl.org/source/openssl-1.0.0e.tar.gz
tar xvzf openssl-1.0.0e.tar.gz
cd openssl-1.0.0e/
#./config --prefix=${HOME}/.shadow -fPIC -g -pg -DPURIFY -Bsymbolic shared
./config --prefix=${HOME}/.shadow -fPIC shared
make
make install

cd ../

wget https://github.com/downloads/libevent/libevent/libevent-2.0.16-stable.tar.gz
tar xvzf libevent-2.0.16-stable.tar.gz
cd libevent-2.0.16-stable/
## CPPFLAGS="-DUSE_DEBUG"
#./configure --prefix=${HOME}/.shadow CFLAGS="-fPIC -I${HOME}/.shadow -g -pg" LDFLAGS="-L${HOME}/.shadow"
./configure --prefix=${HOME}/.shadow CFLAGS="-fPIC -I${HOME}/.shadow" LDFLAGS="-L${HOME}/.shadow"
make
make install

cd ${D}

