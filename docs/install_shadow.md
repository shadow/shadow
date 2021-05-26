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
