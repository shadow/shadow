/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use nix::poll::PollFlags;
use nix::sys::socket::sockopt;
use test_utils::TestEnvironment as TestEnv;
use test_utils::set;
use test_utils::socket_utils;
use test_utils::socket_utils::SockAddr;

struct ListenArguments {
    fd: libc::c_int,
    backlog: libc::c_int,
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

    // tests to repeat for different socket options
    for &domain in [libc::AF_INET, libc::AF_UNIX].iter() {
        // optionally bind to an address before listening
        let bind_addresses = match domain {
            libc::AF_INET => vec![
                None,
                Some(SockAddr::Inet(libc::sockaddr_in {
                    sin_family: libc::AF_INET as u16,
                    sin_port: 0u16.to_be(),
                    sin_addr: libc::in_addr {
                        s_addr: libc::INADDR_LOOPBACK.to_be(),
                    },
                    sin_zero: [1; 8],
                })),
                Some(SockAddr::Inet(libc::sockaddr_in {
                    sin_family: libc::AF_INET as u16,
                    sin_port: 0u16.to_be(),
                    sin_addr: libc::in_addr {
                        s_addr: libc::INADDR_ANY.to_be(),
                    },
                    sin_zero: [1; 8],
                })),
            ],
            libc::AF_UNIX => vec![
                None,
                Some(SockAddr::Unix(libc::sockaddr_un {
                    sun_family: libc::AF_UNIX as u16,
                    sun_path: [0i8; 108],
                })),
            ],
            _ => unimplemented!(),
        };

        for &sock_type in [libc::SOCK_STREAM, libc::SOCK_DGRAM, libc::SOCK_SEQPACKET].iter() {
            // skip tests that use SOCK_SEQPACKET with INET sockets
            if domain == libc::AF_INET && sock_type == libc::SOCK_SEQPACKET {
                continue;
            }

            for &flag in [0, libc::SOCK_NONBLOCK, libc::SOCK_CLOEXEC].iter() {
                for &bind in bind_addresses.iter() {
                    // add details to the test names to avoid duplicates
                    let append_args = |s| {
                        format!("{s} <domain={domain},type={sock_type},flag={flag},bind={bind:?}>")
                    };

                    tests.extend(vec![
                        test_utils::ShadowTest::new(
                            &append_args("test_zero_backlog"),
                            move || test_zero_backlog(domain, sock_type, flag, bind),
                            set![TestEnv::Libc, TestEnv::Shadow],
                        ),
                        test_utils::ShadowTest::new(
                            &append_args("test_negative_backlog"),
                            move || test_negative_backlog(domain, sock_type, flag, bind),
                            set![TestEnv::Libc, TestEnv::Shadow],
                        ),
                        test_utils::ShadowTest::new(
                            &append_args("test_large_backlog"),
                            move || test_large_backlog(domain, sock_type, flag, bind),
                            set![TestEnv::Libc, TestEnv::Shadow],
                        ),
                        test_utils::ShadowTest::new(
                            &append_args("test_listen_twice"),
                            move || test_listen_twice(domain, sock_type, flag, bind),
                            set![TestEnv::Libc, TestEnv::Shadow],
                        ),
                        test_utils::ShadowTest::new(
                            &append_args("test_after_close"),
                            move || test_after_close(domain, sock_type, flag, bind),
                            set![TestEnv::Libc, TestEnv::Shadow],
                        ),
                    ]);
                }
            }

            // add details to the test names to avoid duplicates
            let append_args = |s| format!("{s} <domain={domain},type={sock_type}>");

            tests.extend(vec![test_utils::ShadowTest::new(
                &append_args("test_negative_backlog_connect"),
                move || test_negative_backlog_connect(domain, sock_type),
                set![TestEnv::Libc, TestEnv::Shadow],
            )]);
        }
    }

    for &domain in [libc::AF_INET, libc::AF_UNIX].iter() {
        for &sock_type in [libc::SOCK_STREAM, libc::SOCK_SEQPACKET].iter() {
            // add details to the test names to avoid duplicates
            let append_args = |s| format!("{s} <domain={domain},type={sock_type}>");

            // skip tests that use SOCK_SEQPACKET with INET sockets
            if domain == libc::AF_INET && sock_type == libc::SOCK_SEQPACKET {
                continue;
            }

            for &flag in [0, libc::SOCK_NONBLOCK, libc::SOCK_CLOEXEC].iter() {
                // add details to the test names to avoid duplicates
                let append_args = |s| format!("{s} <domain={domain},type={sock_type},flag={flag}>");

                tests.extend(vec![
                    test_utils::ShadowTest::new(
                        &append_args("test_listening_not_readable"),
                        move || test_listening_not_readable(domain, sock_type, flag),
                        set![TestEnv::Libc, TestEnv::Shadow],
                    ),
                    test_utils::ShadowTest::new(
                        &append_args("test_listening_not_writable"),
                        move || test_listening_not_writable(domain, sock_type, flag),
                        set![TestEnv::Libc, TestEnv::Shadow],
                    ),
                ]);
            }

            tests.extend(vec![
                test_utils::ShadowTest::new(
                    &append_args("test_backlog_size"),
                    move || test_backlog_size(domain, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_reduced_backlog"),
                    move || test_reduced_backlog(domain, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
            ]);
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

    let args = ListenArguments { fd, backlog: 0 };

    test_utils::run_and_close_fds(&[fd], || check_listen_call(&args, Some(libc::EOPNOTSUPP)))
}

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

/// Test listen using a backlog of -1.
fn test_negative_backlog(
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

    let args = ListenArguments { fd, backlog: -1 };

    let expected_errno = match (domain, sock_type, bind) {
        (libc::AF_INET, libc::SOCK_STREAM, _) => None,
        (libc::AF_UNIX, libc::SOCK_STREAM | libc::SOCK_SEQPACKET, Some(_)) => None,
        (libc::AF_UNIX, libc::SOCK_STREAM | libc::SOCK_SEQPACKET, None) => Some(libc::EINVAL),
        (_, libc::SOCK_DGRAM, _) => Some(libc::EOPNOTSUPP),
        _ => unimplemented!(),
    };

    test_utils::run_and_close_fds(&[fd], || check_listen_call(&args, expected_errno))
}

/// Test connecting to a listening socket with a backlog of -1.
fn test_negative_backlog_connect(
    domain: libc::c_int,
    sock_type: libc::c_int,
) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type, 0) };
    assert!(fd >= 0);

    let (bind_address, bind_len) = socket_utils::autobind_helper(fd, domain);

    let args = ListenArguments { fd, backlog: -1 };

    let expected_errno = match (domain, sock_type) {
        (libc::AF_INET, libc::SOCK_STREAM) => None,
        (libc::AF_UNIX, libc::SOCK_STREAM | libc::SOCK_SEQPACKET) => None,
        (_, libc::SOCK_DGRAM) => Some(libc::EOPNOTSUPP),
        _ => unimplemented!(),
    };

    test_utils::run_and_close_fds(&[fd], || {
        check_listen_call(&args, expected_errno)?;

        if expected_errno.is_some() {
            return Ok(());
        }

        let num_clients = 10;

        let client_fds: Vec<_> =
            std::iter::repeat_with(|| unsafe { libc::socket(domain, sock_type, 0) })
                .take(num_clients)
                // make sure the fds are valid
                .map(|x| (x >= 0).then_some(x))
                .collect::<Option<_>>()
                .unwrap();

        // check that all clients connect successfully
        for client_fd in &client_fds {
            let rv = unsafe { libc::connect(*client_fd, bind_address.as_ptr(), bind_len) };
            assert_eq!(rv, 0);
        }

        for client_fd in &client_fds {
            nix::unistd::close(*client_fd).unwrap();
        }

        Ok(())
    })
}

/// Test listen using a backlog of INT_MAX.
fn test_large_backlog(
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

    let args = ListenArguments {
        fd,
        backlog: libc::INT_MAX,
    };

    let expected_errno = match (domain, sock_type, bind) {
        (libc::AF_INET, libc::SOCK_STREAM, _) => None,
        (libc::AF_UNIX, libc::SOCK_STREAM | libc::SOCK_SEQPACKET, Some(_)) => None,
        (libc::AF_UNIX, libc::SOCK_STREAM | libc::SOCK_SEQPACKET, None) => Some(libc::EINVAL),
        (_, libc::SOCK_DGRAM, _) => Some(libc::EOPNOTSUPP),
        _ => unimplemented!(),
    };

    test_utils::run_and_close_fds(&[fd], || check_listen_call(&args, expected_errno))
}

/// Test calling listen twice for the same socket.
fn test_listen_twice(
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

    let args1 = ListenArguments { fd, backlog: 10 };

    let args2 = ListenArguments { fd, backlog: 0 };

    let expected_errno = match (domain, sock_type, bind) {
        (libc::AF_INET, libc::SOCK_STREAM, _) => None,
        (libc::AF_UNIX, libc::SOCK_STREAM | libc::SOCK_SEQPACKET, Some(_)) => None,
        (libc::AF_UNIX, libc::SOCK_STREAM | libc::SOCK_SEQPACKET, None) => Some(libc::EINVAL),
        (_, libc::SOCK_DGRAM, _) => Some(libc::EOPNOTSUPP),
        _ => unimplemented!(),
    };

    test_utils::run_and_close_fds(&[fd], || {
        check_listen_call(&args1, expected_errno)?;
        check_listen_call(&args2, expected_errno)
    })
}

/// Test listen after closing the socket.
fn test_after_close(
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

    // close the file descriptor
    let rv = unsafe { libc::close(fd) };
    assert_eq!(rv, 0);

    let args = ListenArguments { fd, backlog: 100 };

    check_listen_call(&args, Some(libc::EBADF))
}

/// Test that a listening socket is not readable.
fn test_listening_not_readable(
    domain: libc::c_int,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type | flag, 0) };
    assert!(fd >= 0);

    socket_utils::autobind_helper(fd, domain);

    let args = ListenArguments { fd, backlog: 5 };

    test_utils::run_and_close_fds(&[fd], || {
        check_listen_call(&args, None)?;

        let mut poll_fds = [nix::poll::PollFd::new(fd, PollFlags::POLLIN)];
        let count = nix::poll::poll(&mut poll_fds, 50).unwrap();

        // should not be readable
        assert_eq!(count, 0);

        Ok(())
    })
}

/// Test that a listening socket is not writable.
fn test_listening_not_writable(
    domain: libc::c_int,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type | flag, 0) };
    assert!(fd >= 0);

    socket_utils::autobind_helper(fd, domain);

    let args = ListenArguments { fd, backlog: 5 };

    test_utils::run_and_close_fds(&[fd], || {
        check_listen_call(&args, None)?;

        let mut poll_fds = [nix::poll::PollFd::new(fd, PollFlags::POLLOUT)];
        let count = nix::poll::poll(&mut poll_fds, 50).unwrap();

        // should not be writable
        assert_eq!(count, 0);

        Ok(())
    })
}

/// Test connecting and accepting sockets with different listen() backlog values.
fn test_backlog_size(domain: libc::c_int, sock_type: libc::c_int) -> Result<(), String> {
    // try different backlog values
    for backlog in &[0, 1, 8, 16, 17, 18, 19, 30] {
        let server_fd = unsafe { libc::socket(domain, sock_type, 0) };
        assert!(server_fd >= 0);

        // bind the server socket
        let (addr, addr_len) = test_utils::socket_utils::autobind_helper(server_fd, domain);

        // initialize the server socket
        let rv = unsafe { libc::listen(server_fd, *backlog) };
        assert_eq!(rv, 0);

        // get enough sockets to fill the accept queue
        let client_fds: Vec<_> =
            std::iter::repeat_with(|| unsafe { libc::socket(domain, sock_type, 0) })
                // linux will support backlog+1 incoming connections
                .take(*backlog as usize + 1)
                // make sure the fds are valid
                .map(|x| (x >= 0).then_some(x))
                .collect::<Option<_>>()
                .unwrap();

        // connect all of the clients to the server
        for client_fd in &client_fds {
            // a blocking connect
            let rv = unsafe { libc::connect(*client_fd, addr.as_ptr(), addr_len) };
            assert_eq!(rv, 0);
        }

        // get one additional socket that should fail to connect
        let client_fd_extra = unsafe { libc::socket(domain, sock_type | libc::SOCK_NONBLOCK, 0) };
        assert!(client_fd_extra >= 0);

        // a non-blocking connect; should always return an error
        match domain {
            libc::AF_INET => {
                // should always return EINPROGRESS, even if there was room in the accept queue
                let rv = unsafe { libc::connect(client_fd_extra, addr.as_ptr(), addr_len) };
                assert_eq!(rv, -1);
                assert_eq!(test_utils::get_errno(), libc::EINPROGRESS);

                // wait for the connection to complete; should timeout
                let mut poll_fds = [nix::poll::PollFd::new(client_fd_extra, PollFlags::POLLOUT)];
                let count = nix::poll::poll(&mut poll_fds, 100).unwrap();
                assert_eq!(count, 0);

                // wait for the connection to complete; should timeout again
                let mut poll_fds = [nix::poll::PollFd::new(client_fd_extra, PollFlags::POLLOUT)];
                let count = nix::poll::poll(&mut poll_fds, 100).unwrap();
                assert_eq!(count, 0);
            }
            libc::AF_UNIX => {
                // unix sockets will return EAGAIN; they don't continue trying to connect like INET
                // sockets do
                let rv = unsafe { libc::connect(client_fd_extra, addr.as_ptr(), addr_len) };
                assert_eq!(rv, -1);
                assert_eq!(test_utils::get_errno(), libc::EAGAIN);
            }
            _ => unimplemented!(),
        }

        // accept a connection on the server to free up a space in the accept queue
        let accepted_fd =
            unsafe { libc::accept(server_fd, std::ptr::null_mut(), std::ptr::null_mut()) };
        assert!(accepted_fd >= 0);

        // the connection should now be successful
        match domain {
            libc::AF_INET => {
                // wait for the existing connection to complete; should complete successfully
                let mut poll_fds = [nix::poll::PollFd::new(client_fd_extra, PollFlags::POLLOUT)];
                let count = nix::poll::poll(&mut poll_fds, 2000).unwrap();
                assert_eq!(count, 1);
                assert!(poll_fds[0].revents().unwrap().contains(PollFlags::POLLOUT));
            }
            libc::AF_UNIX => {
                // try connecting again; should complete successfully
                let rv = unsafe { libc::connect(client_fd_extra, addr.as_ptr(), addr_len) };
                assert_eq!(rv, 0);
            }
            _ => unimplemented!(),
        }

        // check that there was no socket error
        // connect(2):
        // > After select(2) indicates writability, use getsockopt(2) to read the SO_ERROR
        // > option at level SOL_SOCKET to determine whether connect() completed successfully
        // > (SO_ERROR is zero) or unsuccessfully (SO_ERROR is one of the usual error codes
        // > listed here, explaining the reason for the failure)

        // TODO: always run once shadow supports getsockopt() for unix sockets
        if !test_utils::running_in_shadow() || domain != libc::AF_UNIX {
            let error =
                nix::sys::socket::getsockopt(client_fd_extra, sockopt::SocketError).unwrap();
            assert_eq!(error, 0);
        }

        // close all of the sockets
        for client_fd in &client_fds {
            nix::unistd::close(*client_fd).unwrap();
        }
        nix::unistd::close(client_fd_extra).unwrap();
        nix::unistd::close(accepted_fd).unwrap();
        nix::unistd::close(server_fd).unwrap();
    }

    Ok(())
}

/// Test connecting and accepting sockets after the listen() backlog has been lowered.
fn test_reduced_backlog(domain: libc::c_int, sock_type: libc::c_int) -> Result<(), String> {
    let server_fd = unsafe { libc::socket(domain, sock_type, 0) };
    assert!(server_fd >= 0);

    const INITIAL_BACKLOG: usize = 10;
    const NUM_CLIENTS: usize = 5;
    const NEW_BACKLOG: usize = 2;

    // the new backlog should be lower than the number of clients we plan to connect;
    // a listening socket with a backlog 'x' can queue 'x+1' sockets
    #[allow(clippy::assertions_on_constants)]
    {
        assert!(NEW_BACKLOG + 1 < NUM_CLIENTS);
    }

    // bind the server socket
    let (addr, addr_len) = test_utils::socket_utils::autobind_helper(server_fd, domain);

    // initialize the server socket
    let rv = unsafe { libc::listen(server_fd, INITIAL_BACKLOG as i32) };
    assert_eq!(rv, 0);

    // get enough sockets to partially fill the accept queue
    let client_fds: Vec<_> =
        std::iter::repeat_with(|| unsafe { libc::socket(domain, sock_type, 0) })
            // linux will support backlog+1 incoming connections
            .take(NUM_CLIENTS)
            // make sure the fds are valid
            .map(|x| (x >= 0).then_some(x))
            .collect::<Option<_>>()
            .unwrap();

    // connect all of the clients to the server
    for client_fd in &client_fds {
        // a blocking connect
        let rv = unsafe { libc::connect(*client_fd, addr.as_ptr(), addr_len) };
        assert_eq!(rv, 0);
    }

    // reduce the backlog
    let rv = unsafe { libc::listen(server_fd, NEW_BACKLOG as i32) };
    assert_eq!(rv, 0);

    // get one additional socket that should fail to connect
    let client_fd_extra = unsafe { libc::socket(domain, sock_type | libc::SOCK_NONBLOCK, 0) };
    assert!(client_fd_extra >= 0);

    // try to connect; should fail
    match domain {
        libc::AF_INET => {
            // should always return EINPROGRESS, even if there was room in the accept queue
            let rv = unsafe { libc::connect(client_fd_extra, addr.as_ptr(), addr_len) };
            assert_eq!(rv, -1);
            assert_eq!(test_utils::get_errno(), libc::EINPROGRESS);

            // wait for the connection to complete; should timeout since the accept queue is full
            let mut poll_fds = [nix::poll::PollFd::new(client_fd_extra, PollFlags::POLLOUT)];
            let count = nix::poll::poll(&mut poll_fds, 100).unwrap();
            assert_eq!(count, 0);
        }
        libc::AF_UNIX => {
            // unix sockets will return EAGAIN; they don't continue trying to connect like INET
            // sockets do
            let rv = unsafe { libc::connect(client_fd_extra, addr.as_ptr(), addr_len) };
            assert_eq!(rv, -1);
            assert_eq!(test_utils::get_errno(), libc::EAGAIN);
        }
        _ => unimplemented!(),
    }

    let num_clients_to_accept = match domain {
        // should be able to accept the original clients and the extra client
        libc::AF_INET => NUM_CLIENTS + 1,
        // should be able to accept the original clients (the extra client is not still connecting)
        libc::AF_UNIX => NUM_CLIENTS,
        _ => unimplemented!(),
    };

    // accept clients
    for _ in 0..num_clients_to_accept {
        let accepted_fd =
            unsafe { libc::accept(server_fd, std::ptr::null_mut(), std::ptr::null_mut()) };
        assert!(accepted_fd >= 0);
        nix::unistd::close(accepted_fd).unwrap();
    }

    // the unix socket never connected, so try again; should be successful
    if domain == libc::AF_UNIX {
        let rv = unsafe { libc::connect(client_fd_extra, addr.as_ptr(), addr_len) };
        assert_eq!(rv, 0);

        // can accept this connection
        let accepted_fd =
            unsafe { libc::accept(server_fd, std::ptr::null_mut(), std::ptr::null_mut()) };
        assert!(accepted_fd >= 0);
        nix::unistd::close(accepted_fd).unwrap();
    }

    // close all of the sockets
    for client_fd in &client_fds {
        nix::unistd::close(*client_fd).unwrap();
    }
    nix::unistd::close(client_fd_extra).unwrap();
    nix::unistd::close(server_fd).unwrap();

    Ok(())
}

/// Bind the fd to the address.
fn bind_fd(fd: libc::c_int, bind: SockAddr) {
    let (addr, addr_len) = (bind.as_ptr(), bind.ptr_size());
    let rv = unsafe { libc::bind(fd, addr, addr_len) };
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
