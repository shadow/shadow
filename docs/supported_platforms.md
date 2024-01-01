# Supported Platforms

## Officially supported platforms

We support the following Linux x86-64 distributions:

- Ubuntu 20.04, 22.04
- Debian 10, 11, and 12
- Fedora 38

We do not provide official support for other platforms. This means that we do
not ensure that Shadow successfully builds and passes tests on other platforms.
However, we will review pull requests that allow Shadow to build and run on
unsupported platforms.

Our policy regarding supported platforms can be found in our ["stability
guarantees"](semver.md).

## Supported Linux kernel versions

Some Linux distributions support multiple kernel versions, for example an older
General Availability (GA) kernel and newer hardware-enablement (HWE) kernels.
We try to allow Shadow to run on the oldest kernel supported on each
distribution (the GA kernel). However:

* On Debian 10 (buster) We do not support the GA kernel. We do support the HWE
kernel (e.g. installed via backports).
* We are currently only able to regularly test on the latest Ubuntu kernel,
since that's what GitHub Actions provides.

By these criteria, Shadow's oldest supported kernel version is currently 5.4
(the GA kernel in Ubuntu 20.04.0).

## Docker

If you are installing Shadow within a Docker container, you must increase the
size of the container's `/dev/shm` mount and disable the seccomp security
profile. You can do this by passing additional flags to `docker run`.

Example:

```bash
docker run -it --shm-size=1024g --security-opt seccomp=unconfined ubuntu:22.04
```

If you are having difficulty installing Shadow on any supported platforms, you
may find the [continuous integration build
steps](https://github.com/shadow/shadow/blob/main/.github/workflows/run_tests.yml)
helpful.
