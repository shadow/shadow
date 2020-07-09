## Continuous integration tests

### On GitHub

Our continuous integration tests build and test Shadow on every supported
platform and configuration. GitHub runs these tests automatically when making
or modifying a pull request, in the [build and test
workflow](../.github/workflows/build_shadow.yml). Pull requests without passing
integration tests are blocked from merging.

### Running locally

We also have scripts for running the continuous integration tests locally,
inside Docker containers. This can be useful for debugging and for quickly
iterating on a test that's failing in GitHub's test runs.

The [`run_one.sh`](../ci/run_one.sh) script builds a Docker image for a single
configuration, and then runs the tests inside the Docker image. You must supply
the `CONTAINER`, `CC`, and `BUILDTYPE` via environment variables. For example:

```{.bash}
sudo CONTAINER=centos:8 CC=clang BUILDTYPE=debug ci/run_one.sh
```

You can also run the tests for *all* supported configurations with the
[`run_all.sh`](../ci/run_all.sh) script. This typically takes hours; you're
usually better off pushing your changes into a Pull Request and letting GitHub
run the tests on its cluster instead.

### Debugging locally

After a local run fails, you can use Docker to help debug it. If Shadow was
built successfully and the failure happened at the testing step, then the
Docker image was built and tagged, and you can run an interactive shell in that
image.

e.g.:

```{.bash}
sudo docker run --shm-size=1g -it shadow:centos-8-clang-debug /bin/bash
```

If the failure happened in the middle of building the Docker image, you can do
the same with the last intermediate layer that was built successfully. e.g.
given the output:

```{.bash}
$ sudo CONTAINER=centos:8 CC=clang BUILDTYPE=debug ci/run_one.sh
<snip>
Step 13/13 : RUN . ci/container_scripts/build_and_install.sh
 ---> Running in a11c4a554ef8
<snip>
    516 [ERROR] Non - zero return code from make.
```

You can start a container from the image where Docker tried (and failed) to run
`ci/the build_and_install.sh` script was executed with:

```{.bash}
sudo docker run --shm-size=1g -it a11c4a554ef8 /bin/bash
```
