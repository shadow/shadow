/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use test_utils::set;
use test_utils::TestEnvironment as TestEnv;

struct ListenArguments {
    fd: libc::c_int,
    backlog: libc::c_int,
}

#[derive(Debug, Copy, Clone)]
struct BindAddress {
    address: libc::in_addr_t,
    port: libc::in_port_t,
}

fn main() -> Result<(), String> {
    // should we restrict the tests we run?
    let filter_shadow_passing = std::env::args().any(|x| x == "--shadow-passing");
    let filter_libc_passing = std::env::args().any(|x| x == "--libc-passing");
    // should we summarize the results rather than exit on a failed test
    let summarize = std::env::args().any(|x| x == "--summarize");

    let mut tests = get_tests();
    if filter_shadow_passing {
        tests = tests
            .into_iter()
            .filter(|x| x.passing(TestEnv::Shadow))
            .collect()
    }
    if filter_libc_passing {
        tests = tests
            .into_iter()
            .filter(|x| x.passing(TestEnv::Libc))
            .collect()
    }

    test_utils::run_tests(&tests, summarize)?;

    println!("Success.");
    Ok(())
}

fn get_tests() -> Vec<test_utils::ShadowTest<(), String>> {
    let mut tests: Vec<test_utils::ShadowTest<_, _>> = vec![
        test_utils::ShadowTest::new(
            "test_invalid_fd",
            test_invalid_fd,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_non_existent_fd",
            test_non_existent_fd,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_non_socket_fd",
            test_non_socket_fd,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_invalid_sock_type",
            test_invalid_sock_type,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
    ];

    // optionally bind to an address before listening
    let bind_addresses = [
        None,
        Some(BindAddress {
            address: libc::INADDR_LOOPBACK.to_be(),
            port: 0u16.to_be(),
        }),
        Some(BindAddress {
            address: libc::INADDR_ANY.to_be(),
            port: 0u16.to_be(),
        }),
    ];

    // tests to repeat for different socket options
    for &sock_type in [libc::SOCK_STREAM, libc::SOCK_DGRAM].iter() {
        for &flag in [0, libc::SOCK_NONBLOCK, libc::SOCK_CLOEXEC].iter() {
            for &bind in bind_addresses.iter() {
                // add details to the test names to avoid duplicates
                let append_args =
                    |s| format!("{} <type={},flag={},bind={:?}>", s, sock_type, flag, bind);

                let more_tests: Vec<test_utils::ShadowTest<_, _>> = vec![
                    test_utils::ShadowTest::new(
                        &append_args("test_zero_backlog"),
                        move || test_zero_backlog(sock_type, flag, bind),
                        set![TestEnv::Libc, TestEnv::Shadow],
                    ),
                    test_utils::ShadowTest::new(
                        &append_args("test_negative_backlog"),
                        move || test_negative_backlog(sock_type, flag, bind),
                        set![TestEnv::Libc, TestEnv::Shadow],
                    ),
                    test_utils::ShadowTest::new(
                        &append_args("test_large_backlog"),
                        move || test_large_backlog(sock_type, flag, bind),
                        set![TestEnv::Libc, TestEnv::Shadow],
                    ),
                    test_utils::ShadowTest::new(
                        &append_args("test_listen_twice"),
                        move || test_listen_twice(sock_type, flag, bind),
                        set![TestEnv::Libc],
                    ),
                    test_utils::ShadowTest::new(
                        &append_args("test_after_close"),
                        move || test_after_close(sock_type, flag, bind),
                        set![TestEnv::Libc, TestEnv::Shadow],
                    ),
                ];

                tests.extend(more_tests);
            }
        }
    }

    tests
}

/// Test listen using an argument that cannot be a fd.
fn test_invalid_fd() -> Result<(), String> {
    let args = ListenArguments { fd: -1, backlog: 0 };

    check_listen_call(&args, Some(libc::EBADF))
}

/// Test listen using an argument that could be a fd, but is not.
fn test_non_existent_fd() -> Result<(), String> {
    let args = ListenArguments {
        fd: 8934,
        backlog: 0,
    };

    check_listen_call(&args, Some(libc::EBADF))
}

/// Test listen using a valid fd that is not a socket.
fn test_non_socket_fd() -> Result<(), String> {
    let args = ListenArguments {
        fd: 0, // assume the fd 0 is already open and is not a socket
        backlog: 0,
    };

    check_listen_call(&args, Some(libc::ENOTSOCK))
}

/// Test listen using an invalid socket type.
fn test_invalid_sock_type() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_DGRAM, 0) };
    assert!(fd >= 0);

    let args = ListenArguments { fd: fd, backlog: 0 };

    test_utils::run_and_close_fds(&[fd], || check_listen_call(&args, Some(libc::EOPNOTSUPP)))
}

/// Test listen using a backlog of 0.
fn test_zero_backlog(
    sock_type: libc::c_int,
    flag: libc::c_int,
    bind: Option<BindAddress>,
) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    assert!(fd >= 0);

    if let Some(address) = bind {
        bind_fd(fd, address);
    }

    let args = ListenArguments { fd: fd, backlog: 0 };

    let expected_errno = if [libc::SOCK_STREAM, libc::SOCK_SEQPACKET].contains(&sock_type) {
        None
    } else {
        Some(libc::EOPNOTSUPP)
    };

    test_utils::run_and_close_fds(&[fd], || check_listen_call(&args, expected_errno))
}

/// Test listen using a backlog of -1.
fn test_negative_backlog(
    sock_type: libc::c_int,
    flag: libc::c_int,
    bind: Option<BindAddress>,
) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    assert!(fd >= 0);

    if let Some(address) = bind {
        bind_fd(fd, address);
    }

    let args = ListenArguments {
        fd: fd,
        backlog: -1,
    };

    let expected_errno = if [libc::SOCK_STREAM, libc::SOCK_SEQPACKET].contains(&sock_type) {
        None
    } else {
        Some(libc::EOPNOTSUPP)
    };

    test_utils::run_and_close_fds(&[fd], || check_listen_call(&args, expected_errno))
}

/// Test listen using a backlog of INT_MAX.
fn test_large_backlog(
    sock_type: libc::c_int,
    flag: libc::c_int,
    bind: Option<BindAddress>,
) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    assert!(fd >= 0);

    if let Some(address) = bind {
        bind_fd(fd, address);
    }

    let args = ListenArguments {
        fd: fd,
        backlog: libc::INT_MAX,
    };

    let expected_errno = if [libc::SOCK_STREAM, libc::SOCK_SEQPACKET].contains(&sock_type) {
        None
    } else {
        Some(libc::EOPNOTSUPP)
    };

    test_utils::run_and_close_fds(&[fd], || check_listen_call(&args, expected_errno))
}

/// Test calling listen twice for the same socket.
fn test_listen_twice(
    sock_type: libc::c_int,
    flag: libc::c_int,
    bind: Option<BindAddress>,
) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    assert!(fd >= 0);

    if let Some(address) = bind {
        bind_fd(fd, address);
    }

    let args1 = ListenArguments {
        fd: fd,
        backlog: 10,
    };

    let args2 = ListenArguments { fd: fd, backlog: 0 };

    let expected_errno = if [libc::SOCK_STREAM, libc::SOCK_SEQPACKET].contains(&sock_type) {
        None
    } else {
        Some(libc::EOPNOTSUPP)
    };

    test_utils::run_and_close_fds(&[fd], || {
        check_listen_call(&args1, expected_errno)?;
        check_listen_call(&args2, expected_errno)
    })
}

/// Test listen after closing the socket.
fn test_after_close(
    sock_type: libc::c_int,
    flag: libc::c_int,
    bind: Option<BindAddress>,
) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    assert!(fd >= 0);

    if let Some(address) = bind {
        bind_fd(fd, address);
    }

    // close the file descriptor
    let rv = unsafe { libc::close(fd) };
    assert_eq!(rv, 0);

    let args = ListenArguments {
        fd: fd,
        backlog: 100,
    };

    check_listen_call(&args, Some(libc::EBADF))
}

/// Bind the fd to the address.
fn bind_fd(fd: libc::c_int, bind: BindAddress) {
    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: bind.port,
        sin_addr: libc::in_addr {
            s_addr: bind.address,
        },
        sin_zero: [0; 8],
    };
    let rv = unsafe {
        libc::bind(
            fd,
            &addr as *const libc::sockaddr_in as *const libc::sockaddr,
            std::mem::size_of_val(&addr) as u32,
        )
    };
    assert_eq!(rv, 0);
}

fn check_listen_call(
    args: &ListenArguments,
    expected_errno: Option<libc::c_int>,
) -> Result<(), String> {
    let rv = unsafe { libc::listen(args.fd, args.backlog) };

    let errno = test_utils::get_errno();

    match expected_errno {
        // if we expect the socket() call to return an error (rv should be -1)
        Some(expected_errno) => {
            if rv != -1 {
                return Err(format!("Expecting a return value of -1, received {}", rv));
            }
            if errno != expected_errno {
                return Err(format!(
                    "Expecting errno {} \"{}\", received {} \"{}\"",
                    expected_errno,
                    test_utils::get_errno_message(expected_errno),
                    errno,
                    test_utils::get_errno_message(errno)
                ));
            }
        }
        // if no error is expected (rv should be 0)
        None => {
            if rv != 0 {
                return Err(format!(
                    "Expecting a return value of 0, received {} \"{}\"",
                    rv,
                    test_utils::get_errno_message(errno)
                ));
            }
        }
    }

    Ok(())
}
