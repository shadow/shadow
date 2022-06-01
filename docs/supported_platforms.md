# Supported Platforms

## Officially supported platforms

- Ubuntu 18.04, 20.04, 22.04
- Debian 10 and 11
- Fedora 34, 35, 36
- CentOS Stream 8

A platform refers to an OS distribution's libraries and software, but
specifically not the kernel. Shadow may require a newer kernel version than is
provided by the distribution. Some platforms may provide a [Hardware Enablement
(HWE)][hwe] stack with newer kernel versions.

[hwe]: https://wiki.ubuntu.com/Kernel/LTSEnablementStack

We do not provide official support for other platforms. This means that we do
not ensure that Shadow successfully builds and passes tests on other platforms.
However, we will review pull requests that allow Shadow to build and run on
unsupported platforms.

## Officially supported kernels

- Linux 5.3 and later

## Docker

If you are installing Shadow within a Docker container, you must increase the
size of the container's `/dev/shm` mount and disable the seccomp security
profile. You can do this by passing `--shm-size="1024g" --security-opt
seccomp=unconfined` to `docker run`.

## Known incompatible platforms and kernels

- CentOS 7: We rely on features of glibc that aren't available on CentOS 7.
  Shadow won't compile there due to our use of C11 atomics, and threaded
  virtual processes running with preload-based interposition will deadlock due
  to an incompatible implementation of thread-local-storage.
- Linux <5.3: Shadow requires support for the `SYS_pidfd_open` syscall, which
  was introduced in kernel 5.3.

If you are having difficulty installing Shadow on any supported platforms, you
may find the [continuous integration build
steps](https://github.com/shadow/shadow/blob/main/.github/workflows/run_tests.yml)
helpful.
