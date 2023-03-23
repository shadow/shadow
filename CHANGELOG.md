A list of user-facing changes since the latest Shadow release.

* Added a contributor code of conduct.
  https://github.com/shadow/shadow/blob/8003656d94fe781902f8b09420d994963a81c62c/CODE_OF_CONDUCT.md
* Set the shim library's stdout/stderr to the shim log file. This should only
  affect simulations that use experimental features to disable interposition.
  https://github.com/shadow/shadow/pull/2725
* Removed the experimental options `preload_spin_max` and `use_explicit_block_message`.
  These options were to support an execution model where Shadow workers ran on different
  CPU cores than the managed threads they were controlling, and each side would "spin"
  while waiting for a message from the other side. After extensive benchmarking we found
  that this was rarely a significant win, and dropped support for this behavior while
  migrating the core IPC functionality to Rust.
* Changed the order that events are processed in Shadow. Some simulations may
  see improved runtime performance. https://github.com/shadow/shadow/pull/2522
* Removed the experimental Dockerfile and related documentation. This is
  unrelated to running Shadow in Docker following the [existing supported
  documentation](https://shadow.github.io/docs/guide/supported_platforms.html#docker),
  and we continue to support running Shadow in Docker.

Raw changes since v2.4.0:

* [Merged PRs](https://github.com/shadow/shadow/pulls?q=is%3Apr+merged%3A%3E2023-01-25T10%3A30-0500)
* [Closed issues](https://github.com/shadow/shadow/issues?q=is%3Aissue+closed%3A%3E2023-01-25T10%3A30-0500)
