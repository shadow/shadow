A list of user-facing changes since the latest Shadow release.

* If running Shadow in Docker, you should use `--tmpfs
  /dev/shm:rw,nosuid,nodev,exec,size=1024g` rather than `--shm-size=1024g` to
  mount `/dev/shm` as executable. This fixes errors when the managed process
  maps executable pages. https://github.com/shadow/shadow/issues/2400
* Added latency modeling and potential thread-yield to rdtsc emulation,
  allowing managed code to avoid deadlock in busy-loops that use only the rdtsc
  instruction and no syscalls. https://github.com/shadow/shadow/pull/2314
* The build now internally uses `pkg-config` to locate glib, instead of a custom cmake module.
  This is the [recommended](https://docs.gtk.org/glib/compiling.html) way of
  getting the appropriate glib compile flags, and works better in non-standard layouts such
  as in a [guix](https://guix.gnu.org/) environment.
* The `setup` script now has a `--search` option, which can be used to add additional directories
  to search for pkg-config files, C headers, and libraries. It obsoletes the options `--library` and `--include`.
* Fixed a bug causing `mmap` to fail when called on a file descriptor that was
opened with `O_NOFOLLOW`. https://github.com/shadow/shadow/pull/2353
* Bare executable names are now resolved by searching shadow's `PATH`. Previously these were
interpreted as relative to the current directory. For backwards compatibility, Shadow will currently
prefer a binary in that location if one is found but log a warning. Such cases should be disambiguated
by using an absolute path or prefixing with `./`.
* Fixed order-of-operations bug in CoDel control law that could lead to an
  unexpected packet drop schedule. We think the bug could have caused Shadow to
  slightly more aggressively drop packets that have already been sitting in the
  CoDel queue for longer than 110 milliseconds. Based on the results of some Tor
  network simulations, the bug didn't appear to affect Tor network performance
  enough to lead us to believe that previous Tor simulations are invalid.
  https://github.com/shadow/shadow/pull/2479
* Changed the default scheduler from `thread-per-host` to `thread-per-core`, which has better
  performance on most machines.
* Experimental host heartbeat log messages are enabled by default
  (`experimental.host_heartbeat_interval` defaults to `"1 sec"`), but the
  format of these messages is not stable.
* Some of Shadow's emulated syscalls and object allocations are counted and
  written to a `shadow.data/sim-stats.json` file.
* Improved experimental strace logging for `brk`, `mmap`, `munmap`, `mremap`,
  `mprotect`, `open`, and `openat` syscalls.
* Several small simulation examples were added to an `examples/` directory.
* Fixed the file access mode for stdin in the managed process (changed from
  `O_WRONLY` to `O_RDONLY`).
* Fixed support for `readv` and `writev` syscalls, and added support for
  `preadv` and `pwritev`.
* Fixed a rare crash in Shadow's shim while logging.
  https://github.com/shadow/shadow/pull/2459
* Set the `ifa_netmask` field in `getifaddrs()` to improve compatibility with
  Node.js applications. https://github.com/shadow/shadow/pull/2456
* Shadow no longer depends on its absolute installed location, allowing the
installation directory to be safely moved. https://github.com/shadow/shadow/pull/2391
* Shadow now emulated `PR_SET_DUMPABLE`, allowing it to work for programs that try to
disable memory inspection. https://github.com/shadow/shadow/pull/2370
* Added new test cases to check Shadow's simulated network performance. These
  new tests help us verify that Shadow's network stack is capable of
  facilitating high-bandwidth transfers when using a single TCP stream or when
  using many streams in parallel, and across networks with various latency and
  bandwidth characteristics. Since we run the tests as part of our CI, it is now
  much more likely that we will notice when we make changes that significantly
  reduces Shadow's simulated network performance. We plan to expand the cases
  that we test in future releases. https://github.com/shadow/shadow/pull/2549
* (add entry here)

Raw changes since v2.2.0:

* [Merged PRs](https://github.com/shadow/shadow/pulls?q=is%3Apr+merged%3A%3E2022-07-19T16%3A42-0400)
* [Closed issues](https://github.com/shadow/shadow/issues?q=is%3Aissue+closed%3A%3E2022-07-19T16%3A42-0400)
