A list of changes since the latest Shadow release.

Changes since v3.2.0:

Documentation / policy updates:

*

MAJOR changes (breaking):

*

MINOR changes (backwards-compatible):

* Implemented the `chdir` syscall. (#3368)
* Implemented the `close_range` syscall. (#3364)
* Added partial support the `fstat` syscall with pipes. (#3361)
* We now support direct execution of scripts in shadow's config file.
Instead of specifying `{path: '/usr/bin/python3', args:
'/path/to/my/script.py'}`, you can now use `{path: '/path/to/my/script.py'}`
provided it has an appropriate "shebang" line (e.g. `#!/usr/bin/env python3`).
Likewise execution of such scripts is now supported inside the simulation when
spawning processes via `execve`.
* Relaxed restriction on assigning restricted IPs (such as 192.168.0.\*).
Within the simulation these are treated as fully routable IPs, so are required
to be unique as with any other IP address assignment. (#3414)
  * Re-added restrictions for broadcast, multicast, and unspecified addresses which Shadow cannot route (#3474)
* Removed the experimental tracker feature that logged heartbeat messages for every host every second. Also removed the corresponding python scripts for parsing and plotting tracker log messages and data. (#3446)
* Removed support for Debian 10 (Buster), which has [passed EOL](https://wiki.debian.org/LTS).
* Added a python package `shadowtools` with auxiliary tools. It currently contains a python module for facilitating dynamic generation of shadow config files, and a command-line tool `shadow-exec` for streamlining single-host simulations. (#3449)
* Improved support for netlink sockets with Go. (#3441)
* Replaced C DNS module with a Rust implementation. Also removed the C Address type. (#3464)
* Added support for `SO_BROADCAST` with `getsockopt` for TCP and UDP,
  which always returns 0 as Shadow doesn't support broadcast sockets.
  (#3471)
* Removed the `--coverage` option from the setup script. While this is
  technically a breaking change to the setup script, we don't expect anyone was
  using this option. (#3478)
* Converted the network interface and queuing disciplines to Rust and removed the legacy C implementations. (#3480)
* Converted the legacy C packet and payload structs to Rust for safer reference counting. This also eliminates a payload copy in Rust TCP and UDP code. (#3492)
* Added the experimental option `--native-preemption-enabled` for escaping pure-CPU busy-loops. (#3520)

PATCH changes (bugfixes):

* Log messages about unrecognized sockopt values are now only logged at level WARN once for each distinct value (and at DEBUG afterwards) (#3353).
* Fixed rust/clippy errors and warnings up to rust 1.88. (#3334, #3354, #3355, #3405, #3406, #3421, #3467, #3491, #3515, #3556, #3591, #3632)
* Fixed a link error for rust 1.82. (#3445)
* Fixed a shim panic (which causes shadow to hang) when golang's default SIGTERM handler runs in a managed program (and potentially other cases where a signal handler stack is legitimately reused). (#3396)
* Replaced the experimental option `--log-errors-to-tty` with the (still experimental) option
`--report-errors-to-stderr`. The new option no longer tries to detect whether
`stdout` or `stderr` are terminals (or the same destination), and instead
reports errors in a different format on `stderr` to make the duplication easier
to sort out in the case that `stdout` and `stderr` are merged. (#3428)
* Sending a packet to an unknown IP address no longer causes Shadow to panic. (#3411)
* Improved the "deterministic" strace logging mode by hiding some non-deterministic syscall arguments. (#3473)
* Shadow now overwrites the 16 bytes of random data that the Linux kernel provides in the auxiliary
  vector, fixing (#3539). This improves determinism, especially for golang
  programs (#2693), including tor simulations that include golang programs such
  as the obfs4proxy pluggable transport (#3538). (#3542)
* Fixed the behavior of sockets bound to the loopback interface: Shadow no longer panics in some cases where `connect` and `sendmsg` syscalls are used with a non-loopback address. (#3531)
* Fixed the behaviour of an implicit bind during a `sendmsg` syscall for UDP sockets. (#3545)
* Fixed a TCP socket bug causing the FIN packet to be sent out of order. (#3562, #3570)
* Fixed (lack of) `EPOLLRDHUP` reporting in `epoll` for TCP sockets (#3574), which in turn
fixes network connections sometimes trying to read indefinitely after the other
end has closed the connection in Rust's tokio async runtime (as in
https://gitlab.torproject.org/tpo/core/arti/-/issues/1972).
* Fixed the `faccessat` syscall handler to not incorrectly take a `flags` parameter, and added support the `faccessat2` syscall which *does* take a `flags` parameter. (#3578)
* Flags passed to the `setup` script will now pass "OFF" to CMake explicitly, rather than omitting the value and letting CMake choose whether it's "ON" or "OFF". (#3592)
* Ensure file descriptors are processed in deterministic (sorted) order when bulk-closing, e.g. via `close_range` or when terminating a simulated process. (#3614)
* Fix opening `/proc/self/*` so that they open the file corresponding to the caller's process instead of shadow's. (#3613)
* Intercept reads to `/proc/sys/kernel/random/uuid` and return a simulated pseudorandom result instead of letting the host systerm return an actually-random result. (#3617, fixing #3188).
* Trap and emulate the `cpuid` instruction where the platform supports it (currently on relatively new intel processors), to report that the `rdrand` and `rdseed` instructions are unavailable. (#3619, fixing #1561 and #3610)
* Fixed an error when running clippy on Shadow and using newer compiler versions. (#3631)

Full changelog since v3.2.0:

- [Merged PRs v3.2.0..HEAD](https://github.com/shadow/shadow/pulls?q=is%3Apr+merged%3A2024-06-07T08%3A00-0400..2033-12-30T20%3A30-0400)
- [Closed issues v3.2.0..HEAD](https://github.com/shadow/shadow/issues?q=is%3Aissue+closed%3A2024-06-07T08%3A00-0400..2033-12-30T20%3A30-0400)
- [Full compare v3.2.0..HEAD](https://github.com/shadow/shadow/compare/v3.2.0...HEAD)
