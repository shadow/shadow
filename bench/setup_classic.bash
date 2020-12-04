#!/usr/bin/env bash

ROOT_DIR=/mnt/samsung/scratch
SHADOW_DIR=$ROOT_DIR/shadow
INSTALL_PFX=/home/rwails/.shadow

set -e

rm -rf $INSTALL_PFX

if [ ! -d $SHADOW_DIR ]; then
  mkdir -p $ROOT_DIR
  cd $ROOT_DIR && git clone https://github.com/shadow/shadow/
fi

if [ ! -d $SHADOW_DIR/build ]; then
  cd $SHADOW_DIR && mkdir build && cd build
  cmake -G Ninja .. -DCMAKE_INSTALL_PREFIX=$INSTALL_PFX -DSHADOW_DEBUG=OFF \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo
  ninja && ninja install
fi
