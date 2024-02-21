/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use test_utils::set;
use test_utils::TestEnvironment as TestEnv;

struct BindArguments {
    fd: libc::c_int,
    addr: Option<libc::sockaddr_nl>, // if None, a null pointer should be used
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
    let mut tests: Vec<test_utils::ShadowTest<_, _>> = vec![];

    let sock_types = &[libc::SOCK_RAW, libc::SOCK_DGRAM];

    for &sock_type in sock_types {
        for &flag in [0, libc::SOCK_NONBLOCK, libc::SOCK_CLOEXEC].iter() {
            // add details to the test names to avoid duplicates
            let append_args = |s| format!("{} <type={},flag={}>", s, sock_type, flag);

            tests.extend(vec![
                test_utils::ShadowTest::new(
                    &append_args("test_null_addr"),
                    move || test_null_addr(sock_type, flag),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_double_bind_socket"),
                    move || test_double_bind_socket(sock_type, flag),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_short_addr"),
                    move || test_short_addr(sock_type, flag),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_supported_groups"),
                    move || test_supported_groups(sock_type, flag),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                // This test is tested only in Shadow because Linux supports all the groups, while
                // Shadow does not.
                test_utils::ShadowTest::new(
                    &append_args("test_unsupported_groups"),
                    move || test_unsupported_groups(sock_type, flag),
                    set![TestEnv::Shadow],
                ),
            ]);
        }
    }

    tests
}

// test binding a valid fd, but with a NULL address
fn test_null_addr(sock_type: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_NETLINK, sock_type | flag, libc::NETLINK_ROUTE) };
    assert!(fd >= 0);

    let args = BindArguments {
        fd,
        addr: None,
        addr_len: 5,
    };

    test_utils::run_and_close_fds(&[fd], || check_bind_call(&args, Some(libc::EFAULT)))
}

// test binding a socket twice to the same address
fn test_double_bind_socket(sock_type: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_NETLINK, sock_type | flag, libc::NETLINK_ROUTE) };
    assert!(fd >= 0);

    let mut addr: libc::sockaddr_nl = unsafe { std::mem::zeroed() };
    addr.nl_family = libc::AF_NETLINK as u16;
    addr.nl_pid = 0;
    addr.nl_groups = 0;

    let addr_len = std::mem::size_of::<libc::sockaddr_nl>() as u32;

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

// test binding a valid fd and address, but an address length that is too low
fn test_short_addr(sock_type: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_NETLINK, sock_type | flag, libc::NETLINK_ROUTE) };
    assert!(fd >= 0);

    let mut addr: libc::sockaddr_nl = unsafe { std::mem::zeroed() };
    addr.nl_family = libc::AF_NETLINK as u16;
    addr.nl_pid = 0;
    addr.nl_groups = 0;

    let addr_len = std::mem::size_of::<libc::sockaddr_nl>() as u32 - 1;

    let args = BindArguments {
        fd,
        addr: Some(addr),
        addr_len,
    };

    test_utils::run_and_close_fds(&[fd], || check_bind_call(&args, Some(libc::EINVAL)))
}

// test binding a valid fd and an address with supported groups
fn test_supported_groups(sock_type: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_NETLINK, sock_type | flag, libc::NETLINK_ROUTE) };
    assert!(fd >= 0);

    let mut addr: libc::sockaddr_nl = unsafe { std::mem::zeroed() };
    addr.nl_family = libc::AF_NETLINK as u16;
    addr.nl_pid = 0;
    addr.nl_groups = (libc::RTMGRP_IPV4_IFADDR | libc::RTMGRP_IPV6_IFADDR) as u32;

    let addr_len = std::mem::size_of::<libc::sockaddr_nl>() as u32;

    let args = BindArguments {
        fd,
        addr: Some(addr),
        addr_len,
    };

    test_utils::run_and_close_fds(&[fd], || check_bind_call(&args, None))
}

// test binding a valid fd and an address with unsupported groups
fn test_unsupported_groups(sock_type: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_NETLINK, sock_type | flag, libc::NETLINK_ROUTE) };
    assert!(fd >= 0);

    let mut addr: libc::sockaddr_nl = unsafe { std::mem::zeroed() };
    addr.nl_family = libc::AF_NETLINK as u16;
    addr.nl_pid = 0;
    addr.nl_groups = libc::RTMGRP_IPV4_RULE as u32;

    let addr_len = std::mem::size_of::<libc::sockaddr_nl>() as u32;

    let args = BindArguments {
        fd,
        addr: Some(addr),
        addr_len,
    };

    test_utils::run_and_close_fds(&[fd], || check_bind_call(&args, Some(libc::EINVAL)))
}

fn check_bind_call(
    args: &BindArguments,
    expected_errno: Option<libc::c_int>,
) -> Result<(), String> {
    // get a pointer to the sockaddr and the size of the structure
    // careful use of references here makes sure we don't copy memory, leading to stale pointers
    let (addr_ptr, addr_max_len) = match args.addr {
        Some(ref x) => (
            x as *const _ as *const libc::sockaddr,
            std::mem::size_of_val(x) as u32,
        ),
        None => (std::ptr::null(), 0),
    };

    // if the pointer is non-null, make sure the provided size is not greater than the actual
    // data size so that we don't segfault
    assert!(addr_ptr.is_null() || args.addr_len <= addr_max_len);

    let rv = unsafe { libc::bind(args.fd, addr_ptr, args.addr_len) };

    let errno = test_utils::get_errno();

    match expected_errno {
        // if we expect the call to return an error (rv should be -1)
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
