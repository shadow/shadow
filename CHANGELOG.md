A list of user-facing changes since the latest Shadow release.

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
* (add entry here)

Raw changes since v2.2.0:

* [Merged PRs](https://github.com/shadow/shadow/pulls?q=is%3Apr+merged%3A%3E2022-07-19T16%3A42-0400)
* [Closed issues](https://github.com/shadow/shadow/issues?q=is%3Aissue+closed%3A%3E2022-07-19T16%3A42-0400)
