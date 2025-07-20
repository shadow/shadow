/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use test_utils::TestEnvironment as TestEnv;
use test_utils::socket_utils::{
    SockAddr, SocketInitMethod, autobind_helper, socket_init_helper, stream_connect_helper,
};
use test_utils::{AsMutPtr, set};

struct GetpeernameArguments {
    fd: libc::c_int,
    addr: Option<SockAddr>, // if None, a null pointer should be used
    addr_len: Option<libc::socklen_t>, // if None, a null pointer should be used
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

    tests.extend(vec![
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
            "test_short_len_inet",
            test_short_len_inet,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
    ]);

    let domains = [libc::AF_INET, libc::AF_UNIX];

    for &domain in domains.iter() {
        // add details to the test names to avoid duplicates
        let append_args = |s| format!("{s} <domain={domain:?}>");

        // only inet/inet6 dgram sockets can be connected to a non-existent address
        if [libc::AF_INET, libc::AF_INET6].contains(&domain) {
            tests.extend(vec![test_utils::ShadowTest::new(
                &append_args("test_connected_dgram_socket"),
                move || test_connected_dgram_socket(domain),
                set![TestEnv::Libc, TestEnv::Shadow],
            )]);
        }

        let sock_types = match domain {
            libc::AF_INET => &[libc::SOCK_STREAM, libc::SOCK_DGRAM][..],
            libc::AF_UNIX => &[libc::SOCK_STREAM, libc::SOCK_DGRAM, libc::SOCK_SEQPACKET][..],
            _ => unimplemented!(),
        };

        for &sock_type in sock_types.iter() {
            // add details to the test names to avoid duplicates
            let append_args = |s| format!("{s} <domain={domain:?}, sock_type={sock_type}>");

            tests.extend(vec![
                test_utils::ShadowTest::new(
                    &append_args("test_non_connected_fd"),
                    move || test_non_connected_fd(domain, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_unbound_socket"),
                    move || test_unbound_socket(domain, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_bound_socket"),
                    move || test_bound_socket(domain, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
            ]);

            // if a connection-based socket
            if [libc::SOCK_STREAM, libc::SOCK_SEQPACKET].contains(&sock_type) {
                tests.extend(vec![
                    test_utils::ShadowTest::new(
                        &append_args("test_listening_socket"),
                        move || test_listening_socket(domain, sock_type),
                        set![TestEnv::Libc, TestEnv::Shadow],
                    ),
                    test_utils::ShadowTest::new(
                        &append_args("test_connected_before_accepted"),
                        move || test_connected_before_accepted(domain, sock_type),
                        set![TestEnv::Libc, TestEnv::Shadow],
                    ),
                ]);
            }
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
            // add details to the test names to avoid duplicates
            let append_args = |s| format!("{s} <init_method={method:?}, sock_type={sock_type}>");

            tests.extend(vec![
                test_utils::ShadowTest::new(
                    &append_args("test_null_addr"),
                    move || test_null_addr(method, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_null_len"),
                    move || test_null_len(method, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_zero_len"),
                    move || test_zero_len(method, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_after_close"),
                    move || test_after_close(method, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_connected_socket <close_peer=false>"),
                    move || test_connected_socket(method, sock_type, false),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_connected_socket <close_peer=true>"),
                    move || test_connected_socket(method, sock_type, true),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_peer_socket <bind_client=false>"),
                    move || test_peer_socket(method, sock_type, false),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_peer_socket <bind_client=true>"),
                    move || test_peer_socket(method, sock_type, true),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
            ]);

            // if a connection-based socket
            if [libc::SOCK_STREAM, libc::SOCK_SEQPACKET].contains(&sock_type) {
                tests.extend(vec![test_utils::ShadowTest::new(
                    &append_args("test_sockname_peername"),
                    move || test_sockname_peername(method, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                )]);
            }
        }
    }

    tests
}

/// Test getpeername using an argument that cannot be a fd.
fn test_invalid_fd() -> Result<(), String> {
    let addr: libc::sockaddr_storage = unsafe { std::mem::zeroed() };

    // getpeername() may mutate addr and addr_len
    let mut args = GetpeernameArguments {
        fd: -1,
        addr: Some(SockAddr::Generic(addr)),
        addr_len: Some(std::mem::size_of_val(&addr) as u32),
    };

    check_getpeername_call(&mut args, Some(libc::EBADF))
}

/// Test getpeername using an argument that could be a fd, but is not.
fn test_non_existent_fd() -> Result<(), String> {
    let addr: libc::sockaddr_storage = unsafe { std::mem::zeroed() };

    // getpeername() may mutate addr and addr_len
    let mut args = GetpeernameArguments {
        fd: 8934,
        addr: Some(SockAddr::Generic(addr)),
        addr_len: Some(std::mem::size_of_val(&addr) as u32),
    };

    check_getpeername_call(&mut args, Some(libc::EBADF))
}

/// Test getpeername using a valid fd that is not a socket.
fn test_non_socket_fd() -> Result<(), String> {
    let addr: libc::sockaddr_storage = unsafe { std::mem::zeroed() };

    // getpeername() may mutate addr and addr_len
    let mut args = GetpeernameArguments {
        fd: 0, // assume the fd 0 is already open and is not a socket
        addr: Some(SockAddr::Generic(addr)),
        addr_len: Some(std::mem::size_of_val(&addr) as u32),
    };

    check_getpeername_call(&mut args, Some(libc::ENOTSOCK))
}

/// Test getpeername using a valid fd, but that is not connected to a peer.
fn test_non_connected_fd(domain: libc::c_int, sock_type: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type, 0) };
    assert!(fd >= 0);

    let addr: libc::sockaddr_storage = unsafe { std::mem::zeroed() };

    // getpeername() may mutate addr and addr_len
    let mut args = GetpeernameArguments {
        fd,
        addr: Some(SockAddr::Generic(addr)),
        addr_len: Some(std::mem::size_of_val(&addr) as u32),
    };

    test_utils::run_and_close_fds(&[fd], || {
        check_getpeername_call(&mut args, Some(libc::ENOTCONN))
    })
}

/// Test getpeername using a valid fd, but with a NULL address.
fn test_null_addr(method: SocketInitMethod, sock_type: libc::c_int) -> Result<(), String> {
    let (fd_client, fd_peer) =
        socket_init_helper(method, sock_type, 0, /* bind_client= */ false);

    // getpeername() may mutate addr and addr_len
    let mut args = GetpeernameArguments {
        fd: fd_client,
        addr: None,
        addr_len: Some(5),
    };

    test_utils::run_and_close_fds(&[fd_client, fd_peer], || {
        check_getpeername_call(&mut args, Some(libc::EFAULT))
    })
}

/// Test getpeername using a valid fd and address, a NULL address length.
fn test_null_len(method: SocketInitMethod, sock_type: libc::c_int) -> Result<(), String> {
    let (fd_client, fd_peer) =
        socket_init_helper(method, sock_type, 0, /* bind_client= */ false);

    let addr: libc::sockaddr_storage = unsafe { std::mem::zeroed() };

    // getpeername() may mutate addr and addr_len
    let mut args = GetpeernameArguments {
        fd: fd_client,
        addr: Some(SockAddr::Generic(addr)),
        addr_len: None,
    };

    test_utils::run_and_close_fds(&[fd_client, fd_peer], || {
        check_getpeername_call(&mut args, Some(libc::EFAULT))
    })
}

/// Test getpeername using a valid TCP socket and address, but an address length that is too small.
fn test_short_len_inet() -> Result<(), String> {
    let (fd_client, fd_peer) = socket_init_helper(
        SocketInitMethod::Inet,
        libc::SOCK_STREAM,
        0,
        /* bind_client= */ false,
    );

    // the sockaddr that we expect to have after calling getpeername()
    let expected_addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        // we don't know the port (we'll skip checking this value later)
        sin_port: 0u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        // since our buffer will be short by one byte, we will only be missing one byte of sin_zero
        sin_zero: [0, 0, 0, 0, 0, 0, 0, 1],
    };

    // fill the sockaddr with dummy data
    let addr = libc::sockaddr_in {
        sin_family: 123u16,
        sin_port: 456u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: 789u32.to_be(),
        },
        sin_zero: [1; 8],
    };

    // getpeername() may mutate addr and addr_len
    let mut args = GetpeernameArguments {
        fd: fd_client,
        addr: Some(SockAddr::Inet(addr)),
        addr_len: Some((std::mem::size_of_val(&addr) - 1) as u32),
    };

    // if the buffer was too small, the returned data will be truncated but we won't get an error
    test_utils::run_and_close_fds(&[fd_client, fd_peer], || {
        check_getpeername_call(&mut args, None)
    })?;

    // check that the returned length is expected
    test_utils::result_assert_eq(
        args.addr_len.unwrap() as usize,
        std::mem::size_of_val(&addr),
        "Unexpected addr length",
    )?;

    // check that the returned address is expected
    inet_sockaddr_check_equal(
        args.addr.unwrap().as_inet().unwrap(),
        &expected_addr,
        /* ignore_port= */ true,
    )?;

    // check that the port is valid
    test_utils::result_assert(
        args.addr.unwrap().as_inet().unwrap().sin_port > 0,
        "Unexpected port",
    )
}

/// Test getpeername using a valid fd and address, but an address length of 0.
fn test_zero_len(method: SocketInitMethod, sock_type: libc::c_int) -> Result<(), String> {
    let (fd_client, fd_peer) =
        socket_init_helper(method, sock_type, 0, /* bind_client= */ false);

    // fill the sockaddr with dummy data
    let addr: Vec<u8> = (0..(std::mem::size_of::<libc::sockaddr_storage>() as u8)).collect();
    let addr: libc::sockaddr_storage =
        unsafe { std::ptr::read_unaligned(addr.as_ptr() as *const _) };

    // the sockaddr that we expect to have after calling getpeername();
    let expected_addr = addr;

    // getpeername() may mutate addr and addr_len
    let mut args = GetpeernameArguments {
        fd: fd_client,
        addr: Some(SockAddr::Generic(addr)),
        addr_len: Some(0u32),
    };

    // if the buffer was too small, the returned data will be truncated but we won't get an error
    test_utils::run_and_close_fds(&[fd_client, fd_peer], || {
        check_getpeername_call(&mut args, None)
    })?;

    // the expected length of the client socket address
    let expected_addr_len = match method {
        SocketInitMethod::Inet => std::mem::size_of::<libc::sockaddr_in>() as libc::socklen_t,
        // address family + null byte + 5-byte abstract address (see unix(7))
        SocketInitMethod::Unix => 2 + 1 + 5,
        // address family
        SocketInitMethod::UnixSocketpair => 2,
    };

    // check that the returned length is expected
    test_utils::result_assert_eq(
        args.addr_len.unwrap(),
        expected_addr_len,
        "Unexpected addr length",
    )?;

    // check that the returned address is expected
    test_utils::result_assert_eq(
        args.addr.unwrap().as_generic().unwrap(),
        &expected_addr,
        "Address was changed",
    )
}

/// Test getpeername on a listening socket.
fn test_listening_socket(domain: libc::c_int, sock_type: libc::c_int) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(domain, sock_type, 0) };
    let fd_server = unsafe { libc::socket(domain, sock_type, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    // bind the server socket to some unused address
    let (server_addr, server_addr_len) = autobind_helper(fd_server, domain);

    // connect the client to the server and get the accepted socket
    let fd_peer = stream_connect_helper(
        fd_client,
        fd_server,
        server_addr,
        server_addr_len,
        /* flags= */ 0,
    );

    let addr: libc::sockaddr_storage = unsafe { std::mem::zeroed() };

    // getpeername() may mutate addr and addr_len
    let mut args = GetpeernameArguments {
        fd: fd_server,
        addr: Some(SockAddr::Generic(addr)),
        addr_len: Some(std::mem::size_of_val(&addr) as u32),
    };

    test_utils::run_and_close_fds(&[fd_server, fd_client, fd_peer], || {
        check_getpeername_call(&mut args, Some(libc::ENOTCONN))
    })
}

/// Test getpeername after closing the socket.
fn test_after_close(method: SocketInitMethod, sock_type: libc::c_int) -> Result<(), String> {
    let (fd_client, fd_peer) =
        socket_init_helper(method, sock_type, 0, /* bind_client= */ false);

    // close the socket
    {
        let rv = unsafe { libc::close(fd_client) };
        assert_eq!(rv, 0);
    }

    let addr: libc::sockaddr_storage = unsafe { std::mem::zeroed() };

    // getpeername() may mutate addr and addr_len
    let mut args = GetpeernameArguments {
        fd: fd_client,
        addr: Some(SockAddr::Generic(addr)),
        addr_len: Some(std::mem::size_of_val(&addr) as u32),
    };

    test_utils::run_and_close_fds(&[fd_peer], || {
        check_getpeername_call(&mut args, Some(libc::EBADF))
    })
}

/// Test getpeername using an unbound socket.
fn test_unbound_socket(domain: libc::c_int, sock_type: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type, 0) };
    assert!(fd >= 0);

    let addr: libc::sockaddr_storage = unsafe { std::mem::zeroed() };

    // getpeername() may mutate addr and addr_len
    let mut args = GetpeernameArguments {
        fd,
        addr: Some(SockAddr::Generic(addr)),
        addr_len: Some(std::mem::size_of_val(&addr) as u32),
    };

    test_utils::run_and_close_fds(&[fd], || {
        check_getpeername_call(&mut args, Some(libc::ENOTCONN))
    })
}

/// Test getpeername using a bound socket.
fn test_bound_socket(domain: libc::c_int, sock_type: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type, 0) };
    assert!(fd >= 0);

    // bind on some unused address
    autobind_helper(fd, domain);

    let addr: libc::sockaddr_storage = unsafe { std::mem::zeroed() };

    // getpeername() may mutate addr and addr_len
    let mut args = GetpeernameArguments {
        fd,
        addr: Some(SockAddr::Generic(addr)),
        addr_len: Some(std::mem::size_of_val(&addr) as u32),
    };

    test_utils::run_and_close_fds(&[fd], || {
        check_getpeername_call(&mut args, Some(libc::ENOTCONN))
    })
}

/// Test getpeername using a datagram socket "connected" to a non-existent address.
fn test_connected_dgram_socket(domain: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, libc::SOCK_DGRAM, 0) };
    assert!(fd >= 0);

    // some server address
    let (bind_addr, bind_addr_len) = match domain {
        libc::AF_INET => {
            let addr = libc::sockaddr_in {
                sin_family: libc::AF_INET as u16,
                // arbitrary port number
                sin_port: 11111u16.to_be(),
                sin_addr: libc::in_addr {
                    s_addr: libc::INADDR_LOOPBACK.to_be(),
                },
                sin_zero: [0; 8],
            };
            (SockAddr::Inet(addr), std::mem::size_of_val(&addr) as u32)
        }
        libc::AF_UNIX => {
            let mut addr = libc::sockaddr_un {
                sun_family: libc::AF_UNIX as u16,
                sun_path: [0; 108],
            };
            // arbitrary abstract socket name
            addr.sun_path[1] = 4;
            addr.sun_path[2] = 7;
            (SockAddr::Unix(addr), 2 + 1 + 2) // address family + null byte + 2 characters
        }
        _ => unimplemented!(),
    };

    // connect to the address
    {
        let rv = unsafe { libc::connect(fd, bind_addr.as_ptr(), bind_addr_len) };
        assert_eq!(rv, 0);
    }

    // an empty sockaddr
    let addr: libc::sockaddr_storage = unsafe { std::mem::zeroed() };

    // getpeername() may mutate addr and addr_len
    let mut args = GetpeernameArguments {
        fd,
        addr: Some(SockAddr::Generic(addr)),
        addr_len: Some(std::mem::size_of_val(&addr) as u32),
    };

    test_utils::run_and_close_fds(&[fd], || check_getpeername_call(&mut args, None))?;

    // check that the returned length is expected
    test_utils::result_assert_eq(
        args.addr_len.unwrap(),
        bind_addr_len,
        "Unexpected addr length",
    )?;

    // check that the returned address is expected
    test_utils::result_assert_eq(
        &args.addr.unwrap().as_slice()[..bind_addr_len as usize],
        &bind_addr.as_slice()[..bind_addr_len as usize],
        "Unexpected address",
    )
}

/// Test getpeername on a socket that has connected but not yet been accepted.
fn test_connected_before_accepted(
    domain: libc::c_int,
    sock_type: libc::c_int,
) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(domain, sock_type, 0) };
    let fd_server = unsafe { libc::socket(domain, sock_type, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    let (server_addr, server_addr_len) = autobind_helper(fd_server, domain);

    // listen for connections
    {
        let rv = unsafe { libc::listen(fd_server, 0) };
        assert_eq!(rv, 0);
    }

    // connect to the server address
    {
        let rv = unsafe { libc::connect(fd_client, server_addr.as_ptr(), server_addr_len) };
        assert_eq!(rv, 0);
    }

    // an empty sockaddr
    let addr: libc::sockaddr_storage = unsafe { std::mem::zeroed() };

    // client socket arguments for getpeername()
    let mut args = GetpeernameArguments {
        fd: fd_client,
        addr: Some(SockAddr::Generic(addr)),
        addr_len: Some(std::mem::size_of_val(&addr) as u32),
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        check_getpeername_call(&mut args, None)
    })?;

    // check that the returned length is expected
    test_utils::result_assert_eq(
        args.addr_len.unwrap(),
        server_addr_len,
        "Unexpected addr length",
    )?;

    // check that the returned server address is expected
    test_utils::result_assert_eq(
        &args.addr.unwrap().as_slice()[..server_addr_len as usize],
        &server_addr.as_slice()[..server_addr_len as usize],
        "Unexpected address",
    )?;

    Ok(())
}

/// Test getpeername using a socket connected on loopback.
fn test_connected_socket(
    method: SocketInitMethod,
    sock_type: libc::c_int,
    close_peer: bool,
) -> Result<(), String> {
    let (fd_client, fd_peer) =
        socket_init_helper(method, sock_type, 0, /* bind_client= */ false);

    // test getpeername() after the peer has been closed
    if close_peer {
        nix::unistd::close(fd_peer).unwrap();
    }

    // fill the sockaddr with dummy data
    let addr = match method.domain() {
        libc::AF_INET => SockAddr::dummy_init_inet(),
        libc::AF_UNIX => SockAddr::dummy_init_unix(),
        _ => unimplemented!(),
    };

    // client arguments for getpeername()
    let mut args = GetpeernameArguments {
        fd: fd_client,
        // fill the sockaddr with dummy data
        addr: Some(addr),
        addr_len: Some(addr.ptr_size()),
    };

    let to_close = if close_peer {
        // already closed peer earlier
        vec![fd_client]
    } else {
        vec![fd_client, fd_peer]
    };

    test_utils::run_and_close_fds(&to_close, || check_getpeername_call(&mut args, None))?;

    // check the returned address
    match method {
        SocketInitMethod::Inet => {
            // check that the returned length is expected
            test_utils::result_assert_eq(
                args.addr_len.unwrap() as usize,
                std::mem::size_of::<libc::sockaddr_in>(),
                "Unexpected addr length",
            )?;

            // check that the returned client address is expected
            inet_sockaddr_check_equal(
                args.addr.unwrap().as_inet().unwrap(),
                &libc::sockaddr_in {
                    sin_family: libc::AF_INET as u16,
                    // we don't know the port
                    sin_port: 0u16.to_be(),
                    sin_addr: libc::in_addr {
                        s_addr: libc::INADDR_LOOPBACK.to_be(),
                    },
                    sin_zero: [0; 8],
                },
                /* ignore_port= */ true,
            )?;

            // check that the port is valid
            test_utils::result_assert(
                args.addr.unwrap().as_inet().unwrap().sin_port > 0,
                "Unexpected port",
            )?;
        }
        SocketInitMethod::Unix => {
            // check that the returned length is expected
            test_utils::result_assert_eq(
                args.addr_len.unwrap(),
                // address family + null byte + 5-byte abstract address (see unix(7))
                2 + 1 + 5,
                "Unexpected addr length",
            )?;

            test_utils::result_assert_eq(
                args.addr.unwrap().as_unix().unwrap().sun_family,
                libc::AF_UNIX as u16,
                "Address family was not AF_UNIX",
            )?;

            test_utils::result_assert_eq(
                args.addr.unwrap().as_unix().unwrap().sun_path[0],
                0,
                "Abstract socket name did not begin with null byte",
            )?;

            // unix sockets that are auto-bound have names with 5 bytes on linux; see unix(7)
            test_utils::result_assert(
                !args.addr.unwrap().as_unix().unwrap().sun_path[1..6]
                    .iter()
                    .all(|&x| x == 0),
                "Abstract socket name was empty",
            )?;
        }
        SocketInitMethod::UnixSocketpair => {
            // check that the returned length is expected
            test_utils::result_assert_eq(
                args.addr_len.unwrap(),
                // address family
                2,
                "Unexpected addr length",
            )?;

            test_utils::result_assert_eq(
                args.addr.unwrap().as_unix().unwrap().sun_family,
                libc::AF_UNIX as u16,
                "Address family was not AF_UNIX",
            )?;
        }
    }

    Ok(())
}

/// Test getpeername using the peer's socket (the accepted socket for connection-based sockets).
fn test_peer_socket(
    method: SocketInitMethod,
    sock_type: libc::c_int,
    bind_client: bool,
) -> Result<(), String> {
    let (fd_client, fd_peer) = socket_init_helper(method, sock_type, 0, bind_client);

    // fill the sockaddr with dummy data
    let addr = match method.domain() {
        libc::AF_INET => SockAddr::dummy_init_inet(),
        libc::AF_UNIX => SockAddr::dummy_init_unix(),
        _ => unimplemented!(),
    };

    // client arguments for getpeername()
    let mut args = GetpeernameArguments {
        fd: fd_peer,
        addr: Some(addr),
        addr_len: Some(addr.ptr_size()),
    };

    let expected_errno =
        if sock_type == libc::SOCK_DGRAM && method != SocketInitMethod::UnixSocketpair {
            Some(libc::ENOTCONN)
        } else {
            None
        };

    test_utils::run_and_close_fds(&[fd_client, fd_peer], || {
        check_getpeername_call(&mut args, expected_errno)
    })?;

    // if the getpeername call failed (as expected)
    if expected_errno.is_some() {
        return Ok(());
    }

    // check the returned address
    match (method, bind_client) {
        (SocketInitMethod::Inet, _) => {
            // check that the returned length is expected
            test_utils::result_assert_eq(
                args.addr_len.unwrap() as usize,
                std::mem::size_of::<libc::sockaddr_in>(),
                "Unexpected addr length",
            )?;

            // check that the returned client address is expected
            inet_sockaddr_check_equal(
                args.addr.unwrap().as_inet().unwrap(),
                &libc::sockaddr_in {
                    sin_family: libc::AF_INET as u16,
                    // we don't know the port
                    sin_port: 0u16.to_be(),
                    sin_addr: libc::in_addr {
                        s_addr: libc::INADDR_LOOPBACK.to_be(),
                    },
                    sin_zero: [0; 8],
                },
                /* ignore_port= */ true,
            )?;

            // check that the port is valid
            test_utils::result_assert(
                args.addr.unwrap().as_inet().unwrap().sin_port > 0,
                "Unexpected port",
            )?;
        }
        (SocketInitMethod::Unix, true) => {
            // check that the returned length is expected
            test_utils::result_assert_eq(
                args.addr_len.unwrap(),
                // address family + null byte + 5-byte abstract address (see unix(7))
                2 + 1 + 5,
                "Unexpected addr length",
            )?;

            test_utils::result_assert_eq(
                args.addr.unwrap().as_unix().unwrap().sun_family,
                libc::AF_UNIX as u16,
                "Address family was not AF_UNIX",
            )?;

            test_utils::result_assert_eq(
                args.addr.unwrap().as_unix().unwrap().sun_path[0],
                0,
                "Abstract socket name did not begin with null byte",
            )?;

            // unix sockets that are auto-bound have names with 5 bytes on linux; see unix(7)
            test_utils::result_assert(
                !args.addr.unwrap().as_unix().unwrap().sun_path[1..6]
                    .iter()
                    .all(|&x| x == 0),
                "Abstract socket name was empty",
            )?;
        }
        (SocketInitMethod::UnixSocketpair, _) | (SocketInitMethod::Unix, false) => {
            // check that the returned length is expected
            test_utils::result_assert_eq(
                args.addr_len.unwrap(),
                // address family
                2,
                "Unexpected addr length",
            )?;

            test_utils::result_assert_eq(
                args.addr.unwrap().as_unix().unwrap().sun_family,
                libc::AF_UNIX as u16,
                "Address family was not AF_UNIX",
            )?;
        }
    }

    Ok(())
}

/// Test that getpeername and getsockname return the same results for connection-oriented sockets.
fn test_sockname_peername(method: SocketInitMethod, sock_type: libc::c_int) -> Result<(), String> {
    if method != SocketInitMethod::UnixSocketpair {
        let fd_client = unsafe { libc::socket(method.domain(), sock_type, 0) };
        let fd_server = unsafe { libc::socket(method.domain(), sock_type, 0) };
        assert!(fd_client >= 0);
        assert!(fd_server >= 0);

        // bind the server socket to some unused address
        let (server_addr, server_addr_len) = autobind_helper(fd_server, method.domain());

        // connect the client to the server and get the accepted socket
        let fd_peer = stream_connect_helper(
            fd_client,
            fd_server,
            server_addr,
            server_addr_len,
            /* flags= */ 0,
        );

        // compare getsockname on the first argument to getpeername on the second
        compare_sockname_peername(fd_client, fd_peer)?;
        compare_sockname_peername(fd_peer, fd_client)?;
        compare_sockname_peername(fd_server, fd_client)?;
    } else {
        let (fd_client, fd_peer) = socket_init_helper(method, method.domain(), sock_type, false);

        compare_sockname_peername(fd_client, fd_peer)?;
        compare_sockname_peername(fd_peer, fd_client)?;
    }

    Ok(())
}

/// Run getsockname on one fd and getpeername on another fd, and make sure they match.
fn compare_sockname_peername(
    fd_sockname: libc::c_int,
    fd_peername: libc::c_int,
) -> Result<(), String> {
    // fill the sockaddr with dummy data
    let sockname_addr: Vec<u8> =
        (0..(std::mem::size_of::<libc::sockaddr_storage>() as u8)).collect();
    let mut sockname_addr: libc::sockaddr_storage =
        unsafe { std::ptr::read_unaligned(sockname_addr.as_ptr() as *const _) };

    let mut sockname_len = std::mem::size_of_val(&sockname_addr) as u32;

    {
        let rv = unsafe {
            libc::getsockname(
                fd_sockname,
                std::ptr::from_mut(&mut sockname_addr) as *mut libc::sockaddr,
                std::ptr::from_mut(&mut sockname_len),
            )
        };
        assert_eq!(rv, 0);
        assert!(sockname_len <= std::mem::size_of_val(&sockname_addr) as u32);
    }

    // fill the sockaddr with dummy data
    let peername_addr: Vec<u8> = (0..(std::mem::size_of::<libc::sockaddr_storage>() as u8))
        .rev()
        .collect();
    let mut peername_addr: libc::sockaddr_storage =
        unsafe { std::ptr::read_unaligned(peername_addr.as_ptr() as *const _) };

    let mut peername_len = std::mem::size_of_val(&peername_addr) as u32;

    {
        let rv = unsafe {
            libc::getpeername(
                fd_peername,
                std::ptr::from_mut(&mut peername_addr) as *mut libc::sockaddr,
                std::ptr::from_mut(&mut peername_len),
            )
        };
        assert_eq!(rv, 0);
        assert!(peername_len <= std::mem::size_of_val(&peername_addr) as u32);
    }

    let sockname_addr = SockAddr::Generic(sockname_addr);
    let peername_addr = SockAddr::Generic(peername_addr);

    // check that they are the same
    assert_eq!(sockname_len, peername_len);
    assert_eq!(
        sockname_addr.as_slice()[..(sockname_len as usize)],
        peername_addr.as_slice()[..(peername_len as usize)]
    );

    Ok(())
}

fn inet_sockaddr_check_equal(
    a: &libc::sockaddr_in,
    b: &libc::sockaddr_in,
    ignore_port: bool,
) -> Result<(), String> {
    test_utils::result_assert_eq(a.sin_family, b.sin_family, "Unexpected family")?;
    if !ignore_port {
        test_utils::result_assert_eq(a.sin_port, b.sin_port, "Unexpected port")?;
    }
    test_utils::result_assert_eq(a.sin_addr.s_addr, b.sin_addr.s_addr, "Unexpected address")?;
    test_utils::result_assert_eq(a.sin_zero, b.sin_zero, "Unexpected padding")?;
    Ok(())
}

fn check_getpeername_call(
    args: &mut GetpeernameArguments,
    expected_errno: Option<libc::c_int>,
) -> Result<(), String> {
    let (addr_ptr, addr_max_len) = match args.addr {
        Some(ref mut x) => (x.as_mut_ptr(), x.ptr_size()),
        None => (std::ptr::null_mut(), 0),
    };

    // if the pointers will be non-null, make sure the length is not greater than the actual data size
    // so that we don't segfault
    if args.addr.is_some() && args.addr_len.is_some() {
        assert!(args.addr_len.unwrap() <= addr_max_len);
    }

    // will modify args.addr and args.addr_len
    let rv = unsafe { libc::getpeername(args.fd, addr_ptr, args.addr_len.as_mut_ptr()) };

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
