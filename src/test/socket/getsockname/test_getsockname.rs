/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use test_utils::TestEnvironment as TestEnv;
use test_utils::{set, AsMutPtr};

struct GetsocknameArguments {
    fd: libc::c_int,
    addr: Option<libc::sockaddr_in>, // if None, a null pointer should be used
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
    let tests: Vec<test_utils::ShadowTest<_, _>> = vec![
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
            "test_null_len",
            test_null_len,
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
            "test_unbound_socket",
            test_unbound_socket,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_bound_socket",
            test_bound_socket,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_dgram_socket",
            test_dgram_socket,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_after_close",
            test_after_close,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_connected_socket",
            test_connected_socket,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_implicit_bind",
            test_implicit_bind,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
    ];

    tests
}

/// Test getsockname using an argument that cannot be a fd.
fn test_invalid_fd() -> Result<(), String> {
    // fill the sockaddr with dummy data
    let addr = libc::sockaddr_in {
        sin_family: 123u16,
        sin_port: 456u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: 789u32.to_be(),
        },
        sin_zero: [1; 8],
    };

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd: -1,
        addr: Some(addr),
        addr_len: Some(std::mem::size_of_val(&addr) as u32),
    };

    check_getsockname_call(&mut args, Some(libc::EBADF))
}

/// Test getsockname using an argument that could be a fd, but is not.
fn test_non_existent_fd() -> Result<(), String> {
    // fill the sockaddr with dummy data
    let addr = libc::sockaddr_in {
        sin_family: 123u16,
        sin_port: 456u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: 789u32.to_be(),
        },
        sin_zero: [1; 8],
    };

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd: 8934,
        addr: Some(addr),
        addr_len: Some(std::mem::size_of_val(&addr) as u32),
    };

    check_getsockname_call(&mut args, Some(libc::EBADF))
}

/// Test getsockname using a valid fd that is not a socket.
fn test_non_socket_fd() -> Result<(), String> {
    // fill the sockaddr with dummy data
    let addr = libc::sockaddr_in {
        sin_family: 123u16,
        sin_port: 456u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: 789u32.to_be(),
        },
        sin_zero: [1; 8],
    };

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd: 0, // assume the fd 0 is already open and is not a socket
        addr: Some(addr),
        addr_len: Some(std::mem::size_of_val(&addr) as u32),
    };

    check_getsockname_call(&mut args, Some(libc::ENOTSOCK))
}

/// Test getsockname using a valid fd, but with a NULL address.
fn test_null_addr() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
    assert!(fd >= 0);

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd: fd,
        addr: None,
        addr_len: Some(5),
    };

    test_utils::run_and_close_fds(&[fd], || {
        check_getsockname_call(&mut args, Some(libc::EFAULT))
    })
}

/// Test getsockname using a valid fd and address, a NULL address length.
fn test_null_len() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
    assert!(fd >= 0);

    // fill the sockaddr with dummy data
    let addr = libc::sockaddr_in {
        sin_family: 123u16,
        sin_port: 456u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: 789u32.to_be(),
        },
        sin_zero: [1; 8],
    };

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd: fd,
        addr: Some(addr),
        addr_len: None,
    };

    test_utils::run_and_close_fds(&[fd], || {
        check_getsockname_call(&mut args, Some(libc::EFAULT))
    })
}

/// Test getsockname using a valid fd and address, but an address length that is too small.
fn test_short_len() -> Result<(), String> {
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

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd: fd,
        addr: Some(addr),
        addr_len: Some((std::mem::size_of_val(&addr) - 1) as u32),
    };

    // if the buffer was too small, the returned data will be truncated but we won't get an error
    test_utils::run_and_close_fds(&[fd], || check_getsockname_call(&mut args, None))?;

    // check that the returned length is expected
    test_utils::result_assert_eq(
        args.addr_len.unwrap() as usize,
        std::mem::size_of_val(&addr),
        "Unexpected addr length",
    )?;

    // check that the returned address is expected
    sockaddr_check_equal(&args.addr.unwrap(), &expected_addr)
}

/// Test getsockname using a valid fd and address, but an address length of 0.
fn test_zero_len() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
    assert!(fd >= 0);

    // fill the sockaddr with dummy data
    let addr = libc::sockaddr_in {
        sin_family: 123u16,
        sin_port: 456u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: 789u32.to_be(),
        },
        sin_zero: [1; 8],
    };

    // the sockaddr that we expect to have after calling getsockname();
    let expected_addr = addr;

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd: fd,
        addr: Some(addr),
        addr_len: Some(0u32),
    };

    // if the buffer was too small, the returned data will be truncated but we won't get an error
    test_utils::run_and_close_fds(&[fd], || check_getsockname_call(&mut args, None))?;

    // check that the returned length is expected
    test_utils::result_assert_eq(
        args.addr_len.unwrap() as usize,
        std::mem::size_of_val(&addr),
        "Unexpected addr length",
    )?;

    // check that the returned address is expected
    sockaddr_check_equal(&args.addr.unwrap(), &expected_addr)
}

/// Test getsockname using an unbound socket.
fn test_unbound_socket() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
    assert!(fd >= 0);

    // the sockaddr that we expect to have after calling getsockname()
    let expected_addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 0u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: 0u32.to_be(),
        },
        sin_zero: [0; 8],
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

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd: fd,
        addr: Some(addr),
        addr_len: Some(std::mem::size_of_val(&addr) as u32),
    };

    test_utils::run_and_close_fds(&[fd], || check_getsockname_call(&mut args, None))?;

    // check that the returned length is expected
    test_utils::result_assert_eq(
        args.addr_len.unwrap() as usize,
        std::mem::size_of_val(&addr),
        "Unexpected addr length",
    )?;

    // check that the returned address is expected
    sockaddr_check_equal(&args.addr.unwrap(), &expected_addr)
}

/// Test getsockname using a socket bound to a port on loopback.
fn test_bound_socket() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
    assert!(fd >= 0);

    // the sockaddr that we expect to have after calling getsockname()
    let expected_addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    // bind to the expected sockaddr
    let rv = unsafe {
        libc::bind(
            fd,
            &expected_addr as *const libc::sockaddr_in as *const libc::sockaddr,
            std::mem::size_of_val(&expected_addr) as u32,
        )
    };
    assert_eq!(rv, 0);

    // fill the sockaddr with dummy data
    let addr = libc::sockaddr_in {
        sin_family: 123u16,
        sin_port: 456u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: 789u32.to_be(),
        },
        sin_zero: [1; 8],
    };

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd: fd,
        addr: Some(addr),
        addr_len: Some(std::mem::size_of_val(&addr) as u32),
    };

    test_utils::run_and_close_fds(&[fd], || check_getsockname_call(&mut args, None))?;

    // check that the returned length is expected
    test_utils::result_assert_eq(
        args.addr_len.unwrap() as usize,
        std::mem::size_of_val(&addr),
        "Unexpected addr length",
    )?;

    // check that the returned address is expected
    sockaddr_check_equal(&args.addr.unwrap(), &expected_addr)
}

/// Test getsockname using an unbound datagram socket.
fn test_dgram_socket() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_DGRAM, 0) };
    assert!(fd >= 0);

    // the sockaddr that we expect to have after calling getsockname()
    let expected_addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 0u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: 0u32.to_be(),
        },
        sin_zero: [0; 8],
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

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd: fd,
        addr: Some(addr),
        addr_len: Some(std::mem::size_of_val(&addr) as u32),
    };

    test_utils::run_and_close_fds(&[fd], || check_getsockname_call(&mut args, None))?;

    // check that the returned length is expected
    test_utils::result_assert_eq(
        args.addr_len.unwrap() as usize,
        std::mem::size_of_val(&addr),
        "Unexpected addr length",
    )?;

    // check that the returned address is expected
    sockaddr_check_equal(&args.addr.unwrap(), &expected_addr)
}

/// Test getsockname after closing the socket.
fn test_after_close() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
    assert!(fd >= 0);

    // the sockaddr that we expect to have after calling getsockname()
    let expected_addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    // bind to the expected sockaddr
    let rv = unsafe {
        libc::bind(
            fd,
            &expected_addr as *const libc::sockaddr_in as *const libc::sockaddr,
            std::mem::size_of_val(&expected_addr) as u32,
        )
    };
    assert_eq!(rv, 0);

    // close the socket
    let rv = unsafe { libc::close(fd) };
    assert_eq!(rv, 0);

    // fill the sockaddr with dummy data
    let addr = libc::sockaddr_in {
        sin_family: 123u16,
        sin_port: 456u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: 789u32.to_be(),
        },
        sin_zero: [1; 8],
    };

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd: fd,
        addr: Some(addr),
        addr_len: Some(std::mem::size_of_val(&addr) as u32),
    };

    check_getsockname_call(&mut args, Some(libc::EBADF))
}

/// Test getsockname using a socket bound to a port on loopback.
fn test_connected_socket() -> Result<(), String> {
    let fd_client = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
    let fd_server = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    // the server address
    let server_addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    // bind and listen on the server address
    let rv = unsafe {
        libc::bind(
            fd_server,
            &server_addr as *const libc::sockaddr_in as *const libc::sockaddr,
            std::mem::size_of_val(&server_addr) as u32,
        )
    };
    assert_eq!(rv, 0);

    let rv = unsafe { libc::listen(fd_server, 100) };
    assert_eq!(rv, 0);

    // connect to the server address
    let rv = unsafe {
        libc::connect(
            fd_client,
            &server_addr as *const libc::sockaddr_in as *const libc::sockaddr,
            std::mem::size_of_val(&server_addr) as u32,
        )
    };
    assert_eq!(rv, 0);

    // client arguments for getsockname()
    let mut args_client = GetsocknameArguments {
        fd: fd_client,
        // fill the sockaddr with dummy data
        addr: Some(libc::sockaddr_in {
            sin_family: 123u16,
            sin_port: 456u16.to_be(),
            sin_addr: libc::in_addr {
                s_addr: 789u32.to_be(),
            },
            sin_zero: [1; 8],
        }),
        addr_len: Some(std::mem::size_of::<libc::sockaddr_in>() as u32),
    };

    // server arguments for getsockname()
    let mut args_server = GetsocknameArguments {
        fd: fd_server,
        // fill the sockaddr with dummy data
        addr: Some(libc::sockaddr_in {
            sin_family: 123u16,
            sin_port: 456u16.to_be(),
            sin_addr: libc::in_addr {
                s_addr: 789u32.to_be(),
            },
            sin_zero: [1; 8],
        }),
        addr_len: Some(std::mem::size_of::<libc::sockaddr_in>() as u32),
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        check_getsockname_call(&mut args_client, None)?;
        check_getsockname_call(&mut args_server, None)
    })?;

    // check that the returned length is expected
    test_utils::result_assert_eq(
        args_client.addr_len.unwrap() as usize,
        std::mem::size_of_val(&args_client.addr.unwrap()),
        "Unexpected addr length",
    )?;
    test_utils::result_assert_eq(
        args_server.addr_len.unwrap() as usize,
        std::mem::size_of_val(&args_server.addr.unwrap()),
        "Unexpected addr length",
    )?;

    // check that the returned client address is expected (except the port which is not
    // deterministic)
    test_utils::result_assert_eq(
        args_client.addr.unwrap().sin_family,
        libc::AF_INET as u16,
        "Unexpected family",
    )?;
    test_utils::result_assert_eq(
        args_client.addr.unwrap().sin_addr.s_addr,
        libc::INADDR_LOOPBACK.to_be(),
        "Unexpected address",
    )?;
    test_utils::result_assert_eq(
        args_client.addr.unwrap().sin_zero,
        [0; 8],
        "Unexpected padding",
    )?;

    // check that the returned server address is expected
    sockaddr_check_equal(&args_server.addr.unwrap(), &server_addr)
}

/// Test getsockname using a listening socket without binding (an implicit bind).
fn test_implicit_bind() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
    assert!(fd >= 0);

    let rv = unsafe { libc::listen(fd, 100) };
    assert_eq!(rv, 0);

    // fill the sockaddr with dummy data
    let addr = libc::sockaddr_in {
        sin_family: 123u16,
        sin_port: 456u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: 789u32.to_be(),
        },
        sin_zero: [1; 8],
    };

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd: fd,
        addr: Some(addr),
        addr_len: Some(std::mem::size_of_val(&addr) as u32),
    };

    test_utils::run_and_close_fds(&[fd], || check_getsockname_call(&mut args, None))?;

    // check that the returned length is expected
    test_utils::result_assert_eq(
        args.addr_len.unwrap() as usize,
        std::mem::size_of_val(&args.addr.unwrap()),
        "Unexpected addr length",
    )?;

    // check that the returned client address is expected (except the port which is not
    // deterministic)
    test_utils::result_assert_eq(
        args.addr.unwrap().sin_family,
        libc::AF_INET as u16,
        "Unexpected family",
    )?;
    test_utils::result_assert_eq(
        args.addr.unwrap().sin_addr.s_addr,
        libc::INADDR_ANY.to_be(),
        "Unexpected address",
    )?;
    test_utils::result_assert_eq(args.addr.unwrap().sin_zero, [0; 8], "Unexpected padding")?;

    Ok(())
}

fn sockaddr_check_equal(a: &libc::sockaddr_in, b: &libc::sockaddr_in) -> Result<(), String> {
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
    // if the pointers will be non-null, make sure the length is not greater than the actual data size
    // so that we don't segfault
    if args.addr.is_some() && args.addr_len.is_some() {
        assert!(args.addr_len.unwrap() as usize <= std::mem::size_of_val(&args.addr.unwrap()));
    }

    // will modify args.addr and args.addr_len
    let rv = unsafe {
        libc::getsockname(
            args.fd,
            args.addr.as_mut_ptr() as *mut libc::sockaddr,
            args.addr_len.as_mut_ptr(),
        )
    };

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
