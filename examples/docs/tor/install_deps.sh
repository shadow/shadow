#!/usr/bin/env bash

apt-get install -y cmake libglib2.0-dev libigraph-dev git tor

cd /tmp/ || exit
git clone https://github.com/shadow/tgen.git
cd tgen || exit

# assumes that `$HOME/.local/bin` is in PATH
mkdir build && cd build || exit
cmake .. -DCMAKE_INSTALL_PREFIX="$HOME/.local"
make
make install
