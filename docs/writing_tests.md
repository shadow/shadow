# Writing Tests

Tests for Shadow generally fall into four categories:

- system call tests
- regression tests
- application tests
- unit tests

Some of these tests may be marked as "extra" tests, which means they are not
run by default.

## System call tests

Shadow executes real unmodified applications and co-opts them by intercepting
and interposing at the system call API. This means that Shadow must try to
emulate Linux system calls. Shadow doesn't always need to emulate every system
call exactly as Linux does, but it's usually good to try to emulate Linux as
closely as possible. When Shadow deviates from Linux behaviour, Shadow is less
likely to accurately represent real-world behaviour in its simulation.

When writing new system call handlers or modifying the behaviour of existing
ones, it's important to write tests that verify the correctness of the new
behaviour. These system call tests are **required** in pull requests that add
to or modify the behaviour of Shadow's system calls. Usually this means that
tests are written which execute the system call with a variety of arguments,
and we verify that the system call returns the same values in both Linux and
Shadow.

These tests fall into two categories: domain-specific system call tests and
fuzzing tests. The [domain-specific tests][domain-tests] should test the system
call under a variety of typical use cases, as well as some edge cases (for
example passing NULL pointers, negative lengths, etc). The [fuzz
tests][fuzz-tests] should test many various combinations of the possible
argument values. These two types of tests are discussed further below.

Our existing tests are not always consistent in how the tests are organized or
designed, so you don't need to follow the exact same design as other tests in
the Shadow repository. If you're adding new tests to an existing file, you
should try to write the tests in a similar style to the existing tests.

These tests typically use the [libc][libc] library to test the system calls;
for example `libc::listen(fd, 10)`. For the most part the tests assume that the
libc system call wrappers are the same as the kernel system calls themselves,
but this is not always the case. Sometimes they differ and you might want to
make the system call directly (for example the glibc `fork()` system call
wrapper usually makes a `clone` system call, not a `fork` system call), or
there might not be a libc wrapper for the system call that you wish to test
(for example `set_tid_address`). In this case you probably want to use the
[linux-api][linux-api] library which makes the system call directly without
using a third-party library like glibc. The linux-api library only implements a
handful of system calls, and we've been adding more as we need them. You may
need to add support for the system call you wish to test to linux-api.

These tests are run emulated within Shadow and natively outside of Shadow. This
is done using the CMake `add_linux_tests` and `add_shadow_tests` macros. The
tests are built by Cargo and then run by CMake. For example the `listen` tests
use:

```cmake
add_linux_tests(BASENAME listen COMMAND sh -c "../../../target/debug/test_listen --libc-passing")
add_shadow_tests(BASENAME listen)
```

which results in the CMake tests:

```text
1/2 Test #110: listen-shadow ....................   Passed    0.56 sec
2/2 Test #109: listen-linux .....................   Passed   10.12 sec
```

[libc]: https://docs.rs/libc/latest/libc/
[linux-api]: https://shadow.github.io/docs/rust/linux_api/
[domain-tests]: writing_tests.md#domain-specific-system-call-tests
[fuzz-tests]: writing_tests.md#fuzz-tests

### Domain-specific system call tests

Here is an example of an existing test for the
[`listen`](https://man7.org/linux/man-pages/man2/listen.2.html) system call:

```rust
/// Test listen using a backlog of 0.
fn test_zero_backlog(
    domain: libc::c_int,
    sock_type: libc::c_int,
    flag: libc::c_int,
    bind: Option<SockAddr>,
) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type | flag, 0) };
    assert!(fd >= 0);

    if let Some(address) = bind {
        bind_fd(fd, address);
    }

    let args = ListenArguments { fd, backlog: 0 };

    let expected_errno = match (domain, sock_type, bind) {
        (libc::AF_INET, libc::SOCK_STREAM, _) => None,
        (libc::AF_UNIX, libc::SOCK_STREAM | libc::SOCK_SEQPACKET, Some(_)) => None,
        (libc::AF_UNIX, libc::SOCK_STREAM | libc::SOCK_SEQPACKET, None) => Some(libc::EINVAL),
        (_, libc::SOCK_DGRAM, _) => Some(libc::EOPNOTSUPP),
        _ => unimplemented!(),
    };

    test_utils::run_and_close_fds(&[fd], || check_listen_call(&args, expected_errno))
}
```

There are many `listen` tests including the one above, such as
`test_zero_backlog`, `test_negative_backlog`, `test_large_backlog`,
`test_listen_twice`, `test_reduced_backlog`, and more.

### Fuzz tests

"Fuzz"-style testing is another way we test syscalls: they use some [support
code][fuzz-support] to test many various combinations of the possible argument
values expected by a syscall, and verify that the return value for each
combination of arguments is the same as what Linux returns. Because the
developer usually writes these tests to cover most or all possible argument
combinations, it ensures that Shadow's emulation of the syscall is highly
accurate.

Fuzz tests can be a bit trickier to write, especially for more complicated
syscalls, and sometimes they don't make sense (e.g., when testing what happens
when trying to `connect()` to a TCP server with a full accept queue). They
often help us find inconsistent behavior between Shadow and Linux and help us
make Shadow more accurate, so we prefer that fuzz tests are included with pull
requests when possible.

There are some good examples of writing fuzz tests in our time-related test
code in [`src/test/time`][time-tests]. For example, the
[`clock_nanosleep`][clock-nanosleep] test demonstrates how to test the syscall
with all combinations of its arguments with both valid and invalid values.

[fuzz-support]: https://github.com/shadow/shadow/blob/main/src/test/test_utils.rs
[time-tests]: https://github.com/shadow/shadow/tree/main/src/test/time
[clock-nanosleep]: https://github.com/shadow/shadow/tree/main/src/test/time/clock_nanosleep/test_clock_nanosleep.rs

## Unit tests

Shadow supports unit tests for rust code. These can be written as standard rust
unit tests. These tests run natively and not under Shadow, but they are also
run under [Miri][miri] and [Loom][loom] as "extra" tests.

For example see the [`IntervalMap`][interval-map] tests.

```rust
#[cfg(test)]
mod tests {
    use super::*;

    // ...

    #[test]
    fn test_insert_into_empty() {
        let mut m = IntervalMap::new();
        insert_and_validate(&mut m, 10..20, "x", &[], &[(10..20, "x")]);
    }

    // ...
}
```

```text
1/1 Test #1: rust-unit-tests ..................   Passed  149.52 sec
```

[miri]: https://github.com/rust-lang/miri
[loom]: https://github.com/tokio-rs/loom
[interval-map]: https://github.com/shadow/shadow/blob/main/src/main/utility/interval_map.rs

## Regression tests

Sometimes it's useful to write a regression test that doesn't belong under any
specific system call tests. These tests can be written like the system call
tests above, but are stored in the [`src/test/regression/`][regression-tests]
directory.

[regression-tests]: https://github.com/shadow/shadow/tree/main/src/test/regression

## Application tests

It's often useful to test that applications behave correctly in Shadow. These
tests do not replace the need for the system call tests above, but can
complement them. For example we have [tor][tor-tests] and [tgen][tgen-tests]
tests. These help prevent regressions where we accidentally break Tor
behaviour.

We also run our [examples][examples] as tests. These examples include those in
our documentation (for example see the ["getting started"
example][getting-started]) as well as other application examples.

[tor-tests]: https://github.com/shadow/shadow/tree/main/src/test/tor
[tgen-tests]: https://github.com/shadow/shadow/tree/main/src/test/tgen
[examples]: https://github.com/shadow/shadow/tree/main/examples
[getting-started]: https://github.com/shadow/shadow/blob/main/docs/getting_started_basic.md#configuring-the-simulation

## Extra tests

Any of the tests above may be configured as an "extra" test. These tests are
not run by default and require that Shadow is built and tested using the
`--extra` flag.

```bash
./setup build --test --extra
./setup test --extra
```

These are usually tests that require extra dependencies, tests which take a
long time to build or run, or tests which might be difficult to maintain. These
tests may be removed at any time if they become difficult to maintain or they
update to require features that Shadow doesn't or can't support. An example
could be if an application is using epoll, but then updates to use io\_uring
which Shadow doesn't support (and would take a lot of effort to support), we
would need to remove the test.

Extra tests currently run in the CI environment but only under a single
platform, so they're not as well tested as non-"extra" tests.
