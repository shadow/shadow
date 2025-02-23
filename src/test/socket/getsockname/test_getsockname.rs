/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use test_utils::TestEnvironment as TestEnv;
use test_utils::socket_utils::{
    SockAddr, SocketInitMethod, autobind_helper, dgram_connect_helper, stream_connect_helper,
};
use test_utils::{AsMutPtr, set};

struct GetsocknameArguments {
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
        let sock_types = match domain {
            libc::AF_INET => &[libc::SOCK_STREAM, libc::SOCK_DGRAM][..],
            libc::AF_UNIX => &[libc::SOCK_STREAM, libc::SOCK_DGRAM, libc::SOCK_SEQPACKET][..],
            _ => unimplemented!(),
        };

        for &sock_type in sock_types.iter() {
            // add details to the test names to avoid duplicates
            let append_args = |s| format!("{} <domain={:?}, sock_type={}>", s, domain, sock_type);

            tests.extend(vec![
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
                test_utils::ShadowTest::new(
                    &append_args("test_autobound_socket"),
                    move || test_autobound_socket(domain, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_after_close"),
                    move || test_after_close(domain, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_bound_connected_socket"),
                    move || test_bound_connected_socket(domain, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
            ]);

            if [libc::SOCK_STREAM, libc::SOCK_SEQPACKET].contains(&sock_type) {
                tests.extend(vec![test_utils::ShadowTest::new(
                    &append_args("test_implicit_bind"),
                    move || test_implicit_bind(domain, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                )]);
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
            let append_args =
                |s| format!("{} <init_method={:?}, sock_type={}>", s, method, sock_type);

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
            ]);
        }
    }

    tests
}

/// Test getsockname using an argument that cannot be a fd.
fn test_invalid_fd() -> Result<(), String> {
    let addr: libc::sockaddr_storage = unsafe { std::mem::zeroed() };

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd: -1,
        addr: Some(SockAddr::Generic(addr)),
        addr_len: Some(std::mem::size_of_val(&addr) as u32),
    };

    check_getsockname_call(&mut args, Some(libc::EBADF))
}

/// Test getsockname using an argument that could be a fd, but is not.
fn test_non_existent_fd() -> Result<(), String> {
    let addr: libc::sockaddr_storage = unsafe { std::mem::zeroed() };

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd: 8934,
        addr: Some(SockAddr::Generic(addr)),
        addr_len: Some(std::mem::size_of_val(&addr) as u32),
    };

    check_getsockname_call(&mut args, Some(libc::EBADF))
}

/// Test getsockname using a valid fd that is not a socket.
fn test_non_socket_fd() -> Result<(), String> {
    let addr: libc::sockaddr_storage = unsafe { std::mem::zeroed() };

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd: 0, // assume the fd 0 is already open and is not a socket
        addr: Some(SockAddr::Generic(addr)),
        addr_len: Some(std::mem::size_of_val(&addr) as u32),
    };

    check_getsockname_call(&mut args, Some(libc::ENOTSOCK))
}

/// Test getsockname using a valid fd, but with a NULL address.
fn test_null_addr(method: SocketInitMethod, sock_type: libc::c_int) -> Result<(), String> {
    let fd = socket_helper(method, sock_type);

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd,
        addr: None,
        addr_len: Some(5),
    };

    test_utils::run_and_close_fds(&[fd], || {
        check_getsockname_call(&mut args, Some(libc::EFAULT))
    })
}

/// Test getsockname using a valid fd and address, a NULL address length.
fn test_null_len(method: SocketInitMethod, sock_type: libc::c_int) -> Result<(), String> {
    let fd = socket_helper(method, sock_type);

    let addr: libc::sockaddr_storage = unsafe { std::mem::zeroed() };

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd,
        addr: Some(SockAddr::Generic(addr)),
        addr_len: None,
    };

    test_utils::run_and_close_fds(&[fd], || {
        check_getsockname_call(&mut args, Some(libc::EFAULT))
    })
}

/// Test getsockname using a valid fd and address, but an address length that is too small.
fn test_short_len_inet() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
    assert!(fd >= 0);

    // the sockaddr that we expect to have after calling getsockname()
    let expected_addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 0u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: 0u32.to_be(),
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
    let mut args = GetsocknameArguments {
        fd,
        addr: Some(SockAddr::Inet(addr)),
        addr_len: Some((std::mem::size_of_val(&addr) - 1) as u32),
    };

    // if the buffer was too small, the returned data will be truncated but we won't get an error
    test_utils::run_and_close_fds(&[fd], || check_getsockname_call(&mut args, None))?;

    // check that the returned length is expected
    test_utils::result_assert_eq(
        args.addr_len.unwrap() as usize,
        std::mem::size_of::<libc::sockaddr_in>(),
        "Unexpected addr length",
    )?;

    // check that the returned address is expected
    inet_sockaddr_check_equal(args.addr.unwrap().as_inet().unwrap(), &expected_addr)
}

/// Test getsockname using a valid fd and address, but an address length of 0.
fn test_zero_len(method: SocketInitMethod, sock_type: libc::c_int) -> Result<(), String> {
    let fd = socket_helper(method, sock_type);

    // fill the sockaddr with dummy data
    let addr = SockAddr::dummy_init_generic();

    // the sockaddr that we expect to have after calling getsockname();
    let expected_addr = addr;

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd,
        addr: Some(addr),
        addr_len: Some(0u32),
    };

    // if the buffer was too small, the returned data will be truncated but we won't get an error
    test_utils::run_and_close_fds(&[fd], || check_getsockname_call(&mut args, None))?;

    // the expected length of the socket address
    let expected_addr_len = match method {
        SocketInitMethod::Inet => std::mem::size_of::<libc::sockaddr_in>() as libc::socklen_t,
        // address family
        SocketInitMethod::Unix | SocketInitMethod::UnixSocketpair => 2,
    };

    // check that the returned length is expected
    test_utils::result_assert_eq(
        args.addr_len.unwrap(),
        expected_addr_len,
        "Unexpected addr length",
    )?;

    // check that the returned address is expected
    test_utils::result_assert_eq(args.addr.unwrap(), expected_addr, "Address was changed")
}

/// Test getsockname using an unbound socket.
fn test_unbound_socket(domain: libc::c_int, sock_type: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type, 0) };
    assert!(fd >= 0);

    // fill the sockaddr with dummy data
    let addr = match domain {
        libc::AF_INET => SockAddr::dummy_init_inet(),
        libc::AF_UNIX => SockAddr::dummy_init_unix(),
        _ => unimplemented!(),
    };

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd,
        addr: Some(addr),
        addr_len: Some(addr.ptr_size()),
    };

    test_utils::run_and_close_fds(&[fd], || check_getsockname_call(&mut args, None))?;

    // check the returned address
    match domain {
        libc::AF_INET => {
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
                    sin_port: 0u16.to_be(),
                    sin_addr: libc::in_addr {
                        s_addr: 0u32.to_be(),
                    },
                    sin_zero: [0; 8],
                },
            )?;
        }
        libc::AF_UNIX => {
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
        _ => unimplemented!(),
    }

    Ok(())
}

/// Test getsockname using a bound socket.
fn test_bound_socket(domain: libc::c_int, sock_type: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type, 0) };
    assert!(fd >= 0);

    // the sockaddr that we will bind to
    let (bind_addr, bind_addr_len) = match domain {
        libc::AF_INET => (
            SockAddr::Inet(libc::sockaddr_in {
                sin_family: libc::AF_INET as u16,
                sin_port: 11112u16.to_be(),
                sin_addr: libc::in_addr {
                    s_addr: libc::INADDR_LOOPBACK.to_be(),
                },
                sin_zero: [0; 8],
            }),
            std::mem::size_of::<libc::sockaddr_in>() as u32,
        ),
        libc::AF_UNIX => {
            let mut addr = libc::sockaddr_un {
                sun_family: libc::AF_UNIX as u16,
                sun_path: [0i8; 108],
            };
            // arbitrary abstract socket name
            addr.sun_path[1] = 4;
            addr.sun_path[2] = 7;
            (SockAddr::Unix(addr), 5)
        }
        _ => unimplemented!(),
    };

    // bind to the sockaddr
    let rv = unsafe { libc::bind(fd, bind_addr.as_ptr(), bind_addr_len) };
    assert_eq!(rv, 0);

    // fill the sockaddr with dummy data
    let addr = match domain {
        libc::AF_INET => SockAddr::dummy_init_inet(),
        libc::AF_UNIX => SockAddr::dummy_init_unix(),
        _ => unimplemented!(),
    };

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd,
        addr: Some(addr),
        addr_len: Some(addr.ptr_size()),
    };

    test_utils::run_and_close_fds(&[fd], || check_getsockname_call(&mut args, None))?;

    // check that the returned length is expected
    test_utils::result_assert_eq(
        args.addr_len.unwrap(),
        bind_addr_len,
        "Unexpected addr length",
    )?;

    // check the returned address
    test_utils::result_assert_eq(
        &args.addr.unwrap().as_slice()[..(bind_addr_len as usize)],
        &bind_addr.as_slice()[..(bind_addr_len as usize)],
        "Incorrect addr",
    )?;

    Ok(())
}

/// Test getsockname using an autobound socket.
fn test_autobound_socket(domain: libc::c_int, sock_type: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type, 0) };
    assert!(fd >= 0);

    // the "unnamed" sockaddr that we will bind to
    let (bind_addr, bind_addr_len) = match domain {
        libc::AF_INET => (
            SockAddr::Inet(libc::sockaddr_in {
                sin_family: libc::AF_INET as u16,
                sin_port: 0u16.to_be(),
                sin_addr: libc::in_addr {
                    s_addr: libc::INADDR_LOOPBACK.to_be(),
                },
                sin_zero: [0; 8],
            }),
            std::mem::size_of::<libc::sockaddr_in>() as u32,
        ),
        libc::AF_UNIX => (
            SockAddr::Unix(libc::sockaddr_un {
                sun_family: libc::AF_UNIX as u16,
                sun_path: [0i8; 108],
            }),
            2,
        ),
        _ => unimplemented!(),
    };

    // bind to the sockaddr
    let rv = unsafe { libc::bind(fd, bind_addr.as_ptr(), bind_addr_len) };
    assert_eq!(rv, 0);

    // fill the sockaddr with dummy data
    let addr = match domain {
        libc::AF_INET => SockAddr::dummy_init_inet(),
        libc::AF_UNIX => SockAddr::dummy_init_unix(),
        _ => unimplemented!(),
    };

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd,
        addr: Some(addr),
        addr_len: Some(addr.ptr_size()),
    };

    test_utils::run_and_close_fds(&[fd], || check_getsockname_call(&mut args, None))?;

    let expected_addr_len = match domain {
        libc::AF_INET => std::mem::size_of::<libc::sockaddr_in>() as u32,
        // 2 (domain) + 1 (nul byte) + 5 (autobind address length on linux)
        libc::AF_UNIX => 8,
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
            // check that the returned port was non-zero
            test_utils::result_assert_ne(
                args.addr.unwrap().as_inet().unwrap().sin_port,
                0,
                "Port was 0",
            )?;
        }
        libc::AF_UNIX => {
            // check that the returned name was non-empty
            test_utils::result_assert_ne(
                &args.addr.unwrap().as_unix().unwrap().sun_path[..5],
                &[0, 0, 0, 0, 0],
                "Address was [0,0,0,0,0]",
            )?;
        }
        _ => unimplemented!(),
    }

    Ok(())
}

/// Test getsockname after closing the bound socket.
fn test_after_close(domain: libc::c_int, sock_type: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type, 0) };
    assert!(fd >= 0);

    // the sockaddr that we will bind to
    let (bind_addr, bind_addr_len) = match domain {
        libc::AF_INET => (
            SockAddr::Inet(libc::sockaddr_in {
                sin_family: libc::AF_INET as u16,
                sin_port: 11113u16.to_be(),
                sin_addr: libc::in_addr {
                    s_addr: libc::INADDR_LOOPBACK.to_be(),
                },
                sin_zero: [0; 8],
            }),
            std::mem::size_of::<libc::sockaddr_in>() as u32,
        ),
        libc::AF_UNIX => {
            let mut addr = libc::sockaddr_un {
                sun_family: libc::AF_UNIX as u16,
                sun_path: [0i8; 108],
            };
            // arbitrary abstract socket name
            addr.sun_path[1] = 4;
            addr.sun_path[2] = 7;
            (SockAddr::Unix(addr), 5)
        }
        _ => unimplemented!(),
    };

    // bind to the sockaddr
    let rv = unsafe { libc::bind(fd, bind_addr.as_ptr(), bind_addr_len) };
    assert_eq!(rv, 0);

    // close the socket
    let rv = unsafe { libc::close(fd) };
    assert_eq!(rv, 0);

    // fill the sockaddr with dummy data
    let addr = match domain {
        libc::AF_INET => SockAddr::dummy_init_inet(),
        libc::AF_UNIX => SockAddr::dummy_init_unix(),
        _ => unimplemented!(),
    };

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd,
        addr: Some(addr),
        addr_len: Some(addr.ptr_size()),
    };

    check_getsockname_call(&mut args, Some(libc::EBADF))
}

/// Test getsockname using a bound and connected socket.
fn test_bound_connected_socket(domain: libc::c_int, sock_type: libc::c_int) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(domain, sock_type, 0) };
    let mut fd_server = unsafe { libc::socket(domain, sock_type, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    // the sockaddr that we will bind the client to
    let (client_addr, client_addr_len) = match domain {
        libc::AF_INET => (
            SockAddr::Inet(libc::sockaddr_in {
                sin_family: libc::AF_INET as u16,
                sin_port: 11114u16.to_be(),
                sin_addr: libc::in_addr {
                    s_addr: libc::INADDR_LOOPBACK.to_be(),
                },
                sin_zero: [0; 8],
            }),
            std::mem::size_of::<libc::sockaddr_in>() as u32,
        ),
        libc::AF_UNIX => {
            let mut addr = libc::sockaddr_un {
                sun_family: libc::AF_UNIX as u16,
                sun_path: [0i8; 108],
            };
            // arbitrary abstract socket name
            addr.sun_path[1] = 4;
            addr.sun_path[2] = 7;
            (SockAddr::Unix(addr), 5)
        }
        _ => unimplemented!(),
    };

    // bind the client socket
    {
        let rv = unsafe { libc::bind(fd_client, client_addr.as_ptr(), client_addr_len) };
        assert_eq!(rv, 0);
    }

    // bind the server socket to some unused address
    let (server_addr, server_addr_len) = autobind_helper(fd_server, domain);

    // connect the client to the server
    match sock_type {
        libc::SOCK_STREAM | libc::SOCK_SEQPACKET => {
            // connect the client to the server and get the accepted socket
            let fd_peer = stream_connect_helper(
                fd_client,
                fd_server,
                server_addr,
                server_addr_len,
                /* flags= */ 0,
            );

            // close the server
            assert_eq!(0, unsafe { libc::close(fd_server) });

            fd_server = fd_peer;
        }
        libc::SOCK_DGRAM => {
            // connect the client to the server
            dgram_connect_helper(fd_client, server_addr, server_addr_len);
        }
        _ => unimplemented!(),
    }

    // fill the sockaddr with dummy data
    let addr = match domain {
        libc::AF_INET => SockAddr::dummy_init_inet(),
        libc::AF_UNIX => SockAddr::dummy_init_unix(),
        _ => unimplemented!(),
    };

    // client arguments for getsockname()
    let mut args = GetsocknameArguments {
        fd: fd_client,
        addr: Some(addr),
        addr_len: Some(addr.ptr_size()),
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        check_getsockname_call(&mut args, None)
    })?;

    test_utils::result_assert_eq(
        args.addr_len.unwrap(),
        client_addr_len,
        "Unexpected addr length",
    )?;

    test_utils::result_assert_eq(
        &args.addr.unwrap().as_slice()[..(client_addr_len as usize)],
        &client_addr.as_slice()[..(client_addr_len as usize)],
        "Incorrect addr",
    )?;

    Ok(())
}

/// Test getsockname using a listening socket without binding (an implicit bind).
fn test_implicit_bind(domain: libc::c_int, sock_type: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type, 0) };
    assert!(fd >= 0);

    let rv = unsafe { libc::listen(fd, 100) };
    if domain == libc::AF_INET {
        // only inet sockets seem to support implicit binds
        assert_eq!(rv, 0);
    } else {
        assert_ne!(rv, 0);
        return Ok(());
    }

    // fill the sockaddr with dummy data
    let addr = match domain {
        libc::AF_INET => SockAddr::dummy_init_inet(),
        libc::AF_UNIX => SockAddr::dummy_init_unix(),
        _ => unimplemented!(),
    };

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd,
        addr: Some(addr),
        addr_len: Some(addr.ptr_size()),
    };

    test_utils::run_and_close_fds(&[fd], || check_getsockname_call(&mut args, None))?;

    // check that the returned length is expected
    test_utils::result_assert_eq(
        args.addr_len.unwrap() as usize,
        std::mem::size_of::<libc::sockaddr_in>(),
        "Unexpected addr length",
    )?;

    // check that the returned client address is expected (except the port which is not
    // deterministic)
    test_utils::result_assert_eq(
        args.addr.unwrap().as_inet().unwrap().sin_family,
        libc::AF_INET as u16,
        "Unexpected family",
    )?;
    test_utils::result_assert_eq(
        args.addr.unwrap().as_inet().unwrap().sin_addr.s_addr,
        libc::INADDR_ANY.to_be(),
        "Unexpected address",
    )?;
    test_utils::result_assert(
        args.addr.unwrap().as_inet().unwrap().sin_port > 0,
        "Unexpected port",
    )?;
    test_utils::result_assert_eq(
        args.addr.unwrap().as_inet().unwrap().sin_zero,
        [0; 8],
        "Unexpected padding",
    )?;

    Ok(())
}

fn socket_helper(method: SocketInitMethod, sock_type: libc::c_int) -> libc::c_int {
    match method {
        SocketInitMethod::Inet | SocketInitMethod::Unix => unsafe {
            let fd = libc::socket(method.domain(), sock_type, 0);
            assert!(fd >= 0);
            fd
        },
        SocketInitMethod::UnixSocketpair => {
            // get two connected unix sockets
            let mut fds = vec![-1 as libc::c_int; 2];
            assert_eq!(0, unsafe {
                libc::socketpair(method.domain(), sock_type, 0, fds.as_mut_ptr())
            });

            let (fd_client, fd_peer) = (fds[0], fds[1]);
            assert!(fd_client >= 0 && fd_peer >= 0);

            // close one socket
            assert_eq!(0, unsafe { libc::close(fd_peer) });

            fd_client
        }
    }
}

fn inet_sockaddr_check_equal(a: &libc::sockaddr_in, b: &libc::sockaddr_in) -> Result<(), String> {
    test_utils::result_assert_eq(a.sin_family, b.sin_family, "Unexpected family")?;
    test_utils::result_assert_eq(a.sin_port, b.sin_port, "Unexpected port")?;
    test_utils::result_assert_eq(a.sin_addr.s_addr, b.sin_addr.s_addr, "Unexpected address")?;
    test_utils::result_assert_eq(a.sin_zero, b.sin_zero, "Unexpected padding")?;
    Ok(())
}

fn check_getsockname_call(
    args: &mut GetsocknameArguments,
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
    let rv = unsafe { libc::getsockname(args.fd, addr_ptr, args.addr_len.as_mut_ptr()) };

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
