/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use test_utils::TestEnvironment as TestEnv;
use test_utils::set;
use test_utils::socket_utils::SockAddr;

struct BindArguments {
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
        // don't test outside of shadow since the host will already have ports in use
        test_utils::ShadowTest::new(
            "test_all_ports_used",
            test_all_ports_used,
            set![TestEnv::Shadow],
        ),
    ];

    // get the cartesian product of socket types
    let sock_types = &[libc::SOCK_STREAM, libc::SOCK_DGRAM, libc::SOCK_SEQPACKET];
    let sock_type_combinations: Vec<(libc::c_int, libc::c_int)> = sock_types
        .iter()
        .flat_map(|item_x| sock_types.iter().map(move |item_y| (*item_x, *item_y)))
        .collect();

    for &domain in [libc::AF_INET, libc::AF_UNIX].iter() {
        for &(sock_type_1, sock_type_2) in &sock_type_combinations {
            // add details to the test names to avoid duplicates
            let append_args = |s| {
                format!(
                    "{} <domain={},type_1={},type_2={}>",
                    s, domain, sock_type_1, sock_type_2
                )
            };

            // skip tests that use SOCK_SEQPACKET with INET sockets
            if domain == libc::AF_INET && [sock_type_1, sock_type_2].contains(&libc::SOCK_SEQPACKET)
            {
                continue;
            }

            tests.extend(vec![test_utils::ShadowTest::new(
                &append_args("test_two_types_same_address"),
                move || test_two_types_same_address(domain, sock_type_1, sock_type_2),
                set![TestEnv::Libc, TestEnv::Shadow],
            )]);
        }
    }

    // tests to repeat for different socket options
    for &domain in [libc::AF_INET].iter() {
        for &sock_type in [libc::SOCK_STREAM, libc::SOCK_DGRAM].iter() {
            for &flag in [0, libc::SOCK_NONBLOCK, libc::SOCK_CLOEXEC].iter() {
                // add details to the test names to avoid duplicates
                let append_args =
                    |s| format!("{} <domain={},type={},flag={}>", s, domain, sock_type, flag);

                tests.extend(vec![
                    test_utils::ShadowTest::new(
                        &append_args("test_null_addr"),
                        move || test_null_addr(domain, sock_type, flag),
                        set![TestEnv::Libc, TestEnv::Shadow],
                    ),
                    test_utils::ShadowTest::new(
                        &append_args("test_double_bind_socket"),
                        move || test_double_bind_socket(domain, sock_type, flag),
                        set![TestEnv::Libc, TestEnv::Shadow],
                    ),
                    test_utils::ShadowTest::new(
                        &append_args("test_double_bind_address"),
                        move || test_double_bind_address(domain, sock_type, flag),
                        set![TestEnv::Libc, TestEnv::Shadow],
                    ),
                    test_utils::ShadowTest::new(
                        &append_args("test_autobind"),
                        move || test_autobind(domain, sock_type, flag),
                        set![TestEnv::Libc, TestEnv::Shadow],
                    ),
                ]);

                if domain != libc::AF_UNIX {
                    tests.extend(vec![test_utils::ShadowTest::new(
                        &append_args("test_short_addr"),
                        move || test_short_addr(domain, sock_type, flag),
                        set![TestEnv::Libc, TestEnv::Shadow],
                    )]);
                }
            }
        }
    }

    // tests to repeat for different socket options
    for &sock_type in [libc::SOCK_STREAM, libc::SOCK_DGRAM].iter() {
        for &flag in [0, libc::SOCK_NONBLOCK, libc::SOCK_CLOEXEC].iter() {
            // add details to the test names to avoid duplicates
            let append_args = |s| format!("{} <type={},flag={}>", s, sock_type, flag);

            tests.extend(vec![
                test_utils::ShadowTest::new(
                    &append_args("test_ipv4"),
                    move || test_ipv4(sock_type, flag),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                // Docker does not support IPv6, so the following test is disabled for now
                /*
                test_utils::ShadowTest::new(
                    &append_args("test_ipv6"),
                    move || test_ipv6(sock_type, flag),
                    set![TestEnv::Libc],
                ),
                */
                test_utils::ShadowTest::new(
                    &append_args("test_loopback"),
                    move || test_loopback(sock_type, flag),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_any_interface"),
                    move || test_any_interface(sock_type, flag),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_double_bind_loopback_and_any"),
                    move || {
                        test_double_bind_loopback_and_any(
                            /* reverse= */ false, sock_type, flag,
                        )
                    },
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_double_bind_loopback_and_any <reversed>"),
                    move || {
                        test_double_bind_loopback_and_any(/* reverse= */ true, sock_type, flag)
                    },
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
            ]);
        }
    }

    tests
}

// test binding using an argument that cannot be a fd
fn test_invalid_fd() -> Result<(), String> {
    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    let args = BindArguments {
        fd: -1,
        addr: Some(SockAddr::Inet(addr)),
        addr_len: std::mem::size_of_val(&addr) as u32,
    };

    check_bind_call(&args, Some(libc::EBADF))
}

// test binding using an argument that could be a fd, but is not
fn test_non_existent_fd() -> Result<(), String> {
    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    let args = BindArguments {
        fd: 8934,
        addr: Some(SockAddr::Inet(addr)),
        addr_len: std::mem::size_of_val(&addr) as u32,
    };

    check_bind_call(&args, Some(libc::EBADF))
}

// test binding a valid fd that is not a socket
fn test_non_socket_fd() -> Result<(), String> {
    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    let args = BindArguments {
        fd: 0, // assume the fd 0 is already open and is not a socket
        addr: Some(SockAddr::Inet(addr)),
        addr_len: std::mem::size_of_val(&addr) as u32,
    };

    check_bind_call(&args, Some(libc::ENOTSOCK))
}

// test binding a valid fd, but with a NULL address
fn test_null_addr(
    domain: libc::c_int,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type | flag, 0) };
    assert!(fd >= 0);

    let args = BindArguments {
        fd,
        addr: None,
        addr_len: 5,
    };

    test_utils::run_and_close_fds(&[fd], || check_bind_call(&args, Some(libc::EFAULT)))
}

// test binding a valid fd and address, but an address length that is too low
fn test_short_addr(
    domain: libc::c_int,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type | flag, 0) };
    assert!(fd >= 0);

    let (addr, addr_len) = match domain {
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
            (
                SockAddr::Inet(addr),
                std::mem::size_of_val(&addr) as u32 - 1,
            )
        }
        // any length (>=2) is valid for unix sockets
        libc::AF_UNIX => panic!("This test should not be run for unix sockets"),
        _ => unimplemented!(),
    };

    let args = BindArguments {
        fd,
        addr: Some(addr),
        addr_len,
    };

    test_utils::run_and_close_fds(&[fd], || check_bind_call(&args, Some(libc::EINVAL)))
}

// test binding an INET socket
fn test_ipv4(sock_type: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    assert!(fd >= 0);

    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    let args = BindArguments {
        fd,
        addr: Some(SockAddr::Inet(addr)),
        addr_len: std::mem::size_of_val(&addr) as u32,
    };

    test_utils::run_and_close_fds(&[fd], || check_bind_call(&args, None))
}

// test binding to an unspecified port when all ports are already in use
fn test_all_ports_used() -> Result<(), String> {
    let mut fds_used = vec![];

    fn inner(fds_used: &mut Vec<i32>) -> Result<(), String> {
        // shadow will only assign ports >= 10_000 (MIN_RANDOM_PORT)
        for port in 10_000..=u16::MAX {
            let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
            assert!(fd >= 0);

            fds_used.push(fd);

            let addr = libc::sockaddr_in {
                sin_family: libc::AF_INET as u16,
                sin_port: port.to_be(),
                sin_addr: libc::in_addr {
                    s_addr: libc::INADDR_LOOPBACK.to_be(),
                },
                sin_zero: [0; 8],
            };

            let args = BindArguments {
                fd,
                addr: Some(SockAddr::Inet(addr)),
                addr_len: std::mem::size_of_val(&addr) as u32,
            };

            check_bind_call(&args, None).map_err(|e| format!("port {}: {}", port, e))?;
        }

        let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
        assert!(fd >= 0);

        fds_used.push(fd);

        let addr = libc::sockaddr_in {
            sin_family: libc::AF_INET as u16,
            sin_port: 0u16.to_be(),
            sin_addr: libc::in_addr {
                s_addr: libc::INADDR_LOOPBACK.to_be(),
            },
            sin_zero: [0; 8],
        };

        let args = BindArguments {
            fd,
            addr: Some(SockAddr::Inet(addr)),
            addr_len: std::mem::size_of_val(&addr) as u32,
        };

        // getting an unspecified port (port 0) should fail since all ports are in use
        check_bind_call(&args, Some(libc::EADDRINUSE))
    }

    let rv = inner(&mut fds_used);

    for fd in fds_used.into_iter() {
        let rv_close = unsafe { libc::close(fd) };
        assert_eq!(rv_close, 0, "Could not close fd {}", fd);
    }

    rv
}

fn test_two_types_same_address(
    domain: libc::c_int,
    sock_type_1: libc::c_int,
    sock_type_2: libc::c_int,
) -> Result<(), String> {
    let fd_1 = unsafe { libc::socket(domain, sock_type_1, 0) };
    let fd_2 = unsafe { libc::socket(domain, sock_type_2, 0) };
    assert!(fd_1 >= 0);
    assert!(fd_2 >= 0);

    let (bind_addr, bind_addr_len) = match domain {
        libc::AF_INET => (
            SockAddr::Inet(libc::sockaddr_in {
                sin_family: libc::AF_INET as u16,
                sin_port: 11111u16.to_be(),
                sin_addr: libc::in_addr {
                    s_addr: libc::INADDR_LOOPBACK.to_be(),
                },
                sin_zero: [0; 8],
            }),
            std::mem::size_of::<libc::sockaddr_in>() as libc::socklen_t,
        ),
        libc::AF_UNIX => {
            let mut addr = libc::sockaddr_un {
                sun_family: libc::AF_UNIX as u16,
                sun_path: [0; 108],
            };
            addr.sun_path[1..5].copy_from_slice(&[23, 12, 44, 53]);
            (SockAddr::Unix(addr), 7)
        }
        _ => unimplemented!(),
    };

    let args_1 = BindArguments {
        fd: fd_1,
        addr: Some(bind_addr),
        addr_len: bind_addr_len,
    };

    let args_2 = BindArguments {
        fd: fd_2,
        addr: Some(bind_addr),
        addr_len: bind_addr_len,
    };

    test_utils::run_and_close_fds(&[fd_1, fd_2], || {
        let expected_errno = if sock_type_1 == sock_type_2 {
            Some(libc::EADDRINUSE)
        } else {
            None
        };

        check_bind_call(&args_1, None)?;
        check_bind_call(&args_2, expected_errno)
    })
}

/*
// Docker does not support IPv6, so this test is not run
#[allow(dead_code)]
// test binding an INET6 socket
fn test_ipv6(sock_type: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET6, sock_type | flag, 0) };
    assert!(fd >= 0);

    let mut loopback = [0; 16];
    loopback[15] = 1;

    let addr = libc::sockaddr_in6 {
        sin6_family: libc::AF_INET6 as u16,
        sin6_port: 11111u16.to_be(),
        sin6_flowinfo: 0,
        sin6_addr: libc::in6_addr { s6_addr: loopback },
        sin6_scope_id: 0,
    };

    let args = BindArguments {
        fd,
        addr: Some(SockAddr::Inet6(addr)),
        addr_len: std::mem::size_of_val(&addr) as u32,
    };

    test_utils::run_and_close_fds(&[fd], || check_bind_call(&args, None))
}
*/

// test binding a socket on the loopback interface
fn test_loopback(sock_type: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    assert!(fd >= 0);

    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    let args = BindArguments {
        fd,
        addr: Some(SockAddr::Inet(addr)),
        addr_len: std::mem::size_of_val(&addr) as u32,
    };

    test_utils::run_and_close_fds(&[fd], || check_bind_call(&args, None))
}

// test binding a socket on all interfaces
fn test_any_interface(sock_type: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    assert!(fd >= 0);

    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_ANY.to_be(),
        },
        sin_zero: [0; 8],
    };

    let args = BindArguments {
        fd,
        addr: Some(SockAddr::Inet(addr)),
        addr_len: std::mem::size_of_val(&addr) as u32,
    };

    test_utils::run_and_close_fds(&[fd], || check_bind_call(&args, None))
}

// test binding a socket twice to the same address
fn test_double_bind_socket(
    domain: libc::c_int,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type | flag, 0) };
    assert!(fd >= 0);

    let (addr, addr_len) = match domain {
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
            addr.sun_path[1] = 5;
            addr.sun_path[2] = 8;
            (SockAddr::Unix(addr), 5)
        }
        _ => unimplemented!(),
    };

    let args = BindArguments {
        fd,
        addr: Some(addr),
        addr_len,
    };

    test_utils::run_and_close_fds(&[fd], || {
        check_bind_call(&args, None)?;
        check_bind_call(&args, Some(libc::EINVAL))?;
        Ok(())
    })
}

// test binding two sockets to the same address on the loopback interface
fn test_double_bind_address(
    domain: libc::c_int,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let fd1 = unsafe { libc::socket(domain, sock_type | flag, 0) };
    assert!(fd1 >= 0);
    let fd2 = unsafe { libc::socket(domain, sock_type | flag, 0) };
    assert!(fd2 >= 0);

    let (addr, addr_len) = match domain {
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
            addr.sun_path[1] = 5;
            addr.sun_path[2] = 8;
            (SockAddr::Unix(addr), 5)
        }
        _ => unimplemented!(),
    };

    let args1 = BindArguments {
        fd: fd1,
        addr: Some(addr),
        addr_len,
    };

    let args2 = BindArguments {
        fd: fd2,
        addr: Some(addr),
        addr_len,
    };

    test_utils::run_and_close_fds(&[fd1, fd2], || {
        check_bind_call(&args1, None)?;
        check_bind_call(&args2, Some(libc::EADDRINUSE))?;
        Ok(())
    })
}

// test binding two sockets to the same address, but using both 'loopback' and 'any' interfaces
fn test_double_bind_loopback_and_any(
    reverse: bool,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let fd1 = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    assert!(fd1 >= 0);
    let fd2 = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    assert!(fd2 >= 0);

    let addr1 = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    let addr2 = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_ANY.to_be(),
        },
        sin_zero: [0; 8],
    };

    // if reverse, bind to ANY before LOOPBACK
    let (addr1, addr2) = if reverse {
        (addr2, addr1)
    } else {
        (addr1, addr2)
    };

    let args1 = BindArguments {
        fd: fd1,
        addr: Some(SockAddr::Inet(addr1)),
        addr_len: std::mem::size_of_val(&addr1) as u32,
    };

    let args2 = BindArguments {
        fd: fd2,
        addr: Some(SockAddr::Inet(addr2)),
        addr_len: std::mem::size_of_val(&addr2) as u32,
    };

    test_utils::run_and_close_fds(&[fd1, fd2], || {
        check_bind_call(&args1, None)?;
        check_bind_call(&args2, Some(libc::EADDRINUSE))?;
        Ok(())
    })
}

// test auto-binding (ex: a port of 0 for inet sockets)
fn test_autobind(
    domain: libc::c_int,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type | flag, 0) };
    assert!(fd >= 0);

    let (addr, addr_len) = match domain {
        libc::AF_INET => {
            let addr = libc::sockaddr_in {
                sin_family: libc::AF_INET as u16,
                sin_port: 0u16.to_be(),
                sin_addr: libc::in_addr {
                    s_addr: libc::INADDR_ANY.to_be(),
                },
                sin_zero: [0; 8],
            };
            (
                SockAddr::Inet(addr),
                std::mem::size_of::<libc::sockaddr_in>() as u32,
            )
        }
        libc::AF_UNIX => {
            let addr = libc::sockaddr_un {
                sun_family: libc::AF_UNIX as u16,
                sun_path: [0i8; 108],
            };
            (SockAddr::Unix(addr), 2)
        }
        _ => unimplemented!(),
    };

    let args = BindArguments {
        fd,
        addr: Some(addr),
        addr_len,
    };

    test_utils::run_and_close_fds(&[fd], || check_bind_call(&args, None))
}

fn check_bind_call(
    args: &BindArguments,
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

    let rv = unsafe { libc::bind(args.fd, addr_ptr, args.addr_len) };

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
