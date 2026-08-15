A list of changes since the latest Shadow release.

Changes since v3.3.0:

Documentation / policy updates:

*

MAJOR changes (breaking):

*

MINOR changes (backwards-compatible):

* The experimental command-line option `--use-memory-manager` (experimental
  config option `use_memory_manager`) has been removed. This was a complex
  optimization that mapped managed process memory into memory shared with shadow
  so that it could access it directly. At the time that it was introduced, this
  was a huge improvement over copying a word at a time through `ptrace`
  operations. Since then, we've migrated the "copying" path to use the much more
  efficient `process_vm_readv` and `process_vm_writev` syscalls instead of
  `ptrace`, which made it nearly as fast as the "mapped" path.  Meanwhile, the
  "mapped" path has been expensive to maintain and prone to subtle bugs, and has
  never updated to be compatible with emulating the `exec` syscall. (#3780)

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
