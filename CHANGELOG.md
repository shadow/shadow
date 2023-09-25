A list of changes since the latest Shadow release.

Changes since v3.0.0:

*

MAJOR changes (breaking):

*

MINOR changes (backwards-compatible):

* `ERROR`-level log lines are now logged to `stderr` in addition to `stdout` if `stdout`
is not a tty but `stderr` is. This helps make errors more visible in the common
case that `stdout` is redirected to a log file but `stderr` is not. This can
currently be disabled via the (unstable) option `log-errors-to-tty`.

* Added support for subprocess creation and management.
  * The `fork` syscall and `fork`-like invocations of the `clone` and `clone3` syscalls.
  * Process parent pid's, process group IDs, process session IDs, and related syscalls.
  * Child exit signals (e.g. SIGCHLD)
  * The `execve` syscall.

* Added Debian 12 (Bookworm) to our supported platforms.

PATCH changes (bugfixes):

* Updated documentation and tests to reflect that shadow no longer requires
`/dev/shm` to be executable. (This requirement was actually removed in v3.0.0)

* Removed several incorrect libc syscall wrappers. These wrappers are a "fast
path" for intercepting syscalls at the library level instead of via seccomp. The removed wrappers were for syscalls whose glibc functions have different semantics than the underlying syscall.

* Fixed a bug in `sched_getaffinity`. This bug was previously mostly latent due to an incorrectly generated libc syscall wrapper, though would have affected managed programs that
made the syscall without going through libc.

* Fixed [#2681](https://github.com/shadow/shadow/issues/2681): shadow can now escape spin loops
that use an inlined syscall instruction to make `sched_yield` syscalls.

Full changelog since v3.0.0:

- [Merged PRs v3.0.0..HEAD](https://github.com/shadow/shadow/pulls?q=is%3Apr+merged%3A2023-05-18T18%3A00-0400..2033-05-18T18%3A00-0400)
- [Closed issues v3.0.0..HEAD](https://github.com/shadow/shadow/issues?q=is%3Aissue+closed%3A2023-05-18T18%3A00-0400..2033-05-18T18%3A00-0400)
- [Full compare v3.0.0..HEAD](https://github.com/shadow/shadow/compare/v3.0.0...HEAD)
