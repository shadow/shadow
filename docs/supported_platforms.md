# Supported Platforms

## Officially supported platforms

We support the following Linux x86-64 distributions:

- Ubuntu 18.04, 20.04, 22.04
- Debian 10 and 11
- Fedora 34, 35, 36
- CentOS Stream 8

We do not provide official support for other platforms. This means that we do
not ensure that Shadow successfully builds and passes tests on other platforms.
However, we will review pull requests that allow Shadow to build and run on
unsupported platforms.

## Docker

If you are installing Shadow within a Docker container, you must increase the
size of the container's `/dev/shm` mount and disable the seccomp security
profile. You can do this by passing `--shm-size="1024g" --security-opt
seccomp=unconfined` to `docker run`.

## Known incompatible platforms

- CentOS 7: We rely on features of glibc that aren't available on CentOS 7.
  Shadow won't compile there due to our use of C11 atomics, and threaded
  virtual processes running with preload-based interposition will deadlock due
  to an incompatible implementation of thread-local-storage.

If you are having difficulty installing Shadow on any supported platforms, you
may find the [continuous integration build
steps](https://github.com/shadow/shadow/blob/main/.github/workflows/run_tests.yml)
helpful.
