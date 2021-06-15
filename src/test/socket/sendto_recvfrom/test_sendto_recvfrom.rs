/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use std::hash::Hasher;

use rand::RngCore;
use rand::SeedableRng;

use test_utils::TestEnvironment as TestEnv;
use test_utils::{set, AsMutPtr, AsPtr};

struct SendtoArguments<'a> {
    fd: libc::c_int,
    buf: Option<&'a [u8]>,
    len: libc::size_t,
    flags: libc::c_int,
    addr: Option<libc::sockaddr_in>,
    addr_len: libc::socklen_t,
}

struct RecvfromArguments<'a> {
    fd: libc::c_int,
    buf: Option<&'a mut [u8]>,
    len: libc::size_t,
    flags: libc::c_int,
    addr: Option<libc::sockaddr_in>,
    addr_len: Option<libc::socklen_t>,
}

impl Default for SendtoArguments<'_> {
    fn default() -> Self {
        Self {
            fd: -1,
            buf: None,
            len: 0,
            flags: 0,
            addr: None,
            addr_len: 0,
        }
    }
}

impl Default for RecvfromArguments<'_> {
    fn default() -> Self {
        Self {
            fd: -1,
            buf: None,
            len: 0,
            flags: 0,
            addr: None,
            addr_len: None,
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
            "test_not_connected_tcp",
            test_not_connected_tcp,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_not_connected_udp",
            test_not_connected_udp,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_null_buf <tcp>",
            || test_null_buf(libc::SOCK_STREAM),
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_null_buf <udp>",
            || test_null_buf(libc::SOCK_DGRAM),
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_zero_len_buf <tcp>",
            || test_zero_len_buf(libc::SOCK_STREAM),
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_zero_len_buf <udp>",
            || test_zero_len_buf(libc::SOCK_DGRAM),
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_nonblocking_tcp",
            test_nonblocking_tcp,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_invalid_flag <tcp>",
            || test_invalid_flag(libc::SOCK_STREAM),
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_invalid_flag <udp>",
            || test_invalid_flag(libc::SOCK_DGRAM),
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_flag_dontwait <tcp>",
            || test_flag_dontwait(libc::SOCK_STREAM),
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_flag_dontwait <udp>",
            || test_flag_dontwait(libc::SOCK_DGRAM),
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
    ];

    let domains = [libc::AF_INET];
    let flags = [0, libc::SOCK_NONBLOCK, libc::SOCK_CLOEXEC];

    for &domain in domains.iter() {
        for &flag in flags.iter() {
            // add details to the test names to avoid duplicates
            let append_args = |s| format!("{} <domain={},flag={}>", s, domain, flag);

            let more_tests: Vec<test_utils::ShadowTest<_, _>> = vec![
                test_utils::ShadowTest::new(
                    &append_args("test_null_addr <tcp>"),
                    move || test_null_addr(domain, libc::SOCK_STREAM, flag),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_null_addr <udp>"),
                    move || test_null_addr(domain, libc::SOCK_DGRAM, flag),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_null_addr_not_connected_udp"),
                    move || test_null_addr_not_connected_udp(domain, flag),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_null_addr_len <tcp>"),
                    move || test_null_addr_len(domain, libc::SOCK_STREAM, flag),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_null_addr_len <udp>"),
                    move || test_null_addr_len(domain, libc::SOCK_DGRAM, flag),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_null_both <tcp>"),
                    move || test_null_both(domain, libc::SOCK_STREAM, flag),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_null_both <udp>"),
                    move || test_null_both(domain, libc::SOCK_DGRAM, flag),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_recv_addr_udp"),
                    move || test_recv_addr_udp(domain, flag),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_nonnull_addr_tcp"),
                    move || test_nonnull_addr_tcp(domain, flag),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_large_buf_tcp"),
                    move || test_large_buf_tcp(domain, flag),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_large_buf_udp"),
                    move || test_large_buf_udp(domain, flag),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_short_recv_buf_udp"),
                    move || test_short_recv_buf_udp(domain, flag),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_msg_order_udp"),
                    move || test_msg_order_udp(domain, flag),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
            ];

            tests.extend(more_tests);
        }
    }

    tests
}

/// Test sendto() and recvfrom() using an argument that cannot be a fd.
fn test_invalid_fd() -> Result<(), String> {
    // expect both sendto() and recvfrom() to return EBADF
    fd_test_helper(-1, &[libc::EBADF], &[libc::EBADF])
}

/// Test sendto() and recvfrom() using an argument that could be a fd, but is not.
fn test_non_existent_fd() -> Result<(), String> {
    // expect both sendto() and recvfrom() to return EBADF
    fd_test_helper(8934, &[libc::EBADF], &[libc::EBADF])
}

/// Test sendto() and recvfrom() using a valid fd that is not a socket.
fn test_non_socket_fd() -> Result<(), String> {
    // expect both sendto() and recvfrom() to return ENOTSOCK
    fd_test_helper(0, &[libc::ENOTSOCK], &[libc::ENOTSOCK])
}

/// Test sendto() and recvfrom() using a TCP socket that is not conneected, but using a
/// non-null address argument.
fn test_not_connected_tcp() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM | libc::SOCK_NONBLOCK, 0) };
    assert!(fd >= 0);

    test_utils::run_and_close_fds(&[fd], || {
        // expect sendto() to return ENOTCONN (or EPIPE on Linux, see "BUGS" in "man 2 sendto")
        // and recvfrom() to return ENOTCONN
        fd_test_helper(fd, &[libc::ENOTCONN, libc::EPIPE], &[libc::ENOTCONN])
    })
}

/// Test sendto() and recvfrom() using a UDP socket that is not conneected, but using a
/// non-null address argument.
fn test_not_connected_udp() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_DGRAM | libc::SOCK_NONBLOCK, 0) };
    assert!(fd >= 0);

    // expect sendto() to write successfully, but recvfrom() to return EAGAIN
    test_utils::run_and_close_fds(&[fd], || fd_test_helper(fd, &[], &[libc::EAGAIN]))
}

/// Test sendto() and recvfrom() using a null buffer, and non-zero buffer length.
fn test_null_buf(sock_type: libc::c_int) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(libc::AF_INET, sock_type | libc::SOCK_NONBLOCK, 0) };
    let fd_server = unsafe { libc::socket(libc::AF_INET, sock_type | libc::SOCK_NONBLOCK, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    // connect the client fd to the server
    let fd_server = match sock_type {
        libc::SOCK_STREAM => {
            let fd_accepted = tcp_connect_helper(fd_client, fd_server, libc::SOCK_NONBLOCK);
            unsafe { libc::close(fd_server) };
            fd_accepted
        }
        libc::SOCK_DGRAM => {
            udp_connect_helper(fd_client, fd_server, /* connect= */ true);
            fd_server
        }
        _ => unreachable!(),
    };

    let sendto_buf: Vec<u8> = vec![1, 2, 3];

    let sendto_args_1 = SendtoArguments {
        fd: fd_client,
        len: 5,
        buf: None,
        ..Default::default()
    };

    let sendto_args_2 = SendtoArguments {
        fd: fd_client,
        len: sendto_buf.len(),
        buf: Some(&sendto_buf),
        ..Default::default()
    };

    let mut recvfrom_args = RecvfromArguments {
        fd: fd_server,
        len: 5,
        buf: None,
        ..Default::default()
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // send using a null buffer; an EFAULT error expected
        check_sendto_call(&sendto_args_1, &[libc::EFAULT], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read using a null buffer; an EAGAIN error expected
        check_recvfrom_call(&mut recvfrom_args, &[libc::EAGAIN], true)?;

        // send 3 bytes using a non-null buffer; no error expected
        check_sendto_call(&sendto_args_2, &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read using a null buffer; an EFAULT error expected
        check_recvfrom_call(&mut recvfrom_args, &[libc::EFAULT], true)?;

        Ok(())
    })
}

/// Test sendto() and recvfrom() using a buffer length of zero, and a non-null buffer.
fn test_zero_len_buf(sock_type: libc::c_int) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(libc::AF_INET, sock_type | libc::SOCK_NONBLOCK, 0) };
    let fd_server = unsafe { libc::socket(libc::AF_INET, sock_type | libc::SOCK_NONBLOCK, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    // connect the client fd to the server
    let fd_server = match sock_type {
        libc::SOCK_STREAM => {
            let fd_accepted = tcp_connect_helper(fd_client, fd_server, libc::SOCK_NONBLOCK);
            unsafe { libc::close(fd_server) };
            fd_accepted
        }
        libc::SOCK_DGRAM => {
            udp_connect_helper(fd_client, fd_server, /* connect= */ true);
            fd_server
        }
        _ => unreachable!(),
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // send 0 bytes; no errors expected
        simple_sendto_helper(fd_client, &vec![], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // receive 0 bytes; an EAGAIN error expected only if TCP
        // UDP can receive messages of 0 bytes
        let e = match sock_type {
            libc::SOCK_STREAM => vec![libc::EAGAIN],
            libc::SOCK_DGRAM => vec![],
            _ => unreachable!(),
        };
        simple_recvfrom_helper(fd_server, &mut vec![], &e, true)?;

        // send >0 bytes; no errors expected
        simple_sendto_helper(fd_client, &vec![1, 2, 3], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // receive 0 bytes; no errors expected
        simple_recvfrom_helper(fd_server, &mut vec![], &[], true)?;

        Ok(())
    })
}

/// Test sendto() and recvfrom() using a non-blocking TCP socket.
fn test_nonblocking_tcp() -> Result<(), String> {
    let fd_client =
        unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM | libc::SOCK_NONBLOCK, 0) };
    let fd_server =
        unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM | libc::SOCK_NONBLOCK, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    // connect the client fd to the server
    let fd_accepted = tcp_connect_helper(fd_client, fd_server, libc::SOCK_NONBLOCK);

    test_utils::run_and_close_fds(&[fd_client, fd_server, fd_accepted], || {
        // try to read 10 bytes; an EAGAIN error expected
        simple_recvfrom_helper(fd_accepted, &mut vec![0u8; 10], &[libc::EAGAIN], true)?;

        // send 10 bytes; no errors expected
        simple_sendto_helper(fd_client, &vec![1u8; 10], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read 10 bytes into a 20 byte buffer; no errors expected
        simple_recvfrom_helper(fd_accepted, &mut vec![0u8; 20], &[], false)?;

        // try to read 10 bytes; an EAGAIN error expected
        simple_recvfrom_helper(fd_accepted, &mut vec![0u8; 10], &[libc::EAGAIN], false)?;

        let mut send_hash = std::collections::hash_map::DefaultHasher::new();
        let mut recv_hash = std::collections::hash_map::DefaultHasher::new();

        let mut send_rng = rand::rngs::SmallRng::seed_from_u64(0);

        let mut send_buf = vec![0u8; 1_000_000];
        let mut recv_buf = vec![0u8; 1_000_000];

        // send bytes until an EAGAIN error
        let mut bytes_sent = 0;
        loop {
            send_rng.fill_bytes(&mut send_buf);

            let rv = unsafe {
                libc::sendto(
                    fd_client,
                    send_buf.as_ptr() as *const core::ffi::c_void,
                    send_buf.len(),
                    0,
                    std::ptr::null(),
                    0,
                )
            };
            let errno = test_utils::get_errno();

            // return value should never be 0
            test_utils::result_assert(rv != 0, "rv is 0")?;

            if rv == -1 {
                test_utils::result_assert_eq(errno, libc::EAGAIN, "Unexpected errno")?;
                break;
            }

            send_hash.write(&send_buf[..(rv as usize)]);

            bytes_sent += rv;

            // shadow needs to run events
            assert_eq!(unsafe { libc::usleep(1000) }, 0);
        }

        // read bytes until an EAGAIN error
        let mut bytes_read = 0;
        loop {
            let rv = unsafe {
                libc::recvfrom(
                    fd_accepted,
                    recv_buf.as_mut_ptr() as *mut core::ffi::c_void,
                    recv_buf.len(),
                    0,
                    std::ptr::null_mut(),
                    std::ptr::null_mut(),
                )
            };
            let errno = test_utils::get_errno();

            // return value of 0 means EOF
            test_utils::result_assert(rv != 0, "rv is EOF")?;

            if rv == -1 {
                test_utils::result_assert_eq(errno, libc::EAGAIN, "Unexpected errno")?;
                break;
            }

            recv_hash.write(&recv_buf[..(rv as usize)]);

            bytes_read += rv;

            // shadow needs to run events
            assert_eq!(unsafe { libc::usleep(1000) }, 0);
        }

        test_utils::result_assert_eq(
            bytes_sent,
            bytes_read,
            "Number of sent and read bytes don't match",
        )?;
        test_utils::result_assert_eq(
            send_hash.finish(),
            recv_hash.finish(),
            "Hash of sent and read bytes don't match",
        )?;

        Ok(())
    })
}

/// Test sendto() and recvfrom() using an unused flag.
fn test_invalid_flag(sock_type: libc::c_int) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(libc::AF_INET, sock_type | libc::SOCK_NONBLOCK, 0) };
    let fd_server = unsafe { libc::socket(libc::AF_INET, sock_type | libc::SOCK_NONBLOCK, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    // connect the client fd to the server
    let fd_server = match sock_type {
        libc::SOCK_STREAM => {
            let fd_accepted = tcp_connect_helper(fd_client, fd_server, libc::SOCK_NONBLOCK);
            unsafe { libc::close(fd_server) };
            fd_accepted
        }
        libc::SOCK_DGRAM => {
            udp_connect_helper(fd_client, fd_server, /* connect= */ true);
            fd_server
        }
        _ => unreachable!(),
    };

    let sendto_buf: Vec<u8> = vec![1, 2, 3];
    let mut recvfrom_buf: Vec<u8> = vec![1, 2, 3];

    let sendto_args = SendtoArguments {
        fd: fd_client,
        len: sendto_buf.len(),
        buf: Some(&sendto_buf),
        flags: 1 << 31, // an unused flag
        ..Default::default()
    };

    let mut recvfrom_args = RecvfromArguments {
        fd: fd_server,
        len: recvfrom_buf.len(),
        buf: Some(&mut recvfrom_buf),
        flags: 1 << 31, // an unused flag
        ..Default::default()
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // try to send 3 bytes; no error expected
        check_sendto_call(&sendto_args, &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(100) }, 0);

        // try to read 3 bytes; no error expected
        check_recvfrom_call(&mut recvfrom_args, &[], true)?;

        Ok(())
    })
}

/// Test sendto() and recvfrom() using the `MSG_DONTWAIT` flag.
fn test_flag_dontwait(sock_type: libc::c_int) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(libc::AF_INET, sock_type, 0) };
    let fd_server = unsafe { libc::socket(libc::AF_INET, sock_type, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    // connect the client fd to the server
    let fd_server = match sock_type {
        libc::SOCK_STREAM => {
            let fd_accepted = tcp_connect_helper(fd_client, fd_server, 0);
            unsafe { libc::close(fd_server) };
            fd_accepted
        }
        libc::SOCK_DGRAM => {
            udp_connect_helper(fd_client, fd_server, /* connect= */ true);
            fd_server
        }
        _ => unreachable!(),
    };

    let mut buf_10_bytes: Vec<u8> = vec![1u8; 10];

    let mut recvfrom_args = RecvfromArguments {
        fd: fd_client,
        len: buf_10_bytes.len(),
        buf: Some(&mut buf_10_bytes),
        flags: libc::MSG_DONTWAIT,
        ..Default::default()
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // try to read 10 bytes; an EAGAIN error expected
        check_recvfrom_call(&mut recvfrom_args, &[libc::EAGAIN], true)?;

        Ok(())
    })
}

/// Test sendto() and recvfrom() using a null sockaddr, and non-zero or null sockaddr length.
fn test_null_addr(
    domain: libc::c_int,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(domain, sock_type | flag, 0) };
    let fd_server = unsafe { libc::socket(domain, sock_type | flag, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    // connect the client fd to the server
    let fd_server = match sock_type {
        libc::SOCK_STREAM => {
            let fd_accepted = tcp_connect_helper(fd_client, fd_server, flag);
            unsafe { libc::close(fd_server) };
            fd_accepted
        }
        libc::SOCK_DGRAM => {
            udp_connect_helper(fd_client, fd_server, /* connect= */ true);
            fd_server
        }
        _ => unreachable!(),
    };

    let sendto_buf: Vec<u8> = vec![1, 2, 3];
    let mut recvfrom_buf: Vec<u8> = vec![1, 2, 3];

    let sendto_args = SendtoArguments {
        fd: fd_client,
        len: sendto_buf.len(),
        buf: Some(&sendto_buf),
        flags: 0,
        addr: None,
        addr_len: 5,
    };

    let mut recvfrom_args = RecvfromArguments {
        fd: fd_server,
        len: recvfrom_buf.len(),
        buf: Some(&mut recvfrom_buf),
        flags: 0,
        addr: None,
        addr_len: Some(5),
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // send 3 bytes; no errors expected
        check_sendto_call(&sendto_args, &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read 3 bytes; no errors expected
        check_recvfrom_call(&mut recvfrom_args, &[], true)?;

        Ok(())
    })
}

/// Test sendto() and recvfrom() using a UDP socket that is not conneected, while using a
/// null address argument.
fn test_null_addr_not_connected_udp(domain: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(domain, libc::SOCK_DGRAM | flag, 0) };
    let fd_server = unsafe { libc::socket(domain, libc::SOCK_DGRAM | flag, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    // bind the server fd, but don't connect the client fd to the server
    let server_addr = udp_connect_helper(fd_client, fd_server, /* connect= */ false);

    let sendto_buf: Vec<u8> = vec![1, 2, 3];
    let mut recvfrom_buf: Vec<u8> = vec![1, 2, 3];

    let sendto_args_1 = SendtoArguments {
        fd: fd_client,
        len: sendto_buf.len(),
        buf: Some(&sendto_buf),
        flags: 0,
        addr: None,
        addr_len: 5,
    };

    let sendto_args_2 = SendtoArguments {
        fd: fd_client,
        len: sendto_buf.len(),
        buf: Some(&sendto_buf),
        flags: 0,
        addr: Some(server_addr),
        addr_len: std::mem::size_of_val(&server_addr) as u32,
    };

    let mut recvfrom_args = RecvfromArguments {
        fd: fd_server,
        len: recvfrom_buf.len(),
        buf: Some(&mut recvfrom_buf),
        flags: 0,
        addr: None,
        addr_len: Some(5),
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // send 3 bytes using a null sockaddr; an EDESTADDRREQ error expected
        check_sendto_call(&sendto_args_1, &[libc::EDESTADDRREQ], true)?;

        // send 3 bytes using a non-null sockaddr; no error expected
        check_sendto_call(&sendto_args_2, &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read 3 bytes using a null sockaddr; no error expected
        check_recvfrom_call(&mut recvfrom_args, &[], true)?;

        Ok(())
    })
}

/// Test recvfrom() using a null sockaddr length, and non-null sockaddr.
fn test_null_addr_len(
    domain: libc::c_int,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(domain, sock_type | flag, 0) };
    let fd_server = unsafe { libc::socket(domain, sock_type | flag, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    // connect the client fd to the server
    let fd_server = match sock_type {
        libc::SOCK_STREAM => {
            let fd_accepted = tcp_connect_helper(fd_client, fd_server, flag);
            unsafe { libc::close(fd_server) };
            fd_accepted
        }
        libc::SOCK_DGRAM => {
            udp_connect_helper(fd_client, fd_server, /* connect= */ true);
            fd_server
        }
        _ => unreachable!(),
    };

    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    let mut recvfrom_buf: Vec<u8> = vec![1, 2, 3];

    let mut recvfrom_args = RecvfromArguments {
        fd: fd_server,
        len: recvfrom_buf.len(),
        buf: Some(&mut recvfrom_buf),
        flags: 0,
        addr: Some(addr),
        addr_len: None,
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // send 3 bytes; no error expected
        simple_sendto_helper(fd_client, &vec![1u8, 2, 3], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read 3 bytes using a non-null sockaddr, but a null sockaddr length
        // an EFAULT error expected
        check_recvfrom_call(&mut recvfrom_args, &[libc::EFAULT], true)?;
        Ok(())
    })
}

/// Test recvfrom() using a null sockaddr and a null sockaddr length.
fn test_null_both(
    domain: libc::c_int,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(domain, sock_type | flag, 0) };
    let fd_server = unsafe { libc::socket(domain, sock_type | flag, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    // connect the client fd to the server
    let fd_server = match sock_type {
        libc::SOCK_STREAM => {
            let fd_accepted = tcp_connect_helper(fd_client, fd_server, flag);
            unsafe { libc::close(fd_server) };
            fd_accepted
        }
        libc::SOCK_DGRAM => {
            udp_connect_helper(fd_client, fd_server, /* connect= */ true);
            fd_server
        }
        _ => unreachable!(),
    };

    let mut recvfrom_buf: Vec<u8> = vec![1, 2, 3];

    let mut recvfrom_args = RecvfromArguments {
        fd: fd_server,
        len: recvfrom_buf.len(),
        buf: Some(&mut recvfrom_buf),
        flags: 0,
        addr: None,
        addr_len: None,
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // send 3 bytes; no error expected
        simple_sendto_helper(fd_client, &vec![1u8, 2, 3], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read 3 bytes using a null sockaddr and a null sockaddr length
        // no error expected
        check_recvfrom_call(&mut recvfrom_args, &[], true)?;
        Ok(())
    })
}

/// Test recvfrom() using a UDP socket and verify that the returned sockaddr is correct.
fn test_recv_addr_udp(domain: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(domain, libc::SOCK_DGRAM | flag, 0) };
    let fd_server = unsafe { libc::socket(domain, libc::SOCK_DGRAM | flag, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    // connect the client fd to the server
    udp_connect_helper(fd_client, fd_server, /* connect= */ true);

    let mut client_addr = libc::sockaddr_in {
        sin_family: 0u16,
        sin_port: 0u16.to_be(),
        sin_addr: libc::in_addr { s_addr: 0 },
        sin_zero: [0; 8],
    };
    let mut client_addr_len = std::mem::size_of_val(&client_addr);

    // get the sockaddr of the client fd
    assert_eq!(
        unsafe {
            libc::getsockname(
                fd_client,
                &mut client_addr as *mut _ as *mut _,
                &mut client_addr_len as *mut _ as *mut _,
            )
        },
        0
    );

    // an empty sockaddr
    let recvfrom_addr = libc::sockaddr_in {
        sin_family: 0u16,
        sin_port: 0u16.to_be(),
        sin_addr: libc::in_addr { s_addr: 0 },
        sin_zero: [0; 8],
    };

    let mut buf: Vec<u8> = vec![1, 2, 3];

    let mut recvfrom_args = RecvfromArguments {
        fd: fd_server,
        len: buf.len(),
        buf: Some(&mut buf),
        flags: 0,
        addr: Some(recvfrom_addr),
        addr_len: Some(std::mem::size_of_val(&recvfrom_addr) as u32),
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // send 3 bytes; no error expected
        simple_sendto_helper(fd_client, &vec![1, 2, 3], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read 3 bytes; no error expected
        check_recvfrom_call(&mut recvfrom_args, &[], true)?;

        // check that the returned sockaddr equals the client sockaddr
        test_utils::result_assert_eq(
            recvfrom_args.addr.unwrap().sin_family,
            client_addr.sin_family,
            "Address family does not match",
        )?;
        test_utils::result_assert_eq(
            recvfrom_args.addr.unwrap().sin_port,
            client_addr.sin_port,
            "Address port does not match",
        )?;
        test_utils::result_assert_eq(
            recvfrom_args.addr.unwrap().sin_addr.s_addr,
            client_addr.sin_addr.s_addr,
            "Address s_addr does not match",
        )?;

        Ok(())
    })
}

/// Test sendto() using a connected socket, but also with a different non-null destination address.
fn test_nonnull_addr_tcp(domain: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(domain, libc::SOCK_STREAM | flag, 0) };
    let fd_server = unsafe { libc::socket(domain, libc::SOCK_STREAM | flag, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    // connect the client fd to the server
    let fd_accepted = tcp_connect_helper(fd_client, fd_server, flag);

    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    let sendto_buf: Vec<u8> = vec![1, 2, 3];

    let sendto_args = SendtoArguments {
        fd: fd_client,
        len: sendto_buf.len(),
        buf: Some(&sendto_buf),
        flags: 0,
        addr: Some(addr),
        addr_len: std::mem::size_of_val(&addr) as u32,
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server, fd_accepted], || {
        // the man page for sendto says this may return EISCONN,
        // but this doesn't seem to be the case for glibc
        check_sendto_call(&sendto_args, &[], true)?;
        Ok(())
    })
}
/// Test sendto() and recvfrom() for TCP sockets using large buffers (10^6 bytes).
fn test_large_buf_tcp(domain: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(domain, libc::SOCK_STREAM | flag, 0) };
    let fd_server = unsafe { libc::socket(domain, libc::SOCK_STREAM | flag, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    // connect the client fd to the server
    let fd_accepted = tcp_connect_helper(fd_client, fd_server, flag);

    test_utils::run_and_close_fds(&[fd_client, fd_server, fd_accepted], || {
        // try to send 1_000_000 bytes, but may send fewer; no error expected
        simple_sendto_helper(fd_client, &vec![1u8; 1_000_000], &[], false)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // try to read 1_000_000 bytes, but may read fewer; no error expected
        simple_recvfrom_helper(fd_accepted, &mut vec![0u8; 1_000_000], &[], false)?;

        Ok(())
    })
}

/// Test sendto() and recvfrom() for UDP sockets just above the message limit of 65507 bytes.
fn test_large_buf_udp(domain: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(domain, libc::SOCK_DGRAM | flag, 0) };
    let fd_server = unsafe { libc::socket(domain, libc::SOCK_DGRAM | flag, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    // connect the client fd to the server
    udp_connect_helper(fd_client, fd_server, /* connect= */ true);

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // try sending a buffer slightly too large; an EMSGSIZE error is expected
        simple_sendto_helper(fd_client, &vec![1u8; 65_508], &[libc::EMSGSIZE], true)?;

        // try sending a buffer at the max size; no error expected
        simple_sendto_helper(fd_client, &vec![1u8; 65_507], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // try receiving using a large buffer; no error expected
        let received_bytes = simple_recvfrom_helper(fd_server, &mut vec![0u8; 65_508], &[], false)?;

        test_utils::result_assert_eq(received_bytes, 65_507, "Unexpected number of bytes read")
    })
}

/// Test recvfrom() using a UDP socket with a buffer that is too small to contain all of
/// received the data.
fn test_short_recv_buf_udp(domain: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(domain, libc::SOCK_DGRAM | flag, 0) };
    let fd_server = unsafe { libc::socket(domain, libc::SOCK_DGRAM | flag, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    // connect the client fd to the server
    udp_connect_helper(fd_client, fd_server, /* connect= */ true);

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // send 2000 bytes; no error expected
        simple_sendto_helper(fd_client, &vec![1u8; 2000], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read 1999 bytes; last byte will be discarded and should not cause an error
        simple_recvfrom_helper(fd_server, &mut vec![0u8; 1999], &[], true)?;

        Ok(())
    })
}

/// Test sendto() and recvfrom() using a UDP socket and make sure that all sent
/// messages are received as expected.
fn test_msg_order_udp(domain: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(domain, libc::SOCK_DGRAM | flag, 0) };
    let fd_server = unsafe { libc::socket(domain, libc::SOCK_DGRAM | flag, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    // connect the client fd to the server
    udp_connect_helper(fd_client, fd_server, /* connect= */ true);

    // read and write UDP messages and see if something unexpected happens
    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // send 1000 bytes; no error expected
        simple_sendto_helper(fd_client, &vec![1u8; 1000], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read 500 bytes of the 1000 byte message; no error expected
        simple_recvfrom_helper(fd_server, &mut vec![0u8; 500], &[], true)?;

        // send 200 bytes; no error expected
        simple_sendto_helper(fd_client, &vec![1u8; 200], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read 200 bytes with a 500 byte buffer; no error expected
        let received_bytes = simple_recvfrom_helper(fd_server, &mut vec![0u8; 500], &[], false)?;
        test_utils::result_assert_eq(received_bytes, 200, "Unexpected number of bytes read")?;

        // send 3 messages of different lengths; no error expected
        simple_sendto_helper(fd_client, &vec![1u8; 1], &[], true)?;
        simple_sendto_helper(fd_client, &vec![1u8; 3], &[], true)?;
        simple_sendto_helper(fd_client, &vec![1u8; 5], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read the 3 messages and make sure the lengths are correct; no error expected
        let received_bytes = simple_recvfrom_helper(fd_server, &mut vec![0u8; 500], &[], false)?;
        test_utils::result_assert_eq(received_bytes, 1, "Unexpected number of bytes read")?;
        let received_bytes = simple_recvfrom_helper(fd_server, &mut vec![0u8; 500], &[], false)?;
        test_utils::result_assert_eq(received_bytes, 3, "Unexpected number of bytes read")?;
        let received_bytes = simple_recvfrom_helper(fd_server, &mut vec![0u8; 500], &[], false)?;
        test_utils::result_assert_eq(received_bytes, 5, "Unexpected number of bytes read")?;

        Ok(())
    })
}

/// A helper function to call sendto() and recvfrom() with valid values
/// and a user-provided fd.
fn fd_test_helper(
    fd: libc::c_int,
    sendto_errnos: &[libc::c_int],
    recvfrom_errnos: &[libc::c_int],
) -> Result<(), String> {
    let sendto_buf: Vec<u8> = vec![1, 2, 3];
    let mut recvfrom_buf: Vec<u8> = vec![1, 2, 3];

    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    let sendto_args = SendtoArguments {
        fd,
        len: sendto_buf.len(),
        buf: Some(&sendto_buf),
        flags: 0,
        addr: Some(addr),
        addr_len: std::mem::size_of_val(&addr) as u32,
    };

    let mut recvfrom_args = RecvfromArguments {
        fd,
        len: recvfrom_buf.len(),
        buf: Some(&mut recvfrom_buf),
        flags: 0,
        addr: Some(addr),
        addr_len: Some(std::mem::size_of_val(&addr) as u32),
    };

    check_sendto_call(&sendto_args, sendto_errnos, true)?;

    // shadow needs to run events
    assert_eq!(unsafe { libc::usleep(10000) }, 0);

    check_recvfrom_call(&mut recvfrom_args, recvfrom_errnos, true)?;

    Ok(())
}

/// A helper function for TCP sockets to start a server on one fd and connect another fd
/// to it. Returns the accepted fd.
fn tcp_connect_helper(
    fd_client: libc::c_int,
    fd_server: libc::c_int,
    flags: libc::c_int,
) -> libc::c_int {
    // the server address
    let mut server_addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 0u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
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

    // listen for connections
    {
        let rv = unsafe { libc::listen(fd_server, 10) };
        assert_eq!(rv, 0);
    }

    // connect to the server address
    {
        let rv = unsafe {
            libc::connect(
                fd_client,
                &server_addr as *const libc::sockaddr_in as *const libc::sockaddr,
                std::mem::size_of_val(&server_addr) as u32,
            )
        };
        assert!(rv == 0 || (rv == -1 && test_utils::get_errno() == libc::EINPROGRESS));
    }

    // shadow needs to run events, otherwise the accept call won't know it
    // has an incoming connection (SYN packet)
    {
        let rv = unsafe { libc::usleep(10000) };
        assert_eq!(rv, 0);
    }

    // accept the connection
    let fd = unsafe { libc::accept4(fd_server, std::ptr::null_mut(), std::ptr::null_mut(), flags) };
    assert!(fd >= 0);

    fd
}

/// A helper function for UDP sockets to bind the server fd and optionally connect
/// the client fd to the server fd. Returns the address that the server is bound to.
fn udp_connect_helper(
    fd_client: libc::c_int,
    fd_server: libc::c_int,
    connect: bool,
) -> libc::sockaddr_in {
    // the server address
    let mut server_addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 0u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
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

    // connect to the server address
    if connect {
        let rv = unsafe {
            libc::connect(
                fd_client,
                &server_addr as *const libc::sockaddr_in as *const libc::sockaddr,
                std::mem::size_of_val(&server_addr) as u32,
            )
        };
        assert_eq!(rv, 0);
    }

    server_addr
}

/// Call `sendto()` with a provided fd and buffer and check that the result is as
/// expected. An empty flag mask and a null sockaddr are used.
/// Returns an `Error` if the errno doesn't match an errno in `errnos`, or if
/// `verify_num_bytes` is `true` and the number of bytes sent (the return value) does
/// not match the buffer length.
fn simple_sendto_helper(
    fd: libc::c_int,
    buf: &[u8],
    errnos: &[libc::c_int],
    verify_num_bytes: bool,
) -> Result<libc::ssize_t, String> {
    let args = SendtoArguments {
        fd: fd,
        len: buf.len(),
        buf: Some(buf),
        ..Default::default()
    };

    check_sendto_call(&args, errnos, verify_num_bytes)
}

/// Call `recvfrom()` with a provided fd and buffer and check that the result is as
/// expected. An empty flag mask and a null sockaddr are used.
/// Returns an `Error` if the errno doesn't match an errno in `errnos`, or if
/// `verify_num_bytes` is `true` and the number of bytes received (the return value) does
/// not match the buffer length.
fn simple_recvfrom_helper(
    fd: libc::c_int,
    buf: &mut [u8],
    errnos: &[libc::c_int],
    verify_num_bytes: bool,
) -> Result<libc::ssize_t, String> {
    let mut args = RecvfromArguments {
        fd: fd,
        len: buf.len(),
        buf: Some(buf),
        ..Default::default()
    };

    check_recvfrom_call(&mut args, errnos, verify_num_bytes)
}

fn check_sendto_call(
    args: &SendtoArguments,
    expected_errnos: &[libc::c_int],
    verify_num_bytes: bool,
) -> Result<libc::ssize_t, String> {
    // if the pointers will be non-null, make sure the length is not greater than the actual data size
    // so that we don't segfault
    if args.buf.is_some() {
        assert!(args.len as usize <= args.buf.unwrap().len());
    }
    if args.addr.is_some() {
        assert!(args.addr_len as usize <= std::mem::size_of_val(&args.addr.unwrap()));
    }

    let buf_ptr = match args.buf {
        Some(slice) => slice.as_ptr(),
        None => std::ptr::null(),
    };

    let rv = test_utils::check_system_call!(
        || unsafe {
            libc::sendto(
                args.fd,
                buf_ptr as *const core::ffi::c_void,
                args.len,
                args.flags,
                args.addr.as_ptr() as *const libc::sockaddr,
                args.addr_len,
            )
        },
        expected_errnos,
    )?;

    // only check that all bytes were sent if there were no expected errors
    if verify_num_bytes && expected_errnos.len() == 0 {
        // check that sendto() returned the number of bytes in the buffer,
        // or 0 if the buffer is NULL
        test_utils::result_assert_eq(
            rv,
            args.buf.as_ref().map_or(0, |x| x.len()) as isize,
            "Not all bytes were sent",
        )?;
    }

    Ok(rv)
}

fn check_recvfrom_call(
    args: &mut RecvfromArguments,
    expected_errnos: &[libc::c_int],
    verify_num_bytes: bool,
) -> Result<libc::ssize_t, String> {
    // if the pointers will be non-null, make sure the length is not greater than the actual data size
    // so that we don't segfault
    if args.buf.is_some() {
        assert!(args.len as usize <= args.buf.as_ref().unwrap().len());
    }
    if args.addr.is_some() && args.addr_len.is_some() {
        assert!(args.addr_len.unwrap() as usize <= std::mem::size_of_val(&args.addr.unwrap()));
    }

    let buf_ptr = match &mut args.buf {
        Some(slice) => slice.as_mut_ptr(),
        None => std::ptr::null_mut(),
    };

    let rv = test_utils::check_system_call!(
        || unsafe {
            libc::recvfrom(
                args.fd,
                buf_ptr as *mut core::ffi::c_void,
                args.len,
                args.flags,
                args.addr.as_mut_ptr() as *mut libc::sockaddr,
                args.addr_len.as_mut_ptr(),
            )
        },
        expected_errnos,
    )?;

    // only check that all bytes were received (recv buffer was filled) if there
    // were no expected errors
    if verify_num_bytes && expected_errnos.len() == 0 {
        // check that recvfrom() returned the number of bytes in the buffer,
        // or 0 if the buffer is NULL
        test_utils::result_assert_eq(
            rv,
            args.buf.as_ref().map_or(0, |x| x.len()) as isize,
            "Not all bytes were received (buffer not full)",
        )?;
    }

    Ok(rv)
}
