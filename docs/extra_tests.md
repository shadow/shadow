# Extra Tests

Shadow includes tests that require additional dependencies, such as Tor, TGen,
networkx, and golang. These aren't run by default, but are run as part of the CI tests.

To run them locally, first make sure that both tor and tgen are located on your
shell's `PATH` You should also install all of Shadow's optional dependencies.

To run the golang tests you will need to both install golang, and install
a dynamic version of the golang standard library. The latter can be done with
`go install -buildmode=shared -linkshared std`.

It is recommended to build Shadow in release mode, otherwise the Tor tests may
not complete before the timeout.

```bash
./setup build --test --extra
./setup test --extra
# To exclude the TGen and Tor tests (for example if you built Shadow in debug mode)
./setup test --extra -- --label-exclude tgen|tor
```

If you change the version of tor located at `~/.local/bin/tor`, make sure to
re-run `./setup build --test`.
