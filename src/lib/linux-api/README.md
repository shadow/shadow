This library provides direct kernel type bindings.

You probably want to pin your version of bindgen to keep the diff small.
Check the current comment at the top of `src/bindings.rs` to see the
version used to generate the bindings last time. Then, force your local
tooling to match versions like:

    cargo install --force --version 0.69.4 bindgen-cli

Then the magix you want to regenerate src/bindings.rs is:

    bash ./gen-kernel-bindings.sh

If the linux structs/variables you want are not present, you'll need to
modify `bindings-wrapper.h` and `gen-kernel-bindings.sh` and run again.

To find what to add, grep the install dir, e.g.:

    rg struct_epoll bindings-build/linux-install/

That will give you hints as to what headers need to be included inside
the `bindings-wrapper.h` file.

