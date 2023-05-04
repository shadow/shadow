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

* Removed the host `pcap_directory` configuration option and replaced
it with a new `pcap_enabled` option.

* A host's data files (files in `<data-dir>/hosts/<hostname>/`) are no longer
prefixed with the hostname. For example a file that was previously named
`shadow.data/hosts/server/server.curl.1000.stdout` is now named
`shadow.data/hosts/server/curl.1000.stdout`.

* The `clang` C compiler is no longer supported.

* Host names are restricted to the patterns documented in
[hostname(7)](https://man7.org/linux/man-pages/man7/hostname.7.html).
https://github.com/shadow/shadow/pull/2856

* The per-process option `stop_time` has been replaced with `shutdown_time`.
When set, the signal specified by `shutdown_signal` (a new option) will be sent
to the process at the specified time. While shadow previously sent `SIGKILL` at
a process's `stop_time`, the default `shutdown_signal` is `SIGTERM` to better
support graceful shutdown.

* The minimum version of `cmake` has been bumped from 3.2 to 3.13.4.

* The minimum version of `glib` has been bumped from 2.32 to 2.58.

* Generated pcap files are now named using their interface name instead of
their IP address. For example "lo.pcap" and "eth0.pcap" instead of
"127.0.0.1.pcap" and "11.0.0.1.pcap".

* The process `environment` configuration option now takes a map instead of a
semicolon-delimited string.

* Removed the `quantity` options for hosts and processes. It's now recommended
to use YAML anchors and merge keys instead.

* Renamed the `host_defaults` configuration field to `host_option_defaults` and
renamed the host's `options` field to `host_options`.

* Shadow now interprets a process still running at the end of the simulation as
an error by default. This can be overridden by the new per-process option
`expected_final_state`. https://github.com/shadow/shadow/pull/2886

* The per-process `.exitcode` file has been removed due to its confusing semantics,
and the new `expected_final_state` attribute replacing its primary use-case.
https://github.com/shadow/shadow/pull/2906

MINOR changes (backwards-compatible):

* Support the `MSG_TRUNC` flag for unix sockets.
https://github.com/shadow/shadow/pull/2841

* Support the `TIMER_ABSTIME` flag for `clock_nanosleep`.
https://github.com/shadow/shadow/pull/2854

* The experimental config option `use_shim_syscall_handler` has been removed.
This optimization is now always enabled.

* It is now an error to set a process's `stop_time` or `start_time` to be after
the simulation's `stop_time`.

* Sub-second configuration values are now allowed for all time-related options,
including `start_time`, `stop_time`, etc.

* Removed the `--profile`, `--include`, and `--library` setup script options.

* Added partial support for the `epoll_pwait2` syscall.

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

* A managed process that tries to call the `execve` syscall will now get an
error instead of escaping the Shadow simulation.
https://github.com/shadow/shadow/issues/2718

* Stopped overriding libc's `getcwd` with an incorrect wrapper that was
returning `-1` instead of `NULL` on errors.

* A call to `epoll_ctl` with an unknown operation will return `EINVAL`.

* Simulated Processes are now reaped and deallocated after the exit, reducing
run-time memory usage when processes exit over the course of the simulation.
This was unlikely to have affected most users, since Shadow currently doesn't
support `fork`, so any simulation has a fixed number of processes, all of which
are explicitly specified in shadow's config.

Raw changes since v2.5.0:

* [Merged PRs](https://github.com/shadow/shadow/pulls?q=is%3Apr+merged%3A%3E2023-03-23T18%3A20-0400)
* [Closed issues](https://github.com/shadow/shadow/issues?q=is%3Aissue+closed%3A%3E2023-03-23T18%3A20-0400)
