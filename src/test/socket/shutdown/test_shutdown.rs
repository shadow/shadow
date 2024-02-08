/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use test_utils::set;
use test_utils::TestEnvironment as TestEnv;

struct ShutdownArguments {
    fd: libc::c_int,
    how: libc::c_int,
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
            "test_invalid_how",
            test_invalid_how,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
    ];

    let domains = [libc::AF_INET];
    let sock_types = [libc::SOCK_STREAM, libc::SOCK_DGRAM];
    let flags = [0, libc::SOCK_NONBLOCK, libc::SOCK_CLOEXEC];
    let hows = [libc::SHUT_RD, libc::SHUT_WR, libc::SHUT_RDWR];

    for &domain in domains.iter() {
        for &sock_type in sock_types.iter() {
            for &flag in flags.iter() {
                for &how in hows.iter() {
                    // add details to the test names to avoid duplicates
                    let append_args = |s| {
                        format!(
                            "{} <domain={},type={},flag={},how={}>",
                            s, domain, sock_type, flag, how
                        )
                    };

                    let more_tests: Vec<test_utils::ShadowTest<_, _>> = vec![
                        test_utils::ShadowTest::new(
                            &append_args("test_not_connected"),
                            move || test_not_connected(domain, sock_type, flag, how),
                            set![TestEnv::Libc, TestEnv::Shadow],
                        ),
                        test_utils::ShadowTest::new(
                            &append_args("test_arguments"),
                            move || test_arguments(domain, sock_type, flag, how),
                            set![TestEnv::Libc, TestEnv::Shadow],
                        ),
                        test_utils::ShadowTest::new(
                            &append_args("test_twice"),
                            move || test_twice(domain, sock_type, flag, how),
                            set![TestEnv::Libc, TestEnv::Shadow],
                        ),
                        test_utils::ShadowTest::new(
                            &append_args("test_after_close"),
                            move || test_after_close(domain, sock_type, flag, how),
                            set![TestEnv::Libc, TestEnv::Shadow],
                        ),
                        test_utils::ShadowTest::new(
                            &append_args("test_read_after_client_shutdown"),
                            move || test_read_after_client_shutdown(domain, sock_type, flag, how),
                            // TODO: this passes for rust tcp sockets but not C tcp sockets, so
                            // enable this later
                            if domain != libc::AF_INET || sock_type != libc::SOCK_STREAM {
                                set![TestEnv::Libc, TestEnv::Shadow]
                            } else {
                                set![TestEnv::Libc]
                            },
                        ),
                        test_utils::ShadowTest::new(
                            &append_args("test_read_after_peer_shutdown"),
                            move || test_read_after_peer_shutdown(domain, sock_type, flag, how),
                            set![TestEnv::Libc, TestEnv::Shadow],
                        ),
                        test_utils::ShadowTest::new(
                            &append_args("test_conn_reset"),
                            move || test_conn_reset(domain, sock_type, flag, how),
                            if domain != libc::AF_INET || sock_type != libc::SOCK_STREAM {
                                set![TestEnv::Libc, TestEnv::Shadow]
                            } else {
                                set![TestEnv::Libc]
                            },
                        ),
                        test_utils::ShadowTest::new(
                            &append_args("test_write_after_client_shutdown"),
                            move || test_write_after_client_shutdown(domain, sock_type, flag, how),
                            set![TestEnv::Libc, TestEnv::Shadow],
                        ),
                        test_utils::ShadowTest::new(
                            &append_args("test_write_after_peer_shutdown"),
                            move || test_write_after_peer_shutdown(domain, sock_type, flag, how),
                            if domain != libc::AF_INET || sock_type != libc::SOCK_STREAM {
                                set![TestEnv::Libc, TestEnv::Shadow]
                            } else {
                                set![TestEnv::Libc]
                            },
                        ),
                    ];

                    tests.extend(more_tests);
                }
            }
        }
    }

    for &domain in domains.iter() {
        for &flag in flags.iter() {
            // add details to the test names to avoid duplicates
            let append_args = |s| format!("{} <domain={},flag={}>", s, domain, flag);

            tests.extend(vec![test_utils::ShadowTest::new(
                &append_args("test_tcp_fin_correctness"),
                move || test_tcp_fin_correctness(domain, flag),
                set![TestEnv::Libc, TestEnv::Shadow],
            )])
        }
    }

    tests
}

/// Test shutdown() using an argument that cannot be a fd.
fn test_invalid_fd() -> Result<(), String> {
    let args = ShutdownArguments {
        fd: -1,
        how: libc::SHUT_RDWR,
    };

    check_shutdown_call(&args, &[libc::EBADF])
}

/// Test shutdown() using an argument that could be a fd, but is not.
fn test_non_existent_fd() -> Result<(), String> {
    let args = ShutdownArguments {
        fd: 8934,
        how: libc::SHUT_RDWR,
    };

    check_shutdown_call(&args, &[libc::EBADF])
}

/// Test shutdown() using a valid fd that is not a socket.
fn test_non_socket_fd() -> Result<(), String> {
    let args = ShutdownArguments {
        fd: 0, // assume the fd 0 is already open and is not a socket
        how: libc::SHUT_RDWR,
    };

    check_shutdown_call(&args, &[libc::ENOTSOCK])
}

/// Test shutdown() using a valid socket, but invalid `how` argument.
fn test_invalid_how() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
    assert!(fd >= 0);

    let args = ShutdownArguments { fd, how: 88 };

    test_utils::run_and_close_fds(&[fd], || check_shutdown_call(&args, &[libc::EINVAL]))
}

/// Test shutdown() using a non-connected socket.
fn test_not_connected(
    domain: libc::c_int,
    sock_type: libc::c_int,
    flag: libc::c_int,
    how: libc::c_int,
) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type | flag, 0) };
    assert!(fd >= 0);

    let args = ShutdownArguments { fd, how };

    test_utils::run_and_close_fds(&[fd], || check_shutdown_call(&args, &[libc::ENOTCONN]))
}

/// Generate a pair of connected TCP sockets.
fn setup_stream_sockets(domain: libc::c_int, flag: libc::c_int) -> (libc::c_int, libc::c_int) {
    let fd_client = unsafe { libc::socket(domain, libc::SOCK_STREAM | flag, 0) };
    let fd_listener = unsafe { libc::socket(domain, libc::SOCK_STREAM | flag, 0) };
    assert!(fd_client >= 0);
    assert!(fd_listener >= 0);

    assert_eq!(unsafe { libc::listen(fd_listener, 10) }, 0);

    // get the listener address
    let addr = {
        let mut addr: libc::sockaddr_in = unsafe { std::mem::zeroed() };
        let mut addr_len = std::mem::size_of_val(&addr) as u32;
        let rv = unsafe {
            libc::getsockname(
                fd_listener,
                std::ptr::from_mut(&mut addr) as *mut libc::sockaddr,
                &mut addr_len,
            )
        };
        assert_eq!(rv, 0);
        assert_eq!(addr_len as usize, std::mem::size_of_val(&addr));
        addr
    };

    // connect the client socket to the listener address
    {
        let rv = unsafe {
            libc::connect(
                fd_client,
                std::ptr::from_ref(&addr) as *const libc::sockaddr,
                std::mem::size_of_val(&addr) as u32,
            )
        };
        assert!(rv == 0 || (rv == -1 && test_utils::get_errno() == libc::EINPROGRESS));
    }

    // shadow needs to run events, otherwise the accept call won't know it
    // has an incoming connection (SYN packet)
    assert_eq!(unsafe { libc::usleep(10000) }, 0);

    // accept the new connection
    let fd_server =
        unsafe { libc::accept(fd_listener, std::ptr::null_mut(), std::ptr::null_mut()) };

    // close the listening socket
    assert_eq!(unsafe { libc::close(fd_listener) }, 0);

    (fd_client, fd_server)
}

/// Generate a pair of connected UDP sockets.
fn setup_dgram_sockets(domain: libc::c_int, flag: libc::c_int) -> (libc::c_int, libc::c_int) {
    let fd_client = unsafe { libc::socket(domain, libc::SOCK_DGRAM | flag, 0) };
    let fd_server = unsafe { libc::socket(domain, libc::SOCK_DGRAM | flag, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    // bind the server socket to an address (but unspecified port)
    {
        let addr = libc::sockaddr_in {
            sin_family: libc::AF_INET as u16,
            sin_port: 0u16.to_be(),
            sin_addr: libc::in_addr {
                s_addr: libc::INADDR_LOOPBACK.to_be(),
            },
            sin_zero: [0; 8],
        };
        let rv = unsafe {
            libc::bind(
                fd_server,
                std::ptr::from_ref(&addr) as *const libc::sockaddr,
                std::mem::size_of_val(&addr) as u32,
            )
        };
        assert_eq!(rv, 0);
    }

    // get the server socket's full address
    let server_addr = {
        let mut addr: libc::sockaddr_in = unsafe { std::mem::zeroed() };
        let mut addr_len = std::mem::size_of_val(&addr) as u32;
        let rv = unsafe {
            libc::getsockname(
                fd_server,
                std::ptr::from_mut(&mut addr) as *mut libc::sockaddr,
                &mut addr_len,
            )
        };
        assert_eq!(rv, 0);
        assert_eq!(addr_len as usize, std::mem::size_of_val(&addr));
        addr
    };

    // connect the client socket to the server address
    {
        let rv = unsafe {
            libc::connect(
                fd_client,
                std::ptr::from_ref(&server_addr) as *const libc::sockaddr,
                std::mem::size_of_val(&server_addr) as u32,
            )
        };
        assert!(rv == 0 || (rv == -1 && test_utils::get_errno() == libc::EINPROGRESS));
    }

    // get the client socket's full address
    let client_addr = {
        let mut addr: libc::sockaddr_in = unsafe { std::mem::zeroed() };
        let mut addr_len = std::mem::size_of_val(&addr) as u32;
        let rv = unsafe {
            libc::getsockname(
                fd_client,
                std::ptr::from_mut(&mut addr) as *mut libc::sockaddr,
                &mut addr_len,
            )
        };
        assert_eq!(rv, 0);
        assert_eq!(addr_len as usize, std::mem::size_of_val(&addr));
        addr
    };

    // connect the server socket to the client address
    {
        let rv = unsafe {
            libc::connect(
                fd_server,
                std::ptr::from_ref(&client_addr) as *const libc::sockaddr,
                std::mem::size_of_val(&client_addr) as u32,
            )
        };
        assert!(rv == 0 || (rv == -1 && test_utils::get_errno() == libc::EINPROGRESS));
    }

    (fd_client, fd_server)
}

/// Test shutdown() with different arguments, making sure that the return value is expected.
fn test_arguments(
    domain: libc::c_int,
    sock_type: libc::c_int,
    flag: libc::c_int,
    how: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_server) = if sock_type == libc::SOCK_STREAM {
        setup_stream_sockets(domain, flag)
    } else if sock_type == libc::SOCK_DGRAM {
        setup_dgram_sockets(domain, flag)
    } else {
        unreachable!("Unhandled socket type: {}", sock_type);
    };

    let args = ShutdownArguments { fd: fd_client, how };
    test_utils::run_and_close_fds(&[fd_client, fd_server], || check_shutdown_call(&args, &[]))
}

/// Test calling shutdown() twice with the same arguments.
fn test_twice(
    domain: libc::c_int,
    sock_type: libc::c_int,
    flag: libc::c_int,
    how: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_server) = if sock_type == libc::SOCK_STREAM {
        setup_stream_sockets(domain, flag)
    } else if sock_type == libc::SOCK_DGRAM {
        setup_dgram_sockets(domain, flag)
    } else {
        unreachable!("Unhandled socket type: {}", sock_type);
    };

    let args = ShutdownArguments { fd: fd_client, how };
    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        check_shutdown_call(&args, &[])?;
        check_shutdown_call(&args, &[])?;
        Ok(())
    })
}

/// Test shutdown() after closing the socket.
fn test_after_close(
    domain: libc::c_int,
    sock_type: libc::c_int,
    flag: libc::c_int,
    how: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_server) = if sock_type == libc::SOCK_STREAM {
        setup_stream_sockets(domain, flag)
    } else if sock_type == libc::SOCK_DGRAM {
        setup_dgram_sockets(domain, flag)
    } else {
        unreachable!("Unhandled socket type: {}", sock_type);
    };

    assert_eq!(unsafe { libc::close(fd_client) }, 0);

    let args = ShutdownArguments { fd: fd_client, how };
    test_utils::run_and_close_fds(&[fd_server], || check_shutdown_call(&args, &[libc::EBADF]))
}

/// A wrapper for libc::write() which uses a slice.
fn write_once(fd: libc::c_int, buf: &[u8]) -> libc::ssize_t {
    unsafe { libc::write(fd, buf.as_ptr() as *const libc::c_void, buf.len()) }
}

/// A wrapper for libc::read() which uses a slice.
fn read_once(fd: libc::c_int, buf: &mut [u8]) -> libc::ssize_t {
    unsafe { libc::read(fd, buf.as_mut_ptr() as *mut libc::c_void, buf.len()) }
}

/// Write all bytes in the buffer.
fn write_all(fd: libc::c_int, buf: &[u8]) {
    let mut bytes_written = 0;

    loop {
        if bytes_written == buf.len() {
            break;
        }
        let num = write_once(fd, &buf[bytes_written..]);
        assert!(num > 0);
        bytes_written += num as usize;
    }
}

/// Read from the fd until an EOF.
fn read_all(fd: libc::c_int) -> Vec<u8> {
    let mut buf = vec![];

    loop {
        let mut tmp_buf = [0u8; 1024];
        let num = read_once(fd, &mut tmp_buf);
        if num == 0 {
            break;
        }

        // might get EAGAIN if the peer is sending more data than fits in the receive window
        if num == -1 && test_utils::get_errno() == libc::EAGAIN {
            // need to wait for more data
            assert_eq!(unsafe { libc::usleep(10000) }, 0);
            continue;
        }

        assert!(num > 0);
        buf.extend_from_slice(&tmp_buf[..(num as usize)]);
    }

    buf
}

/// Test reading from the socket after shutdown().
fn test_read_after_client_shutdown(
    domain: libc::c_int,
    sock_type: libc::c_int,
    flag: libc::c_int,
    how: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_server) = if sock_type == libc::SOCK_STREAM {
        setup_stream_sockets(domain, flag)
    } else if sock_type == libc::SOCK_DGRAM {
        setup_dgram_sockets(domain, flag)
    } else {
        unreachable!("Unhandled socket type: {}", sock_type);
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // some bytes/messages to write from the server
        const MESSAGE_SIZE: usize = 5;
        const NUM_WRITES: i32 = 4;

        let message: [u8; MESSAGE_SIZE] = [1, 2, 3, 4, 5];

        // write one message at a time from the server
        for _ in 0..NUM_WRITES {
            write_all(fd_server, &message);
        }

        // shadow needs to run events, letting the data arrive before the shutdown()
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // run shutdown() on the client's socket
        check_shutdown_call(&ShutdownArguments { fd: fd_client, how }, &[])?;

        // read one message at a time at the client
        for x in 0..NUM_WRITES {
            // read a single message
            let rv = test_utils::check_system_call!(
                || {
                    let mut buf: [u8; MESSAGE_SIZE] = [0; MESSAGE_SIZE];
                    read_once(fd_client, &mut buf) as libc::c_int
                },
                &[],
            )?;

            test_utils::result_assert_eq(
                rv,
                MESSAGE_SIZE as libc::c_int,
                &format!("Unexpected return value when read()ing message {}", x + 1),
            )?;
        }

        // if receptions are disallowed, we should check the behaviour of additional read() calls
        // since they shouldn't block and instead should return 0 (EOF)
        if how == libc::SHUT_RD || how == libc::SHUT_RDWR {
            // if we have a non-blocking dgram socket, we expect to get an EAGAIN
            let expected_errnos = if sock_type == libc::SOCK_DGRAM && flag == libc::SOCK_NONBLOCK {
                vec![libc::EAGAIN]
            } else {
                vec![]
            };

            // read a single message
            let rv = test_utils::check_system_call!(
                || {
                    let mut buf: [u8; 1] = [0];
                    read_once(fd_client, &mut buf) as libc::c_int
                },
                &expected_errnos,
            )?;

            // if it didn't return an error, we expect read() to signal EOF
            if expected_errnos.is_empty() {
                test_utils::result_assert_eq(
                    rv,
                    0,
                    "Unexpected return value when checking additional read() calls",
                )?;
            }
        }

        // if the client shut down only reading with SHUT_RD (not SHUT_RDWR), we may still be able to
        // read more bytes even though we previously received an EOF
        if how == libc::SHUT_RD {
            // write one message at a time from the server
            for _ in 0..NUM_WRITES {
                write_all(fd_server, &message);
            }

            // shadow needs to run events
            assert_eq!(unsafe { libc::usleep(10000) }, 0);

            // read one message at a time at the client
            for x in 0..NUM_WRITES {
                // read a single message
                let rv = test_utils::check_system_call!(
                    || {
                        let mut buf: [u8; MESSAGE_SIZE] = [0; MESSAGE_SIZE];
                        read_once(fd_client, &mut buf) as libc::c_int
                    },
                    &[],
                )?;

                test_utils::result_assert_eq(
                    rv,
                    MESSAGE_SIZE as libc::c_int,
                    &format!("Unexpected return value when read()ing message {}", x + 1),
                )?;
            }
        }

        Ok(())
    })
}

/// Test a certain case where we receive ECONNRESET when reading after shutdown().
fn test_conn_reset(
    domain: libc::c_int,
    sock_type: libc::c_int,
    flag: libc::c_int,
    how: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_server) = if sock_type == libc::SOCK_STREAM {
        setup_stream_sockets(domain, flag)
    } else if sock_type == libc::SOCK_DGRAM {
        setup_dgram_sockets(domain, flag)
    } else {
        unreachable!("Unhandled socket type: {}", sock_type);
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // run shutdown() on the client's socket
        check_shutdown_call(&ShutdownArguments { fd: fd_client, how }, &[])?;

        // write a single byte/message to write from the server
        write_all(fd_server, &[1u8]);

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read the byte/message at the client
        {
            // in this specific case we expect to get a ECONNRESET on read()
            let expected_errnos = if how == libc::SHUT_RDWR && sock_type == libc::SOCK_STREAM {
                vec![libc::ECONNRESET]
            } else {
                vec![]
            };

            // read a single byte/message
            let rv = test_utils::check_system_call!(
                || {
                    let mut buf: [u8; 1] = [0];
                    read_once(fd_client, &mut buf) as libc::c_int
                },
                &expected_errnos,
            )?;

            // if it didn't return an error, make sure we successfully read the byte/message
            if expected_errnos.is_empty() {
                test_utils::result_assert_eq(rv, 1, "Unexpected return value when read()ing")?;
            }
        }

        // if receptions are disallowed, we should check the behaviour of additional read() calls
        // since they shouldn't block and instead should return 0 (even if we previously received
        // an ECONNRESET)
        if how == libc::SHUT_RD || how == libc::SHUT_RDWR {
            // if we have a non-blocking dgram socket, we expect to get an EAGAIN
            let expected_errnos = if sock_type == libc::SOCK_DGRAM && flag == libc::SOCK_NONBLOCK {
                vec![libc::EAGAIN]
            } else {
                vec![]
            };

            // read a single byte/message
            let rv = test_utils::check_system_call!(
                || {
                    let mut buf: [u8; 1] = [0];
                    read_once(fd_client, &mut buf) as libc::c_int
                },
                &expected_errnos,
            )?;

            // if it didn't return an error, we expect read() to signal EOF
            if expected_errnos.is_empty() {
                test_utils::result_assert_eq(
                    rv,
                    0,
                    "Unexpected return value when checking additional read() calls",
                )?;
            }
        }

        Ok(())
    })
}

/// Test reading from the socket after shutdown() on the peer.
fn test_read_after_peer_shutdown(
    domain: libc::c_int,
    sock_type: libc::c_int,
    flag: libc::c_int,
    how: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_server) = if sock_type == libc::SOCK_STREAM {
        setup_stream_sockets(domain, flag)
    } else if sock_type == libc::SOCK_DGRAM {
        setup_dgram_sockets(domain, flag)
    } else {
        unreachable!("Unhandled socket type: {}", sock_type);
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // message to write from the server
        const MESSAGE_SIZE: usize = 5;
        const NUM_WRITES: i32 = 4;

        let message: [u8; MESSAGE_SIZE] = [1, 2, 3, 4, 5];

        // write one message at a time from the server
        for _ in 0..NUM_WRITES {
            write_all(fd_server, &message);
        }

        // run shutdown() on the server's socket
        check_shutdown_call(&ShutdownArguments { fd: fd_server, how }, &[])?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read one message at a time at the client
        for x in 0..NUM_WRITES {
            // read a single message
            let rv = test_utils::check_system_call!(
                || {
                    let mut buf: [u8; MESSAGE_SIZE] = [0; MESSAGE_SIZE];
                    read_once(fd_client, &mut buf) as libc::c_int
                },
                &[],
            )?;

            // make sure we successfully read the message
            test_utils::result_assert_eq(
                rv,
                MESSAGE_SIZE as libc::c_int,
                &format!("Unexpected return value when read()ing message {}", x + 1),
            )?;
        }

        // if the server TCP socket's writing was shutdown
        if how == libc::SHUT_WR || how == libc::SHUT_RDWR {
            if sock_type == libc::SOCK_STREAM {
                // read a single byte, expecting an EOF
                let rv = test_utils::check_system_call!(
                    || {
                        let mut buf: [u8; 1] = [0];
                        read_once(fd_client, &mut buf) as libc::c_int
                    },
                    &[],
                )?;

                // we expect read() to signal EOF
                test_utils::result_assert_eq(
                    rv,
                    0,
                    "Unexpected return value when checking additional read() calls",
                )?;
            } else if sock_type == libc::SOCK_DGRAM && flag == libc::SOCK_NONBLOCK {
                // read a single byte, expecting to receive EAGAIN
                test_utils::check_system_call!(
                    || {
                        let mut buf: [u8; 1] = [0];
                        read_once(fd_client, &mut buf) as libc::c_int
                    },
                    &[libc::EAGAIN],
                )?;
            }
        }

        Ok(())
    })
}

/// Test writing to the socket after shutdown().
fn test_write_after_client_shutdown(
    domain: libc::c_int,
    sock_type: libc::c_int,
    flag: libc::c_int,
    how: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_server) = if sock_type == libc::SOCK_STREAM {
        setup_stream_sockets(domain, flag)
    } else if sock_type == libc::SOCK_DGRAM {
        setup_dgram_sockets(domain, flag)
    } else {
        unreachable!("Unhandled socket type: {}", sock_type);
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // run shutdown() on the client's socket
        check_shutdown_call(&ShutdownArguments { fd: fd_client, how }, &[])?;

        // if transmissions are disallowed, we expect an EPIPE error from write(), even for UDP sockets
        let expected_errnos = if [libc::SHUT_WR, libc::SHUT_RDWR].contains(&how) {
            vec![libc::EPIPE]
        } else {
            vec![]
        };

        // run write()
        test_utils::check_system_call!(
            || {
                let buf: [u8; 4] = [1, 2, 3, 4];
                write_once(fd_client, &buf) as libc::c_int
            },
            &expected_errnos,
        )?;

        // check that write() returns the same errno if the buf length is 0
        test_utils::check_system_call!(
            || {
                let buf: [u8; 0] = [];
                write_once(fd_client, &buf) as libc::c_int
            },
            &expected_errnos,
        )?;

        Ok(())
    })
}

/// Test writing to the socket after shutdown() on the peer.
fn test_write_after_peer_shutdown(
    domain: libc::c_int,
    sock_type: libc::c_int,
    flag: libc::c_int,
    how: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_server) = if sock_type == libc::SOCK_STREAM {
        setup_stream_sockets(domain, flag)
    } else if sock_type == libc::SOCK_DGRAM {
        setup_dgram_sockets(domain, flag)
    } else {
        unreachable!("Unhandled socket type: {}", sock_type);
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // run shutdown() on the server's socket
        check_shutdown_call(&ShutdownArguments { fd: fd_server, how }, &[])?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(5000) }, 0);

        // first write() should not have an error
        test_utils::check_system_call!(
            || {
                let buf: [u8; 4] = [1, 2, 3, 4];
                write_once(fd_client, &buf) as libc::c_int
            },
            &[],
        )?;

        // we expect an EPIPE error from the second write() when using a TCP socket and the server
        // has shutdown both reading and writing
        let expected_errnos = if sock_type == libc::SOCK_STREAM && how == libc::SHUT_RDWR {
            vec![libc::EPIPE]
        } else {
            vec![]
        };

        // second write() may have an error
        test_utils::check_system_call!(
            || {
                let buf: [u8; 4] = [1, 2, 3, 4];
                write_once(fd_client, &buf) as libc::c_int
            },
            &expected_errnos,
        )?;

        // check that write() returns the same errno if the buf length is 0
        test_utils::check_system_call!(
            || {
                let buf: [u8; 0] = [];
                write_once(fd_client, &buf) as libc::c_int
            },
            &expected_errnos,
        )?;

        Ok(())
    })
}

/// Test that the FIN is not sent until the buffer is cleared after shutdown() on both sockets.
fn test_tcp_fin_correctness(domain: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let (fd_client, fd_server) = setup_stream_sockets(domain, flag);

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        const NUM_BYTES: usize = 120000;
        let buf = [0u8; NUM_BYTES];

        // run shutdown(SHUT_WR) on the client's socket to send a FIN
        check_shutdown_call(
            &ShutdownArguments {
                fd: fd_client,
                how: libc::SHUT_WR,
            },
            &[],
        )?;

        // write data from the server
        write_all(fd_server, &buf);

        // run shutdown(SHUT_WR) on the server's socket to send a FIN
        check_shutdown_call(
            &ShutdownArguments {
                fd: fd_server,
                how: libc::SHUT_WR,
            },
            &[],
        )?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read at the client until an EOF
        let read_buf = read_all(fd_client);

        test_utils::result_assert_eq(
            NUM_BYTES,
            read_buf.len(),
            "Bytes written and read do not match.",
        )?;

        Ok(())
    })
}

fn check_shutdown_call(
    args: &ShutdownArguments,
    expected_errnos: &[libc::c_int],
) -> Result<(), String> {
    test_utils::check_system_call!(
        move || unsafe { libc::shutdown(args.fd, args.how) },
        expected_errnos,
    )?;

    Ok(())
}
