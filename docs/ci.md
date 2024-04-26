# Continuous integration tests

## On GitHub

Our continuous integration tests build and test Shadow on every supported
platform and configuration. GitHub runs these tests automatically when making
or modifying a pull request, in the [build and test
workflow](../.github/workflows/build_shadow.yml). Pull requests without passing
integration tests are blocked from merging.

## Running locally

We also have scripts for running the continuous integration tests locally,
inside Docker containers. This can be useful for debugging and for quickly
iterating on a test that's failing in GitHub's test runs.

The [`run.sh`](../ci/run.sh) script builds shadow inside a Docker image, and
runs our tests in it.

By default, the script will attempt to use a Docker image with already shadow
built, perform an incremental build on top of that, and then run shadow's tests.
If you don't already have a local image, the script will implicitly try to pull
from the [shadowsim/shadow-ci](https://hub.docker.com/r/shadowsim/shadow-ci) on
dockerhub. You can override this repo with `-r` or force the script to build a
new image locally with `-i`.

For example, to perform an incremental build and test on ubuntu 24.04,
with the gcc compiler in debug mode:

```{.bash}
ci/run.sh -c ubuntu:24.04 -C gcc -b debug
```

If the tests fail, shadow's build directory, including test outputs, will be copied
from the ephemeral Docker container into `ci/build`.

For additional options, run `ci/run.sh -h`.

## Debugging locally

After a local run fails, you can use Docker to help debug it. If you previously
ran the tests without the `-i` option, re-run with the `-i` option to rebuild
the Docker image(s). If Shadow was built successfully and the failure happened
at the testing step, then the Docker image was built and tagged, and you can
run an interactive shell in a container built from that image.

e.g.:

```{.bash}
docker run --shm-size=1024g --security-opt=seccomp=unconfined -it shadowsim/shadow-ci:ubuntu-24.04-gcc-debug /bin/bash
```

If the failure happened in the middle of building the Docker image, you can do
the same with the last intermediate layer that was built successfully. e.g.
given the output:

```{.bash}
$ ci/run.sh -i -c ubuntu:24.04 -C gcc -b debug
<snip>
Step 13/13 : RUN . ci/container_scripts/build_and_install.sh
 ---> Running in a11c4a554ef8
<snip>
    516 [ERROR] Non - zero return code from make.
```

You can start a container from the image where Docker tried (and failed) to run
`ci/the build_and_install.sh` script was executed with:

```{.bash}
docker run --shm-size=1024g --security-opt=seccomp=unconfined -it a11c4a554ef8 /bin/bash
```
