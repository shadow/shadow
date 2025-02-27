/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use test_utils::AsMutPtr;
use test_utils::TestEnvironment as TestEnv;
use test_utils::set;

#[derive(Debug, Clone)]
struct GetsockoptArguments {
    fd: libc::c_int,
    level: libc::c_int,
    optname: libc::c_int,
    optval: Option<Vec<u8>>,
    optlen: Option<libc::socklen_t>,
}

#[derive(Debug, Clone)]
struct SetsockoptArguments {
    fd: libc::c_int,
    level: libc::c_int,
    optname: libc::c_int,
    optval: Option<Vec<u8>>,
    optlen: libc::socklen_t,
}

impl GetsockoptArguments {
    pub fn new(
        fd: libc::c_int,
        level: libc::c_int,
        optname: libc::c_int,
        optval: Option<Vec<u8>>,
    ) -> Self {
        let len = optval.as_ref().map_or(0, |v| v.len());
        Self {
            fd,
            level,
            optname,
            optlen: Some(len as libc::socklen_t),
            optval,
        }
    }
}

impl SetsockoptArguments {
    pub fn new(
        fd: libc::c_int,
        level: libc::c_int,
        optname: libc::c_int,
        optval: Option<Vec<u8>>,
    ) -> Self {
        let len = optval.as_ref().map_or(0, |v| v.len());
        Self {
            fd,
            level,
            optname,
            optlen: len as libc::socklen_t,
            optval,
        }
    }
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
            "test_long_len",
            test_long_len,
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
            "test_null_val",
            test_null_val,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_null_val_zero_len",
            test_null_val_zero_len,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_null_val_nonzero_len",
            test_null_val_nonzero_len,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_null_val_null_len",
            test_null_val_null_len,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_invalid_level",
            test_invalid_level,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
    ];

    let domains = [libc::AF_INET];
    let sock_types = [libc::SOCK_STREAM, libc::SOCK_DGRAM];

    for &domain in domains.iter() {
        for &sock_type in sock_types.iter() {
            // add details to the test names to avoid duplicates
            let append_args = |s| format!("{} <domain={},sock_type={}>", s, domain, sock_type);

            let more_tests: Vec<test_utils::ShadowTest<_, _>> = vec![
                test_utils::ShadowTest::new(
                    &append_args("test_so_sndbuf"),
                    move || test_so_sndbuf(domain, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_so_rcvbuf"),
                    move || test_so_rcvbuf(domain, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_so_error"),
                    move || test_so_error(domain, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_so_type"),
                    move || test_so_type(domain, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_so_domain"),
                    move || test_so_domain(domain, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_so_protocol"),
                    move || test_so_protocol(domain, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_so_acceptconn"),
                    move || test_so_acceptconn(domain, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_so_broadcast_0"),
                    move || test_so_broadcast_0(domain, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_so_broadcast"),
                    move || test_so_broadcast(domain, sock_type),
                    // TODO: enable if/when we support broadcast sockets in shadow, and remove the
                    // above test
                    set![TestEnv::Libc],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_tcp_info"),
                    move || test_tcp_info(domain, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_tcp_nodelay"),
                    move || test_tcp_nodelay(domain, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_tcp_congestion"),
                    move || test_tcp_congestion(domain, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
            ];

            tests.extend(more_tests);
        }
    }

    tests
}

/// Test getsockopt() and setsockopt() using an argument that cannot be a fd.
fn test_invalid_fd() -> Result<(), String> {
    let fd = -1;
    let level = libc::SOL_SOCKET;
    let optname = libc::SO_SNDBUF;
    let optval = 1024i32.to_ne_bytes();

    let mut get_args = GetsockoptArguments::new(fd, level, optname, Some(optval.into()));
    let mut set_args = SetsockoptArguments::new(fd, level, optname, Some(optval.into()));

    check_getsockopt_call(&mut get_args, &[libc::EBADF])?;
    check_setsockopt_call(&mut set_args, &[libc::EBADF])?;

    Ok(())
}

/// Test getsockopt() and setsockopt() using an argument that could be a fd, but is not.
fn test_non_existent_fd() -> Result<(), String> {
    let fd = 8934;
    let level = libc::SOL_SOCKET;
    let optname = libc::SO_SNDBUF;
    let optval = 1024i32.to_ne_bytes();

    let mut get_args = GetsockoptArguments::new(fd, level, optname, Some(optval.into()));
    let mut set_args = SetsockoptArguments::new(fd, level, optname, Some(optval.into()));

    check_getsockopt_call(&mut get_args, &[libc::EBADF])?;
    check_setsockopt_call(&mut set_args, &[libc::EBADF])?;

    Ok(())
}

/// Test getsockopt() and setsockopt() using a valid fd that is not a socket.
fn test_non_socket_fd() -> Result<(), String> {
    let fd = 0;
    let level = libc::SOL_SOCKET;
    let optname = libc::SO_SNDBUF;
    let optval = 1024i32.to_ne_bytes();

    let mut get_args = GetsockoptArguments::new(fd, level, optname, Some(optval.into()));
    let mut set_args = SetsockoptArguments::new(fd, level, optname, Some(optval.into()));

    check_getsockopt_call(&mut get_args, &[libc::ENOTSOCK])?;
    check_setsockopt_call(&mut set_args, &[libc::ENOTSOCK])?;

    Ok(())
}

/// Test getsockopt() and setsockopt() using a non-null optval and a long optlen.
fn test_long_len() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM | libc::SOCK_NONBLOCK, 0) };
    assert!(fd >= 0);

    let level = libc::SOL_SOCKET;
    let optname = libc::SO_SNDBUF;
    let optval = 1024u64.to_ne_bytes();

    let mut get_args = GetsockoptArguments::new(fd, level, optname, Some(optval.into()));
    let mut set_args = SetsockoptArguments::new(fd, level, optname, Some(optval.into()));

    test_utils::run_and_close_fds(&[fd], || {
        check_getsockopt_call(&mut get_args, &[])?;
        // the optlen should have changed
        test_utils::result_assert_eq(
            get_args.optlen.as_ref().unwrap(),
            &4,
            "The optlen should have changed",
        )?;
        check_setsockopt_call(&mut set_args, &[])?;
        Ok(())
    })
}

/// Test getsockopt() and setsockopt() using a non-null optval and a short optlen.
fn test_short_len() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM | libc::SOCK_NONBLOCK, 0) };
    assert!(fd >= 0);

    let level = libc::SOL_SOCKET;
    let optname = libc::SO_SNDBUF;
    let optlen = 2usize;

    test_utils::run_and_close_fds(&[fd], || {
        // get the socket's initial sndbuf optval
        let mut args = GetsockoptArguments::new(fd, level, optname, Some(vec![0u8; 4]));
        check_getsockopt_call(&mut args, &[])?;
        let expected_optval = args.optval.unwrap();

        // set the buffer to some dummy values
        let dummy_optval = vec![10u8, 11, 12, 13];

        // get only two bytes of the sndbuf optval
        let mut args = GetsockoptArguments {
            fd,
            level,
            optname,
            optval: Some(dummy_optval.clone()),
            optlen: Some(optlen as u32),
        };

        check_getsockopt_call(&mut args, &[])?;

        // check that only the first two bytes changed
        test_utils::result_assert_eq(
            &args.optval.as_ref().unwrap()[..optlen],
            &expected_optval[..optlen],
            "First bytes should be the expected bytes",
        )?;
        test_utils::result_assert_eq(
            &args.optval.as_ref().unwrap()[optlen..],
            &dummy_optval[optlen..],
            "Remaining bytes should not have changed",
        )?;
        test_utils::result_assert_eq(
            args.optlen.as_ref().unwrap(),
            &(optlen as u32),
            "The optlen should not have changed",
        )?;

        // try setting only two bytes of the sndbuf optval
        let mut args = SetsockoptArguments {
            fd,
            level,
            optname,
            optval: Some(1024i32.to_ne_bytes().into()),
            optlen: optlen as u32,
        };

        check_setsockopt_call(&mut args, &[libc::EINVAL])
    })
}

/// Test getsockopt() and setsockopt() using a non-null optval and a zero optlen.
fn test_zero_len() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM | libc::SOCK_NONBLOCK, 0) };
    assert!(fd >= 0);

    let level = libc::SOL_SOCKET;
    let optname = libc::SO_SNDBUF;
    let optval = 1024i32.to_ne_bytes();
    let optlen = 0;

    let mut get_args = GetsockoptArguments {
        fd,
        level,
        optname,
        optval: Some(optval.into()),
        optlen: Some(optlen),
    };
    let mut set_args = SetsockoptArguments {
        fd,
        level,
        optname,
        optval: Some(optval.into()),
        optlen,
    };

    test_utils::run_and_close_fds(&[fd], || {
        check_getsockopt_call(&mut get_args, &[])?;
        // the optval and optlen should not have changed
        test_utils::result_assert_eq(
            &get_args.optval.as_ref().unwrap()[..],
            &optval,
            "The optval should not have changed",
        )?;
        test_utils::result_assert_eq(
            get_args.optlen.as_ref().unwrap(),
            &optlen,
            "The optlen should not have changed",
        )?;
        check_setsockopt_call(&mut set_args, &[libc::EINVAL])?;
        Ok(())
    })
}

/// Test getsockopt() and setsockopt() using a null optval and a correct optlen.
fn test_null_val() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM | libc::SOCK_NONBLOCK, 0) };
    assert!(fd >= 0);

    let level = libc::SOL_SOCKET;
    let optname = libc::SO_SNDBUF;
    let optlen = 4;

    let mut get_args = GetsockoptArguments {
        fd,
        level,
        optname,
        optval: None,
        optlen: Some(optlen),
    };
    let mut set_args = SetsockoptArguments {
        fd,
        level,
        optname,
        optval: None,
        optlen,
    };

    test_utils::run_and_close_fds(&[fd], || {
        check_getsockopt_call(&mut get_args, &[libc::EFAULT])?;
        check_setsockopt_call(&mut set_args, &[libc::EFAULT])?;
        Ok(())
    })
}

/// Test getsockopt() and setsockopt() using a null optval and a zero optlen.
fn test_null_val_zero_len() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM | libc::SOCK_NONBLOCK, 0) };
    assert!(fd >= 0);

    let level = libc::SOL_SOCKET;
    let optname = libc::SO_SNDBUF;
    let optlen = 0;

    let mut get_args = GetsockoptArguments {
        fd,
        level,
        optname,
        optval: None,
        optlen: Some(optlen),
    };
    let mut set_args = SetsockoptArguments {
        fd,
        level,
        optname,
        optval: None,
        optlen,
    };

    test_utils::run_and_close_fds(&[fd], || {
        check_getsockopt_call(&mut get_args, &[])?;
        check_setsockopt_call(&mut set_args, &[libc::EINVAL])?;
        Ok(())
    })
}

/// Test getsockopt() and setsockopt() using a null optval and a non-zero optlen.
fn test_null_val_nonzero_len() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM | libc::SOCK_NONBLOCK, 0) };
    assert!(fd >= 0);

    let level = libc::SOL_SOCKET;
    let optname = libc::SO_SNDBUF;
    let optlen = 1;

    let mut get_args = GetsockoptArguments {
        fd,
        level,
        optname,
        optval: None,
        optlen: Some(optlen),
    };
    let mut set_args = SetsockoptArguments {
        fd,
        level,
        optname,
        optval: None,
        optlen,
    };

    test_utils::run_and_close_fds(&[fd], || {
        check_getsockopt_call(&mut get_args, &[libc::EFAULT])?;
        // glibc returns EINVAL but shadow returns EFAULT
        check_setsockopt_call(&mut set_args, &[libc::EINVAL, libc::EFAULT])?;
        Ok(())
    })
}

/// Test getsockopt() using a null optval and a null optlen.
fn test_null_val_null_len() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM | libc::SOCK_NONBLOCK, 0) };
    assert!(fd >= 0);

    let mut args = GetsockoptArguments {
        fd,
        level: libc::SOL_SOCKET,
        optname: libc::SO_SNDBUF,
        optval: None,
        optlen: None,
    };

    test_utils::run_and_close_fds(&[fd], || check_getsockopt_call(&mut args, &[libc::EFAULT]))
}

/// Test getsockopt() and setsockopt() using an invalid level.
fn test_invalid_level() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM | libc::SOCK_NONBLOCK, 0) };
    assert!(fd >= 0);

    // these levels should not be valid for a TCP socket
    let levels = &[-100, -1, libc::SOL_RAW, libc::SOL_UDP, libc::SOL_NETLINK];

    test_utils::run_and_close_fds(&[fd], || {
        for &level in levels {
            let optname = libc::SO_SNDBUF;
            let optval = 1024i32.to_ne_bytes();

            let mut get_args = GetsockoptArguments::new(fd, level, optname, Some(optval.into()));
            let mut set_args = SetsockoptArguments::new(fd, level, optname, Some(optval.into()));

            // glibc returns EOPNOTSUPP but Shadow returns ENOPROTOOPT
            check_getsockopt_call(&mut get_args, &[libc::EOPNOTSUPP, libc::ENOPROTOOPT])?;
            check_setsockopt_call(&mut set_args, &[libc::ENOPROTOOPT])?;
        }

        Ok(())
    })
}

/// Test getsockopt() and setsockopt() using the SO_SNDBUF option.
fn test_so_sndbuf(domain: libc::c_int, sock_type: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type, 0) };
    assert!(fd >= 0);

    let level = libc::SOL_SOCKET;
    let optname = libc::SO_SNDBUF;
    let optvals = [0i32, 512, 1000, 2000, 8192, 16_384];

    test_utils::run_and_close_fds(&[fd], || {
        for &optval in &optvals {
            // The man page (man 7 socket) is incorrect, and the actual minimum doubled value is
            // 2*2048 + some offset. See the definition of SOCK_MIN_SNDBUF in the kernel. We just
            // use 4096 and ignore the offset in these tests.
            let min_sndbuf = 4096;

            bufsize_test_helper(fd, level, optname, optval, min_sndbuf)?;
        }

        Ok(())
    })
}

/// Test getsockopt() and setsockopt() using the SO_RCVBUF option.
fn test_so_rcvbuf(domain: libc::c_int, sock_type: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type, 0) };
    assert!(fd >= 0);

    let level = libc::SOL_SOCKET;
    let optname = libc::SO_RCVBUF;
    let optvals = [0i32, 512, 1000, 2000, 8192, 16_384];

    test_utils::run_and_close_fds(&[fd], || {
        for &optval in &optvals {
            // The man page (man 7 socket) is incorrect, and the actual minimum doubled value is
            // 2048 + some offset. See the definition of SOCK_MIN_RCVBUF in the kernel. We just
            // use 2048 and ignore the offset in these tests.
            let min_rcvbuf = 2048;

            bufsize_test_helper(fd, level, optname, optval, min_rcvbuf)?;
        }

        Ok(())
    })
}

fn bufsize_test_helper(
    fd: libc::c_int,
    level: libc::c_int,
    optname: libc::c_int,
    optval: i32,
    min: i32,
) -> Result<(), String> {
    let optval = optval.to_ne_bytes();

    let mut initial_args = GetsockoptArguments::new(fd, level, optname, Some(vec![0u8; 4]));
    let mut set_args = SetsockoptArguments::new(fd, level, optname, Some(optval.into()));
    let mut after_args = initial_args.clone();

    // get the value, set the value, and then get the value again
    check_getsockopt_call(&mut initial_args, &[])?;
    check_setsockopt_call(&mut set_args, &[])?;
    check_getsockopt_call(&mut after_args, &[])?;

    test_utils::result_assert_ne(
        &initial_args.optval.as_ref().unwrap()[..],
        &[0u8; 4],
        "The initial option was 0",
    )?;

    // convert the bytes to integers
    let set_optval = i32::from_ne_bytes(set_args.optval.as_ref().unwrap()[..].try_into().unwrap());
    let after_optval =
        i32::from_ne_bytes(after_args.optval.as_ref().unwrap()[..].try_into().unwrap());

    // linux always doubles the value when you set it (see man 7 socket)
    let set_optval = 2 * set_optval;

    // the value should be somewhere above the lower limit so that the program cannot set
    // very small sizes
    test_utils::result_assert(
        after_optval >= min,
        &format!(
            "Resulting value {} was expected to be larger than the min {}",
            after_optval, min
        ),
    )?;

    // if the value we set was above the lower limit, they should be equal
    if set_optval >= min {
        test_utils::result_assert_eq(
            after_optval,
            set_optval,
            "Resulting value was expected to be equal",
        )?;
    }

    Ok(())
}

/// Test getsockopt() and setsockopt() using the SO_ERROR option.
fn test_so_error(domain: libc::c_int, sock_type: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type | libc::SOCK_NONBLOCK, 0) };
    assert!(fd >= 0);

    let level = libc::SOL_SOCKET;
    let optname = libc::SO_ERROR;
    let optval = 0i32.to_ne_bytes();

    let mut get_args = GetsockoptArguments::new(fd, level, optname, Some(optval.into()));
    let mut set_args = SetsockoptArguments::new(fd, level, optname, Some(optval.into()));

    test_utils::run_and_close_fds(&[fd], || {
        check_getsockopt_call(&mut get_args, &[])?;
        check_setsockopt_call(&mut set_args, &[libc::ENOPROTOOPT])?;

        let returned_optval =
            i32::from_ne_bytes(get_args.optval.as_ref().unwrap()[..].try_into().unwrap());

        test_utils::result_assert_eq(returned_optval, 0, "Expected there to be no socket error")?;

        // We could try to trigger a socket error here and check to see that the value changed.
        // I tried to do this by making a non-blocking connection to localhost, but it didn't
        // seem to update the error either within Shadow or outside Shadow.

        Ok(())
    })
}

/// Test getsockopt() and setsockopt() using the SO_TYPE option.
fn test_so_type(domain: libc::c_int, sock_type: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type | libc::SOCK_NONBLOCK, 0) };
    assert!(fd >= 0);

    let level = libc::SOL_SOCKET;
    let optname = libc::SO_TYPE;
    let optval = 0i32.to_ne_bytes();

    let mut get_args = GetsockoptArguments::new(fd, level, optname, Some(optval.into()));
    let mut set_args = SetsockoptArguments::new(fd, level, optname, Some(optval.into()));

    test_utils::run_and_close_fds(&[fd], || {
        check_getsockopt_call(&mut get_args, &[])?;
        check_setsockopt_call(&mut set_args, &[libc::ENOPROTOOPT])?;

        let returned_optval =
            i32::from_ne_bytes(get_args.optval.as_ref().unwrap()[..].try_into().unwrap());

        test_utils::result_assert_eq(returned_optval, sock_type, "Wrong socket type")?;

        Ok(())
    })
}

/// Test getsockopt() and setsockopt() using the SO_DOMAIN option.
fn test_so_domain(domain: libc::c_int, sock_type: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type | libc::SOCK_NONBLOCK, 0) };
    assert!(fd >= 0);

    let level = libc::SOL_SOCKET;
    let optname = libc::SO_DOMAIN;
    let optval = 0i32.to_ne_bytes();

    let mut get_args = GetsockoptArguments::new(fd, level, optname, Some(optval.into()));
    let mut set_args = SetsockoptArguments::new(fd, level, optname, Some(optval.into()));

    test_utils::run_and_close_fds(&[fd], || {
        check_getsockopt_call(&mut get_args, &[])?;
        check_setsockopt_call(&mut set_args, &[libc::ENOPROTOOPT])?;

        let returned_optval =
            i32::from_ne_bytes(get_args.optval.as_ref().unwrap()[..].try_into().unwrap());

        test_utils::result_assert_eq(returned_optval, domain, "Wrong socket domain")?;

        Ok(())
    })
}

/// Test getsockopt() and setsockopt() using the SO_PROTOCOL option.
fn test_so_protocol(domain: libc::c_int, sock_type: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type | libc::SOCK_NONBLOCK, 0) };
    assert!(fd >= 0);

    let level = libc::SOL_SOCKET;
    let optname = libc::SO_PROTOCOL;
    let optval = 0i32.to_ne_bytes();

    let mut get_args = GetsockoptArguments::new(fd, level, optname, Some(optval.into()));
    let mut set_args = SetsockoptArguments::new(fd, level, optname, Some(optval.into()));

    test_utils::run_and_close_fds(&[fd], || {
        check_getsockopt_call(&mut get_args, &[])?;
        check_setsockopt_call(&mut set_args, &[libc::ENOPROTOOPT])?;

        let returned_optval =
            i32::from_ne_bytes(get_args.optval.as_ref().unwrap()[..].try_into().unwrap());

        let expected_optval = match (domain, sock_type) {
            (libc::AF_INET, libc::SOCK_STREAM) => libc::IPPROTO_TCP,
            (libc::AF_INET, libc::SOCK_DGRAM) => libc::IPPROTO_UDP,
            (libc::AF_UNIX, _) => 0,
            _ => unimplemented!(),
        };

        test_utils::result_assert_eq(returned_optval, expected_optval, "Wrong socket protocol")?;

        Ok(())
    })
}

/// Test getsockopt() and setsockopt() using the SO_ACCEPTCONN option.
fn test_so_acceptconn(domain: libc::c_int, sock_type: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type | libc::SOCK_NONBLOCK, 0) };
    assert!(fd >= 0);

    let level = libc::SOL_SOCKET;
    let optname = libc::SO_ACCEPTCONN;
    let optval = 0i32.to_ne_bytes();

    let mut get_args = GetsockoptArguments::new(fd, level, optname, Some(optval.into()));
    let mut set_args = SetsockoptArguments::new(fd, level, optname, Some(optval.into()));

    test_utils::run_and_close_fds(&[fd], || {
        check_getsockopt_call(&mut get_args, &[])?;
        check_setsockopt_call(&mut set_args, &[libc::ENOPROTOOPT])?;

        let returned_optval =
            i32::from_ne_bytes(get_args.optval.as_ref().unwrap()[..].try_into().unwrap());

        test_utils::result_assert_eq(
            returned_optval,
            0,
            "Wrong value returned for SO_ACCEPTCONN before listen()",
        )?;

        let listen_rv = unsafe { libc::listen(fd, 100) };

        check_getsockopt_call(&mut get_args, &[])?;
        check_setsockopt_call(&mut set_args, &[libc::ENOPROTOOPT])?;

        let returned_optval =
            i32::from_ne_bytes(get_args.optval.as_ref().unwrap()[..].try_into().unwrap());

        // if listen() was successful, then expecting the sockopt val to be 1
        let expected_optval = if listen_rv == 0 { 1 } else { 0 };

        test_utils::result_assert_eq(
            returned_optval,
            expected_optval,
            "Wrong value returned for SO_ACCEPTCONN after listen()",
        )?;

        Ok(())
    })
}

/// Test getsockopt() and setsockopt() using the SO_BROADCAST option with the value 0.
fn test_so_broadcast_0(domain: libc::c_int, sock_type: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type | libc::SOCK_NONBLOCK, 0) };
    assert!(fd >= 0);

    let level = libc::SOL_SOCKET;
    let optname = libc::SO_BROADCAST;
    let zero = 0i32.to_ne_bytes();

    let mut get_args = GetsockoptArguments::new(fd, level, optname, Some(zero.into()));
    let mut set_args = SetsockoptArguments::new(fd, level, optname, Some(zero.into()));

    test_utils::run_and_close_fds(&[fd], || {
        check_getsockopt_call(&mut get_args, &[])?;
        check_setsockopt_call(&mut set_args, &[])?;

        let returned_optval =
            i32::from_ne_bytes(get_args.optval.as_ref().unwrap()[..].try_into().unwrap());

        test_utils::result_assert_eq(returned_optval, 0, "expected SO_BROADCAST to return 0")?;

        Ok(())
    })
}

/// Test getsockopt() and setsockopt() using the SO_BROADCAST option.
fn test_so_broadcast(domain: libc::c_int, sock_type: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type | libc::SOCK_NONBLOCK, 0) };
    assert!(fd >= 0);

    let level = libc::SOL_SOCKET;
    let optname = libc::SO_BROADCAST;
    let zero = 0i32.to_ne_bytes();
    let one = 1i32.to_ne_bytes();
    let ten = 10i32.to_ne_bytes();

    let mut get_args = GetsockoptArguments::new(fd, level, optname, Some(zero.into()));

    let mut set_args_1 = SetsockoptArguments::new(fd, level, optname, Some(one.into()));
    let mut set_args_2 = SetsockoptArguments::new(fd, level, optname, Some(zero.into()));
    let mut set_args_3 = SetsockoptArguments::new(fd, level, optname, Some(ten.into()));

    test_utils::run_and_close_fds(&[fd], || {
        // initially should be 0
        check_getsockopt_call(&mut get_args, &[])?;
        let returned_optval =
            i32::from_ne_bytes(get_args.optval.as_ref().unwrap()[..].try_into().unwrap());
        test_utils::result_assert_eq(returned_optval, 0, "unexpected value from SO_BROADCAST")?;

        // set to 1
        check_setsockopt_call(&mut set_args_1, &[])?;

        // should now be 1
        check_getsockopt_call(&mut get_args, &[])?;
        let returned_optval =
            i32::from_ne_bytes(get_args.optval.as_ref().unwrap()[..].try_into().unwrap());
        test_utils::result_assert_eq(returned_optval, 1, "unexpected value from SO_BROADCAST")?;

        // set to 0
        check_setsockopt_call(&mut set_args_2, &[])?;

        // should now be 0
        check_getsockopt_call(&mut get_args, &[])?;
        let returned_optval =
            i32::from_ne_bytes(get_args.optval.as_ref().unwrap()[..].try_into().unwrap());
        test_utils::result_assert_eq(returned_optval, 0, "unexpected value from SO_BROADCAST")?;

        // set to 10
        check_setsockopt_call(&mut set_args_3, &[])?;

        // should now be 1
        check_getsockopt_call(&mut get_args, &[])?;
        let returned_optval =
            i32::from_ne_bytes(get_args.optval.as_ref().unwrap()[..].try_into().unwrap());
        test_utils::result_assert_eq(returned_optval, 1, "unexpected value from SO_BROADCAST")?;

        Ok(())
    })
}

/// Test getsockopt() and setsockopt() using the TCP_INFO option.
fn test_tcp_info(domain: libc::c_int, sock_type: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type, 0) };
    assert!(fd >= 0);

    let level = libc::SOL_TCP;
    let optname = libc::TCP_INFO;
    let optval = [0; 20];

    let mut get_args = GetsockoptArguments::new(fd, level, optname, Some(optval.into()));
    let mut set_args = SetsockoptArguments::new(fd, level, optname, Some(optval.into()));

    test_utils::run_and_close_fds(&[fd], || {
        let expected_errnos = if sock_type == libc::SOCK_STREAM {
            vec![]
        } else {
            vec![libc::EOPNOTSUPP]
        };
        check_getsockopt_call(&mut get_args, &expected_errnos)?;
        check_setsockopt_call(&mut set_args, &[libc::ENOPROTOOPT])?;

        // the libc package doesn't expose 'struct tcp_info' so if we wanted to look at the actual
        // values we'd have to use our own binding, but it's probably good enough here just to make
        // sure getsockopt() is returning something without an error

        Ok(())
    })
}

/// Test getsockopt() and setsockopt() using the TCP_NODELAY option.
fn test_tcp_nodelay(domain: libc::c_int, sock_type: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type, 0) };
    assert!(fd >= 0);

    let level = libc::SOL_TCP;
    let optname = libc::TCP_NODELAY;

    // shadow doesn't support setting a value of 0
    let optval = 1i32.to_ne_bytes();
    let zero = 0i32.to_ne_bytes();

    let mut get_args_1 = GetsockoptArguments::new(fd, level, optname, Some(zero.into()));
    let mut get_args_2 = GetsockoptArguments::new(fd, level, optname, Some(zero.into()));
    let mut set_args = SetsockoptArguments::new(fd, level, optname, Some(optval.into()));

    test_utils::run_and_close_fds(&[fd], || {
        let expected_errnos = if sock_type == libc::SOCK_STREAM {
            vec![]
        } else {
            vec![libc::ENOPROTOOPT, libc::EOPNOTSUPP]
        };
        check_getsockopt_call(&mut get_args_1, &expected_errnos)?;

        if sock_type == libc::SOCK_STREAM {
            // in shadow will return 1, but in linux will return 0
            let value = u32::from_ne_bytes(get_args_1.optval.unwrap().try_into().unwrap());
            test_utils::result_assert([0, 1].contains(&value), "Unexpected value for TCP_NODELAY")?;
        }

        check_setsockopt_call(&mut set_args, &expected_errnos)?;
        check_getsockopt_call(&mut get_args_2, &expected_errnos)?;

        if sock_type == libc::SOCK_STREAM {
            let value = u32::from_ne_bytes(get_args_2.optval.unwrap().try_into().unwrap());
            test_utils::result_assert_eq(value, 1, "Unexpected value for TCP_NODELAY")?;
        }

        Ok(())
    })
}

/// Test getsockopt() and setsockopt() using the TCP_CONGESTION option.
fn test_tcp_congestion(domain: libc::c_int, sock_type: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type, 0) };
    assert!(fd >= 0);

    let level = libc::SOL_TCP;
    let optname = libc::TCP_CONGESTION;

    let get_args_1 = GetsockoptArguments::new(fd, level, optname, Some(vec![0u8; 20]));
    let get_args_2 = GetsockoptArguments::new(fd, level, optname, Some(vec![0u8; 3]));
    let mut set_args_1 = SetsockoptArguments::new(fd, level, optname, Some("reno".into()));
    let mut set_args_2 = SetsockoptArguments::new(fd, level, optname, Some("ren".into()));

    test_utils::run_and_close_fds(&[fd], || {
        for mut get_args in [get_args_1, get_args_2] {
            let expected_errnos = if sock_type == libc::SOCK_STREAM {
                vec![]
            } else {
                vec![libc::ENOPROTOOPT, libc::EOPNOTSUPP]
            };
            check_getsockopt_call(&mut get_args, &expected_errnos)?;

            if sock_type != libc::SOCK_STREAM {
                // if not a TCP socket, no need to check the results
                continue;
            }

            let returned_str_len = get_args.optlen.unwrap() as usize;

            test_utils::result_assert_eq(
                returned_str_len,
                std::cmp::min(get_args.optval.as_ref().unwrap().len(), 16),
                "Returned length is unexpected",
            )?;

            let returned_str = get_args.optval.as_ref().unwrap();

            // limit to the number of bytes returned by the kernel
            let returned_str = &returned_str[..returned_str_len];

            // limit to the bytes before the first nul
            let returned_str = &returned_str[..returned_str
                .iter()
                .position(|&c| c == b'\0')
                // if no nul was found, use the entire string
                .unwrap_or(returned_str.len())];

            let expected_values = [&b"reno"[..], &b"cubic"[..], &b"bbr"[..]];
            // shorten the expected strings if necessary
            let expected_values =
                expected_values.map(|x| &x[..std::cmp::min(x.len(), returned_str_len)]);

            test_utils::result_assert(
                expected_values.contains(&returned_str),
                "Unexpected value for TCP_CONGESTION",
            )?;
        }

        // try setting a valid name
        let expected_errnos = if sock_type == libc::SOCK_STREAM {
            vec![]
        } else {
            vec![libc::ENOPROTOOPT, libc::EOPNOTSUPP]
        };
        check_setsockopt_call(&mut set_args_1, &expected_errnos)?;

        // try setting an invalid name
        let expected_errnos = if sock_type == libc::SOCK_STREAM {
            vec![libc::ENOENT]
        } else {
            vec![libc::ENOPROTOOPT, libc::EOPNOTSUPP]
        };
        check_setsockopt_call(&mut set_args_2, &expected_errnos)?;

        Ok(())
    })
}

fn check_getsockopt_call(
    args: &mut GetsockoptArguments,
    expected_errnos: &[libc::c_int],
) -> Result<(), String> {
    // if the pointers will be non-null, make sure the length is not greater than the actual data size
    // so that we don't segfault
    if let (Some(optval), Some(optlen)) = (&args.optval, &args.optlen) {
        assert!(*optlen as usize <= optval.len());
    }

    let optval_ptr = match &mut args.optval {
        Some(slice) => slice.as_mut_ptr(),
        None => std::ptr::null_mut(),
    };

    test_utils::check_system_call!(
        move || unsafe {
            libc::getsockopt(
                args.fd,
                args.level,
                args.optname,
                optval_ptr as *mut core::ffi::c_void,
                args.optlen.as_mut_ptr(),
            )
        },
        expected_errnos,
    )?;

    Ok(())
}

fn check_setsockopt_call(
    args: &mut SetsockoptArguments,
    expected_errnos: &[libc::c_int],
) -> Result<(), String> {
    // if the pointers will be non-null, make sure the length is not greater than the actual data size
    // so that we don't segfault
    if let Some(optval) = &args.optval {
        assert!(args.optlen as usize <= optval.len());
    }

    let optval_ptr = match &args.optval {
        Some(slice) => slice.as_ptr(),
        None => std::ptr::null(),
    };

    test_utils::check_system_call!(
        move || unsafe {
            libc::setsockopt(
                args.fd,
                args.level,
                args.optname,
                optval_ptr as *mut core::ffi::c_void,
                args.optlen,
            )
        },
        expected_errnos,
    )?;

    Ok(())
}
