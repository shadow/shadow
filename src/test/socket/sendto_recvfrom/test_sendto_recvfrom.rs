/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use std::collections::VecDeque;
use std::hash::Hasher;
use std::os::unix::io::AsRawFd;

use nix::sys::socket::MsgFlags;
use rand::RngCore;
use rand::SeedableRng;

use test_utils::running_in_shadow;
use test_utils::socket_utils::{
    autobind_helper, connect_to_peername, dgram_connect_helper, socket_init_helper, SockAddr,
    SocketInitMethod,
};
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

    let domains = [libc::AF_INET, libc::AF_UNIX];

    for &domain in domains.iter() {
        // add details to the test names to avoid duplicates
        let append_args = |s| format!("{} <domain={:?}>", s, domain);

        tests.extend(vec![
            test_utils::ShadowTest::new(
                &append_args("test_invalid_fd"),
                move || test_invalid_fd(domain),
                set![TestEnv::Libc, TestEnv::Shadow],
            ),
            test_utils::ShadowTest::new(
                &append_args("test_non_existent_fd"),
                move || test_non_existent_fd(domain),
                set![TestEnv::Libc, TestEnv::Shadow],
            ),
            test_utils::ShadowTest::new(
                &append_args("test_non_socket_fd"),
                move || test_non_socket_fd(domain),
                set![TestEnv::Libc, TestEnv::Shadow],
            ),
            test_utils::ShadowTest::new(
                &append_args("test_large_buf_udp"),
                test_large_buf_udp,
                set![TestEnv::Libc, TestEnv::Shadow],
            ),
            test_utils::ShadowTest::new(
                &append_args("test_send_after_dgram_peer_close"),
                move || test_send_after_dgram_peer_close(domain),
                set![TestEnv::Libc, TestEnv::Shadow],
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
                set![TestEnv::Libc, TestEnv::Shadow],
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
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_zero_len_buf"),
                    move || test_zero_len_buf(method, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_invalid_flag"),
                    move || test_invalid_flag(method, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_flag_dontwait"),
                    move || test_flag_dontwait(method, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
            ]);
        }

        tests.extend(vec![test_utils::ShadowTest::new(
            &append_args("test_nonblocking_stream"),
            move || test_nonblocking_stream(method),
            set![TestEnv::Libc, TestEnv::Shadow],
        )]);
    }

    for &method in &[SocketInitMethod::Unix, SocketInitMethod::UnixSocketpair] {
        for &sock_type in &[libc::SOCK_STREAM, libc::SOCK_DGRAM, libc::SOCK_SEQPACKET] {
            // add details to the test names to avoid duplicates
            let append_args =
                |s| format!("{} <init_method={:?}, sock_type={}>", s, method, sock_type);

            tests.extend(vec![test_utils::ShadowTest::new(
                &append_args("test_unix_buffer_full"),
                move || test_unix_buffer_full(method, sock_type),
                set![TestEnv::Libc, TestEnv::Shadow],
            )]);
        }
    }

    let flags = [0, libc::SOCK_NONBLOCK, libc::SOCK_CLOEXEC];

    for &method in init_methods.iter() {
        for &flag in flags.iter() {
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
                        set![TestEnv::Libc, TestEnv::Shadow],
                    ),
                    test_utils::ShadowTest::new(
                        &append_args("test_null_addr_len"),
                        move || test_null_addr_len(method, sock_type, flag),
                        set![TestEnv::Libc, TestEnv::Shadow],
                    ),
                    test_utils::ShadowTest::new(
                        &append_args("test_null_both"),
                        move || test_null_both(method, sock_type, flag),
                        set![TestEnv::Libc, TestEnv::Shadow],
                    ),
                    test_utils::ShadowTest::new(
                        &append_args("test_nonnull_addr"),
                        move || test_nonnull_addr(method, sock_type, flag),
                        set![TestEnv::Libc, TestEnv::Shadow],
                    ),
                    test_utils::ShadowTest::new(
                        &append_args("test_recv_addr <bind_client=false>"),
                        move || test_recv_addr(method, sock_type, flag, false),
                        set![TestEnv::Libc, TestEnv::Shadow],
                    ),
                    test_utils::ShadowTest::new(
                        &append_args("test_recv_addr <bind_client=true>"),
                        move || test_recv_addr(method, sock_type, flag, true),
                        set![TestEnv::Libc, TestEnv::Shadow],
                    ),
                ]);

                // if non-blocking
                if flag & libc::SOCK_NONBLOCK != 0 {
                    tests.extend(vec![
                        test_utils::ShadowTest::new(
                            &append_args("test_large_buf"),
                            move || test_large_buf(method, sock_type, flag),
                            set![TestEnv::Libc, TestEnv::Shadow],
                        ),
                        test_utils::ShadowTest::new(
                            &append_args("test_after_peer_close_empty_buf"),
                            move || test_after_peer_close_empty_buf(method, sock_type, flag),
                            set![TestEnv::Libc, TestEnv::Shadow],
                        ),
                        test_utils::ShadowTest::new(
                            &append_args("test_after_peer_close_nonempty_buf"),
                            move || test_after_peer_close_nonempty_buf(method, sock_type, flag),
                            // TODO: doesn't pass in shadow for inet or unix sockets
                            set![TestEnv::Libc],
                        ),
                        test_utils::ShadowTest::new(
                            &append_args("test_recvfrom_econnrefused_after_sendto"),
                            move || {
                                test_recvfrom_econnrefused_after_sendto(method, sock_type, flag)
                            },
                            match method.domain() {
                                // TODO: enable if shadow ever supports ICMP
                                libc::AF_INET => set![TestEnv::Libc],
                                _ => set![TestEnv::Libc, TestEnv::Shadow],
                            },
                        ),
                    ]);
                }

                // if a message-based socket
                if [libc::SOCK_DGRAM, libc::SOCK_SEQPACKET].contains(&sock_type) {
                    tests.extend(vec![
                        test_utils::ShadowTest::new(
                            &append_args("test_short_recv_buf_dgram"),
                            move || test_short_recv_buf_dgram(method, sock_type, flag),
                            set![TestEnv::Libc, TestEnv::Shadow],
                        ),
                        test_utils::ShadowTest::new(
                            &append_args("test_msg_order_dgram"),
                            move || test_msg_order_dgram(method, sock_type, flag),
                            set![TestEnv::Libc, TestEnv::Shadow],
                        ),
                    ]);
                }

                if method != SocketInitMethod::UnixSocketpair {
                    tests.extend(vec![test_utils::ShadowTest::new(
                        &append_args("test_null_addr_not_connected"),
                        move || test_null_addr_not_connected(method, sock_type, flag),
                        set![TestEnv::Libc, TestEnv::Shadow],
                    )]);
                }
            }
        }
    }

    tests.extend(vec![test_utils::ShadowTest::new(
        "test_unix_dgram_multiple_senders",
        test_unix_dgram_multiple_senders,
        set![TestEnv::Libc, TestEnv::Shadow],
    )]);

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
        simple_sendto_helper(fd_client, &[], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // receive 0 bytes; an EAGAIN error expected only if a stream socket
        // dgram sockets can receive messages of 0 bytes
        let e = match sock_type {
            libc::SOCK_STREAM => vec![libc::EAGAIN],
            libc::SOCK_DGRAM | libc::SOCK_SEQPACKET => vec![],
            _ => unimplemented!(),
        };
        simple_recvfrom_helper(fd_server, &mut [], &e, true)?;

        // send >0 bytes; no errors expected
        simple_sendto_helper(fd_client, &[1, 2, 3], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // receive 0 bytes; no errors expected
        simple_recvfrom_helper(fd_server, &mut [], &[], true)?;

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
        assert!(!test_utils::is_readable(fd_peer, 0).unwrap());
        simple_recvfrom_helper(fd_peer, &mut [0u8; 10], &[libc::EAGAIN], true)?;

        // send 10 bytes; no errors expected
        assert!(test_utils::is_writable(fd_client, 0).unwrap());
        simple_sendto_helper(fd_client, &[1u8; 10], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read 10 bytes into a 20 byte buffer; no errors expected
        assert!(test_utils::is_readable(fd_peer, 0).unwrap());
        simple_recvfrom_helper(fd_peer, &mut [0u8; 20], &[], false)?;

        // try to read 10 bytes; an EAGAIN error expected
        assert!(!test_utils::is_readable(fd_peer, 0).unwrap());
        simple_recvfrom_helper(fd_peer, &mut [0u8; 10], &[libc::EAGAIN], false)?;

        let mut send_hash = std::collections::hash_map::DefaultHasher::new();
        let mut recv_hash = std::collections::hash_map::DefaultHasher::new();

        let mut send_rng = rand::rngs::SmallRng::seed_from_u64(0);

        let mut send_buf = vec![0u8; 1_000_000];
        let mut recv_buf = vec![0u8; 1_000_000];

        // send bytes until an EAGAIN error
        let mut bytes_sent = 0;
        loop {
            send_rng.fill_bytes(&mut send_buf);

            let was_writable = test_utils::is_writable(fd_client, 0).unwrap();

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
            test_utils::result_assert(rv != 0, "sendto rv is 0")?;

            if rv == -1 {
                test_utils::result_assert_eq(errno, libc::EAGAIN, "Unexpected errno")?;
                assert!(!was_writable);
                break;
            }

            // for some reason this isn't always true on Linux
            if test_utils::running_in_shadow() {
                // if we successfully sent bytes, we'd expect that the socket was writable
                assert!(was_writable);
            }

            send_hash.write(&send_buf[..(rv as usize)]);

            bytes_sent += rv;

            // shadow needs to run events
            assert_eq!(unsafe { libc::usleep(1000) }, 0);
        }

        // read bytes until an EAGAIN error
        let mut bytes_read = 0;
        loop {
            let was_readable = test_utils::is_readable(fd_peer, 0).unwrap();

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
            test_utils::result_assert(rv != 0, "recvfrom rv is EOF")?;

            if rv == -1 {
                test_utils::result_assert_eq(errno, libc::EAGAIN, "Unexpected errno")?;
                assert!(!was_readable);
                if bytes_read < bytes_sent {
                    // This can happen when running natively.
                    println!(
                        "Read blocked with {} bytes remaining; sleeping and retrying",
                        bytes_sent - bytes_read
                    );
                    assert_eq!(unsafe { libc::usleep(1000) }, 0);
                    continue;
                }
                break;
            }

            // if we successfully received bytes, we'd expect that the socket was readable
            assert!(was_readable);

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

/// Test sendto() and recvfrom() using a socket that is not connected, while using a null address
/// argument.
fn test_null_addr_not_connected(
    init_method: SocketInitMethod,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(init_method.domain(), sock_type | flag, 0) };
    let fd_server = unsafe { libc::socket(init_method.domain(), sock_type | flag, 0) };
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
        let expected_err = match (init_method.domain(), sock_type) {
            (libc::AF_INET, libc::SOCK_DGRAM) => vec![libc::EDESTADDRREQ],
            (libc::AF_INET, _) => vec![libc::EPIPE],
            (libc::AF_UNIX, _) => vec![libc::ENOTCONN],
            _ => unimplemented!(),
        };
        check_sendto_call(&sendto_args_1, &expected_err, true)?;

        // send 3 bytes using a non-null sockaddr; no error expected for dgram sockets
        let expected_err = match (init_method.domain(), sock_type) {
            (_, libc::SOCK_DGRAM) => vec![],
            (libc::AF_UNIX, libc::SOCK_STREAM) => vec![libc::EOPNOTSUPP],
            (libc::AF_UNIX, libc::SOCK_SEQPACKET) => vec![libc::ENOTCONN],
            _ => vec![libc::EPIPE],
        };
        check_sendto_call(&sendto_args_2, &expected_err, true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read 3 bytes using a null sockaddr; no error expected for dgram sockets
        let expected_err = match (init_method.domain(), sock_type) {
            (_, libc::SOCK_DGRAM) => vec![],
            (libc::AF_UNIX, libc::SOCK_STREAM) => vec![libc::EINVAL],
            _ => vec![libc::ENOTCONN],
        };
        check_recvfrom_call(&mut recvfrom_args, &expected_err, true)?;

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
        simple_sendto_helper(fd_client, &[1u8, 2, 3], &[], true)?;

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
        simple_sendto_helper(fd_client, &[1u8, 2, 3], &[], true)?;

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
        addr_len,
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
        simple_sendto_helper(fd_client, &[1, 2, 3], &[], true)?;

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

/// Test sendto()/recvfrom() on a socket after its peer has been closed, with no buffered data.
fn test_after_peer_close_empty_buf(
    init_method: SocketInitMethod,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_peer) =
        socket_init_helper(init_method, sock_type, flag, /* bind_client = */ false);

    nix::unistd::close(fd_peer).unwrap();

    // shadow needs to run events
    assert_eq!(unsafe { libc::usleep(10000) }, 0);

    test_utils::run_and_close_fds(&[fd_client], || {
        let expected_errnos = match sock_type {
            // connectionless sockets
            libc::SOCK_DGRAM => &[libc::EAGAIN][..],
            // connection-oriented sockets
            _ => &[][..],
        };

        // read 3 bytes using a null sockaddr and a null sockaddr length
        // connection-oriented sockets will return EOF
        let rv = simple_recvfrom_helper(fd_client, &mut [1u8, 2, 3], expected_errnos, false)?;
        if expected_errnos.is_empty() {
            // if there was no error, should have returned EOF
            assert_eq!(0, rv);
        }

        let expected_errnos = match (init_method.domain(), sock_type) {
            // connectionless unix sockets
            (libc::AF_UNIX, libc::SOCK_DGRAM) => &[libc::ECONNREFUSED][..],
            // connection-oriented unix sockets
            (libc::AF_UNIX, _) => &[libc::EPIPE][..],
            // non-unix sockets
            _ => &[][..],
        };

        // send 3 bytes; unix sockets will return an error
        simple_sendto_helper(fd_client, &[1u8, 2, 3], expected_errnos, true)?;

        Ok(())
    })
}

/// Test sendto()/recvfrom() on a socket after its peer has been closed, with some buffered data.
fn test_after_peer_close_nonempty_buf(
    init_method: SocketInitMethod,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_peer) =
        socket_init_helper(init_method, sock_type, flag, /* bind_client = */ true);

    // if it's a non-socketpair dgram socket, connect the peer back to the client
    if init_method != SocketInitMethod::UnixSocketpair && sock_type == libc::SOCK_DGRAM {
        assert_eq!(0, connect_to_peername(fd_peer, fd_client));
    }

    // send 2 bytes in each direction
    simple_sendto_helper(fd_client, &[1u8, 2], &[], true)?;
    simple_sendto_helper(fd_peer, &[1u8, 2], &[], true)?;

    nix::unistd::close(fd_peer).unwrap();

    // shadow needs to run events
    assert_eq!(unsafe { libc::usleep(10000) }, 0);

    test_utils::run_and_close_fds(&[fd_client], || {
        let expected_errnos = match sock_type {
            // only seqpacket sockets return an error for some reason
            libc::SOCK_SEQPACKET => &[libc::ECONNRESET][..],
            _ => &[][..],
        };

        // read 3 bytes using a null sockaddr and a null sockaddr length
        let rv = simple_recvfrom_helper(fd_client, &mut [1u8, 2, 3], expected_errnos, false)?;
        if expected_errnos.is_empty() {
            // if there was no error, should have returned EOF
            assert_eq!(2, rv);
        }

        let expected_errnos = match (init_method.domain(), sock_type) {
            // connectionless unix sockets
            (libc::AF_UNIX, libc::SOCK_DGRAM) => &[libc::ECONNREFUSED][..],
            // connection-oriented unix sockets
            (libc::AF_UNIX, _) => &[libc::EPIPE][..],
            // connection-oriented inet socket
            (libc::AF_INET, libc::SOCK_STREAM) => &[libc::ECONNRESET][..],
            _ => &[][..],
        };

        // send 3 bytes; unix and tcp sockets will return an error
        simple_sendto_helper(fd_client, &[1u8, 2, 3], expected_errnos, true)?;

        Ok(())
    })
}

/// Test that recvfrom() on an inet dgram socket returns ECONNREFUSED if a previous sendto() failed.
fn test_recvfrom_econnrefused_after_sendto(
    init_method: SocketInitMethod,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_peer) =
        socket_init_helper(init_method, sock_type, flag, /* bind_client = */ false);

    nix::unistd::close(fd_peer).unwrap();

    // shadow needs to run events
    assert_eq!(unsafe { libc::usleep(10000) }, 0);

    test_utils::run_and_close_fds(&[fd_client], || {
        let expected_errnos = match (init_method.domain(), sock_type) {
            // connectionless unix sockets
            (libc::AF_UNIX, libc::SOCK_DGRAM) => &[libc::ECONNREFUSED][..],
            // connection-oriented unix sockets
            (libc::AF_UNIX, _) => &[libc::EPIPE][..],
            // non-unix sockets
            _ => &[][..],
        };

        // send 3 bytes; unix sockets will return an error
        simple_sendto_helper(fd_client, &[1u8, 2, 3], expected_errnos, true)?;

        let expected_errnos = match (init_method.domain(), sock_type) {
            // connectionless unix sockets
            (libc::AF_UNIX, libc::SOCK_DGRAM) => &[libc::EAGAIN][..],
            // connectionless inet sockets
            (libc::AF_INET, libc::SOCK_DGRAM) => &[libc::ECONNREFUSED][..],
            // connection-oriented sockets
            _ => &[][..],
        };

        // read 3 bytes using a null sockaddr and a null sockaddr length
        // connection-oriented sockets will return EOF
        // inet dgram (udp) socket will return ECONNREFUSED due to the previous sendto()
        let rv = simple_recvfrom_helper(fd_client, &mut [1u8, 2, 3], expected_errnos, false)?;
        if expected_errnos.is_empty() {
            // if there was no error, should have returned EOF
            assert_eq!(0, rv);
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
        simple_recvfrom_helper(fd_server, &mut [0u8; 500], &[], true)?;

        // send 200 bytes; no error expected
        simple_sendto_helper(fd_client, &[1u8; 200], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read 200 bytes with a 500 byte buffer; no error expected
        let received_bytes = simple_recvfrom_helper(fd_server, &mut vec![0u8; 500], &[], false)?;
        test_utils::result_assert_eq(received_bytes, 200, "Unexpected number of bytes read")?;

        // send 3 messages of different lengths; no error expected
        simple_sendto_helper(fd_client, &[1u8; 1], &[], true)?;
        simple_sendto_helper(fd_client, &[1u8; 3], &[], true)?;
        simple_sendto_helper(fd_client, &[1u8; 5], &[], true)?;

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
        simple_sendto_helper(fd_client, &[1u8; 1_000_000], expected_err, false)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // try to read 1_000_000 bytes, but may read fewer
        let expected_err = match (init_method.domain(), sock_type) {
            (_, libc::SOCK_DGRAM) => &[libc::EAGAIN][..],
            (_, libc::SOCK_SEQPACKET) => &[libc::EAGAIN][..],
            _ => &[],
        };
        simple_recvfrom_helper(fd_peer, &mut [0u8; 1_000_000], expected_err, false)?;

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

/// Test connecting a dgram socket to a bound socket, closing the bound socket, creating a new
/// socket and binding it to that same bind address, and then writing to the connected socket.
fn test_send_after_dgram_peer_close(domain: libc::c_int) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(domain, libc::SOCK_DGRAM | libc::SOCK_NONBLOCK, 0) };
    let fd_peer = unsafe { libc::socket(domain, libc::SOCK_DGRAM | libc::SOCK_NONBLOCK, 0) };
    assert!(fd_client >= 0);
    assert!(fd_peer >= 0);

    // bind the peer socket to some unused address
    let (peer_addr, peer_addr_len) = autobind_helper(fd_peer, domain);
    // connect the client to the peer
    dgram_connect_helper(fd_client, peer_addr, peer_addr_len);

    // close the original peer
    nix::unistd::close(fd_peer).unwrap();

    // a new socket that will be given the same address as the original peer
    let fd_new_peer = unsafe { libc::socket(domain, libc::SOCK_DGRAM | libc::SOCK_NONBLOCK, 0) };
    assert!(fd_new_peer >= 0);

    // bind the new socket to the old peer address
    {
        let rv = unsafe { libc::bind(fd_new_peer, peer_addr.as_ptr(), peer_addr_len) };
        assert_eq!(rv, 0);
    }

    test_utils::run_and_close_fds(&[fd_client, fd_new_peer], || {
        let expected_err = match domain {
            // even though there is a new socket bound to the same peer address, the unix socket
            // will not send new messages to it
            libc::AF_UNIX => &[libc::ECONNREFUSED][..],
            _ => &[],
        };

        simple_sendto_helper(fd_client, &[1u8; 100], expected_err, true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10_000) }, 0);

        let expected_err = match domain {
            // since the unix socket send was unsuccessful, the recv will be as well
            libc::AF_UNIX => &[libc::EWOULDBLOCK][..],
            // non-unix sockets will successfully read the message on the new peer
            _ => &[],
        };

        simple_recvfrom_helper(fd_new_peer, &mut [0u8; 100], expected_err, true)?;

        Ok(())
    })
}

/// Test reading and writing from/to unix sockets when their buffers are full.
fn test_unix_buffer_full(
    init_method: SocketInitMethod,
    sock_type: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_server) = socket_init_helper(
        init_method,
        sock_type,
        libc::SOCK_NONBLOCK,
        /* bind_client = */ false,
    );

    const BUF_SIZE: usize = 10_000;

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        let send_buf = vec![0u8; BUF_SIZE];
        let mut recv_buf = vec![0u8; BUF_SIZE];

        // fill up buffer (might not be completely full for dgram sockets)
        loop {
            let was_writable = test_utils::is_writable(fd_client, 0).unwrap();
            let rv = nix::sys::socket::send(fd_client, &send_buf, MsgFlags::empty());
            if rv == Err(nix::errno::Errno::EAGAIN) {
                if sock_type == libc::SOCK_STREAM {
                    // dgram sockets may have space available, but not enough for this specific
                    // packet, so may have been writable
                    assert!(!was_writable);
                }

                break;
            }

            if sock_type == libc::SOCK_STREAM {
                assert!(rv.unwrap() <= BUF_SIZE);
            } else {
                assert_eq!(rv.unwrap(), BUF_SIZE);
            }

            if test_utils::running_in_shadow() {
                // for some reason this isn't always true on Linux
                assert!(was_writable);
            }
        }

        // read one packet/chunk
        assert!(test_utils::is_readable(fd_server, 0).unwrap());
        let rv = nix::sys::socket::recv(fd_server, &mut recv_buf, MsgFlags::empty()).unwrap();
        assert_eq!(rv, BUF_SIZE);

        // write one packet/chunk
        if test_utils::running_in_shadow() {
            // for some reason this isn't always true on Linux
            assert!(test_utils::is_writable(fd_client, 0).unwrap());
        }
        let rv = nix::sys::socket::send(fd_client, &send_buf, MsgFlags::empty()).unwrap();
        assert_eq!(rv, BUF_SIZE);

        // write one packet/chunk, but will fail
        if sock_type == libc::SOCK_STREAM {
            // dgram sockets may have space available, but not enough for this specific
            // packet, so may have been writable
            assert!(!test_utils::is_writable(fd_client, 0).unwrap());
        }
        let rv = nix::sys::socket::send(fd_client, &send_buf, MsgFlags::empty());
        assert_eq!(rv, Err(nix::errno::Errno::EAGAIN));

        // fill up buffer (one byte at a time for dgram sockets)
        loop {
            let was_writable = test_utils::is_writable(fd_client, 0).unwrap();
            let rv = nix::sys::socket::send(fd_client, &[0u8], MsgFlags::empty());
            if rv == Err(nix::errno::Errno::EAGAIN) {
                // the buffer is completely full (for both stream and dgram sockets)
                assert!(!was_writable);
                break;
            }

            assert_eq!(rv.unwrap(), 1);
            assert!(was_writable);
        }

        // reads one byte for stream sockets, or one BUF_SIZE packet for dgram sockets
        assert!(test_utils::is_readable(fd_server, 0).unwrap());
        let rv = nix::sys::socket::recv(fd_server, &mut [0u8], MsgFlags::empty()).unwrap();
        assert_eq!(rv, 1);

        // reads one byte for stream sockets, or one BUF_SIZE packet for dgram sockets
        assert!(test_utils::is_readable(fd_server, 0).unwrap());
        let rv = nix::sys::socket::recv(fd_server, &mut [0u8], MsgFlags::empty()).unwrap();
        assert_eq!(rv, 1);

        // this fails in linux for some reason
        if test_utils::running_in_shadow() {
            // write one byte
            assert!(test_utils::is_writable(fd_client, 0).unwrap());
            let rv = nix::sys::socket::send(fd_client, &[0u8], MsgFlags::empty()).unwrap();
            assert_eq!(rv, 1);

            // write one byte
            assert!(test_utils::is_writable(fd_client, 0).unwrap());
            let rv = nix::sys::socket::send(fd_client, &[0u8], MsgFlags::empty()).unwrap();
            assert_eq!(rv, 1);

            // attempt to write one byte
            if sock_type == libc::SOCK_STREAM {
                // will fail
                assert!(!test_utils::is_writable(fd_client, 0).unwrap());
                let rv = nix::sys::socket::send(fd_client, &[0u8], MsgFlags::empty());
                assert_eq!(rv, Err(nix::errno::Errno::EAGAIN));
            } else {
                // will succeed
                assert!(test_utils::is_writable(fd_client, 0).unwrap());
                let rv = nix::sys::socket::send(fd_client, &[0u8], MsgFlags::empty()).unwrap();
                assert_eq!(rv, 1);
            }
        }

        Ok(())
    })
}

/// Test the behaviour of unix dgram sockets when there are multiple senders.
fn test_unix_dgram_multiple_senders() -> Result<(), String> {
    // a single destination socket
    let dst_fd = unsafe { libc::socket(libc::AF_UNIX, libc::SOCK_DGRAM | libc::SOCK_NONBLOCK, 0) };
    assert!(dst_fd >= 0);

    let (addr, addr_len) = autobind_helper(dst_fd, libc::AF_UNIX);

    // get 10 source sockets
    let src_fds: Vec<_> = std::iter::repeat_with(|| unsafe {
        libc::socket(libc::AF_UNIX, libc::SOCK_DGRAM | libc::SOCK_NONBLOCK, 0)
    })
    .take(10)
    // make sure the fds are valid
    .map(|x| (x >= 0).then_some(x))
    .collect::<Option<_>>()
    .unwrap();

    // connect all of the clients to the destination
    for src_fd in &src_fds {
        dgram_connect_helper(*src_fd, addr, addr_len)
    }

    let send_buf = vec![0u8; 1000];

    // list of (src_fd, bytes_written)
    let mut packets_written: VecDeque<(libc::c_int, usize)> = VecDeque::new();

    // loop over source sockets until one of them fails to write
    for x in 0.. {
        let src_fd = src_fds[x % src_fds.len()];

        // write one packet
        let n = match nix::unistd::write(src_fd, &send_buf) {
            Ok(n) => n,
            // if any socket can't write, then we're done writing
            Err(nix::errno::Errno::EWOULDBLOCK) => break,
            Err(e) => panic!("unexpected write() errno {:?}", e),
        };

        packets_written.push_back((src_fd, n));
    }

    // loop until we can't read any more packets
    loop {
        let mut recv_buf = vec![0u8; send_buf.len()];

        // read one packet
        let n = match nix::unistd::read(dst_fd, &mut recv_buf) {
            Ok(n) => n,
            Err(nix::errno::Errno::EWOULDBLOCK) => break,
            Err(e) => panic!("unexpected write() errno {:?}", e),
        };

        // check that the packet length is as expected
        let (src_fd, len) = packets_written.pop_front().unwrap();
        assert_eq!(len, n);

        // make sure at least one source socket is now writable
        let mut poll_fds: Vec<_> = src_fds
            .iter()
            .map(|fd| nix::poll::PollFd::new(*fd, nix::poll::PollFlags::POLLOUT))
            .collect();
        if nix::poll::poll(&mut poll_fds, 0).unwrap() == 0 {
            if running_in_shadow() {
                panic!("Expected at least one writable socket");
            }
            // This happens in Linux sometimes, but we don't understand why.
            // https://github.com/shadow/shadow/issues/2195
            continue;
        }

        // make sure the socket that wrote the packet is now one of the writable sockets
        let mut ready_fds = poll_fds
            .iter()
            .filter(|x| x.revents().unwrap().contains(nix::poll::PollFlags::POLLOUT))
            .map(|x| x.as_raw_fd());
        assert!(ready_fds.any(|x| x == src_fd));
    }

    for src_fd in &src_fds {
        nix::unistd::close(*src_fd).unwrap();
    }

    nix::unistd::close(dst_fd).unwrap();

    Ok(())
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
        fd,
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
        fd,
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
    if verify_num_bytes && expected_errnos.is_empty() {
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
    if verify_num_bytes && expected_errnos.is_empty() {
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
