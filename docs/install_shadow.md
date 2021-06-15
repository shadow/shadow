# Shadow Setup

After building and testing Shadow, the install step is optional. If you do not
wish to install Shadow, you can run it directly from the build directory
(`./build/src/main/shadow`).

```bash
git clone https://github.com/shadow/shadow.git
cd shadow
./setup build --clean --test
./setup test
# Optionally install (to ~/.local/bin by default). Can otherwise run the binary
# directly at build/src/main/shadow.
./setup install
```

For the remainder of this documentation, we assume the Shadow binary is in your
`PATH`. The default installed location of `/home/${USER}/.local/bin` is
probably already in your `PATH`. If it isn't, you can add it by running:

```bash
echo 'export PATH="${PATH}:/home/${USER}/.local/bin"' >> ~/.bashrc && source ~/.bashrc
```

Check that Shadow is installed and runs:

```bash
shadow --version
shadow --help
```

## Uninstall Shadow

After running `./setup install`, you can find the list of installed files in
`./build/install_manifest.txt`. To uninstall Shadow, remove any files listed.

## Setup Notes

  + All build output is generated out-of-source, by default to the `./build`
    directory.
  + Use `./setup build --help` to see all build options; some useful build
    options are:  
    + `-g` or `--debug` to build Shadow with debugging symbols
    + `--include` and `--library` if you installed any dependencies in
      non-standard locations or somewhere other than `~/.local`.
    + `--prefix` if you want to install Shadow somewhere besides `~/.local`
  + The `setup` script is a wrapper to `cmake` and `make`. Using `cmake` and
    `make` directly is also possible, but unsupported. For example:

    ```bash
    # alternative installation method
    rm -r build && mkdir build && cd build
    cmake -DCMAKE_INSTALL_PREFIX="~/.local" -DSHADOW_TEST=ON ..
    make
    ctest
    make install
    ```
