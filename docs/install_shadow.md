## Shadow Setup

```bash
git clone https://github.com/shadow/shadow.git
cd shadow
./setup build --clean --test
./setup test
./setup install
```

You should add `/home/${USER}/.shadow/bin` to your shell setup for the PATH environment variable (e.g., in `~/.bashrc` or `~/.bash_profile`).

```bash
echo 'export PATH="${PATH}:/home/${USER}/.shadow/bin"' >> ~/.bashrc && source ~/.bashrc
```

Check that Shadow is installed and runs:

```bash
shadow --version
shadow --help
```

#### Setup Notes

  + All build output is generated out-of-source, by default to the `./build` directory.
  + Use `./setup build --help` to see all build options; the most useful build options are:  
    + `-g` or `--debug` to build Shadow with debugging symbols
    + `--include` and `--library` if you installed any dependencies in non-standard locations or somewhere other than `~/.shadow`.
    + `--prefix` if you want to install Shadow somewhere besides `~/.shadow`
  + The `setup` script is a wrapper to `cmake` and `make`. Using `cmake` and `make` directly is also possible, but strongly discouraged. For example:

    ```bash
    # alternative installation method
    rm -r build && mkdir build && cd build
    cmake -DCMAKE_INSTALL_PREFIX="~/.shadow" -DSHADOW_TEST=ON ..
    make
    ctest
    make install
    ```

## TGen Setup

Installing Shadow gives you the simulation environment, but you'll almost certainly want to run some processes inside of Shadow. The [TGen traffic generator](https://github.com/shadow/tgen) is useful for generating and transferring traffic through Shadow.

TGen was moved to its own repo on April 3, 2019 as of [this commit](https://github.com/shadow/shadow/commit/75973e75a6ab7d08ff0f04d9aab47fc0e4e97d89) for organizational reasons, but installing it is easy (TGen's dependencies are a subset of Shadow's):

```bash
git clone git@github.com:shadow/tgen.git
cd tgen && mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/home/$USER/.shadow
make && make install
```

Now you can run `~/.shadow/bin/tgen` either inside of Shadow, or outside of Shadow. See the [TGen repo](https://github.com/shadow/tgen) for more info.
