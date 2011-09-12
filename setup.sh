#!/bin/bash

rm -rf ./build
rm -rf ./install
mkdir -p build/shadow install

currentDirectory=`pwd`
build=$currentDirectory/build
install=$currentDirectory/install

cd $build

# download, configure, make, and install openssl
wget "http://www.openssl.org/source/openssl-1.0.0d.tar.gz"
tar xvzf openssl-1.0.0d.tar.gz
cd openssl-1.0.0d
./config --prefix=$install -fPIC shared
make
make install
cd ..

# download, configure, make, and install libevent
wget "http://monkey.org/~provos/libevent-2.0.11-stable.tar.gz"
tar xvzf libevent-2.0.11-stable.tar.gz
cd libevent-2.0.11-stable
./configure --prefix=$install CFLAGS="-fPIC -I$install" LDFLAGS="-L$install"
make
make install
cd ..

wget "http://shadow.cs.umn.edu/downloads/shadow-resources.tar.gz"
tar xvzf shadow-resources.tar.gz

cd shadow

# now shadow
cmake $currentDirectory -DCMAKE_BUILD_PREFIX=$build/shadow  -DCMAKE_INSTALL_PREFIX=$install -DCMAKE_EXTRA_INCLUDES=$install/include/ -DCMAKE_EXTRA_LIBRARIES=$install/lib/ -DSHADOW_COVERAGE=OFF -DSHADOW_DOC=OFF -DSHADOW_DEBUG=OFF -DSHADOW_TEST=OFF

make
make install

echo "**************************************************************************"
echo "Shadow and its dependencies successfully installed to "$install
echo "**************************************************************************"

