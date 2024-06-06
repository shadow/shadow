A list of changes since the latest Shadow release.

Changes since v3.1.0:

Documentation / policy updates:

* We've added support for Ubuntu 24.04 and Fedora 40, and dropped support for EOL versions of Fedora. See [supported platforms](https://shadow.github.io/docs/guide/supported_platforms.html).
* We've updated our documentation for profiling shadow simulations. See [profiling](https://shadow.github.io/docs/guide/profiling.html).
* We've updated our contributor guide on [merging pull requests](https://shadow.github.io/docs/guide/pull_requests.html#merging) to include instructions for rebasing before merging.
* We've added a page on [Performance-tuning configuration options](https://shadow.github.io/docs/guide/perf_config_options.html).

MAJOR changes (breaking):

*

MINOR changes (backwards-compatible):

* Added support for the `CLONE_CLEAR_SIGHAND` flag for the `clone3` syscall.
* Improved shadow's "strace" output for unreadable memory accesses (#2821).
* Added some support for netlink sockets (#3198).
* Added `lseek` support for pipes (#3320).
* Added support for the `alarm` syscall (#3321).

PATCH changes (bugfixes):

* On fork and fork-like invocations of `clone`, signal handlers are now correctly copied
from the parent instead of reset to default (unless `CLONE_CLEAR_SIGHAND` is used) (#3284).
* Fix exponential slowdown after repeated usage of the `wait4` syscall.
* Fix the build system's clang version check (#3262).
* Fixed a cmake warning (#3269).
* Fixed epoll edge trigger behavior with files (#3277, fixing issue #3274).
* Fixed `--version` output (#3287).
* Fixed a panic-causing race condition when logging unsupported syscall numbers (#3288).
* Fixed behaviour of edge-triggered epoll when more data arrives (#3243).
* Fixed some panics in debug builds when the `clone` syscall handler returns abnormally (#3291, fixing #3290).
* Improved build time of tests (#3304).
* The build system now logs a warning when detecting golang version 1.21.x, which is
incompatible with shadow's golang tests. (#3307, fixing #3267).
* Changed some syscall error cases to return `ENOTSUP` instead of `ENOSYS` (#3314).
* Fixed error detection and handling when spawning processes (#3344).

Full changelog since v3.1.0:

- [Merged PRs v3.1.0..HEAD](https://github.com/shadow/shadow/pulls?q=is%3Apr+merged%3A2023-12-30T20%3A30-0400..2033-12-30T20%3A30-0400)
- [Closed issues v3.1.0..HEAD](https://github.com/shadow/shadow/issues?q=is%3Aissue+closed%3A2023-12-30T20%3A30-0400..2033-12-30T20%3A30-0400)
- [Full compare v3.1.0..HEAD](https://github.com/shadow/shadow/compare/v3.1.0...HEAD)
