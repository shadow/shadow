/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use std::sync::{Arc, Barrier};

use test_utils::TestEnvironment as TestEnv;
use test_utils::set;
use test_utils::socket_utils::SockAddr;
use test_utils::socket_utils::{self, SocketInitMethod};

use nix::errno::Errno;

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
        test_utils::ShadowTest::new(
            "test_recv_original_bind_port",
            test_recv_original_bind_port,
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
                    &append_args("test_two_sockets_same_port"),
                    move || test_two_sockets_same_port(sock_type, flag),
                    if sock_type == libc::SOCK_DGRAM {
                        set![TestEnv::Libc, TestEnv::Shadow]
                    } else {
                        // TODO: this doesn't work in shadow for TCP
                        set![TestEnv::Libc]
                    },
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
                    &append_args("test_double_connect_same_ip_port"),
                    move || {
                        test_double_connect(
                            sock_type, flag, /* change_ip= */ false,
                            /* change_port= */ false,
                        )
                    },
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_double_connect_same_ip_different_port"),
                    move || {
                        test_double_connect(
                            sock_type, flag, /* change_ip= */ false,
                            /* change_port= */ true,
                        )
                    },
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_double_connect_same_port_different_ip"),
                    move || {
                        test_double_connect(
                            sock_type, flag, /* change_ip= */ true,
                            /* change_port= */ false,
                        )
                    },
                    if sock_type == libc::SOCK_STREAM {
                        // TODO: this test causes shadow to panic for TCP sockets
                        set![TestEnv::Libc]
                    } else {
                        set![TestEnv::Libc, TestEnv::Shadow]
                    },
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_loopback_bound_connect"),
                    move || test_loopback_bound_connect(sock_type, flag),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
            ]);
        }
    }

    // TCP-only tests
    for &sock_type in [libc::SOCK_STREAM].iter() {
        for &flag in [0, libc::SOCK_NONBLOCK, libc::SOCK_CLOEXEC].iter() {
            // add details to the test names to avoid duplicates
            let append_args = |s| format!("{} <type={},flag={}>", s, sock_type, flag);

            tests.extend(vec![test_utils::ShadowTest::new(
                &append_args("test_loopback_listening_connect"),
                move || test_loopback_listening_connect(sock_type, flag),
                set![TestEnv::Libc, TestEnv::Shadow],
            )]);
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

                tests.extend(vec![
                    test_utils::ShadowTest::new(
                        &append_args("test_listening"),
                        move || test_listening(domain, sock_type, flag),
                        set![TestEnv::Libc, TestEnv::Shadow],
                    ),
                    test_utils::ShadowTest::new(
                        &append_args("test_connect_when_server_queue_full"),
                        move || test_connect_when_server_queue_full(domain, sock_type, flag),
                        set![TestEnv::Libc, TestEnv::Shadow],
                    ),
                ]);
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

/// Test associating two sockets with the same local ip/port, but with one socket having a wildcard
/// peer association.
fn test_two_sockets_same_port(sock_type: libc::c_int, flags: libc::c_int) -> Result<(), String> {
    // since we don't explicitly bind the client, the client will be given an ephemeral port and
    // associated using the specific 4-tuple (local_ip, local_port, peer_ip, peer_port) for TCP and
    // the specific 2-tuple (local_ip, local_port) for UDP
    let (fd_client, fd_peer) = socket_utils::socket_init_helper(
        socket_utils::SocketInitMethod::Inet,
        sock_type,
        flags,
        /* bind_client= */ false,
    );

    let fd_other = unsafe { libc::socket(libc::AF_INET, sock_type | flags, 0) };

    test_utils::run_and_close_fds(&[fd_client, fd_peer, fd_other], || {
        let mut client_addr: libc::sockaddr_in = unsafe { std::mem::zeroed() };
        let mut client_addr_len: libc::socklen_t =
            std::mem::size_of_val(&client_addr).try_into().unwrap();

        // get the client address
        {
            let rv = unsafe {
                libc::getsockname(
                    fd_client,
                    std::ptr::from_mut(&mut client_addr) as *mut libc::sockaddr,
                    &mut client_addr_len,
                )
            };
            assert_eq!(rv, 0);
        }

        // try binding a new socket to the same ip/port as the client socket, which would associate
        // using the wildcard 4-tuple (local_ip, local_port, *, *) for TCP and the 2-tuple (local_ip,
        // local_port) for UDP
        {
            let rv = Errno::result(unsafe {
                libc::bind(
                    fd_other,
                    std::ptr::from_ref(&client_addr) as *const libc::sockaddr,
                    client_addr_len,
                )
            });
            // even though the TCP association is different, Linux doesn't allow this (although if this
            // other socket had a specific peer rather than a wildcard peer, it would work)
            assert_eq!(rv, Err(Errno::EADDRINUSE));
        }

        Ok(())
    })
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
                std::ptr::from_ref(&server_addr) as *const libc::sockaddr,
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
                std::ptr::from_mut(&mut server_addr) as *mut libc::sockaddr,
                std::ptr::from_mut(&mut server_addr_size),
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

/// Test connect() to a server twice, optionally changing the IP and/or port.
fn test_double_connect(
    sock_type: libc::c_int,
    flag: libc::c_int,
    change_ip: bool,
    change_port: bool,
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
                std::ptr::from_ref(&server_addr) as *const libc::sockaddr,
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
                std::ptr::from_mut(&mut server_addr) as *mut libc::sockaddr,
                std::ptr::from_mut(&mut server_addr_size),
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

    // if we should use a different address for the second connect() call, change the port and/or ip
    if change_port {
        // note the endianness of the port
        args_2
            .addr
            .as_mut()
            .unwrap()
            .as_inet_mut()
            .unwrap()
            .sin_port += 1;
    }
    if change_ip {
        // we use an IP on a different interface from the first connect (first was on loopback,
        // second will be on eth0)
        let other_ip: std::net::Ipv4Addr = if test_utils::running_in_shadow() {
            // this IP is the IP for the host 'othernode' in the shadow config file
            "26.153.52.74".parse().unwrap()
        } else {
            // if running outside of shadow, we use a local network address here so that the tests
            // running outside of shadow would only be trying to connect to a server on a local
            // network rather than some random server on the internet
            "192.168.1.100".parse().unwrap()
        };
        args_2
            .addr
            .as_mut()
            .unwrap()
            .as_inet_mut()
            .unwrap()
            .sin_addr
            .s_addr = u32::from(other_ip).to_be();
    }
    let args_2 = args_2;

    // expected errno for the second connect() call
    let is_nonblock = flag & libc::SOCK_NONBLOCK != 0;
    let expected_errno_2 = match (sock_type, is_nonblock, change_ip) {
        // dgram sockets with a different peer IP
        (libc::SOCK_DGRAM, _, true) => Some(libc::EINVAL),
        // dgram sockets with the same peer IP
        (libc::SOCK_DGRAM, _, false) => None,
        // all nonblocking sockets: for some reason connect() doesn't return an error for
        // non-blocking sockets, even if the address changed
        (_, true, _) => None,
        // all other sockets
        (_, _, _) => Some(libc::EISCONN),
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        check_connect_call(&args_1, expected_errno_1)?;

        // shadow needs to run events, otherwise connect will continue returning EINPROGRESS
        let rv = unsafe { libc::usleep(2000) };
        assert_eq!(rv, 0);

        check_connect_call(&args_2, expected_errno_2)
    })
}

/// Test receiving messages on a UDP socket that was originally bound with no peer, then was given a
/// peer using `connect()`.
fn test_recv_original_bind_port() -> Result<(), String> {
    let fd_server =
        unsafe { libc::socket(libc::AF_INET, libc::SOCK_DGRAM | libc::SOCK_NONBLOCK, 0) };
    let fd_client =
        unsafe { libc::socket(libc::AF_INET, libc::SOCK_DGRAM | libc::SOCK_NONBLOCK, 0) };
    let fd_other =
        unsafe { libc::socket(libc::AF_INET, libc::SOCK_DGRAM | libc::SOCK_NONBLOCK, 0) };
    assert!(fd_server >= 0);
    assert!(fd_client >= 0);
    assert!(fd_other >= 0);

    test_utils::run_and_close_fds(&[fd_client, fd_server, fd_other], || {
        // Bind both the client and server to some loopback port. They both have no peer, so should
        // be able to receive packets from any source address.
        let (_client_addr, _client_addrlen) =
            socket_utils::autobind_helper(fd_client, libc::AF_INET);
        let (_server_addr, _server_addrlen) =
            socket_utils::autobind_helper(fd_server, libc::AF_INET);

        // PART 1: connect other -> client

        // connect other to the client address
        let rv = socket_utils::connect_to_peername(fd_other, fd_client);
        assert_eq!(rv, 0);

        // PART 2: send message other -> client and recv (success)

        // send a message from other to the client
        nix::sys::socket::send(fd_other, &[1, 2, 3], nix::sys::socket::MsgFlags::empty()).unwrap();

        // shadow needs to run events
        let rv = unsafe { libc::usleep(2000) };
        assert_eq!(rv, 0);

        // can successfully receive bytes from the other socket
        let mut buffer = [0u8; 20];
        let rv =
            nix::sys::socket::recv(fd_client, &mut buffer, nix::sys::socket::MsgFlags::empty());
        assert_eq!(rv, Ok(3));
        assert_eq!(&buffer[..3], &[1, 2, 3]);

        // PART 3: connect client -> server

        // connect client to the server address (this adds a peer to the client, which means the
        // client should only be able to receive packets from the server)
        let rv = socket_utils::connect_to_peername(fd_client, fd_server);
        assert_eq!(rv, 0);

        // PART 4: send message other -> client and recv (fail)

        // send another message from other to the client
        nix::sys::socket::send(fd_other, &[4, 5, 6], nix::sys::socket::MsgFlags::empty()).unwrap();

        // shadow needs to run events
        let rv = unsafe { libc::usleep(2000) };
        assert_eq!(rv, 0);

        // can no longer receive bytes from the other socket
        let rv = nix::sys::socket::recv(
            fd_client,
            &mut [0u8; 20][..],
            nix::sys::socket::MsgFlags::empty(),
        );
        assert_eq!(rv, Err(Errno::EAGAIN));

        // PART 5: connect server -> client

        // connect server to the client address
        let rv = socket_utils::connect_to_peername(fd_server, fd_client);
        assert_eq!(rv, 0);

        // PART 6: send message server -> client (success)

        // send a message from server to the client
        nix::sys::socket::send(fd_server, &[7, 8, 9], nix::sys::socket::MsgFlags::empty()).unwrap();

        // shadow needs to run events
        let rv = unsafe { libc::usleep(2000) };
        assert_eq!(rv, 0);

        // can successfully receive bytes from the server socket
        let mut buffer = [0u8; 20];
        let rv =
            nix::sys::socket::recv(fd_client, &mut buffer, nix::sys::socket::MsgFlags::empty());
        assert_eq!(rv, Ok(3));
        assert_eq!(&buffer[..3], &[7, 8, 9]);

        Ok(())
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

/// Test connect() on a listening socket.
fn test_listening(
    domain: libc::c_int,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type | flag, 0) };
    assert!(fd >= 0);

    // unix sockets must be bound before you can call `listen()`
    if domain == libc::AF_UNIX {
        let (_addr, _addr_len) = socket_utils::autobind_helper(fd, domain);
    }

    {
        let rv = unsafe { libc::listen(fd, 10) };
        eprintln!("errno: {}", test_utils::get_errno());
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

    let expected_errno = match domain {
        libc::AF_INET => libc::EISCONN,
        libc::AF_UNIX => libc::EINVAL,
        _ => unimplemented!(),
    };

    check_connect_call(&args, Some(expected_errno))
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

// Test the behavior of loopback-bound sockets when connect() is used with an external address
fn test_loopback_bound_connect(sock_type: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    assert!(fd >= 0);

    // bind the socket to loopback
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
                fd,
                std::ptr::from_ref(&addr) as *const libc::sockaddr,
                std::mem::size_of_val(&addr) as u32,
            )
        };
        eprintln!("errno: {}", test_utils::get_errno());
        assert_eq!(rv, 0);
    }

    let other_ip: std::net::Ipv4Addr = if test_utils::running_in_shadow() {
        // this IP is the IP for the host 'othernode' in the shadow config file
        "26.153.52.74".parse().unwrap()
    } else {
        // if running outside of shadow, we use a local network address here so that the tests
        // running outside of shadow would only be trying to connect to a server on a local
        // network rather than some random server on the internet
        "192.168.1.100".parse().unwrap()
    };

    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: u32::from(other_ip).to_be(),
        },
        sin_zero: [0; 8],
    };

    let args = ConnectArguments {
        fd,
        addr: Some(SockAddr::Inet(addr)),
        addr_len: std::mem::size_of_val(&addr) as u32,
    };

    check_connect_call(&args, Some(libc::EINVAL))
}

// Test the behavior of loopback-bound sockets when connect() is used with an external address
fn test_loopback_listening_connect(
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    assert!(fd >= 0);

    // bind the socket to loopback
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
                fd,
                std::ptr::from_ref(&addr) as *const libc::sockaddr,
                std::mem::size_of_val(&addr) as u32,
            )
        };
        eprintln!("errno: {}", test_utils::get_errno());
        assert_eq!(rv, 0);
    }

    {
        let rv = unsafe { libc::listen(fd, 10) };
        eprintln!("errno: {}", test_utils::get_errno());
        assert_eq!(rv, 0);
    }

    let other_ip: std::net::Ipv4Addr = if test_utils::running_in_shadow() {
        // this IP is the IP for the host 'othernode' in the shadow config file
        "26.153.52.74".parse().unwrap()
    } else {
        // if running outside of shadow, we use a local network address here so that the tests
        // running outside of shadow would only be trying to connect to a server on a local
        // network rather than some random server on the internet
        "192.168.1.100".parse().unwrap()
    };

    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: u32::from(other_ip).to_be(),
        },
        sin_zero: [0; 8],
    };

    let args = ConnectArguments {
        fd,
        addr: Some(SockAddr::Inet(addr)),
        addr_len: std::mem::size_of_val(&addr) as u32,
    };

    check_connect_call(&args, Some(libc::EISCONN))
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
