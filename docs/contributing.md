# Contributing

**Summary:**

- contribute changes through [pull requests][pull-requests]
- encouraged to use [issue][issues] and [discussion][discussions] posts to
  notify us beforehand
- changes must include [tests][tests]
  - System call tests (domain-specific and fuzz tests) (**required**)
  - Unit tests (preferred when possible)
  - Regression tests (as needed)
  - Application tests (as needed)
- pull requests should be easy to review
- changes should be easy to maintain
- new code should be written in [Rust](https://www.rust-lang.org/)
- see our [coding guide and best practices][coding] for Shadow

New features, bug fixes, documentation changes, etc. can be submitted through a
[GitHub pull request][pull-requests]. For large changes we encourage you to
post an [issue][issues] or [discussion][discussions] before submitting your
pull request so that you can make sure your changes fit well with the direction
of the project. This is especially applicable to large changes. This way you
won't spend time writing a pull request that we can't merge into Shadow. For
details about how to draft pull requests and respond to reviewer feedback, see
our [additional documentation][pull-requests-doc].

All pull requests with new or changed features should contain tests to validate
that they work as expected and that they mirror similar behaviour in Linux. If
changes or additions are made that affect Shadow's system call support, the
pull request **must** also include [system call tests][system-call-tests] that
test the new or changed behaviour. The more tests that you include, the more
confident that we'll be that the changes are correct, and the more likely it
will be that your changes can be merged. We know that tests aren't very
exciting to write, but Shadow relies heavily on tests to catch broken features
and discrepancies with Linux. For more information about writing tests for
Shadow, see our ["Writing Tests"][tests] documentation.

Shadow is a community-supported project and the maintainers might not have a
lot of time to review pull requests. Submitting pull requests with good
documentation, tests, clear commit messages, and concise changes will help the
maintainers with their reviews, and also help increase the likelihood that we
will be able to merge your changes.

A core principle of Shadow development is that the project should be easy to
maintain. This means that we try to reduce the number of dependencies when
possible, and when we need to add new dependencies they should be popular
well-used dependencies with community support. This also means that it is
unlikely that we will add new dependencies for non-rust packages (for example
distro packages). Shadow is supported on multiple Linux platforms with
different packaging styles (APT and DNF) and different package versions, so
distro packages are difficult to support and maintain across all of our
supported platforms.

The main Shadow code base currently consists of both Rust and C code. We have
been migrating our C code to Rust, but this migration is still in progress. All
new code should be written in Rust. This includes the main Shadow application,
the shim, and tests. Exceptions may be made for bug fixes or when the change is
small and is in existing C code.

While we've been moving Shadow to Rust, we've learned a lot and have changed
some designs. This means that the existing Shadow code is not always consistent
in the way that it designs features or uses third-party libraries. For best
practices and details about writing new code for Shadow, see our
[coding][coding] documentation.

If you have any questions about contributing to Shadow, feel free to ask us by
making a new [discussion][discussions] post.

[pull-requests]: https://github.com/shadow/shadow/pulls
[issues]: https://github.com/shadow/shadow/issues
[discussions]: https://github.com/shadow/shadow/discussions
[pull-requests-doc]: pull_requests.md
[tests]: writing_tests.md
[system-call-tests]: writing_tests.md#system-call-tests
[coding]: coding.md
