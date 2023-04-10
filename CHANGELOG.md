A list of changes since the latest Shadow release.

MAJOR changes (breaking):

* Removed deprecated python scripts that only worked on Shadow 1.x config files
and topologies.

* Shadow no longer implicitly searches its working directory for executables
to be run under the simulation. If you wish to specify a path relative to
Shadow's working directory, prefix that path with `./`.

* Shadow now always enables support for YAML [merge keys](https://yaml.org/type/merge.html)
and [extension fields](https://docs.docker.com/compose/compose-file/#extension).
The experimental configuration option that previously enabled this support,
`use_extended_yaml`, has been removed.

* Removed the `host_defaults.pcap_directory` configuration option and replaced
it with a new `host_defaults.pcap_enabled` option.

* A host's data files (files in `<data-dir>/hosts/<hostname>/`) are no longer
prefixed with the hostname. For example a file that was previously named
`shadow.data/hosts/server/server.curl.1000.stdout` is now named
`shadow.data/hosts/server/curl.1000.stdout`.

* The `clang` C compiler is no longer supported.

* Host names are restricted to the patterns documented in
[hostname(7)](https://man7.org/linux/man-pages/man7/hostname.7.html).
https://github.com/shadow/shadow/pull/2856

MINOR changes (backwards-compatible):

* Support the `MSG_TRUNC` flag for unix sockets.
https://github.com/shadow/shadow/pull/2841

* Support the `TIMER_ABSTIME` flag for `clock_nanosleep`.
https://github.com/shadow/shadow/pull/2854

* The experimental config option `use_shim_syscall_handler` has been removed.
This optimization is now always enabled.

PATCH changes (bugfixes):

* Fixed a memory leak of about 16 bytes per thread due to
failing to unregister exited threads with a watchdog thread. This is unlikely to
have been noticeable effect in typical simulations. In particular the per-thread
data was already getting freed when the whole process exited, so it would only
affect a process that created and terminated many threads over its lifetime.

* Fixed a potential race condition when exiting managed threads that did not
have the `clear_child_tid` attribute set. This is unlikely to have affected most
software running under Shadow, since most thread APIs use this attribute.

* Changed an error value in `clock_nanosleep` and `nanosleep` from `ENOSYS` to
`ENOTSUP`.

Raw changes since v2.5.0:

* [Merged PRs](https://github.com/shadow/shadow/pulls?q=is%3Apr+merged%3A%3E2023-03-23T18%3A20-0400)
* [Closed issues](https://github.com/shadow/shadow/issues?q=is%3Aissue+closed%3A%3E2023-03-23T18%3A20-0400)
