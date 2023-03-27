/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use std::sync::{Arc, Barrier};

use test_utils::set;
use test_utils::socket_utils::SockAddr;
use test_utils::socket_utils::{self, SocketInitMethod};
use test_utils::TestEnvironment as TestEnv;

struct ConnectArguments {
    fd: libc::c_int,
    addr: Option<SockAddr>, // if None, a null pointer should be used
    addr_len: libc::socklen_t,
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
            "test_null_addr",
            test_null_addr,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_short_len",
            test_short_len,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_zero_len",
            test_zero_len,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
    ];

    // inet-only tests
    for &sock_type in [libc::SOCK_STREAM, libc::SOCK_DGRAM].iter() {
        for &flag in [0, libc::SOCK_NONBLOCK, libc::SOCK_CLOEXEC].iter() {
            // add details to the test names to avoid duplicates
            let append_args = |s| format!("{} <type={},flag={}>", s, sock_type, flag);

            tests.extend(vec![
                test_utils::ShadowTest::new(
                    &append_args("test_non_existent_server"),
                    move || test_non_existent_server(sock_type, flag),
                    set![TestEnv::Libc],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_port_zero"),
                    move || test_port_zero(sock_type, flag),
                    set![TestEnv::Libc],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_after_close"),
                    move || test_after_close(sock_type, flag),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_interface_loopback"),
                    move || test_interface(sock_type, flag, libc::INADDR_LOOPBACK, None),
                    set![TestEnv::Libc, TestEnv::Shadow],
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
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_interface_any"),
                    move || test_interface(sock_type, flag, libc::INADDR_ANY, None),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_interface_any_any"),
                    move || {
                        test_interface(sock_type, flag, libc::INADDR_ANY, Some(libc::INADDR_ANY))
                    },
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_double_connect_same_addr"),
                    move || test_double_connect(sock_type, flag, /* change_address= */ false),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_double_connect_different_addr"),
                    move || test_double_connect(sock_type, flag, /* change_address= */ true),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
            ]);
        }
    }

    // unix-only tests
    for &sock_type in [libc::SOCK_STREAM, libc::SOCK_DGRAM, libc::SOCK_SEQPACKET].iter() {
        for &flag in [0, libc::SOCK_NONBLOCK, libc::SOCK_CLOEXEC].iter() {
            // add details to the test names to avoid duplicates
            let append_args = |s| format!("{} <type={},flag={}>", s, sock_type, flag);

            tests.extend(vec![test_utils::ShadowTest::new(
                &append_args("test_non_existent_path"),
                move || test_non_existent_path(sock_type, flag),
                set![TestEnv::Libc, TestEnv::Shadow],
            )]);
        }
    }

    for &domain in [libc::AF_INET, libc::AF_UNIX].iter() {
        let sock_types = match domain {
            libc::AF_INET => &[libc::SOCK_STREAM, libc::SOCK_DGRAM][..],
            libc::AF_UNIX => &[libc::SOCK_STREAM, libc::SOCK_DGRAM, libc::SOCK_SEQPACKET][..],
            _ => unimplemented!(),
        };

        for &sock_type in sock_types.iter() {
            for &flag in [0, libc::SOCK_NONBLOCK, libc::SOCK_CLOEXEC].iter() {
                // add details to the test names to avoid duplicates
                let append_args =
                    |s| format!("{} <domain={},type={},flag={}>", s, domain, sock_type, flag);

                tests.extend(vec![test_utils::ShadowTest::new(
                    &append_args("test_af_unspec"),
                    move || test_af_unspec(domain, sock_type, flag),
                    // TODO: shadow doesn't support AF_UNSPEC for inet or unix sockets
                    set![TestEnv::Libc],
                )]);
            }
        }

        let sock_types = match domain {
            libc::AF_INET => &[libc::SOCK_STREAM][..],
            libc::AF_UNIX => &[libc::SOCK_STREAM, libc::SOCK_SEQPACKET][..],
            _ => unimplemented!(),
        };

        for &sock_type in sock_types.iter() {
            // add details to the test names to avoid duplicates
            let append_args = |s| format!("{} <domain={},type={}>", s, domain, sock_type);

            for &flag in [0, libc::SOCK_NONBLOCK, libc::SOCK_CLOEXEC].iter() {
                // add details to the test names to avoid duplicates
                let append_args =
                    |s| format!("{} <domain={},type={},flag={}>", s, domain, sock_type, flag);

                tests.extend(vec![test_utils::ShadowTest::new(
                    &append_args("test_connect_when_server_queue_full"),
                    move || test_connect_when_server_queue_full(domain, sock_type, flag),
                    set![TestEnv::Libc, TestEnv::Shadow],
                )]);
            }

            tests.extend(vec![test_utils::ShadowTest::new(
                &append_args("test_server_close_during_blocking_connect"),
                move || test_server_close_during_blocking_connect(domain, sock_type),
                if domain != libc::AF_INET {
                    set![TestEnv::Libc, TestEnv::Shadow]
                } else {
                    // TODO: enable once we send RST packets for unbound dest addresses (issue
                    // #2162)
                    set![TestEnv::Libc]
                },
            )]);
        }
    }

    let init_methods = [
        SocketInitMethod::Inet,
        SocketInitMethod::Unix,
        SocketInitMethod::UnixSocketpair,
    ];

    for &method in init_methods.iter() {
        let sock_types = match method.domain() {
            libc::AF_INET => &[libc::SOCK_STREAM, libc::SOCK_DGRAM][..],
            libc::AF_UNIX => &[libc::SOCK_STREAM, libc::SOCK_DGRAM, libc::SOCK_SEQPACKET][..],
            _ => unimplemented!(),
        };

        for &sock_type in sock_types.iter() {
            for &flag in [0, libc::SOCK_NONBLOCK, libc::SOCK_CLOEXEC].iter() {
                // add details to the test names to avoid duplicates
                let append_args = |s| {
                    format!(
                        "{} <init_method={:?},type={},flag={}>",
                        s, method, sock_type, flag
                    )
                };

                tests.extend(vec![test_utils::ShadowTest::new(
                    &append_args("test_af_unspec_after_connect"),
                    move || test_af_unspec_after_connect(method, sock_type, flag),
                    // TODO: shadow doesn't support AF_UNSPEC for inet or unix sockets
                    set![TestEnv::Libc],
                )]);
            }
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
        addr: Some(SockAddr::Inet(addr)),
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
        addr: Some(SockAddr::Inet(addr)),
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
        addr: Some(SockAddr::Inet(addr)),
        addr_len: std::mem::size_of_val(&addr) as u32,
    };

    check_connect_call(&args, Some(libc::ENOTSOCK))
}

/// Test connect() using a valid fd, but with a NULL address.
fn test_null_addr() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
    assert!(fd >= 0);

    let args = ConnectArguments {
        fd,
        addr: None,
        addr_len: std::mem::size_of::<libc::sockaddr_in>() as u32,
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
        fd,
        addr: Some(SockAddr::Inet(addr)),
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
        fd,
        addr: Some(SockAddr::Inet(addr)),
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
        fd,
        addr: Some(SockAddr::Inet(addr)),
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
        fd,
        addr: Some(SockAddr::Inet(addr)),
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
        fd,
        addr: Some(SockAddr::Inet(addr)),
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
        addr: Some(SockAddr::Inet(server_addr)),
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
    #[allow(clippy::if_same_then_else)]
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
        addr: Some(SockAddr::Inet(server_addr)),
        addr_len: std::mem::size_of_val(&server_addr) as u32,
    };

    let mut args_2 = ConnectArguments {
        fd: fd_client,
        addr: Some(SockAddr::Inet(server_addr)),
        addr_len: std::mem::size_of_val(&server_addr) as u32,
    };

    // if we should use a different address for the second connect() call, change the port
    if change_address {
        // note the endianness of the port
        args_2
            .addr
            .as_mut()
            .unwrap()
            .as_inet_mut()
            .unwrap()
            .sin_port += 1;
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

/// Test connect() to a path that doesn't exist.
fn test_non_existent_path(sock_type: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_UNIX, sock_type | flag, 0) };
    assert!(fd >= 0);

    const NON_EXISTENT_PATH: &[u8] = b"/asdf/qwerty/y89pq234589oles.sock";
    let mut path = [0i8; 108];
    path[..NON_EXISTENT_PATH.len()].copy_from_slice(test_utils::u8_to_i8_slice(NON_EXISTENT_PATH));

    let addr = libc::sockaddr_un {
        sun_family: libc::AF_UNIX as u16,
        sun_path: path,
    };

    let args = ConnectArguments {
        fd,
        addr: Some(SockAddr::Unix(addr)),
        addr_len: std::mem::size_of_val(&addr) as u32,
    };

    test_utils::run_and_close_fds(&[fd], || check_connect_call(&args, Some(libc::ENOENT)))
}

/// Test connect() when the server queue is full, and for blocking sockets that an accept() unblocks
/// a blocked connect().
fn test_connect_when_server_queue_full(
    domain: libc::c_int,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let fd_server = unsafe { libc::socket(domain, sock_type | flag, 0) };
    let fd_client = unsafe { libc::socket(domain, sock_type | flag, 0) };
    assert!(fd_server >= 0);
    assert!(fd_client >= 0);

    let (server_addr, server_addr_len) = socket_utils::autobind_helper(fd_server, domain);

    nix::sys::socket::listen(fd_server, 0).map_err(|e| e.to_string())?;

    // use a barrier to help synchronize threads
    let first_connect_barrier = Arc::new(Barrier::new(2));
    let first_connect_barrier_clone = Arc::clone(&first_connect_barrier);

    let thread = std::thread::spawn(move || -> Result<(), String> {
        let fd_client = unsafe { libc::socket(domain, sock_type | flag, 0) };
        assert!(fd_client >= 0);

        let args = ConnectArguments {
            fd: fd_client,
            addr: Some(server_addr),
            addr_len: server_addr_len,
        };

        // the server accept queue will be full
        let expected_errno = match (domain, flag & libc::SOCK_NONBLOCK != 0) {
            (libc::AF_UNIX, true) => Some(libc::EAGAIN),
            (_, true) => Some(libc::EINPROGRESS),
            _ => None,
        };

        // first connect() was made, now we can connect()
        first_connect_barrier_clone.wait();

        let time_before_connect = std::time::Instant::now();

        // second connect(); if non-blocking it should return immediately, but if blocking it should
        // block until the accept()
        check_connect_call(&args, expected_errno)?;

        // if we expect it to have blocked, make sure it actually did block for some amount of time
        if flag & libc::SOCK_NONBLOCK == 0 {
            // the sleep below is for 50 ms, so we'd expect it to have blocked for at least 5 ms
            let duration = std::time::Instant::now().duration_since(time_before_connect);
            assert!(duration.as_millis() >= 5);
        }

        Ok(())
    });

    let args = ConnectArguments {
        fd: fd_client,
        addr: Some(server_addr),
        addr_len: server_addr_len,
    };

    let expected_errno = if domain != libc::AF_UNIX && flag & libc::SOCK_NONBLOCK != 0 {
        Some(libc::EINPROGRESS)
    } else {
        None
    };

    // first connect(); should return immediately
    check_connect_call(&args, expected_errno)?;

    // first connect() was made, and the accept queue should be full
    first_connect_barrier.wait();

    // sleep until the second connect() is either blocking or complete
    std::thread::sleep(std::time::Duration::from_millis(50));

    // accept a socket to unblock the second connect if it's blocking
    let accepted_fd = nix::sys::socket::accept(fd_server).unwrap();
    nix::unistd::close(accepted_fd).unwrap();

    // the second connect() should be unblocked
    thread.join().unwrap()?;

    Ok(())
}

fn test_af_unspec(
    domain: libc::c_int,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type | flag, 0) };
    assert!(fd >= 0);

    let mut addr: libc::sockaddr_storage = unsafe { std::mem::zeroed() };
    addr.ss_family = libc::AF_UNSPEC as u16;
    let addr = SockAddr::Generic(addr);

    let args = ConnectArguments {
        fd,
        addr: Some(addr),
        addr_len: 2,
    };

    let expected_errno = match (domain, sock_type) {
        (_, libc::SOCK_DGRAM) => None,
        (libc::AF_UNIX, _) => Some(libc::EINVAL),
        _ => None,
    };

    test_utils::run_and_close_fds(&[fd], || check_connect_call(&args, expected_errno))
}

fn test_af_unspec_after_connect(
    init_method: SocketInitMethod,
    sock_type: libc::c_int,
    flags: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_server) = socket_utils::socket_init_helper(
        init_method,
        sock_type,
        flags,
        /* bind_client = */ false,
    );

    let mut addr: libc::sockaddr_storage = unsafe { std::mem::zeroed() };
    addr.ss_family = libc::AF_UNSPEC as u16;
    let addr = SockAddr::Generic(addr);

    let args = ConnectArguments {
        fd: fd_client,
        addr: Some(addr),
        addr_len: 2,
    };

    let expected_errno = match (init_method.domain(), sock_type) {
        (_, libc::SOCK_DGRAM) => None,
        (libc::AF_UNIX, _) => Some(libc::EINVAL),
        _ => None,
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        check_connect_call(&args, expected_errno)
    })
}

/// Test that a blocking connect() returns when the server socket is closed.
fn test_server_close_during_blocking_connect(
    domain: libc::c_int,
    sock_type: libc::c_int,
) -> Result<(), String> {
    let fd_server = unsafe { libc::socket(domain, sock_type, 0) };
    let fd_client = unsafe { libc::socket(domain, sock_type, 0) };
    assert!(fd_server >= 0);
    assert!(fd_client >= 0);

    let (server_addr, server_addr_len) = socket_utils::autobind_helper(fd_server, domain);

    nix::sys::socket::listen(fd_server, 0).map_err(|e| e.to_string())?;

    // use a barrier to help synchronize threads
    let first_connect_barrier = Arc::new(Barrier::new(2));
    let first_connect_barrier_clone = Arc::clone(&first_connect_barrier);

    let thread = std::thread::spawn(move || -> Result<(), String> {
        let fd_client = unsafe { libc::socket(domain, sock_type, 0) };
        assert!(fd_client >= 0);

        let args = ConnectArguments {
            fd: fd_client,
            addr: Some(server_addr),
            addr_len: server_addr_len,
        };

        // first connect() was made, now we can connect()
        first_connect_barrier_clone.wait();

        let time_before_connect = std::time::Instant::now();

        // second connect(); should block until the close(fd_server)
        check_connect_call(&args, Some(libc::ECONNREFUSED))?;

        // make sure it actually did block for some amount of time
        // the sleep below is for 50 ms, so we'd expect it to have blocked for at least 5 ms
        let duration = std::time::Instant::now().duration_since(time_before_connect);
        assert!(duration.as_millis() >= 5);

        Ok(())
    });

    let args = ConnectArguments {
        fd: fd_client,
        addr: Some(server_addr),
        addr_len: server_addr_len,
    };

    // first connect(); should return immediately
    check_connect_call(&args, None)?;

    // first connect() was made, and the accept queue should be full
    first_connect_barrier.wait();

    // sleep until the second connect() is blocking
    std::thread::sleep(std::time::Duration::from_millis(50));

    // close the server socket to unblock the second connect
    nix::unistd::close(fd_server).unwrap();

    // the second connect() should be unblocked
    thread.join().unwrap()?;

    Ok(())
}

fn check_connect_call(
    args: &ConnectArguments,
    expected_errno: Option<libc::c_int>,
) -> Result<(), String> {
    // get a pointer to the sockaddr and the size of the structure
    // careful use of references here makes sure we don't copy memory, leading to stale pointers
    let (addr_ptr, addr_max_len) = match args.addr {
        Some(ref x) => (x.as_ptr(), x.ptr_size()),
        None => (std::ptr::null(), 0),
    };

    // if the pointer is non-null, make sure the provided size is not greater than the actual
    // data size so that we don't segfault
    assert!(addr_ptr.is_null() || args.addr_len <= addr_max_len);

    let rv = unsafe { libc::connect(args.fd, addr_ptr, args.addr_len) };

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
