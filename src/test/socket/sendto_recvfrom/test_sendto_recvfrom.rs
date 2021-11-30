/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use std::hash::Hasher;

use rand::RngCore;
use rand::SeedableRng;

use test_utils::socket_utils::{autobind_helper, socket_init_helper, SockAddr, SocketInitMethod};
use test_utils::TestEnvironment as TestEnv;
use test_utils::{set, AsMutPtr};

#[derive(Debug)]
struct SendtoArguments<'a> {
    fd: libc::c_int,
    buf: Option<&'a [u8]>,
    len: libc::size_t,
    flags: libc::c_int,
    addr: Option<SockAddr>,
    addr_len: libc::socklen_t,
}

#[derive(Debug)]
struct RecvfromArguments<'a> {
    fd: libc::c_int,
    buf: Option<&'a mut [u8]>,
    len: libc::size_t,
    flags: libc::c_int,
    addr: Option<SockAddr>,
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
    let mut tests: Vec<test_utils::ShadowTest<_, _>> = vec![];

    let domains = [libc::AF_INET, libc::AF_UNIX];

    for &domain in domains.iter() {
        // add details to the test names to avoid duplicates
        let append_args = |s| format!("{} <domain={:?}>", s, domain);

        let passing = if domain != libc::AF_UNIX {
            set![TestEnv::Libc, TestEnv::Shadow]
        } else {
            set![TestEnv::Libc] // TODO: enable once we support socket() for unix sockets
        };

        tests.extend(vec![
            test_utils::ShadowTest::new(
                &append_args("test_invalid_fd"),
                move || test_invalid_fd(domain),
                passing.clone(),
            ),
            test_utils::ShadowTest::new(
                &append_args("test_non_existent_fd"),
                move || test_non_existent_fd(domain),
                passing.clone(),
            ),
            test_utils::ShadowTest::new(
                &append_args("test_non_socket_fd"),
                move || test_non_socket_fd(domain),
                passing.clone(),
            ),
            test_utils::ShadowTest::new(
                &append_args("test_large_buf_udp"),
                test_large_buf_udp,
                passing.clone(),
            ),
        ]);

        let sock_types = match domain {
            libc::AF_INET => &[libc::SOCK_STREAM, libc::SOCK_DGRAM][..],
            libc::AF_UNIX => &[libc::SOCK_STREAM, libc::SOCK_DGRAM, libc::SOCK_SEQPACKET][..],
            _ => unimplemented!(),
        };

        for &sock_type in sock_types.iter() {
            // add details to the test names to avoid duplicates
            let append_args = |s| format!("{} <domain={:?}, sock_type={}>", s, domain, sock_type);

            tests.extend(vec![test_utils::ShadowTest::new(
                &append_args("test_not_connected"),
                move || test_not_connected(domain, sock_type),
                passing.clone(),
            )]);
        }
    }

    let init_methods = [
        SocketInitMethod::Inet,
        SocketInitMethod::Unix,
        SocketInitMethod::UnixSocketpair,
    ];

    for &method in init_methods.iter() {
        // add details to the test names to avoid duplicates
        let append_args = |s| format!("{} <init_method={:?}>", s, method);

        let passing = if method != SocketInitMethod::Unix {
            set![TestEnv::Libc, TestEnv::Shadow]
        } else {
            set![TestEnv::Libc] // TODO: enable once we support socket() for unix sockets
        };

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
                    &append_args("test_null_buf"),
                    move || test_null_buf(method, sock_type),
                    passing.clone(),
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_zero_len_buf"),
                    move || test_zero_len_buf(method, sock_type),
                    passing.clone(),
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_invalid_flag"),
                    move || test_invalid_flag(method, sock_type),
                    passing.clone(),
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_flag_dontwait"),
                    move || test_flag_dontwait(method, sock_type),
                    passing.clone(),
                ),
            ]);
        }

        tests.extend(vec![test_utils::ShadowTest::new(
            &append_args("test_nonblocking_stream"),
            move || test_nonblocking_stream(method),
            passing.clone(),
        )]);
    }

    let flags = [0, libc::SOCK_NONBLOCK, libc::SOCK_CLOEXEC];

    for &method in init_methods.iter() {
        let passing = if method != SocketInitMethod::Unix {
            set![TestEnv::Libc, TestEnv::Shadow]
        } else {
            set![TestEnv::Libc] // TODO: enable once we support socket() for unix sockets
        };

        for &flag in flags.iter() {
            // add details to the test names to avoid duplicates
            let append_args = |s| format!("{} <init_method={:?}, flag={}>", s, method, flag);

            let sock_types = match method.domain() {
                libc::AF_INET => &[libc::SOCK_STREAM, libc::SOCK_DGRAM][..],
                libc::AF_UNIX => &[libc::SOCK_STREAM, libc::SOCK_DGRAM, libc::SOCK_SEQPACKET][..],
                _ => unimplemented!(),
            };

            for &sock_type in sock_types.iter() {
                // add details to the test names to avoid duplicates
                let append_args = |s| {
                    format!(
                        "{} <init_method={:?}, flag={}, sock_type={}>",
                        s, method, flag, sock_type
                    )
                };

                tests.extend(vec![
                    test_utils::ShadowTest::new(
                        &append_args("test_null_addr"),
                        move || test_null_addr(method, sock_type, flag),
                        passing.clone(),
                    ),
                    test_utils::ShadowTest::new(
                        &append_args("test_null_addr_len"),
                        move || test_null_addr_len(method, sock_type, flag),
                        passing.clone(),
                    ),
                    test_utils::ShadowTest::new(
                        &append_args("test_null_both"),
                        move || test_null_both(method, sock_type, flag),
                        passing.clone(),
                    ),
                    test_utils::ShadowTest::new(
                        &append_args("test_nonnull_addr"),
                        move || test_nonnull_addr(method, sock_type, flag),
                        passing.clone(),
                    ),
                    test_utils::ShadowTest::new(
                        &append_args("test_recv_addr <bind_client=false>"),
                        move || test_recv_addr(method, sock_type, flag, false),
                        passing.clone(),
                    ),
                    test_utils::ShadowTest::new(
                        &append_args("test_recv_addr <bind_client=true>"),
                        move || test_recv_addr(method, sock_type, flag, true),
                        passing.clone(),
                    ),
                ]);

                // if non-blocking
                if flag & libc::SOCK_NONBLOCK != 0 {
                    tests.extend(vec![test_utils::ShadowTest::new(
                        &append_args("test_large_buf"),
                        move || test_large_buf(method, sock_type, flag),
                        passing.clone(),
                    )]);
                }

                // if a message-based socket
                if [libc::SOCK_DGRAM, libc::SOCK_SEQPACKET].contains(&sock_type) {
                    tests.extend(vec![
                        test_utils::ShadowTest::new(
                            &append_args("test_short_recv_buf_dgram"),
                            move || test_short_recv_buf_dgram(method, sock_type, flag),
                            passing.clone(),
                        ),
                        test_utils::ShadowTest::new(
                            &append_args("test_msg_order_dgram"),
                            move || test_msg_order_dgram(method, sock_type, flag),
                            passing.clone(),
                        ),
                    ]);
                }
            }

            if method != SocketInitMethod::UnixSocketpair {
                tests.extend(vec![test_utils::ShadowTest::new(
                    &append_args("test_null_addr_not_connected_dgram"),
                    move || test_null_addr_not_connected_dgram(method, flag),
                    passing.clone(),
                )]);
            }
        }
    }

    tests
}

/// Test sendto() and recvfrom() using an argument that cannot be a fd.
fn test_invalid_fd(domain: libc::c_int) -> Result<(), String> {
    // expect both sendto() and recvfrom() to return EBADF
    fd_test_helper(-1, domain, &[libc::EBADF], &[libc::EBADF])
}

/// Test sendto() and recvfrom() using an argument that could be a fd, but is not.
fn test_non_existent_fd(domain: libc::c_int) -> Result<(), String> {
    // expect both sendto() and recvfrom() to return EBADF
    fd_test_helper(8934, domain, &[libc::EBADF], &[libc::EBADF])
}

/// Test sendto() and recvfrom() using a valid fd that is not a socket.
fn test_non_socket_fd(domain: libc::c_int) -> Result<(), String> {
    // expect both sendto() and recvfrom() to return ENOTSOCK
    fd_test_helper(0, domain, &[libc::ENOTSOCK], &[libc::ENOTSOCK])
}

/// Test sendto() and recvfrom() using a socket that is not conneected, but using a non-null address
/// argument.
fn test_not_connected(domain: libc::c_int, sock_type: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type | libc::SOCK_NONBLOCK, 0) };
    assert!(fd >= 0);

    let expected_sendto_errs = match (domain, sock_type) {
        // may return EPIPE on Linux, see "BUGS" in sendto(2)
        (libc::AF_INET, libc::SOCK_STREAM) => &[libc::ENOTCONN, libc::EPIPE][..],
        (libc::AF_INET, libc::SOCK_DGRAM) => &[][..],
        (libc::AF_UNIX, libc::SOCK_STREAM) => &[libc::EOPNOTSUPP][..],
        (libc::AF_UNIX, libc::SOCK_DGRAM) => &[libc::ECONNREFUSED][..],
        (libc::AF_UNIX, libc::SOCK_SEQPACKET) => &[libc::ENOTCONN][..],
        _ => unimplemented!(),
    };

    let expected_recvfrom_errs = match (domain, sock_type) {
        (libc::AF_INET, libc::SOCK_STREAM) => &[libc::ENOTCONN][..],
        (libc::AF_INET, libc::SOCK_DGRAM) => &[libc::EAGAIN],
        (libc::AF_UNIX, libc::SOCK_STREAM) => &[libc::EINVAL][..],
        (libc::AF_UNIX, libc::SOCK_DGRAM) => &[libc::EAGAIN],
        (libc::AF_UNIX, libc::SOCK_SEQPACKET) => &[libc::ENOTCONN][..],
        _ => unimplemented!(),
    };

    test_utils::run_and_close_fds(&[fd], || {
        fd_test_helper(fd, domain, expected_sendto_errs, expected_recvfrom_errs)
    })
}

/// Test sendto() and recvfrom() using a null buffer, and non-zero buffer length.
fn test_null_buf(init_method: SocketInitMethod, sock_type: libc::c_int) -> Result<(), String> {
    let (fd_client, fd_server) = socket_init_helper(
        init_method,
        sock_type,
        libc::SOCK_NONBLOCK,
        /* bind_client = */ false,
    );

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
fn test_zero_len_buf(init_method: SocketInitMethod, sock_type: libc::c_int) -> Result<(), String> {
    let (fd_client, fd_server) = socket_init_helper(
        init_method,
        sock_type,
        libc::SOCK_NONBLOCK,
        /* bind_client = */ false,
    );

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // send 0 bytes; no errors expected
        simple_sendto_helper(fd_client, &vec![], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // receive 0 bytes; an EAGAIN error expected only if a stream socket
        // dgram sockets can receive messages of 0 bytes
        let e = match sock_type {
            libc::SOCK_STREAM => vec![libc::EAGAIN],
            libc::SOCK_DGRAM | libc::SOCK_SEQPACKET => vec![],
            _ => unimplemented!(),
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

/// Test sendto() and recvfrom() using an unused flag.
fn test_invalid_flag(init_method: SocketInitMethod, sock_type: libc::c_int) -> Result<(), String> {
    let (fd_client, fd_server) = socket_init_helper(
        init_method,
        sock_type,
        libc::SOCK_NONBLOCK,
        /* bind_client = */ false,
    );

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
        let expected_err = match init_method.domain() {
            libc::AF_INET => vec![],
            libc::AF_UNIX => vec![],
            _ => unimplemented!(),
        };
        check_sendto_call(&sendto_args, &expected_err, true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(100) }, 0);

        // try to read 3 bytes; no error expected
        let expected_err = match init_method.domain() {
            libc::AF_INET => vec![],
            libc::AF_UNIX => vec![],
            _ => unimplemented!(),
        };
        check_recvfrom_call(&mut recvfrom_args, &expected_err, true)?;

        Ok(())
    })
}

/// Test sendto() and recvfrom() using the `MSG_DONTWAIT` flag.
fn test_flag_dontwait(init_method: SocketInitMethod, sock_type: libc::c_int) -> Result<(), String> {
    let (fd_client, fd_server) =
        socket_init_helper(init_method, sock_type, 0, /* bind_client = */ false);

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

/// Test sendto() and recvfrom() using a non-blocking stream socket.
fn test_nonblocking_stream(init_method: SocketInitMethod) -> Result<(), String> {
    let (fd_client, fd_peer) = socket_init_helper(
        init_method,
        libc::SOCK_STREAM,
        libc::SOCK_NONBLOCK,
        /* bind_client = */ false,
    );

    test_utils::run_and_close_fds(&[fd_client, fd_peer], || {
        // try to read 10 bytes; an EAGAIN error expected
        simple_recvfrom_helper(fd_peer, &mut vec![0u8; 10], &[libc::EAGAIN], true)?;

        // send 10 bytes; no errors expected
        simple_sendto_helper(fd_client, &vec![1u8; 10], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read 10 bytes into a 20 byte buffer; no errors expected
        simple_recvfrom_helper(fd_peer, &mut vec![0u8; 20], &[], false)?;

        // try to read 10 bytes; an EAGAIN error expected
        simple_recvfrom_helper(fd_peer, &mut vec![0u8; 10], &[libc::EAGAIN], false)?;

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
                    fd_peer,
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

/// Test sendto() and recvfrom() using a null sockaddr, and non-zero or null sockaddr length.
fn test_null_addr(
    init_method: SocketInitMethod,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_server) =
        socket_init_helper(init_method, sock_type, flag, /* bind_client = */ false);

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

/// Test sendto() and recvfrom() using a dgram socket that is not connected, while using a
/// null address argument.
fn test_null_addr_not_connected_dgram(
    init_method: SocketInitMethod,
    flag: libc::c_int,
) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(init_method.domain(), libc::SOCK_DGRAM | flag, 0) };
    let fd_server = unsafe { libc::socket(init_method.domain(), libc::SOCK_DGRAM | flag, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    let (server_addr, server_addr_len) = autobind_helper(fd_server, init_method.domain());

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
        addr_len: server_addr_len,
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
        // send 3 bytes using a null sockaddr
        let expected_err = match init_method.domain() {
            libc::AF_INET => vec![libc::EDESTADDRREQ],
            libc::AF_UNIX => vec![libc::ENOTCONN],
            _ => unimplemented!(),
        };
        check_sendto_call(&sendto_args_1, &expected_err, true)?;

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
    init_method: SocketInitMethod,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_server) =
        socket_init_helper(init_method, sock_type, flag, /* bind_client = */ false);

    let addr = match init_method.domain() {
        libc::AF_INET => SockAddr::Inet(libc::sockaddr_in {
            sin_family: libc::AF_INET as u16,
            sin_port: 11111u16.to_be(),
            sin_addr: libc::in_addr {
                s_addr: libc::INADDR_LOOPBACK.to_be(),
            },
            sin_zero: [0; 8],
        }),
        libc::AF_UNIX => {
            let mut addr = libc::sockaddr_un {
                sun_family: libc::AF_UNIX as u16,
                sun_path: [0; 108],
            };
            // arbitrary abstract socket name
            addr.sun_path[1] = 4;
            addr.sun_path[2] = 7;
            SockAddr::Unix(addr)
        }
        _ => unimplemented!(),
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
    init_method: SocketInitMethod,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_server) =
        socket_init_helper(init_method, sock_type, flag, /* bind_client = */ false);

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

/// Test sendto() using a connected socket, but also with a different non-null destination address.
fn test_nonnull_addr(
    init_method: SocketInitMethod,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_server) =
        socket_init_helper(init_method, sock_type, flag, /* bind_client = */ false);

    let (addr, addr_len) = match init_method.domain() {
        libc::AF_INET => {
            let addr = libc::sockaddr_in {
                sin_family: libc::AF_INET as u16,
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

    let sendto_buf: Vec<u8> = vec![1, 2, 3];

    let sendto_args = SendtoArguments {
        fd: fd_client,
        len: sendto_buf.len(),
        buf: Some(&sendto_buf),
        flags: 0,
        addr: Some(addr),
        addr_len: addr_len,
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // some configurations seem to ignore the destination address for connected sockets
        let expected_err = match (init_method.domain(), sock_type) {
            (libc::AF_INET, _) => vec![],
            (libc::AF_UNIX, libc::SOCK_STREAM) => vec![libc::EISCONN],
            (libc::AF_UNIX, libc::SOCK_DGRAM) => vec![libc::ECONNREFUSED],
            (libc::AF_UNIX, libc::SOCK_SEQPACKET) => vec![],
            _ => unimplemented!(),
        };

        check_sendto_call(&sendto_args, &expected_err, true)?;
        Ok(())
    })
}

/// Test recvfrom() using a socket and verify that the returned sockaddr is correct.
fn test_recv_addr(
    init_method: SocketInitMethod,
    sock_type: libc::c_int,
    flag: libc::c_int,
    bind_client: bool,
) -> Result<(), String> {
    let (fd_client, fd_server) = socket_init_helper(init_method, sock_type, flag, bind_client);

    let mut client_addr: libc::sockaddr_storage = unsafe { std::mem::zeroed() };
    let mut client_addr_len = std::mem::size_of_val(&client_addr) as libc::socklen_t;

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
    let recvfrom_addr: libc::sockaddr_storage = unsafe { std::mem::zeroed() };

    let mut buf: Vec<u8> = vec![1, 2, 3];

    let mut recvfrom_args = RecvfromArguments {
        fd: fd_server,
        len: buf.len(),
        buf: Some(&mut buf),
        flags: 0,
        addr: Some(SockAddr::Generic(recvfrom_addr)),
        addr_len: Some(std::mem::size_of_val(&recvfrom_addr) as u32),
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // send 3 bytes to the server; no error expected
        simple_sendto_helper(fd_client, &vec![1, 2, 3], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read 3 bytes at the server; no error expected
        check_recvfrom_call(&mut recvfrom_args, &[], true)?;

        // validate the returned socket address
        match (init_method, sock_type, bind_client) {
            // check that the returned sockaddr equals the client sockaddr
            (SocketInitMethod::Inet, libc::SOCK_DGRAM, _) | (SocketInitMethod::Unix, _, true) => {
                test_utils::result_assert_eq(
                    recvfrom_args.addr_len.unwrap(),
                    client_addr_len,
                    "Address lengths did not match",
                )?;
                test_utils::result_assert_eq(
                    recvfrom_args.addr.unwrap(),
                    SockAddr::Generic(client_addr),
                    "Addresses did not match",
                )?;
            }
            // all others should just set the address length as 0
            _ => {
                test_utils::result_assert_eq(
                    recvfrom_args.addr_len.unwrap(),
                    0,
                    "Address length was not zero",
                )?;
                test_utils::result_assert_eq(
                    recvfrom_args.addr.unwrap(),
                    SockAddr::Generic(unsafe { std::mem::zeroed() }),
                    "Address was unexpectedly changed",
                )?;
            }
        }

        Ok(())
    })
}

/// Test recvfrom() using a dgram socket with a buffer that is too small to contain all of
/// received the data.
fn test_short_recv_buf_dgram(
    init_method: SocketInitMethod,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_server) =
        socket_init_helper(init_method, sock_type, flag, /* bind_client = */ false);

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

/// Test sendto() and recvfrom() using a dgram socket and make sure that all sent
/// messages are received as expected.
fn test_msg_order_dgram(
    init_method: SocketInitMethod,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_server) =
        socket_init_helper(init_method, sock_type, flag, /* bind_client = */ false);

    // read and write messages and see if something unexpected happens
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

/// Test sendto() and recvfrom() for sockets using large buffers (10^6 bytes).
fn test_large_buf(
    init_method: SocketInitMethod,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_peer) =
        socket_init_helper(init_method, sock_type, flag, /* bind_client = */ false);

    test_utils::run_and_close_fds(&[fd_client, fd_peer], || {
        // try to send 1_000_000 bytes, but may send fewer
        let expected_err = match (init_method.domain(), sock_type) {
            (_, libc::SOCK_DGRAM) => &[libc::EMSGSIZE][..],
            (_, libc::SOCK_SEQPACKET) => &[libc::EMSGSIZE][..],
            _ => &[],
        };
        simple_sendto_helper(fd_client, &vec![1u8; 1_000_000], &expected_err, false)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // try to read 1_000_000 bytes, but may read fewer
        let expected_err = match (init_method.domain(), sock_type) {
            (_, libc::SOCK_DGRAM) => &[libc::EAGAIN][..],
            (_, libc::SOCK_SEQPACKET) => &[libc::EAGAIN][..],
            _ => &[],
        };
        simple_recvfrom_helper(fd_peer, &mut vec![0u8; 1_000_000], &expected_err, false)?;

        Ok(())
    })
}

/// Test sendto() and recvfrom() for UDP sockets just above the message limit of 65507 bytes.
fn test_large_buf_udp() -> Result<(), String> {
    let (fd_client, fd_server) = socket_init_helper(
        SocketInitMethod::Inet,
        libc::SOCK_DGRAM,
        libc::SOCK_NONBLOCK,
        /* bind_client = */ false,
    );

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

/// A helper function to call sendto() and recvfrom() with valid values
/// and a user-provided fd.
fn fd_test_helper(
    fd: libc::c_int,
    domain: libc::c_int,
    sendto_errnos: &[libc::c_int],
    recvfrom_errnos: &[libc::c_int],
) -> Result<(), String> {
    let sendto_buf: Vec<u8> = vec![1, 2, 3];
    let mut recvfrom_buf: Vec<u8> = vec![1, 2, 3];

    let (addr, len) = match domain {
        libc::AF_INET => (
            SockAddr::Inet(libc::sockaddr_in {
                sin_family: libc::AF_INET as u16,
                sin_port: 11111u16.to_be(),
                sin_addr: libc::in_addr {
                    s_addr: libc::INADDR_LOOPBACK.to_be(),
                },
                sin_zero: [0; 8],
            }),
            std::mem::size_of::<libc::sockaddr_in>() as u32,
        ),
        libc::AF_UNIX => {
            let mut path = [0i8; 108];
            path[1] = 3;
            path[2] = 63;
            path[3] = 103;
            path[4] = -124;
            path[5] = 107;
            (
                SockAddr::Unix(libc::sockaddr_un {
                    sun_family: libc::AF_UNIX as u16,
                    sun_path: path,
                }),
                2 + 1 + 5, // address family + null byte + 5 characters
            )
        }
        _ => unimplemented!(),
    };

    let sendto_args = SendtoArguments {
        fd,
        len: sendto_buf.len(),
        buf: Some(&sendto_buf),
        flags: 0,
        addr: Some(addr),
        addr_len: len,
    };

    let mut recvfrom_args = RecvfromArguments {
        fd,
        len: recvfrom_buf.len(),
        buf: Some(&mut recvfrom_buf),
        flags: 0,
        addr: Some(addr),
        addr_len: Some(len),
    };

    check_sendto_call(&sendto_args, sendto_errnos, true)?;

    // shadow needs to run events
    assert_eq!(unsafe { libc::usleep(10000) }, 0);

    check_recvfrom_call(&mut recvfrom_args, recvfrom_errnos, true)?;

    Ok(())
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
    let (addr_ptr, addr_max_len) = match args.addr {
        Some(ref x) => (x.as_ptr(), x.ptr_size()),
        None => (std::ptr::null(), 0),
    };

    // if the pointers will be non-null, make sure the length is not greater than the actual data size
    // so that we don't segfault
    if args.buf.is_some() {
        assert!(args.len as usize <= args.buf.unwrap().len());
    }
    if args.addr.is_some() {
        assert!(args.addr_len <= addr_max_len);
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
                addr_ptr,
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
    let (addr_ptr, addr_max_len) = match args.addr {
        Some(ref mut x) => (x.as_mut_ptr(), x.ptr_size()),
        None => (std::ptr::null_mut(), 0),
    };

    // if the pointers will be non-null, make sure the length is not greater than the actual data size
    // so that we don't segfault
    if args.buf.is_some() {
        assert!(args.len as usize <= args.buf.as_ref().unwrap().len());
    }
    if args.addr.is_some() && args.addr_len.is_some() {
        assert!(args.addr_len.unwrap() <= addr_max_len);
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
                addr_ptr,
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
