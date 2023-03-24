A list of user-facing changes since the latest Shadow release.

* Fixed a memory leak of about 16 bytes per thread due to
failing to unregister exited threads with a watchdog thread. This is unlikely to
have been noticeable effect in typical simulations. In particular the per-thread
data was already getting freed when the whole process exited, so it would only
affect a process that created and terminated many threads over its lifetime.

Raw changes since v2.5.0:

* [Merged PRs](https://github.com/shadow/shadow/pulls?q=is%3Apr+merged%3A%3E2023-03-23T18%3A20-0400)
* [Closed issues](https://github.com/shadow/shadow/issues?q=is%3Aissue+closed%3A%3E2023-03-23T18%3A20-0400)
