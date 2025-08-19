/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use std::os::fd::AsRawFd as _;
use std::os::fd::FromRawFd as _;
use std::os::fd::IntoRawFd as _;
use std::os::fd::OwnedFd;

use test_utils::TestEnvironment as TestEnv;
use test_utils::socket_utils;
use test_utils::socket_utils::SockAddr;
use test_utils::{AsMutPtr, set};

struct AcceptArguments {
    fd: libc::c_int,
    addr: Option<SockAddr>,
    addr_len: Option<libc::socklen_t>,
    flags: libc::c_int,
}

#[derive(Debug, Copy, Clone, PartialEq, Eq)]
enum AcceptFn {
    Accept,
    Accept4,
}

fn main() -> Result<(), String> {
    // should we restrict the tests we run?
    let filter_shadow_passing = std::env::args().any(|x| x == "--shadow-passing");
    let filter_libc_passing = std::env::args().any(|x| x == "--libc-passing");
    // should we summarize the results rather than exit on a failed test
    let summarize = std::env::args().any(|x| x == "--summarize");

    let mut tests = get_tests();
    if filter_shadow_passing {
        tests.retain(|x| x.passing(TestEnv::Shadow));
    }
    if filter_libc_passing {
        tests.retain(|x| x.passing(TestEnv::Libc));
    }

    test_utils::run_tests(&tests, summarize)?;

    println!("Success.");
    Ok(())
}

fn get_tests() -> Vec<test_utils::ShadowTest<(), String>> {
    let mut tests: Vec<test_utils::ShadowTest<_, _>> = vec![];

    for &accept_fn in [AcceptFn::Accept, AcceptFn::Accept4].iter() {
        let append_args = |s| format!("{s} <fn={accept_fn:?}>");

        tests.extend(vec![
            test_utils::ShadowTest::new(
                &append_args("test_invalid_fd"),
                move || test_invalid_fd(accept_fn),
                set![TestEnv::Libc, TestEnv::Shadow],
            ),
            test_utils::ShadowTest::new(
                &append_args("test_non_existent_fd"),
                move || test_non_existent_fd(accept_fn),
                set![TestEnv::Libc, TestEnv::Shadow],
            ),
            test_utils::ShadowTest::new(
                &append_args("test_non_socket_fd"),
                move || test_non_socket_fd(accept_fn),
                set![TestEnv::Libc, TestEnv::Shadow],
            ),
            test_utils::ShadowTest::new(
                &append_args("test_invalid_sock_type"),
                move || test_invalid_sock_type(accept_fn),
                set![TestEnv::Libc, TestEnv::Shadow],
            ),
        ]);

        let accept_flags = [
            0,
            libc::SOCK_NONBLOCK,
            libc::SOCK_CLOEXEC,
            libc::SOCK_NONBLOCK | libc::SOCK_CLOEXEC,
        ];

        for &domain in [libc::AF_INET, libc::AF_UNIX].iter() {
            for &sock_type in [libc::SOCK_STREAM, libc::SOCK_DGRAM, libc::SOCK_SEQPACKET].iter() {
                // skip tests that use SOCK_SEQPACKET with INET sockets
                if domain == libc::AF_INET && sock_type == libc::SOCK_SEQPACKET {
                    continue;
                }

                for &sock_flag in [0, libc::SOCK_NONBLOCK, libc::SOCK_CLOEXEC].iter() {
                    for &accept_flag in accept_flags.iter() {
                        // since accept() doesn't accept flags, we should skip them
                        if accept_flag != 0 && accept_fn != AcceptFn::Accept4 {
                            continue;
                        }

                        let append_args = |s| {
                            format!(
                                "{s} <fn={accept_fn:?},domain={domain},sock_type={sock_type},sock_flag={sock_flag},accept_flag={accept_flag:?}>",
                            )
                        };

                        tests.extend(vec![
                            test_utils::ShadowTest::new(
                                &append_args("test_non_listening_fd"),
                                move || {
                                    test_non_listening_fd(
                                        accept_fn,
                                        domain,
                                        sock_type,
                                        sock_flag,
                                        accept_flag,
                                    )
                                },
                                set![TestEnv::Libc, TestEnv::Shadow],
                            ),
                            test_utils::ShadowTest::new(
                                &append_args("test_null_addr"),
                                move || {
                                    test_null_addr(
                                        accept_fn,
                                        domain,
                                        sock_type,
                                        sock_flag,
                                        accept_flag,
                                    )
                                },
                                set![TestEnv::Libc, TestEnv::Shadow],
                            ),
                            test_utils::ShadowTest::new(
                                &append_args("test_null_len"),
                                move || {
                                    test_null_len(
                                        accept_fn,
                                        domain,
                                        sock_type,
                                        sock_flag,
                                        accept_flag,
                                    )
                                },
                                set![TestEnv::Libc, TestEnv::Shadow],
                            ),
                            test_utils::ShadowTest::new(
                                &append_args("test_short_len"),
                                move || {
                                    test_short_len(
                                        accept_fn,
                                        domain,
                                        sock_type,
                                        sock_flag,
                                        accept_flag,
                                    )
                                },
                                set![TestEnv::Libc, TestEnv::Shadow],
                            ),
                            test_utils::ShadowTest::new(
                                &append_args("test_zero_len"),
                                move || {
                                    test_zero_len(
                                        accept_fn,
                                        domain,
                                        sock_type,
                                        sock_flag,
                                        accept_flag,
                                    )
                                },
                                set![TestEnv::Libc, TestEnv::Shadow],
                            ),
                            test_utils::ShadowTest::new(
                                &append_args("test_after_close"),
                                move || {
                                    test_after_close(
                                        accept_fn,
                                        domain,
                                        sock_type,
                                        sock_flag,
                                        accept_flag,
                                    )
                                },
                                set![TestEnv::Libc, TestEnv::Shadow],
                            ),
                            test_utils::ShadowTest::new(
                                &append_args("test_correctness <bind_client=false>"),
                                move || {
                                    test_correctness(
                                        accept_fn,
                                        domain,
                                        sock_type,
                                        sock_flag,
                                        accept_flag,
                                        /* bind_client= */ false,
                                    )
                                },
                                set![TestEnv::Libc, TestEnv::Shadow],
                            ),
                            test_utils::ShadowTest::new(
                                &append_args("test_correctness <bind_client=true>"),
                                move || {
                                    test_correctness(
                                        accept_fn,
                                        domain,
                                        sock_type,
                                        sock_flag,
                                        accept_flag,
                                        /* bind_client= */ true,
                                    )
                                },
                                set![TestEnv::Libc, TestEnv::Shadow],
                            ),
                            test_utils::ShadowTest::new(
                                &append_args("test_after_client_closed"),
                                move || {
                                    test_after_client_closed(
                                        accept_fn,
                                        domain,
                                        sock_type,
                                        sock_flag,
                                        accept_flag,
                                    )
                                },
                                set![TestEnv::Libc, TestEnv::Shadow],
                            ),
                        ]);
                    }
                }
            }
        }
    }

    tests
}

/// Test accept using an argument that cannot be a fd.
fn test_invalid_fd(accept_fn: AcceptFn) -> Result<(), String> {
    let mut args = AcceptArguments {
        fd: -1,
        addr: None,
        addr_len: None,
        flags: 0,
    };

    let fd = check_accept_call(&mut args, accept_fn, Some(libc::EBADF))?;
    if let Some(fd) = fd {
        let rv = unsafe { libc::close(fd) };
        assert_eq!(rv, 0, "Could not close the fd");
    }

    Ok(())
}

/// Test accept using an argument that could be a fd, but is not.
fn test_non_existent_fd(accept_fn: AcceptFn) -> Result<(), String> {
    let mut args = AcceptArguments {
        fd: 8934,
        addr: None,
        addr_len: None,
        flags: 0,
    };

    let fd = check_accept_call(&mut args, accept_fn, Some(libc::EBADF))?;
    if let Some(fd) = fd {
        let rv = unsafe { libc::close(fd) };
        assert_eq!(rv, 0, "Could not close the fd");
    }

    Ok(())
}

/// Test accept using a valid fd that is not a socket.
fn test_non_socket_fd(accept_fn: AcceptFn) -> Result<(), String> {
    // assume the fd 0 is already open and is not a socket
    let mut args = AcceptArguments {
        fd: 0,
        addr: None,
        addr_len: None,
        flags: 0,
    };

    let fd = check_accept_call(&mut args, accept_fn, Some(libc::ENOTSOCK))?;
    if let Some(fd) = fd {
        let rv = unsafe { libc::close(fd) };
        assert_eq!(rv, 0, "Could not close the fd");
    }

    Ok(())
}

/// Test accept using an invalid socket type.
fn test_invalid_sock_type(accept_fn: AcceptFn) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_DGRAM, 0) };
    assert!(fd >= 0);

    let mut args = AcceptArguments {
        fd,
        addr: None,
        addr_len: None,
        flags: 0,
    };

    test_utils::run_and_close_fds(&[fd], || {
        let fd = check_accept_call(&mut args, accept_fn, Some(libc::EOPNOTSUPP))?;
        if let Some(fd) = fd {
            let rv = unsafe { libc::close(fd) };
            assert_eq!(rv, 0, "Could not close the fd");
        }
        Ok(())
    })
}

/// Test accept using a non-listening socket.
fn test_non_listening_fd(
    accept_fn: AcceptFn,
    domain: libc::c_int,
    sock_type: libc::c_int,
    sock_flag: libc::c_int,
    accept_flag: libc::c_int,
) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type | sock_flag, 0) };
    assert!(fd >= 0);

    let mut args = AcceptArguments {
        fd,
        addr: None,
        addr_len: None,
        flags: accept_flag,
    };

    let expected_errno = match (domain, sock_type) {
        (libc::AF_INET, libc::SOCK_STREAM) => Some(libc::EINVAL),
        (libc::AF_UNIX, libc::SOCK_STREAM | libc::SOCK_SEQPACKET) => Some(libc::EINVAL),
        (_, libc::SOCK_DGRAM) => Some(libc::EOPNOTSUPP),
        _ => unimplemented!(),
    };

    test_utils::run_and_close_fds(&[fd], || {
        let fd = check_accept_call(&mut args, accept_fn, expected_errno)?;
        if let Some(fd) = fd {
            let rv = unsafe { libc::close(fd) };
            assert_eq!(rv, 0, "Could not close the fd");
        }
        Ok(())
    })
}

/// Test accept using a NULL pointer in the address argument.
fn test_null_addr(
    accept_fn: AcceptFn,
    domain: libc::c_int,
    sock_type: libc::c_int,
    sock_flag: libc::c_int,
    accept_flag: libc::c_int,
) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(domain, sock_type | sock_flag, 0) };
    let fd_server = unsafe { libc::socket(domain, sock_type | sock_flag, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    let (server_addr, server_addr_len) = socket_utils::autobind_helper(fd_server, domain);

    if sock_type != libc::SOCK_DGRAM {
        // listen for connections
        let rv = unsafe { libc::listen(fd_server, 10) };
        assert_eq!(rv, 0);
    }

    // connect to the server address
    let rv = unsafe { libc::connect(fd_client, server_addr.as_ptr(), server_addr_len) };
    assert!(rv == 0 || (rv == -1 && test_utils::get_errno() == libc::EINPROGRESS));

    // shadow needs to run events, otherwise the accept call won't know it
    // has an incoming connection (SYN packet)
    let rv = unsafe { libc::usleep(2000) };
    assert_eq!(rv, 0);

    // accept() may mutate addr and addr_len
    let mut args = AcceptArguments {
        fd: fd_server,
        addr: None,
        addr_len: Some(5),
        flags: accept_flag,
    };

    let expected_errno = match (domain, sock_type) {
        (libc::AF_INET, libc::SOCK_STREAM) => None,
        (libc::AF_UNIX, libc::SOCK_STREAM | libc::SOCK_SEQPACKET) => None,
        (_, libc::SOCK_DGRAM) => Some(libc::EOPNOTSUPP),
        _ => unimplemented!(),
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        let fd = check_accept_call(&mut args, accept_fn, expected_errno)?;
        if let Some(fd) = fd {
            let rv = unsafe { libc::close(fd) };
            assert_eq!(rv, 0, "Could not close the fd");
        }
        Ok(())
    })
}

/// Test accept using a NULL pointer in the address length argument.
fn test_null_len(
    accept_fn: AcceptFn,
    domain: libc::c_int,
    sock_type: libc::c_int,
    sock_flag: libc::c_int,
    accept_flag: libc::c_int,
) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(domain, sock_type | sock_flag, 0) };
    let fd_server = unsafe { libc::socket(domain, sock_type | sock_flag, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    let (server_addr, server_addr_len) = socket_utils::autobind_helper(fd_server, domain);

    if sock_type != libc::SOCK_DGRAM {
        // listen for connections
        let rv = unsafe { libc::listen(fd_server, 10) };
        assert_eq!(rv, 0);
    }

    // connect to the server address
    let rv = unsafe { libc::connect(fd_client, server_addr.as_ptr(), server_addr_len) };
    //assert_eq!(rv, 0, "{}", test_utils::get_errno_message(test_utils::get_errno()));
    assert!(rv == 0 || (rv == -1 && test_utils::get_errno() == libc::EINPROGRESS));

    // shadow needs to run events, otherwise the accept call won't know it
    // has an incoming connection (SYN packet)
    let rv = unsafe { libc::usleep(2000) };
    assert_eq!(rv, 0);

    // accept() may mutate addr and addr_len
    let mut args = AcceptArguments {
        fd: fd_server,
        addr: Some(SockAddr::dummy_init_generic()),
        addr_len: None,
        flags: accept_flag,
    };

    let expected_errno = match (domain, sock_type) {
        (libc::AF_INET, libc::SOCK_STREAM) => Some(libc::EFAULT),
        (libc::AF_UNIX, libc::SOCK_STREAM | libc::SOCK_SEQPACKET) => Some(libc::EFAULT),
        (_, libc::SOCK_DGRAM) => Some(libc::EOPNOTSUPP),
        _ => unimplemented!(),
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        let fd = check_accept_call(&mut args, accept_fn, expected_errno)?;
        if let Some(fd) = fd {
            let rv = unsafe { libc::close(fd) };
            assert_eq!(rv, 0, "Could not close the fd");
        }
        Ok(())
    })
}

/// Test accept using an address length that is too small.
fn test_short_len(
    accept_fn: AcceptFn,
    domain: libc::c_int,
    sock_type: libc::c_int,
    sock_flag: libc::c_int,
    accept_flag: libc::c_int,
) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(domain, sock_type | sock_flag, 0) };
    let fd_server = unsafe { libc::socket(domain, sock_type | sock_flag, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    let (server_addr, server_addr_len) = socket_utils::autobind_helper(fd_server, domain);

    if sock_type != libc::SOCK_DGRAM {
        // listen for connections
        let rv = unsafe { libc::listen(fd_server, 10) };
        assert_eq!(rv, 0);
    }

    // connect to the server address
    let rv = unsafe { libc::connect(fd_client, server_addr.as_ptr(), server_addr_len) };
    //assert_eq!(rv, 0, "{}", test_utils::get_errno_message(test_utils::get_errno()));
    assert!(rv == 0 || (rv == -1 && test_utils::get_errno() == libc::EINPROGRESS));

    // shadow needs to run events, otherwise the accept call won't know it
    // has an incoming connection (SYN packet)
    let rv = unsafe { libc::usleep(10000) };
    assert_eq!(rv, 0);

    let accept_addr = match domain {
        libc::AF_INET => SockAddr::dummy_init_inet(),
        libc::AF_UNIX => SockAddr::dummy_init_unix(),
        _ => unimplemented!(),
    };

    // accept() may mutate addr and addr_len
    let mut args = AcceptArguments {
        fd: fd_server,
        addr: Some(accept_addr),
        addr_len: Some(accept_addr.ptr_size() - 1),
        flags: accept_flag,
    };

    let expected_errno = match (domain, sock_type) {
        (libc::AF_INET, libc::SOCK_STREAM) => None,
        (libc::AF_UNIX, libc::SOCK_STREAM | libc::SOCK_SEQPACKET) => None,
        (_, libc::SOCK_DGRAM) => Some(libc::EOPNOTSUPP),
        _ => unimplemented!(),
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || -> Result<(), String> {
        let fd = check_accept_call(&mut args, accept_fn, expected_errno)?;
        if let Some(fd) = fd {
            let rv = unsafe { libc::close(fd) };
            assert_eq!(rv, 0, "Could not close the fd");
        }
        Ok(())
    })?;

    // there was an error and the syscall didn't complete, so skip the later checks
    if expected_errno.is_some() {
        return Ok(());
    }

    let expected_addr_len = match domain {
        libc::AF_INET => std::mem::size_of::<libc::sockaddr_in>() as u32,
        libc::AF_UNIX => 2, // domain only
        _ => unimplemented!(),
    };

    // check that the returned length is expected
    test_utils::result_assert_eq(
        args.addr_len.unwrap(),
        expected_addr_len,
        "Unexpected addr length",
    )?;

    match domain {
        libc::AF_INET => {
            // check that the returned client address is expected
            test_utils::result_assert_eq(
                args.addr.unwrap().as_inet().unwrap().sin_family,
                libc::AF_INET as u16,
                "Unexpected family",
            )?;
            test_utils::result_assert(
                args.addr.unwrap().as_inet().unwrap().sin_port != 0u16.to_be(),
                "Unexpected port",
            )?;
            test_utils::result_assert_eq(
                args.addr.unwrap().as_inet().unwrap().sin_addr.s_addr,
                libc::INADDR_LOOPBACK.to_be(),
                "Unexpected address",
            )?;
            test_utils::result_assert_eq(
                args.addr.unwrap().as_inet().unwrap().sin_zero,
                [0, 0, 0, 0, 0, 0, 0, 1],
                "Unexpected padding",
            )?;
        }
        libc::AF_UNIX => {
            test_utils::result_assert_eq(
                args.addr.unwrap().as_unix().unwrap().sun_family,
                libc::AF_UNIX as u16,
                "Unexpected family",
            )?;
        }
        _ => unimplemented!(),
    }

    Ok(())
}

/// Test accept using an address length of 0.
fn test_zero_len(
    accept_fn: AcceptFn,
    domain: libc::c_int,
    sock_type: libc::c_int,
    sock_flag: libc::c_int,
    accept_flag: libc::c_int,
) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(domain, sock_type | sock_flag, 0) };
    let fd_server = unsafe { libc::socket(domain, sock_type | sock_flag, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    let (server_addr, server_addr_len) = socket_utils::autobind_helper(fd_server, domain);

    if sock_type != libc::SOCK_DGRAM {
        // listen for connections
        let rv = unsafe { libc::listen(fd_server, 10) };
        assert_eq!(rv, 0);
    }

    // connect to the server address
    let rv = unsafe { libc::connect(fd_client, server_addr.as_ptr(), server_addr_len) };
    assert!(rv == 0 || (rv == -1 && test_utils::get_errno() == libc::EINPROGRESS));

    // shadow needs to run events, otherwise the accept call won't know it
    // has an incoming connection (SYN packet)
    let rv = unsafe { libc::usleep(10000) };
    assert_eq!(rv, 0);

    let accept_addr = match domain {
        libc::AF_INET => SockAddr::dummy_init_inet(),
        libc::AF_UNIX => SockAddr::dummy_init_unix(),
        _ => unimplemented!(),
    };

    // accept() may mutate addr and addr_len
    let mut args = AcceptArguments {
        fd: fd_server,
        addr: Some(accept_addr),
        addr_len: Some(0u32),
        flags: accept_flag,
    };

    let expected_errno = match (domain, sock_type) {
        (libc::AF_INET, libc::SOCK_STREAM) => None,
        (libc::AF_UNIX, libc::SOCK_STREAM | libc::SOCK_SEQPACKET) => None,
        (_, libc::SOCK_DGRAM) => Some(libc::EOPNOTSUPP),
        _ => unimplemented!(),
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || -> Result<(), String> {
        let fd = check_accept_call(&mut args, accept_fn, expected_errno)?;
        if let Some(fd) = fd {
            let rv = unsafe { libc::close(fd) };
            assert_eq!(rv, 0, "Could not close the fd");
        }
        Ok(())
    })?;

    // there was an error and the syscall didn't complete, so skip the later checks
    if expected_errno.is_some() {
        return Ok(());
    }

    let expected_addr_len = match domain {
        libc::AF_INET => std::mem::size_of::<libc::sockaddr_in>() as u32,
        libc::AF_UNIX => 2, // domain only
        _ => unimplemented!(),
    };

    // check that the returned length is expected
    test_utils::result_assert_eq(
        args.addr_len.unwrap(),
        expected_addr_len,
        "Unexpected addr length",
    )?;

    match domain {
        libc::AF_INET => {
            // check that the returned client address is expected
            test_utils::result_assert_eq(
                args.addr.unwrap().as_inet().unwrap().sin_family,
                accept_addr.as_inet().unwrap().sin_family,
                "Unexpected family",
            )?;
            test_utils::result_assert_eq(
                args.addr.unwrap().as_inet().unwrap().sin_port,
                accept_addr.as_inet().unwrap().sin_port,
                "Unexpected port",
            )?;
            test_utils::result_assert_eq(
                args.addr.unwrap().as_inet().unwrap().sin_addr.s_addr,
                accept_addr.as_inet().unwrap().sin_addr.s_addr,
                "Unexpected address",
            )?;
            test_utils::result_assert_eq(
                args.addr.unwrap().as_inet().unwrap().sin_zero,
                accept_addr.as_inet().unwrap().sin_zero,
                "Unexpected padding",
            )?;
        }
        libc::AF_UNIX => {
            test_utils::result_assert_eq(
                args.addr.unwrap().as_unix().unwrap().sun_family,
                accept_addr.as_unix().unwrap().sun_family,
                "Unexpected family",
            )?;
        }
        _ => unimplemented!(),
    }

    Ok(())
}

/// Test accept after closing the socket.
fn test_after_close(
    accept_fn: AcceptFn,
    domain: libc::c_int,
    sock_type: libc::c_int,
    sock_flag: libc::c_int,
    accept_flag: libc::c_int,
) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type | sock_flag, 0) };
    assert!(fd >= 0);

    socket_utils::autobind_helper(fd, domain);

    if sock_type != libc::SOCK_DGRAM {
        // listen for connections
        let rv = unsafe { libc::listen(fd, 10) };
        assert_eq!(rv, 0);
    }

    // close the connection
    let rv = unsafe { libc::close(fd) };
    assert_eq!(rv, 0);

    // accept() may mutate addr and addr_len
    let mut args = AcceptArguments {
        fd,
        addr: None,
        addr_len: None,
        flags: accept_flag,
    };

    let fd = check_accept_call(&mut args, accept_fn, Some(libc::EBADF))?;
    if let Some(fd) = fd {
        let rv = unsafe { libc::close(fd) };
        assert_eq!(rv, 0, "Could not close the fd");
    }

    Ok(())
}

fn test_close_connection_without_accept(
    domain: libc::c_int,
    sock_type: libc::c_int,
    sock_flag: libc::c_int,
) -> Result<(), String> {
    assert_ne!(
        sock_type,
        libc::SOCK_DGRAM,
        "This test doesn't support datagram sockets"
    );

    let fd = unsafe { libc::socket(domain, sock_type | sock_flag, 0) };
    assert!(fd >= 0);
    let fd = unsafe { OwnedFd::from_raw_fd(fd) };

    let (server_addr, server_addr_len) = socket_utils::autobind_helper(fd.as_raw_fd(), domain);

    // listen for connections
    test_utils::assert_with_errno!(unsafe { libc::listen(fd.as_raw_fd(), 10) } == 0);

    let fd_client = unsafe { libc::socket(domain, sock_type | sock_flag, 0) };
    let fd_client = unsafe { OwnedFd::from_raw_fd(fd_client) };
    // connect to the server address
    let rv = unsafe { libc::connect(fd_client.as_raw_fd(), server_addr.as_ptr(), server_addr_len) };
    assert!(rv == 0 || (rv == -1 && test_utils::get_errno() == libc::EINPROGRESS));

    // close the listener
    test_utils::assert_with_errno!(unsafe { libc::close(fd.into_raw_fd()) } == 0);

    // client should detect that the connection was destroyed.

    // For now set the client to non-blocking to facilitate debugging this test
    // under shadow.  Without this, the operations below block indefinitely,
    // making it difficult to e.g. use --summarize to check the behavior across
    // different socket types etc.
    {
        let flags = unsafe { libc::fcntl(fd_client.as_raw_fd(), libc::F_GETFL, 0) };
        test_utils::assert_with_errno!(flags != -1);
        test_utils::assert_with_errno!(
            unsafe {
                libc::fcntl(
                    fd_client.as_raw_fd(),
                    libc::F_SETFL,
                    flags | libc::O_NONBLOCK,
                )
            } == 0
        );
    }

    let mut buf = [0u8; 10];
    test_utils::check_system_call!(
        || unsafe { libc::recv(fd_client.as_raw_fd(), buf.as_mut_ptr().cast(), buf.len(), 0) },
        &[libc::ECONNRESET]
    )?;
    test_utils::check_system_call!(
        || unsafe { libc::send(fd_client.as_raw_fd(), buf.as_ptr().cast(), buf.len(), 0) },
        &[libc::EPIPE]
    )?;

    // close the client
    test_utils::assert_with_errno!(unsafe { libc::close(fd_client.into_raw_fd()) } == 0);

    Ok(())
}

/// Test accept by checking the returned address fields.
fn test_correctness(
    accept_fn: AcceptFn,
    domain: libc::c_int,
    sock_type: libc::c_int,
    sock_flag: libc::c_int,
    accept_flag: libc::c_int,
    bind_client: bool,
) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(domain, sock_type | sock_flag, 0) };
    let fd_server = unsafe { libc::socket(domain, sock_type | sock_flag, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    let (server_addr, server_addr_len) = socket_utils::autobind_helper(fd_server, domain);

    // optionally bind the client
    let client_bind_addr_and_len = if bind_client {
        Some(socket_utils::autobind_helper(fd_client, domain))
    } else {
        None
    };

    if sock_type != libc::SOCK_DGRAM {
        // listen for connections
        let rv = unsafe { libc::listen(fd_server, 10) };
        assert_eq!(rv, 0);
    }

    // connect to the server address
    let rv = unsafe { libc::connect(fd_client, server_addr.as_ptr(), server_addr_len) };

    assert!(rv == 0 || (rv == -1 && test_utils::get_errno() == libc::EINPROGRESS));

    // shadow needs to run events, otherwise the accept call won't know it
    // has an incoming connection (SYN packet)
    // we assume that we're connected after the sleep to avoid depending on
    // select()/poll() and getsockopt()
    let rv = unsafe { libc::usleep(10000) };
    assert_eq!(rv, 0);

    let accept_addr = match domain {
        libc::AF_INET => SockAddr::dummy_init_inet(),
        libc::AF_UNIX => SockAddr::dummy_init_unix(),
        _ => unimplemented!(),
    };

    // accept() may mutate addr and addr_len
    let mut args = AcceptArguments {
        fd: fd_server,
        // fill the sockaddr with dummy data
        addr: Some(accept_addr),
        addr_len: Some(accept_addr.ptr_size()),
        flags: accept_flag,
    };

    let expected_errno = match (domain, sock_type) {
        (libc::AF_INET, libc::SOCK_STREAM) => None,
        (libc::AF_UNIX, libc::SOCK_STREAM | libc::SOCK_SEQPACKET) => None,
        (_, libc::SOCK_DGRAM) => Some(libc::EOPNOTSUPP),
        _ => unimplemented!(),
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || -> Result<(), String> {
        let fd = check_accept_call(&mut args, accept_fn, expected_errno)?;
        if let Some(fd) = fd {
            let rv = unsafe { libc::close(fd) };
            assert_eq!(rv, 0, "Could not close the fd");
        }
        Ok(())
    })?;

    // there was an error and the syscall didn't complete, so skip the later checks
    if expected_errno.is_some() {
        return Ok(());
    }

    // if the client was bound
    if let Some((expected_addr, expected_len)) = client_bind_addr_and_len {
        // check that the returned length is expected
        test_utils::result_assert_eq(
            args.addr_len.unwrap(),
            expected_len,
            "Unexpected addr length",
        )?;

        // the client was bound, so the returned address should be exactly the same as the client's
        // address
        let expected_len = expected_len as usize;
        test_utils::result_assert_eq(
            &args.addr.unwrap().as_slice()[..expected_len],
            &expected_addr.as_slice()[..expected_len],
            "Unexpected address",
        )?;
    } else {
        // check that the returned length is expected
        let expected_addr_len = match domain {
            libc::AF_INET => std::mem::size_of::<libc::sockaddr_in>() as u32,
            libc::AF_UNIX => 2, // domain only
            _ => unimplemented!(),
        };
        test_utils::result_assert_eq(
            args.addr_len.unwrap(),
            expected_addr_len,
            "Unexpected addr length",
        )?;

        // the client was not bound, so we don't know exactly what address to expect
        match domain {
            libc::AF_INET => {
                // check that the returned client address is expected
                test_utils::result_assert_eq(
                    args.addr.unwrap().as_inet().unwrap().sin_family,
                    libc::AF_INET as u16,
                    "Unexpected family",
                )?;
                test_utils::result_assert(
                    args.addr.unwrap().as_inet().unwrap().sin_port != 0u16.to_be(),
                    "Unexpected port",
                )?;
                test_utils::result_assert_eq(
                    args.addr.unwrap().as_inet().unwrap().sin_addr.s_addr,
                    libc::INADDR_LOOPBACK.to_be(),
                    "Unexpected address",
                )?;
                test_utils::result_assert_eq(
                    args.addr.unwrap().as_inet().unwrap().sin_zero,
                    [0; 8],
                    "Unexpected padding",
                )?;
            }
            libc::AF_UNIX => {
                test_utils::result_assert_eq(
                    args.addr.unwrap().as_unix().unwrap().sun_family,
                    libc::AF_UNIX as u16,
                    "Unexpected family",
                )?;
            }
            _ => unimplemented!(),
        }
    }

    Ok(())
}

/// Test accept after the client has connected and closed.
fn test_after_client_closed(
    accept_fn: AcceptFn,
    domain: libc::c_int,
    sock_type: libc::c_int,
    sock_flag: libc::c_int,
    accept_flag: libc::c_int,
) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(domain, sock_type | sock_flag, 0) };
    let fd_server = unsafe { libc::socket(domain, sock_type | sock_flag, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    let (server_addr, server_addr_len) = socket_utils::autobind_helper(fd_server, domain);

    if sock_type != libc::SOCK_DGRAM {
        // listen for connections
        let rv = unsafe { libc::listen(fd_server, 10) };
        assert_eq!(rv, 0);
    }

    // connect to the server address
    let rv = unsafe { libc::connect(fd_client, server_addr.as_ptr(), server_addr_len) };
    assert!(rv == 0 || (rv == -1 && test_utils::get_errno() == libc::EINPROGRESS));

    // shadow needs to run events, otherwise the accept call won't know it
    // has an incoming connection (SYN packet)
    // we assume that we're connected after the sleep to avoid depending on
    // select()/poll() and getsockopt()
    let rv = unsafe { libc::usleep(10000) };
    assert_eq!(rv, 0);

    // data sent before the server-side calls `accept` should be buffered,
    // and readable after the socket is `accept`ed.
    let send_buf = [1u8, 2u8, 3u8, 4u8];
    assert_eq!(
        nix::sys::socket::send(fd_client, &send_buf, nix::sys::socket::MsgFlags::empty()),
        Ok(send_buf.len())
    );

    // close the client socket
    nix::unistd::close(fd_client).unwrap();

    // shadow needs to run events
    let rv = unsafe { libc::usleep(10000) };
    assert_eq!(rv, 0);

    let accept_addr = match domain {
        libc::AF_INET => SockAddr::dummy_init_inet(),
        libc::AF_UNIX => SockAddr::dummy_init_unix(),
        _ => unimplemented!(),
    };

    // accept() may mutate addr and addr_len
    let mut args = AcceptArguments {
        fd: fd_server,
        // fill the sockaddr with dummy data
        addr: Some(accept_addr),
        addr_len: Some(accept_addr.ptr_size()),
        flags: accept_flag,
    };

    let expected_errno = match (domain, sock_type) {
        (libc::AF_INET, libc::SOCK_STREAM) => None,
        (libc::AF_UNIX, libc::SOCK_STREAM | libc::SOCK_SEQPACKET) => None,
        (_, libc::SOCK_DGRAM) => Some(libc::EOPNOTSUPP),
        _ => unimplemented!(),
    };

    test_utils::run_and_close_fds(&[fd_server], || -> Result<(), String> {
        // should still return a child socket
        let fd = check_accept_call(&mut args, accept_fn, expected_errno)?;

        if let Some(fd) = fd {
            // let's test recv() while we're here...
            let mut buf = [0u8; 10];
            // receive the data that was sent before `accept`
            let num_read =
                nix::sys::socket::recv(fd, &mut buf, nix::sys::socket::MsgFlags::empty()).unwrap();
            assert_eq!(&buf[..num_read], send_buf.as_slice());
            // returns EOF
            assert_eq!(
                nix::sys::socket::recv(fd, &mut buf, nix::sys::socket::MsgFlags::empty()),
                Ok(0)
            );

            let rv = unsafe { libc::close(fd) };
            assert_eq!(rv, 0, "Could not close the fd");
        }
        Ok(())
    })?;

    // there was an error and the syscall didn't complete, so skip the later checks
    if expected_errno.is_some() {
        return Ok(());
    }

    // check that the returned length is expected
    let expected_addr_len = match domain {
        libc::AF_INET => std::mem::size_of::<libc::sockaddr_in>() as u32,
        libc::AF_UNIX => 2, // domain only
        _ => unimplemented!(),
    };
    test_utils::result_assert_eq(
        args.addr_len.unwrap(),
        expected_addr_len,
        "Unexpected addr length",
    )?;

    // the client was not bound, so we don't know exactly what address to expect
    match domain {
        libc::AF_INET => {
            // check that the returned client address is expected
            test_utils::result_assert_eq(
                args.addr.unwrap().as_inet().unwrap().sin_family,
                libc::AF_INET as u16,
                "Unexpected family",
            )?;
            test_utils::result_assert(
                args.addr.unwrap().as_inet().unwrap().sin_port != 0u16.to_be(),
                "Unexpected port",
            )?;
            test_utils::result_assert_eq(
                args.addr.unwrap().as_inet().unwrap().sin_addr.s_addr,
                libc::INADDR_LOOPBACK.to_be(),
                "Unexpected address",
            )?;
            test_utils::result_assert_eq(
                args.addr.unwrap().as_inet().unwrap().sin_zero,
                [0; 8],
                "Unexpected padding",
            )?;
        }
        libc::AF_UNIX => {
            test_utils::result_assert_eq(
                args.addr.unwrap().as_unix().unwrap().sun_family,
                libc::AF_UNIX as u16,
                "Unexpected family",
            )?;
        }
        _ => unimplemented!(),
    }

    Ok(())
}

fn check_accept_call(
    args: &mut AcceptArguments,
    accept_fn: AcceptFn,
    expected_errno: Option<libc::c_int>,
) -> Result<Option<libc::c_int>, String> {
    // get a pointer to the sockaddr and the size of the structure
    // careful use of references here makes sure we don't copy memory, leading to stale pointers
    let (addr_ptr, addr_max_len) = match args.addr {
        Some(ref mut x) => (x.as_mut_ptr(), x.ptr_size()),
        None => (std::ptr::null_mut(), 0),
    };

    // if the pointer is non-null, make sure the provided size is not greater than the actual data
    // size so that we don't segfault
    if args.addr.is_some() && args.addr_len.is_some() {
        assert!(args.addr_len.unwrap() <= addr_max_len);
    }

    let rv = match accept_fn {
        AcceptFn::Accept => unsafe {
            assert_eq!(args.flags, 0);
            libc::accept(args.fd, addr_ptr, args.addr_len.as_mut_ptr())
        },
        AcceptFn::Accept4 => unsafe {
            libc::accept4(args.fd, addr_ptr, args.addr_len.as_mut_ptr(), args.flags)
        },
    };

    let errno = test_utils::get_errno();

    let fd = match expected_errno {
        // if we expect the accept() call to return an error (rv should be -1)
        Some(expected_errno) => {
            if rv != -1 {
                return Err(format!("Expecting a return value of -1, received {rv}"));
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
            None
        }
        // if no error is expected (rv should be non-negative)
        None => {
            if rv < 0 {
                return Err(format!(
                    "Expecting a non-negative return value, received {} \"{}\"",
                    rv,
                    test_utils::get_errno_message(errno)
                ));
            }
            Some(rv)
        }
    };

    Ok(fd)
}
