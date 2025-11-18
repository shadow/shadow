A list of changes since the latest Shadow release.

Changes since v3.3.0:

Documentation / policy updates:

*

MAJOR changes (breaking):

*

MINOR changes (backwards-compatible):

*

PATCH changes (bugfixes):

* Avoid lowering native resource limits of managed processes beyond what is needed
for shadow's `LD_PRELOAD`d shim to function. (#3682 fixing #3681)
* Fixed a bug where setting `hosts.<hostname>.bandwidth_up` was ineffective. (#3699)
* Fixed a bug where if `hosts.<hostname>.bandwidth_down` was configured for a host,
  it would also overwrite the host's `bandwidth_up`. (#3699)

Full changelog since v3.3.0:

- [Merged PRs v3.3.0..HEAD](https://github.com/shadow/shadow/pulls?q=is%3Apr%20merged%3A2025-10-16T11%3A30-0400..2033-12-30T20%3A30-0400)
- [Closed issues v3.3.0..HEAD](https://github.com/shadow/shadow/issues?q=is%3Aissue%20closed%3A2025-10-16T11%3A30-0400..2033-12-30T20%3A30-0400)
- [Full compare v3.3.0..HEAD](https://github.com/shadow/shadow/compare/v3.3.0...HEAD)
