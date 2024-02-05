A list of changes since the latest Shadow release.

Changes since v3.1.0:

*

MAJOR changes (breaking):

*

MINOR changes (backwards-compatible):

* Added support for the `CLONE_CLEAR_SIGHAND` flag for the `clone3` syscall.

PATCH changes (bugfixes):

* On fork and fork-like invocations of `clone`, signal handlers are now correctly copied
from the parent instead of reset to default (unless `CLONE_CLEAR_SIGHAND` is used).
* Fix exponential slowdown after repeated usage of the `wait4` syscall.

Full changelog since v3.1.0:

- [Merged PRs v3.1.0..HEAD](https://github.com/shadow/shadow/pulls?q=is%3Apr+merged%3A2023-12-30T20%3A30-0400..2033-12-30T20%3A30-0400)
- [Closed issues v3.1.0..HEAD](https://github.com/shadow/shadow/issues?q=is%3Aissue+closed%3A2023-12-30T20%3A30-0400..2033-12-30T20%3A30-0400)
- [Full compare v3.1.0..HEAD](https://github.com/shadow/shadow/compare/v3.1.0...HEAD)
