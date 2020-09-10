/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use test_utils::AsPtr;

struct ConnectArguments {
    fd: libc::c_int,
    addr: Option<libc::sockaddr_in>, // if None, a null pointer should be used
    addr_len: libc::socklen_t,
}

fn main() -> Result<(), String> {
    // should we run only tests that shadow supports
    let run_only_passing_tests = std::env::args().any(|x| x == "--shadow-passing");
    // should we summarize the results rather than exit on a failed test
    let summarize = std::env::args().any(|x| x == "--summarize");

    let mut tests = get_tests();
    if run_only_passing_tests {
        tests = tests
            .into_iter()
            .filter(|x| x.shadow_passing() == test_utils::ShadowPassing::Yes)
            .collect()
    }

    test_utils::run_tests(&tests, summarize)?;

    println!("Success.");
    Ok(())
}

fn get_tests() -> Vec<test_utils::ShadowTest<String>> {
    let mut tests: Vec<test_utils::ShadowTest<_>> = vec![
        test_utils::ShadowTest::new(
            "test_invalid_fd",
            test_invalid_fd,
            test_utils::ShadowPassing::Yes,
        ),
        test_utils::ShadowTest::new(
            "test_non_existent_fd",
            test_non_existent_fd,
            test_utils::ShadowPassing::Yes,
        ),
        test_utils::ShadowTest::new(
            "test_non_socket_fd",
            test_non_socket_fd,
            test_utils::ShadowPassing::No,
        ),
        test_utils::ShadowTest::new(
            "test_null_addr",
            test_null_addr,
            test_utils::ShadowPassing::Yes,
        ),
        test_utils::ShadowTest::new(
            "test_short_len",
            test_short_len,
            test_utils::ShadowPassing::Yes,
        ),
        test_utils::ShadowTest::new(
            "test_zero_len",
            test_zero_len,
            test_utils::ShadowPassing::Yes,
        ),
    ];

    // tests to repeat for different socket options
    for &sock_type in [libc::SOCK_STREAM, libc::SOCK_DGRAM].iter() {
        for &flag in [0, libc::SOCK_NONBLOCK, libc::SOCK_CLOEXEC].iter() {
            // add details to the test names to avoid duplicates
            let append_args = |s| format!("{} <type={},flag={}>", s, sock_type, flag);

            let more_tests: Vec<test_utils::ShadowTest<_>> = vec![
                test_utils::ShadowTest::new(
                    &append_args("test_non_existent_server"),
                    move || test_non_existent_server(sock_type, flag),
                    test_utils::ShadowPassing::No,
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_port_zero"),
                    move || test_port_zero(sock_type, flag),
                    test_utils::ShadowPassing::No,
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_after_close"),
                    move || test_after_close(sock_type, flag),
                    test_utils::ShadowPassing::Yes,
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_interface_loopback"),
                    move || test_interface(sock_type, flag, libc::INADDR_LOOPBACK, None),
                    test_utils::ShadowPassing::Yes,
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_interface_loopback_any"),
                    move || {
                        test_interface(
                            sock_type,
                            flag,
                            libc::INADDR_LOOPBACK,
                            Some(libc::INADDR_ANY),
                        )
                    },
                    test_utils::ShadowPassing::Yes,
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_interface_any"),
                    move || test_interface(sock_type, flag, libc::INADDR_ANY, None),
                    test_utils::ShadowPassing::Yes,
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_interface_any_any"),
                    move || {
                        test_interface(sock_type, flag, libc::INADDR_ANY, Some(libc::INADDR_ANY))
                    },
                    test_utils::ShadowPassing::Yes,
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_double_connect_same_addr"),
                    move || test_double_connect(sock_type, flag, /* change_address= */ false),
                    test_utils::ShadowPassing::Yes,
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_double_connect_different_addr"),
                    move || test_double_connect(sock_type, flag, /* change_address= */ true),
                    test_utils::ShadowPassing::Yes,
                ),
            ];

            tests.extend(more_tests);
        }
    }

    tests
}

/// Test connect() using an argument that cannot be a fd.
fn test_invalid_fd() -> Result<(), String> {
    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    let args = ConnectArguments {
        fd: -1,
        addr: Some(addr),
        addr_len: std::mem::size_of_val(&addr) as u32,
    };

    check_connect_call(&args, Some(libc::EBADF))
}

/// Test connect() using an argument that could be a fd, but is not.
fn test_non_existent_fd() -> Result<(), String> {
    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    let args = ConnectArguments {
        fd: 8934,
        addr: Some(addr),
        addr_len: std::mem::size_of_val(&addr) as u32,
    };

    check_connect_call(&args, Some(libc::EBADF))
}

/// Test connect() using a valid fd that is not a socket.
fn test_non_socket_fd() -> Result<(), String> {
    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    let args = ConnectArguments {
        fd: 0, // assume the fd 0 is already open and is not a socket
        addr: Some(addr),
        addr_len: std::mem::size_of_val(&addr) as u32,
    };

    check_connect_call(&args, Some(libc::ENOTSOCK))
}

/// Test connect() using a valid fd, but with a NULL address.
fn test_null_addr() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
    assert!(fd >= 0);

    let args = ConnectArguments {
        fd: fd,
        addr: None,
        addr_len: 5,
    };

    test_utils::run_and_close_fds(&[fd], || check_connect_call(&args, Some(libc::EFAULT)))
}

/// Test connect() using a valid fd and address, but an address length that is too low.
fn test_short_len() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
    assert!(fd >= 0);

    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    let args = ConnectArguments {
        fd: fd,
        addr: Some(addr),
        addr_len: (std::mem::size_of_val(&addr) - 1) as u32,
    };

    test_utils::run_and_close_fds(&[fd], || check_connect_call(&args, Some(libc::EINVAL)))
}

/// Test connect() using a valid fd and address, but an address length that is zero.
fn test_zero_len() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
    assert!(fd >= 0);

    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    let args = ConnectArguments {
        fd: fd,
        addr: Some(addr),
        addr_len: 0u32,
    };

    test_utils::run_and_close_fds(&[fd], || check_connect_call(&args, Some(libc::EINVAL)))
}

/// Test connect() to an address that doesn't exist.
fn test_non_existent_server(sock_type: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    assert!(fd >= 0);

    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        // this port should not be in use
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    let args = ConnectArguments {
        fd: fd,
        addr: Some(addr),
        addr_len: std::mem::size_of_val(&addr) as u32,
    };

    let expected_errno = if sock_type == libc::SOCK_DGRAM {
        None
    } else if flag & libc::SOCK_NONBLOCK != 0 {
        Some(libc::EINPROGRESS)
    } else {
        Some(libc::ECONNREFUSED)
    };

    test_utils::run_and_close_fds(&[fd], || check_connect_call(&args, expected_errno))
}

/// Test connect() to an address with port 0.
fn test_port_zero(sock_type: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    assert!(fd >= 0);

    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 0u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    let args = ConnectArguments {
        fd: fd,
        addr: Some(addr),
        addr_len: std::mem::size_of_val(&addr) as u32,
    };

    let expected_errno = if sock_type == libc::SOCK_DGRAM {
        None
    } else if flag & libc::SOCK_NONBLOCK != 0 {
        Some(libc::EINPROGRESS)
    } else {
        Some(libc::ECONNREFUSED)
    };

    test_utils::run_and_close_fds(&[fd], || check_connect_call(&args, expected_errno))
}

/// Test connect() after closing the socket.
fn test_after_close(sock_type: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    assert!(fd >= 0);

    {
        let rv = unsafe { libc::close(fd) };
        assert_eq!(rv, 0);
    }

    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    let args = ConnectArguments {
        fd: fd,
        addr: Some(addr),
        addr_len: std::mem::size_of_val(&addr) as u32,
    };

    check_connect_call(&args, Some(libc::EBADF))
}

/// Test connect() to a server listening on the given interface (optionally overriding
/// the interface the client connects on).
fn test_interface(
    sock_type: libc::c_int,
    flag: libc::c_int,
    server_interface: libc::in_addr_t,
    override_client_interface: Option<libc::in_addr_t>,
) -> Result<(), String> {
    let fd_server = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    let fd_client = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    assert!(fd_server >= 0);
    assert!(fd_client >= 0);

    // the server address
    let mut server_addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 0u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: server_interface.to_be(),
        },
        sin_zero: [0; 8],
    };

    // bind on the server address
    {
        let rv = unsafe {
            libc::bind(
                fd_server,
                &server_addr as *const libc::sockaddr_in as *const libc::sockaddr,
                std::mem::size_of_val(&server_addr) as u32,
            )
        };
        assert_eq!(rv, 0);
    }

    // get the assigned port number
    {
        let mut server_addr_size = std::mem::size_of_val(&server_addr) as u32;
        let rv = unsafe {
            libc::getsockname(
                fd_server,
                &mut server_addr as *mut libc::sockaddr_in as *mut libc::sockaddr,
                &mut server_addr_size as *mut libc::socklen_t,
            )
        };
        assert_eq!(rv, 0);
        assert_eq!(server_addr_size, std::mem::size_of_val(&server_addr) as u32);
    }

    if sock_type == libc::SOCK_STREAM {
        // listen for connections
        let rv = unsafe { libc::listen(fd_server, 10) };
        assert_eq!(rv, 0);
    }

    // optionally overwrite the interface used with connect()
    if let Some(client_interface) = override_client_interface {
        server_addr.sin_addr.s_addr = client_interface.to_be();
    }

    let expected_errno = if sock_type == libc::SOCK_DGRAM {
        None
    } else if flag & libc::SOCK_NONBLOCK != 0 {
        Some(libc::EINPROGRESS)
    } else {
        None
    };

    let args = ConnectArguments {
        fd: fd_client,
        addr: Some(server_addr),
        addr_len: std::mem::size_of_val(&server_addr) as u32,
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        check_connect_call(&args, expected_errno)
    })
}

/// Test connect() to a server twice, optionally changing the address.
fn test_double_connect(
    sock_type: libc::c_int,
    flag: libc::c_int,
    change_address: bool,
) -> Result<(), String> {
    let fd_server = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    let fd_client = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    assert!(fd_server >= 0);
    assert!(fd_client >= 0);

    // the server address
    let mut server_addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 0u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_ANY.to_be(),
        },
        sin_zero: [0; 8],
    };

    // bind on the server address
    {
        let rv = unsafe {
            libc::bind(
                fd_server,
                &server_addr as *const libc::sockaddr_in as *const libc::sockaddr,
                std::mem::size_of_val(&server_addr) as u32,
            )
        };
        assert_eq!(rv, 0);
    }

    // get the assigned port number
    {
        let mut server_addr_size = std::mem::size_of_val(&server_addr) as u32;
        let rv = unsafe {
            libc::getsockname(
                fd_server,
                &mut server_addr as *mut libc::sockaddr_in as *mut libc::sockaddr,
                &mut server_addr_size as *mut libc::socklen_t,
            )
        };
        assert_eq!(rv, 0);
        assert_eq!(server_addr_size, std::mem::size_of_val(&server_addr) as u32);
    }

    if sock_type == libc::SOCK_STREAM {
        // listen for connections
        let rv = unsafe { libc::listen(fd_server, 10) };
        assert_eq!(rv, 0);
    }

    // expected errno for the first connect() call
    let expected_errno_1 = if sock_type == libc::SOCK_DGRAM {
        None
    } else if flag & libc::SOCK_NONBLOCK != 0 {
        Some(libc::EINPROGRESS)
    } else {
        None
    };

    // expected errno for the second connect() call
    let expected_errno_2 = if sock_type == libc::SOCK_DGRAM {
        None
    } else if flag & libc::SOCK_NONBLOCK != 0 {
        // for some reason connect() doesn't return an error for non-blocking
        // sockets, even if the address changed
        None
    } else {
        Some(libc::EISCONN)
    };

    let args_1 = ConnectArguments {
        fd: fd_client,
        addr: Some(server_addr),
        addr_len: std::mem::size_of_val(&server_addr) as u32,
    };

    let mut args_2 = ConnectArguments {
        fd: fd_client,
        addr: Some(server_addr),
        addr_len: std::mem::size_of_val(&server_addr) as u32,
    };

    // if we should use a different address for the second connect() call, change the port
    if change_address {
        // note the endianness of the port
        args_2.addr.as_mut().unwrap().sin_port += 1;
    }
    let args_2 = args_2;

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        check_connect_call(&args_1, expected_errno_1)?;

        // shadow needs to run events, otherwise connect will continue returning EINPROGRESS
        let rv = unsafe { libc::usleep(2000) };
        assert_eq!(rv, 0);

        check_connect_call(&args_2, expected_errno_2)
    })
}

fn check_connect_call(
    args: &ConnectArguments,
    expected_errno: Option<libc::c_int>,
) -> Result<(), String> {
    // if the pointers will be non-null, make sure the length is not greater than the actual data size
    // so that we don't segfault
    if args.addr.is_some() {
        assert!(args.addr_len as usize <= std::mem::size_of_val(&args.addr.unwrap()));
    }

    let rv = unsafe {
        libc::connect(
            args.fd,
            args.addr.as_ptr() as *mut libc::sockaddr,
            args.addr_len,
        )
    };

    let errno = test_utils::get_errno();

    match expected_errno {
        // if we expect the connect() call to return an error (rv should be -1)
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
