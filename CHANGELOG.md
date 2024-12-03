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
* Relaxed restriction on assigning restricted IPs (such as 192.168.0.*).
Within the simulation these are treated as fully routable IPs, so are required
to be unique as with any other IP address assignment. (#3414)
* Removed the experimental tracker feature that logged heartbeat messages for every host every second. Also removed the corresponding python scripts for parsing and plotting tracker log messages and data. (#3446)
* Removed support for Debian 10 (Buster), which has [passed EOL](https://wiki.debian.org/LTS).

PATCH changes (bugfixes):

* Log messages about unrecognized sockopt values are now only logged at level WARN once for each distinct value (and at DEBUG afterwards) (#3353).
* Fixed rust/clippy warnings for rust 1.80. (#3354, #3355)
* Fixed a build error on rust nightly (and future stable rust versions) by upgrading dependencies. (#3334)
* Fixed a shim panic (which causes shadow to hang) when golang's default SIGTERM handler runs in a managed program (and potentially other cases where a signal handler stack is legitimately reused). (#3396)
* Replaced the experimental option `--log-errors-to-tty` with the (still experimental) option
`--report-errors-to-stderr`. The new option no longer tries to detect whether
`stdout` or `stderr` are terminals (or the same destination), and instead
reports errors in a different format on `stderr` to make the duplication easier
to sort out in the case that `stdout` and `stderr` are merged. (#3428)
* Fixed a link error for rust 1.82. (#3445)

Full changelog since v3.2.0:

- [Merged PRs v3.2.0..HEAD](https://github.com/shadow/shadow/pulls?q=is%3Apr+merged%3A2024-06-07T08%3A00-0400..2033-12-30T20%3A30-0400)
- [Closed issues v3.2.0..HEAD](https://github.com/shadow/shadow/issues?q=is%3Aissue+closed%3A2024-06-07T08%3A00-0400..2033-12-30T20%3A30-0400)
- [Full compare v3.2.0..HEAD](https://github.com/shadow/shadow/compare/v3.2.0...HEAD)
