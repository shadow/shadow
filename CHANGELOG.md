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

PATCH changes (bugfixes):

* Log messages about unrecognized sockopt values are now only logged at level WARN once for each distinct value (and at DEBUG afterwards) (#3353).
* Fixed rust/clippy warnings for rust 1.80. (#3354, #3355)
* Fixed a build error on rust nightly (and future stable rust versions) by upgrading dependencies. (#3334)

Full changelog since v3.2.0:

- [Merged PRs v3.2.0..HEAD](https://github.com/shadow/shadow/pulls?q=is%3Apr+merged%3A2024-06-07T08%3A00-0400..2033-12-30T20%3A30-0400)
- [Closed issues v3.2.0..HEAD](https://github.com/shadow/shadow/issues?q=is%3Aissue+closed%3A2024-06-07T08%3A00-0400..2033-12-30T20%3A30-0400)
- [Full compare v3.2.0..HEAD](https://github.com/shadow/shadow/compare/v3.2.0...HEAD)
