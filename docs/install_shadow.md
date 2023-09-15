# Shadow Setup

After building and testing Shadow, the install step is optional. If you do not
wish to install Shadow, you can run it directly from the build directory
(`./build/src/main/shadow`). Shadow only supports building from directories
that do not have whitespace characters.

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

The path that Shadow is installed to must not contain any space characters as
they are not supported by the dynamic linker's `LD_PRELOAD` mechanism.

Check that Shadow is installed and runs:

```bash
shadow --version
shadow --help
```

## Uninstall Shadow

After running `./setup install`, you can find the list of installed files in
`./build/install_manifest.txt`. To uninstall Shadow, remove any files listed.

## Setup Notes

  + All build output is generated to the `./build` directory.
  + Use `./setup build --help` to see all build options; some useful build
    options are:  
    + `-g` or `--debug` to build Shadow with debugging symbols and additional
      runtime checks. This option will significantly reduce the simulator
      performance.
    + `--search` if you installed dependencies to non-standard locations.
      Used when searching for libraries, headers, and pkg-config files.
      Appropriate suffixes like `/lib` and `/include` of the provided path
      are also searched  when looking for files of the corresponding type.
    + `--prefix` if you want to install Shadow somewhere besides `~/.local`.
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
