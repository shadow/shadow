/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use test_utils::TestEnvironment as TestEnv;
use test_utils::set;

struct SocketpairArguments {
    domain: libc::c_int,
    sock_type: libc::c_int,
    flag: libc::c_int,
    protocol: libc::c_int,
    fds: Option<[libc::c_int; 2]>, // if None, a null pointer should be used
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

    let results = test_utils::run_tests(&tests, summarize)?;
    let used_fds: Vec<_> = results
        .into_iter()
        .flatten()
        .flat_map(|x| x.to_vec())
        .collect();
    let dedup: std::collections::HashSet<_> = used_fds.iter().cloned().collect();

    // return an error if not all file descriptors were unique
    if used_fds.len() != dedup.len() {
        return Err("Duplicate file descriptors were returned".to_string());
    }

    println!("Success.");
    Ok(())
}

fn get_tests() -> Vec<test_utils::ShadowTest<Option<[libc::c_int; 2]>, String>> {
    let mut tests: Vec<test_utils::ShadowTest<_, _>> = vec![test_utils::ShadowTest::new(
        "test_null_fds",
        test_null_fds,
        set![TestEnv::Libc, TestEnv::Shadow],
    )];

    // tests to repeat for different socket options
    for &domain in [libc::AF_UNIX, libc::AF_LOCAL, libc::AF_INET].iter() {
        for &sock_type in [libc::SOCK_STREAM, libc::SOCK_DGRAM].iter() {
            for &flag in [0, libc::SOCK_NONBLOCK, libc::SOCK_CLOEXEC].iter() {
                for &protocol in [0, libc::IPPROTO_TCP, libc::IPPROTO_UDP].iter() {
                    // add details to the test names to avoid duplicates
                    let append_args = |s| {
                        format!(
                            "{} <domain={},type={},flag={},protocol={}>",
                            s, domain, sock_type, flag, protocol
                        )
                    };

                    let more_tests: Vec<test_utils::ShadowTest<_, _>> = vec![
                        test_utils::ShadowTest::new(
                            &append_args("test_arguments"),
                            move || test_arguments(domain, sock_type, flag, protocol),
                            set![TestEnv::Libc, TestEnv::Shadow],
                        ),
                        test_utils::ShadowTest::new(
                            &append_args("test_sockname_peername"),
                            move || test_sockname_peername(domain, sock_type, flag, protocol),
                            set![TestEnv::Libc, TestEnv::Shadow],
                        ),
                    ];

                    tests.extend(more_tests);
                }
            }
        }
    }

    tests
}

/// Test socketpair with a null fd array.
fn test_null_fds() -> Result<Option<[libc::c_int; 2]>, String> {
    // socketpair() may mutate fds
    let mut args = SocketpairArguments {
        domain: libc::AF_UNIX,
        sock_type: libc::SOCK_STREAM,
        flag: 0,
        protocol: 0,
        fds: None,
    };

    check_socketpair_call(&mut args, Some(&[libc::EFAULT]))?;

    Ok(None)
}

/// Test socketpair with various arguments.
fn test_arguments(
    domain: libc::c_int,
    sock_type: libc::c_int,
    flag: libc::c_int,
    protocol: libc::c_int,
) -> Result<Option<[libc::c_int; 2]>, String> {
    // socketpair() may mutate fds
    let mut args = SocketpairArguments {
        domain,
        sock_type,
        flag,
        protocol,
        fds: Some([44, 55]),
    };

    // make a list of all the possible errnos
    let mut expected_errnos = vec![];

    // linux only supports socketpair for the following domains
    if ![libc::AF_UNIX, libc::AF_LOCAL, libc::AF_TIPC].contains(&domain) {
        expected_errnos.push(libc::EOPNOTSUPP);
    }

    // does not support protocols other than the default
    if protocol != 0 {
        expected_errnos.push(libc::EPROTONOSUPPORT);
    }

    let expected_errnos = if !expected_errnos.is_empty() {
        Some(expected_errnos.as_slice())
    } else {
        None
    };

    check_socketpair_call(&mut args, expected_errnos)?;

    // if there was an (expected) error, make sure the fds array did not change
    if expected_errnos.is_some() {
        // unlike what the man page specifies, linux will modify the "sv" on failure because
        // of a kernel bug(?) caused by:
        // https://github.com/torvalds/linux/commit/016a266bdfeda268afb2228b6217fd4771334635
        /*
        test_utils::result_assert_eq(args.fds.unwrap()[0], 44, "fds[0] changed")?;
        test_utils::result_assert_eq(args.fds.unwrap()[1], 55, "fds[1] changed")?;
        */

        return Ok(None);
    }

    Ok(args.fds)
}

/// Test socketpair with various arguments and check if getsockname and getpeername work.
fn test_sockname_peername(
    domain: libc::c_int,
    sock_type: libc::c_int,
    flag: libc::c_int,
    protocol: libc::c_int,
) -> Result<Option<[libc::c_int; 2]>, String> {
    let fds = test_arguments(domain, sock_type, flag, protocol);

    // check that getsockname() and getpeername() work without returning ENOTCONN,
    // and that they return the same domain.
    if let Ok(Some(fds)) = fds {
        compare_sockname_peername(fds[0], fds[1])?;
        compare_sockname_peername(fds[1], fds[0])?;
    }

    fds
}

/// Run getsockname() on one fd and getpeername() on another fd, and make sure they
/// match. Assumes that the sockets were created with socketpair().
fn compare_sockname_peername(
    fd_sockname: libc::c_int,
    fd_peername: libc::c_int,
) -> Result<(), String> {
    let mut sockname_addr = libc::sockaddr_un {
        sun_family: 123u16,
        sun_path: [1; 108],
    };
    sockname_addr.sun_path[107] = 0;

    {
        let mut size = std::mem::size_of_val(&sockname_addr) as u32;
        let rv = unsafe {
            libc::getsockname(
                fd_sockname,
                std::ptr::from_mut(&mut sockname_addr) as *mut libc::sockaddr,
                std::ptr::from_mut(&mut size),
            )
        };
        assert_eq!(rv, 0);
        // since the sockets from socketpair() are unnamed, only the address family will be returned
        assert_eq!(size, 2);
    }

    let mut peername_addr = libc::sockaddr_un {
        sun_family: 321u16,
        sun_path: [2; 108],
    };
    peername_addr.sun_path[107] = 0;

    {
        let mut size = std::mem::size_of_val(&peername_addr) as u32;
        let rv = unsafe {
            libc::getpeername(
                fd_peername,
                std::ptr::from_mut(&mut peername_addr) as *mut libc::sockaddr,
                std::ptr::from_mut(&mut size),
            )
        };
        assert_eq!(rv, 0);
        // since the sockets from socketpair() are unnamed, only the address family will be returned
        assert_eq!(size, 2);
    }

    // since the returned size will be only 2 bytes, we only need to compare the address family
    test_utils::result_assert_eq(
        &sockname_addr.sun_family,
        &peername_addr.sun_family,
        "Unexpected family",
    )?;

    Ok(())
}

fn check_socketpair_call(
    args: &mut SocketpairArguments,
    expected_errnos: Option<&[libc::c_int]>,
) -> Result<(), String> {
    let rv = unsafe {
        libc::socketpair(
            args.domain,
            args.sock_type | args.flag,
            args.protocol,
            match &mut args.fds {
                Some(x) => x.as_mut_ptr(),
                None => std::ptr::null_mut(),
            },
        )
    };

    let errno = test_utils::get_errno();

    match expected_errnos {
        // if we expect the socketpair() call to return an error (rv should be -1)
        Some(expected_errnos) => {
            if rv != -1 {
                return Err(format!("Expecting a return value of -1, received {}", rv));
            }
            if !expected_errnos.contains(&errno) {
                return Err(format!(
                    "Expecting errnos {:?}, received {} \"{}\"",
                    expected_errnos,
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
