# Supported Platforms

## Officially supported platforms

  + Ubuntu 18.04 and 20.04
  + Debian 10
  + Fedora 33
  + CentOS Stream 8

If you are installing Shadow within a Docker container, you must increase the
size of the container's `/dev/shm` mount by passing `--shm-size="1g"` (with a
suitable size for your system and experiments) to `docker run`.

If you are having difficulty installing Shadow on any of these platforms, you
may find the [continuous integration build
steps](https://github.com/shadow/shadow/blob/main/.github/workflows/run_tests.yml)
helpful.

We do not provide official support for other platforms. This means that we do
not ensure that Shadow successfully builds and passes tests on other platforms.
However, we will review pull requests that allow Shadow to build and run on
unsupported platforms.

## Known incompatible platforms

 * CentOS 7: We rely on features of glibc that aren't available on CentOS 7. Shadow won't compile there
 due to our use of C11 atomics, and threaded virtual processes running with
 preload-based interposition will deadlock due to an incompatible implementation
 of thread-local-storage.
