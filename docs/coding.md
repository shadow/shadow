# Coding

## Building the C-Rust bindings

Shadow contains both C and Rust code, and we automatically generate bindings
for both languages so that they can interoperate. Changing function or type
definitions may require you to rebuild the bindings.

When required, you can rebuild all of the C-Rust bindings by running:

```bash
cd build && cmake --target bindings .. && make bindings
```

To see the specific options/flags provided to bindgen and cbindgen, you can use
`make VERBOSE=1 bindings`.

Since the C bindings and Rust bindings rely on each other, you may sometimes
need to build the bindings in a specific order. Instead of `make bindings`, you
can be more specific using for example `make bindings_main_rust` to make the
Rust bindings for `src/main`.

You may need to install bindgen, cbindgen, and clang:

```bash
apt install -y clang
cargo install --force --version 0.20.0 cbindgen
cargo install --force --version 0.59.1 bindgen
```

The versions of bindgen and cbindgen you install should match the [versions
installed in the
CI](https://github.com/shadow/shadow/blob/main/.github/workflows/lint.yml).
