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

The [`run.sh`](../ci/run.sh) script builds a Docker images for all
supported configurations, and runs our tests in them.

```{.bash}
sudo ci/run.sh
```

Note that running all tests locally typically takes hours. More often,
you'll want to only run some smaller set of configurations locally.
To run only the configurations you specify, use the `-o` flag:

```{.bash}
sudo ci/run.sh -o "ubuntu:18.04;clang;debug fedora:32;gcc;release"
```

For additional options, run `ci/run.sh -h`.

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
$ sudo ci/run.sh -o "centos:8;clang;debug"
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
