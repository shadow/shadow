A list of user-facing changes since the latest Shadow release.

* Removed the experimental options `preload_spin_max` and `use_explicit_block_message`.
  These options were to support an execution model where Shadow workers ran on different
  CPU cores than the managed threads they were controlling, and each side would "spin"
  while waiting for a message from the other side. After extensive benchmarking we found
  that this was rarely a significant win, and dropped support for this behavior while
  migrating the core IPC functionality to Rust.

Raw changes since v2.4.0:

* [Merged PRs](https://github.com/shadow/shadow/pulls?q=is%3Apr+merged%3A%3E2023-01-25T10%3A30-0500)
* [Closed issues](https://github.com/shadow/shadow/issues?q=is%3Aissue+closed%3A%3E2023-01-25T10%3A30-0500)
