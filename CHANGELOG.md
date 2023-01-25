A list of user-facing changes since the latest Shadow release.

* Fixed an uncommon memory leak in `epoll_ctl`.
  https://github.com/shadow/shadow/pull/2586
* Tests that use shadow and tgen now use the binaries from `$PATH` and not
  `~/.local/bin`.
  https://github.com/shadow/shadow/pull/2572
* Shadow now forces the use of a specific Rust version using a
  `rust-toolchain.toml` file.
  https://github.com/shadow/shadow/pull/2614
* Added official support for Fedora 37.
  https://github.com/shadow/shadow/pull/2687
* Fixed a bug that could leak closed UDP sockets.
  https://github.com/shadow/shadow/pull/2594
* Emulate `sched_{get,set}affinity` syscalls.
  https://github.com/shadow/shadow/pull/2602
* Emulate reading from `/sys/devices/system/cpu/possible` and
  `/sys/devices/system/cpu/online`.
  https://github.com/shadow/shadow/pull/2602
* Fixed the TCP header sizes in pcap files.
  https://github.com/shadow/shadow/pull/2620
* Various minor improvements to the experimental strace logger (improved
  formatting of strings, buffers, and socket addresses, added logging of
  vdso-handled syscalls, etc).
* Added etcd and wget2 examples to the `examples/` directory.
  https://github.com/shadow/shadow/pull/2637
  https://github.com/shadow/shadow/pull/2659
* Improved the line styles in plotting script.
  https://github.com/shadow/shadow/pull/2638
* Support higher-level host-specific log levels.
  https://github.com/shadow/shadow/pull/2645
* Fixed a bug where a socket can receive packets that were intended for a
  different socket.
  https://github.com/shadow/shadow/issues/2593
* (add entry here)

Raw changes since v2.3.0:

* [Merged PRs](https://github.com/shadow/shadow/pulls?q=is%3Apr+merged%3A%3E2022-11-29T13%3A02-0500)
* [Closed issues](https://github.com/shadow/shadow/issues?q=is%3Aissue+closed%3A%3E2022-11-29T13%3A02-0500)
