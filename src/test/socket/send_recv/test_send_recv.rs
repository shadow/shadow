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
    autobind_helper, connect_to_peername, dgram_connect_helper, socket_init_helper,
    stream_connect_helper, SockAddr, SocketInitMethod,
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

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
enum SendRecvMethod {
    /// For `sendto()`/`recvfrom()`.
    ToFrom,
    /// For `sendmsg()`/`recvmsg()`.
    #[allow(dead_code)]
    Msg,
    /// For `sendmmsg()`/`recvmmsg()`.
    #[allow(dead_code)]
    Mmsg,
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

    let sys_methods = [
        SendRecvMethod::ToFrom,
        SendRecvMethod::Msg,
        // TODO: support sendmmsg/recvmmsg
        //SendRecvMethod::Mmsg,
    ];

    for &sys_method in sys_methods.iter() {
        // add details to the test names to avoid duplicates
        let append_args = |s| format!("{s} <sys_method={sys_method:?}>");

        let domains = [libc::AF_INET, libc::AF_UNIX];

        for &domain in domains.iter() {
            // add details to the test names to avoid duplicates
            let append_args = |s| format!("{s} <sys_method={sys_method:?}, domain={domain:?}>");

            tests.extend(vec![
                test_utils::ShadowTest::new(
                    &append_args("test_invalid_fd"),
                    move || test_invalid_fd(sys_method, domain),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_non_existent_fd"),
                    move || test_non_existent_fd(sys_method, domain),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_non_socket_fd"),
                    move || test_non_socket_fd(sys_method, domain),
                    set![TestEnv::Libc, TestEnv::Shadow],
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_send_after_dgram_peer_close"),
                    move || test_send_after_dgram_peer_close(sys_method, domain),
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
                let append_args = |s| {
                    format!(
                        "{s} <sys_method={sys_method:?}, domain={domain:?}, sock_type={sock_type}>"
                    )
                };

                tests.extend(vec![test_utils::ShadowTest::new(
                    &append_args("test_not_connected"),
                    move || test_not_connected(sys_method, domain, sock_type),
                    set![TestEnv::Libc, TestEnv::Shadow],
                )]);
            }
        }

        let init_methods = [
            SocketInitMethod::Inet,
            SocketInitMethod::Unix,
            SocketInitMethod::UnixSocketpair,
        ];

        for &init_method in init_methods.iter() {
            // add details to the test names to avoid duplicates
            let append_args =
                |s| format!("{s} <sys_method={sys_method:?}, init_method={init_method:?}>");

            let sock_types = match init_method.domain() {
                libc::AF_INET => &[libc::SOCK_STREAM, libc::SOCK_DGRAM][..],
                libc::AF_UNIX => &[libc::SOCK_STREAM, libc::SOCK_DGRAM, libc::SOCK_SEQPACKET][..],
                _ => unimplemented!(),
            };

            for &sock_type in sock_types.iter() {
                // add details to the test names to avoid duplicates
                let append_args = |s| {
                    format!("{s} <sys_method={sys_method:?}, init_method={init_method:?}, sock_type={sock_type}>")
                };

                tests.extend(vec![
                    test_utils::ShadowTest::new(
                        &append_args("test_null_buf"),
                        move || test_null_buf(sys_method, init_method, sock_type),
                        set![TestEnv::Libc, TestEnv::Shadow],
                    ),
                    test_utils::ShadowTest::new(
                        &append_args("test_zero_len_buf"),
                        move || test_zero_len_buf(sys_method, init_method, sock_type),
                        set![TestEnv::Libc, TestEnv::Shadow],
                    ),
                    test_utils::ShadowTest::new(
                        &append_args("test_flag_dontwait"),
                        move || test_flag_dontwait(sys_method, init_method, sock_type),
                        set![TestEnv::Libc, TestEnv::Shadow],
                    ),
                    test_utils::ShadowTest::new(
                        &append_args("test_flag_peek"),
                        move || test_flag_peek(sys_method, init_method, sock_type),
                        match (init_method.domain(), sock_type) {
                            // TODO: enable if shadow supports MSG_PEEK for tcp or unix sockets
                            (libc::AF_INET, libc::SOCK_DGRAM) => {
                                set![TestEnv::Libc, TestEnv::Shadow]
                            }
                            _ => set![TestEnv::Libc],
                        },
                    ),
                    test_utils::ShadowTest::new(
                        &append_args("test_blocking"),
                        move || test_blocking(sys_method, init_method, sock_type),
                        set![TestEnv::Libc, TestEnv::Shadow],
                    ),
                ]);
            }

            tests.extend(vec![test_utils::ShadowTest::new(
                &append_args("test_nonblocking_stream"),
                move || test_nonblocking_stream(sys_method, init_method),
                set![TestEnv::Libc, TestEnv::Shadow],
            )]);
        }

        let flags = [0, libc::SOCK_NONBLOCK, libc::SOCK_CLOEXEC];

        for &init_method in init_methods.iter() {
            for &flag in flags.iter() {
                let sock_types = match init_method.domain() {
                    libc::AF_INET => &[libc::SOCK_STREAM, libc::SOCK_DGRAM][..],
                    libc::AF_UNIX => {
                        &[libc::SOCK_STREAM, libc::SOCK_DGRAM, libc::SOCK_SEQPACKET][..]
                    }
                    _ => unimplemented!(),
                };

                for &sock_type in sock_types.iter() {
                    // add details to the test names to avoid duplicates
                    let append_args = |s| {
                        format!(
                            "{s} <sys_method={sys_method:?}, init_method={init_method:?}, flag={flag}, sock_type={sock_type}>"
                        )
                    };

                    tests.extend(vec![
                        test_utils::ShadowTest::new(
                            &append_args("test_null_addr"),
                            move || test_null_addr(sys_method, init_method, sock_type, flag),
                            set![TestEnv::Libc, TestEnv::Shadow],
                        ),
                        test_utils::ShadowTest::new(
                            &append_args("test_null_both"),
                            move || test_null_both(sys_method, init_method, sock_type, flag),
                            set![TestEnv::Libc, TestEnv::Shadow],
                        ),
                        test_utils::ShadowTest::new(
                            &append_args("test_nonnull_addr"),
                            move || test_nonnull_addr(sys_method, init_method, sock_type, flag),
                            set![TestEnv::Libc, TestEnv::Shadow],
                        ),
                        test_utils::ShadowTest::new(
                            &append_args("test_recv_addr <bind_client=false>"),
                            move || test_recv_addr(sys_method, init_method, sock_type, flag, false),
                            set![TestEnv::Libc, TestEnv::Shadow],
                        ),
                        test_utils::ShadowTest::new(
                            &append_args("test_recv_addr <bind_client=true>"),
                            move || test_recv_addr(sys_method, init_method, sock_type, flag, true),
                            set![TestEnv::Libc, TestEnv::Shadow],
                        ),
                        test_utils::ShadowTest::new(
                            &append_args("test_recv_flag_trunc"),
                            move || test_recv_flag_trunc(sys_method, init_method, sock_type, flag),
                            match (init_method.domain(), sock_type) {
                                // TODO: enable if shadow supports MSG_TRUNC for tcp sockets
                                (libc::AF_INET, libc::SOCK_STREAM) => set![TestEnv::Libc],
                                _ => set![TestEnv::Libc, TestEnv::Shadow],
                            },
                        ),
                        test_utils::ShadowTest::new(
                            &append_args("test_send_flag_trunc"),
                            move || test_send_flag_trunc(sys_method, init_method, sock_type, flag),
                            set![TestEnv::Libc, TestEnv::Shadow],
                        ),
                    ]);

                    // if sendto()/recvfrom()
                    if sys_method == SendRecvMethod::ToFrom {
                        tests.extend(vec![test_utils::ShadowTest::new(
                            &append_args("test_null_addr_len"),
                            move || test_null_addr_len(init_method, sock_type, flag),
                            set![TestEnv::Libc, TestEnv::Shadow],
                        )]);
                    }

                    // if non-blocking
                    if flag & libc::SOCK_NONBLOCK != 0 {
                        tests.extend(vec![
                            test_utils::ShadowTest::new(
                                &append_args("test_large_buf"),
                                move || test_large_buf(sys_method, init_method, sock_type, flag),
                                set![TestEnv::Libc, TestEnv::Shadow],
                            ),
                            test_utils::ShadowTest::new(
                                &append_args("test_after_peer_close_empty_buf"),
                                move || {
                                    test_after_peer_close_empty_buf(
                                        sys_method,
                                        init_method,
                                        sock_type,
                                        flag,
                                    )
                                },
                                set![TestEnv::Libc, TestEnv::Shadow],
                            ),
                            test_utils::ShadowTest::new(
                                &append_args("test_after_peer_close_nonempty_buf"),
                                move || {
                                    test_after_peer_close_nonempty_buf(
                                        sys_method,
                                        init_method,
                                        sock_type,
                                        flag,
                                    )
                                },
                                // TODO: doesn't pass in shadow for inet or unix sockets
                                set![TestEnv::Libc],
                            ),
                            test_utils::ShadowTest::new(
                                &append_args("test_recvfrom_econnrefused_after_sendto"),
                                move || {
                                    test_recvfrom_econnrefused_after_sendto(
                                        sys_method,
                                        init_method,
                                        sock_type,
                                        flag,
                                    )
                                },
                                match init_method.domain() {
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
                                move || {
                                    test_short_recv_buf_dgram(
                                        sys_method,
                                        init_method,
                                        sock_type,
                                        flag,
                                    )
                                },
                                set![TestEnv::Libc, TestEnv::Shadow],
                            ),
                            test_utils::ShadowTest::new(
                                &append_args("test_msg_order_dgram"),
                                move || {
                                    test_msg_order_dgram(sys_method, init_method, sock_type, flag)
                                },
                                set![TestEnv::Libc, TestEnv::Shadow],
                            ),
                        ]);
                    }

                    if init_method != SocketInitMethod::UnixSocketpair {
                        tests.extend(vec![test_utils::ShadowTest::new(
                            &append_args("test_null_addr_not_connected"),
                            move || {
                                test_null_addr_not_connected(
                                    sys_method,
                                    init_method,
                                    sock_type,
                                    flag,
                                )
                            },
                            set![TestEnv::Libc, TestEnv::Shadow],
                        )]);
                    }
                }
            }
        }

        for &sock_type in &[libc::SOCK_STREAM, libc::SOCK_DGRAM] {
            // add details to the test names to avoid duplicates
            let append_args = |s| format!("{s} <sys_method={sys_method:?}, sock_type={sock_type}>");

            tests.extend(vec![test_utils::ShadowTest::new(
                &append_args("test_bound_to_inaddr_any"),
                move || test_bound_to_inaddr_any(sys_method, sock_type),
                set![TestEnv::Libc, TestEnv::Shadow],
            )]);
        }

        tests.extend(vec![test_utils::ShadowTest::new(
            &append_args("test_large_buf_udp"),
            move || test_large_buf_udp(sys_method),
            set![TestEnv::Libc, TestEnv::Shadow],
        )]);
    }

    let init_methods = [
        SocketInitMethod::Inet,
        SocketInitMethod::Unix,
        SocketInitMethod::UnixSocketpair,
    ];

    for &init_method in &init_methods {
        let sock_types = match init_method.domain() {
            libc::AF_INET => &[libc::SOCK_STREAM, libc::SOCK_DGRAM][..],
            libc::AF_UNIX => &[libc::SOCK_STREAM, libc::SOCK_DGRAM, libc::SOCK_SEQPACKET][..],
            _ => unimplemented!(),
        };

        for &sock_type in sock_types {
            // add details to the test names to avoid duplicates
            let append_args =
                |s| format!("{s} <init_method={init_method:?}, sock_type={sock_type}>");

            tests.extend(vec![test_utils::ShadowTest::new(
                &append_args("test_zero_len_buf_read_and_recv"),
                move || test_zero_len_buf_read_and_recv(init_method, sock_type),
                set![TestEnv::Libc, TestEnv::Shadow],
            )]);
        }

        let sock_types = match init_method.domain() {
            libc::AF_INET => &[libc::SOCK_DGRAM][..],
            libc::AF_UNIX => &[libc::SOCK_DGRAM, libc::SOCK_SEQPACKET][..],
            _ => unimplemented!(),
        };

        for &sock_type in sock_types {
            // add details to the test names to avoid duplicates
            let append_args =
                |s| format!("{s} <init_method={init_method:?}, sock_type={sock_type}>");

            tests.extend(vec![test_utils::ShadowTest::new(
                &append_args("test_zero_len_msg_read_and_recv"),
                move || test_zero_len_msg_read_and_recv(init_method, sock_type),
                set![TestEnv::Libc, TestEnv::Shadow],
            )]);
        }
    }

    for &init_method in &[SocketInitMethod::Unix, SocketInitMethod::UnixSocketpair] {
        for &sock_type in &[libc::SOCK_STREAM, libc::SOCK_DGRAM, libc::SOCK_SEQPACKET] {
            // add details to the test names to avoid duplicates
            let append_args =
                |s| format!("{s} <init_method={init_method:?}, sock_type={sock_type}>");

            tests.extend(vec![test_utils::ShadowTest::new(
                &append_args("test_unix_buffer_full"),
                move || test_unix_buffer_full(init_method, sock_type),
                set![TestEnv::Libc, TestEnv::Shadow],
            )]);
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
fn test_invalid_fd(sys_method: SendRecvMethod, domain: libc::c_int) -> Result<(), String> {
    // expect both sendto() and recvfrom() to return EBADF
    fd_test_helper(sys_method, -1, domain, &[libc::EBADF], &[libc::EBADF])
}

/// Test sendto() and recvfrom() using an argument that could be a fd, but is not.
fn test_non_existent_fd(sys_method: SendRecvMethod, domain: libc::c_int) -> Result<(), String> {
    // expect both sendto() and recvfrom() to return EBADF
    fd_test_helper(sys_method, 8934, domain, &[libc::EBADF], &[libc::EBADF])
}

/// Test sendto() and recvfrom() using a valid fd that is not a socket.
fn test_non_socket_fd(sys_method: SendRecvMethod, domain: libc::c_int) -> Result<(), String> {
    // expect both sendto() and recvfrom() to return ENOTSOCK
    fd_test_helper(sys_method, 0, domain, &[libc::ENOTSOCK], &[libc::ENOTSOCK])
}

/// Test sendto() and recvfrom() using a socket that is not conneected, but using a non-null address
/// argument.
fn test_not_connected(
    sys_method: SendRecvMethod,
    domain: libc::c_int,
    sock_type: libc::c_int,
) -> Result<(), String> {
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
        fd_test_helper(
            sys_method,
            fd,
            domain,
            expected_sendto_errs,
            expected_recvfrom_errs,
        )
    })
}

/// Test sendto() and recvfrom() using a null buffer, and non-zero buffer length.
fn test_null_buf(
    sys_method: SendRecvMethod,
    init_method: SocketInitMethod,
    sock_type: libc::c_int,
) -> Result<(), String> {
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
        check_send_call(&sendto_args_1, sys_method, &[libc::EFAULT], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read using a null buffer; an EAGAIN error expected
        check_recv_call(&mut recvfrom_args, sys_method, &[libc::EAGAIN], true)?;

        // send 3 bytes using a non-null buffer; no error expected
        check_send_call(&sendto_args_2, sys_method, &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read using a null buffer; an EFAULT error expected
        check_recv_call(&mut recvfrom_args, sys_method, &[libc::EFAULT], true)?;

        Ok(())
    })
}

/// Test sendto() and recvfrom() using a buffer length of zero, and a non-null buffer.
fn test_zero_len_buf(
    sys_method: SendRecvMethod,
    init_method: SocketInitMethod,
    sock_type: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_server) = socket_init_helper(
        init_method,
        sock_type,
        libc::SOCK_NONBLOCK,
        /* bind_client = */ false,
    );

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // send 0 bytes; no errors expected
        simple_sendto_helper(sys_method, fd_client, &[], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // receive 0 bytes; an EAGAIN error expected only if a stream socket
        // dgram sockets can receive messages of 0 bytes
        let e = match sock_type {
            libc::SOCK_STREAM => vec![libc::EAGAIN],
            libc::SOCK_DGRAM | libc::SOCK_SEQPACKET => vec![],
            _ => unimplemented!(),
        };
        simple_recvfrom_helper(sys_method, fd_server, &mut [], &e, true)?;

        // send >0 bytes; no errors expected
        simple_sendto_helper(sys_method, fd_client, &[1, 2, 3], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // receive 0 bytes; no errors expected
        simple_recvfrom_helper(sys_method, fd_server, &mut [], &[], true)?;

        Ok(())
    })
}

/// Test recv() and read(), which behave differently for zero-len buffers.
fn test_zero_len_buf_read_and_recv(
    init_method: SocketInitMethod,
    sock_type: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_server) = socket_init_helper(
        init_method,
        sock_type,
        libc::SOCK_NONBLOCK,
        /* bind_client = */ false,
    );

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // PART 1: test with an empty receive buffer (no data available to read)

        // recv(): use a null buffer
        let rv = unsafe { libc::recv(fd_server, std::ptr::null_mut(), 0, 0) };
        let errno = test_utils::get_errno();
        assert_eq!(rv, -1);
        assert_eq!(errno, libc::EAGAIN);

        // recvmsg(): use a null iov array
        let mut msg = libc::msghdr {
            msg_name: std::ptr::null_mut(),
            msg_namelen: 0,
            msg_iov: std::ptr::null_mut(),
            msg_iovlen: 0,
            msg_control: std::ptr::null_mut(),
            msg_controllen: 0,
            msg_flags: 0,
        };
        let rv = unsafe { libc::recvmsg(fd_server, &mut msg, 0) };
        let errno = test_utils::get_errno();
        assert_eq!(rv, -1);
        assert_eq!(errno, libc::EAGAIN);

        // recvmsg(): use a non-null iov array
        let mut iov = libc::iovec {
            iov_base: std::ptr::null_mut(),
            iov_len: 0,
        };
        let mut msg = libc::msghdr {
            msg_name: std::ptr::null_mut(),
            msg_namelen: 0,
            msg_iov: &mut iov,
            msg_iovlen: 1,
            msg_control: std::ptr::null_mut(),
            msg_controllen: 0,
            msg_flags: 0,
        };
        let rv = unsafe { libc::recvmsg(fd_server, &mut msg, 0) };
        let errno = test_utils::get_errno();
        assert_eq!(rv, -1);
        assert_eq!(errno, libc::EAGAIN);

        // read(): use a null buffer
        let rv = unsafe { libc::read(fd_server, std::ptr::null_mut(), 0) };
        assert_eq!(rv, 0);

        // readv(): use a null iov array
        let rv = unsafe { libc::readv(fd_server, std::ptr::null(), 0) };
        assert_eq!(rv, 0);

        // readv: use a non-null iov array
        let iov = libc::iovec {
            iov_base: std::ptr::null_mut(),
            iov_len: 0,
        };
        let rv = unsafe { libc::readv(fd_server, &iov, 1) };
        assert_eq!(rv, 0);

        // PART 2: test with a non-empty receive buffer (some data available to read)

        // note: for non-stream sockets the recv will actually read and truncate a message from the
        // socket even though the given buffer has length 0, so there must be enough messages in the
        // socket's receive buffer for all of the tests below

        // recv(2) says the following which is relevant to this test, but it doesn't seem to be true
        // (see the "test_zero_len_msg_read_and_recv" test): "If a zero-length datagram is pending,
        // read(2) and recv() with a flags argument of zero provide different behavior. In this
        // circumstance, read(2) has no effect (the datagram remains pending), while recv() consumes
        // the pending datagram."

        // send some data to the server to partially fill its receive buffer (call send() multiple
        // times so that non-stream sockets won't run out of messages to read)
        for _ in 0..10 {
            // send 10 chunks of 10 bytes
            assert_eq!(
                10,
                nix::sys::socket::send(fd_client, &[1; 10], MsgFlags::empty()).unwrap()
            );
        }

        // shadow needs to run events
        std::thread::sleep(std::time::Duration::from_millis(10));

        // recv(): use a null buffer
        let rv = unsafe { libc::recv(fd_server, std::ptr::null_mut(), 0, 0) };
        assert_eq!(rv, 0);

        // recvmsg(): use a null iov array
        let mut msg = libc::msghdr {
            msg_name: std::ptr::null_mut(),
            msg_namelen: 0,
            msg_iov: std::ptr::null_mut(),
            msg_iovlen: 0,
            msg_control: std::ptr::null_mut(),
            msg_controllen: 0,
            msg_flags: 0,
        };
        let rv = unsafe { libc::recvmsg(fd_server, &mut msg, 0) };
        assert_eq!(rv, 0);

        // recvmsg(): use a non-null iov array
        let mut iov = libc::iovec {
            iov_base: std::ptr::null_mut(),
            iov_len: 0,
        };
        let mut msg = libc::msghdr {
            msg_name: std::ptr::null_mut(),
            msg_namelen: 0,
            msg_iov: &mut iov,
            msg_iovlen: 1,
            msg_control: std::ptr::null_mut(),
            msg_controllen: 0,
            msg_flags: 0,
        };
        let rv = unsafe { libc::recvmsg(fd_server, &mut msg, 0) };
        assert_eq!(rv, 0);

        // read(): use a null buffer
        let rv = unsafe { libc::read(fd_server, std::ptr::null_mut(), 0) };
        assert_eq!(rv, 0);

        // readv(): use a null iov array
        let rv = unsafe { libc::readv(fd_server, std::ptr::null(), 0) };
        assert_eq!(rv, 0);

        // readv: use a non-null iov array
        let iov = libc::iovec {
            iov_base: std::ptr::null_mut(),
            iov_len: 0,
        };
        let rv = unsafe { libc::readv(fd_server, &iov, 1) };
        assert_eq!(rv, 0);

        Ok(())
    })
}

/// Test recv() and read(), which may behave differently for zero-len messages.
fn test_zero_len_msg_read_and_recv(
    init_method: SocketInitMethod,
    sock_type: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_server) = socket_init_helper(
        init_method,
        sock_type,
        libc::SOCK_NONBLOCK,
        /* bind_client = */ false,
    );

    // The man page recv(2) says the following:
    //
    // > If a zero-length datagram is pending, read(2) and recv() with a flags argument of zero
    // > provide different behavior. In this circumstance, read(2) has no effect (the datagram remains
    // > pending), while recv() consumes the pending datagram.
    //
    // But this doesn't actually seem to be the case. In this test, we recv() and read() 0-length
    // messages, and read() does actually remove a 0-length message from the socket's receive
    // buffer. Unless I'm misunderstanding the man page, it seems like the documentation is wrong.
    //
    // There is a more-specific case where the documentation holds true, which is when read()ing or
    // recv()ing with a 0-length buffer. It seems that when read()ing from a socket with a 0-length
    // buffer no pending messages of any zero or non-zero size will be removed, whereas recv()ing
    // from a socket with a 0-length buffer does remove the pending message.

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // PART 1: Try recv()ing and read()ing with non-zero length buffers.

        // Send 5 messages of length 0, interleaved with 5 messages of length 1 containing the loop
        // iteration. The 1-byte message allows us to know how many 0-byte messages we've read.
        for x in 1..=3 {
            assert_eq!(
                0,
                nix::sys::socket::send(fd_client, &[], MsgFlags::empty()).unwrap()
            );
            assert_eq!(
                1,
                nix::sys::socket::send(fd_client, &[x], MsgFlags::empty()).unwrap()
            );
        }

        // shadow needs to run events
        std::thread::sleep(std::time::Duration::from_millis(10));

        // recv() message of length 0
        let mut buffer = [0u8; 1];
        let rv = unsafe { libc::recv(fd_server, buffer.as_mut_ptr() as *mut _, buffer.len(), 0) };
        assert_eq!(rv, 0);

        // recv() message of length 1 with contents "1"
        let mut buffer = [0u8; 1];
        let rv = unsafe { libc::recv(fd_server, buffer.as_mut_ptr() as *mut _, buffer.len(), 0) };
        assert_eq!(rv, 1);
        assert_eq!(buffer[0], 1);

        // read() message of length 0
        let mut buffer = [0u8; 1];
        let rv = unsafe { libc::read(fd_server, buffer.as_mut_ptr() as *mut _, buffer.len()) };
        assert_eq!(rv, 0);

        // read() message of length 1 with contents "2"
        let mut buffer = [0u8; 1];
        let rv = unsafe { libc::read(fd_server, buffer.as_mut_ptr() as *mut _, buffer.len()) };
        assert_eq!(rv, 1);
        assert_eq!(buffer[0], 2);

        // recv() message of length 0
        let mut buffer = [0u8; 1];
        let rv = unsafe { libc::recv(fd_server, buffer.as_mut_ptr() as *mut _, buffer.len(), 0) };
        assert_eq!(rv, 0);

        // recv() message of length 1 with contents "3"
        let mut buffer = [0u8; 1];
        let rv = unsafe { libc::recv(fd_server, buffer.as_mut_ptr() as *mut _, buffer.len(), 0) };
        assert_eq!(rv, 1);
        assert_eq!(buffer[0], 3);

        // PART 2: Try recv()ing and read()ing with zero length buffers. This is similar to the
        // "test_zero_len_buf_read_and_recv" test, but here we're testing dgram/seqpacket-specific
        // behaviour.

        for x in 1..=3 {
            assert_eq!(
                0,
                nix::sys::socket::send(fd_client, &[], MsgFlags::empty()).unwrap()
            );
            assert_eq!(
                1,
                nix::sys::socket::send(fd_client, &[x], MsgFlags::empty()).unwrap()
            );
        }

        // shadow needs to run events
        std::thread::sleep(std::time::Duration::from_millis(10));

        // recv() message of length 0
        let mut buffer = [0u8; 0];
        let rv = unsafe { libc::recv(fd_server, buffer.as_mut_ptr() as *mut _, buffer.len(), 0) };
        assert_eq!(rv, 0);

        // recv() message of length 1 with contents "1"
        let mut buffer = [0u8; 1];
        let rv = unsafe { libc::recv(fd_server, buffer.as_mut_ptr() as *mut _, buffer.len(), 0) };
        assert_eq!(rv, 1);
        assert_eq!(buffer[0], 1);

        // read() message of length 0 (but it doesn't actually remove the message)
        let mut buffer = [0u8; 0];
        let rv = unsafe { libc::read(fd_server, buffer.as_mut_ptr() as *mut _, buffer.len()) };
        assert_eq!(rv, 0);

        // readv() message of length 0 (but it doesn't actually remove the message)
        let rv = unsafe { libc::readv(fd_server, std::ptr::null(), 0) };
        assert_eq!(rv, 0);

        // readv() message of length 0 (but it doesn't actually remove the message)
        let mut buffer = [0u8; 0];
        let iov = libc::iovec {
            iov_base: buffer.as_mut_ptr() as *mut _,
            iov_len: buffer.len(),
        };
        let rv = unsafe { libc::readv(fd_server, &iov, 1) };
        assert_eq!(rv, 0);

        // recv() message of length 0
        let mut buffer = [0u8; 0];
        let rv = unsafe { libc::recv(fd_server, buffer.as_mut_ptr() as *mut _, buffer.len(), 0) };
        assert_eq!(rv, 0);

        // recv() message of length 1 with contents "2"
        let mut buffer = [0u8; 1];
        let rv = unsafe { libc::recv(fd_server, buffer.as_mut_ptr() as *mut _, buffer.len(), 0) };
        assert_eq!(rv, 1);
        assert_eq!(buffer[0], 2);

        // recvmsg() message of length 0
        let mut msg = libc::msghdr {
            msg_name: std::ptr::null_mut(),
            msg_namelen: 0,
            msg_iov: std::ptr::null_mut(),
            msg_iovlen: 0,
            msg_control: std::ptr::null_mut(),
            msg_controllen: 0,
            msg_flags: 0,
        };
        let rv = unsafe { libc::recvmsg(fd_server, &mut msg, 0) };
        assert_eq!(rv, 0);

        // recvmsg() message of length 1 with contents "3"
        let mut buffer = [0u8; 1];
        let mut iov = libc::iovec {
            iov_base: buffer.as_mut_ptr() as *mut _,
            iov_len: buffer.len(),
        };
        let mut msg = libc::msghdr {
            msg_name: std::ptr::null_mut(),
            msg_namelen: 0,
            msg_iov: &mut iov,
            msg_iovlen: 1,
            msg_control: std::ptr::null_mut(),
            msg_controllen: 0,
            msg_flags: 0,
        };
        let rv = unsafe { libc::recvmsg(fd_server, &mut msg, 0) };
        assert_eq!(rv, 1);
        assert_eq!(buffer[0], 3);

        Ok(())
    })
}

/// Test sendto() and recvfrom() using the `MSG_DONTWAIT` flag.
fn test_flag_dontwait(
    sys_method: SendRecvMethod,
    init_method: SocketInitMethod,
    sock_type: libc::c_int,
) -> Result<(), String> {
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
        check_recv_call(&mut recvfrom_args, sys_method, &[libc::EAGAIN], true)?;

        Ok(())
    })
}

/// Test sendto() and recvfrom() using the `MSG_PEEK` flag.
fn test_flag_peek(
    sys_method: SendRecvMethod,
    init_method: SocketInitMethod,
    sock_type: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_server) = socket_init_helper(
        init_method,
        sock_type,
        libc::SOCK_NONBLOCK,
        /* bind_client = */ false,
    );

    let outbuf_10_bytes: Vec<u8> = vec![1u8; 10];
    let mut inbuf_10_bytes: Vec<u8> = vec![0u8; 10];

    let sendto_args = SendtoArguments {
        fd: fd_client,
        len: outbuf_10_bytes.len(),
        buf: Some(&outbuf_10_bytes),
        flags: libc::MSG_PEEK,
        ..Default::default()
    };

    let mut recvfrom_args = RecvfromArguments {
        fd: fd_server,
        len: inbuf_10_bytes.len(),
        buf: Some(&mut inbuf_10_bytes),
        // set the flags below
        ..Default::default()
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // peek 10 bytes from the empty recv buffer
        recvfrom_args.flags |= libc::MSG_PEEK;
        check_recv_call(&mut recvfrom_args, sys_method, &[libc::EAGAIN], true)?;

        // write 10 bytes
        check_send_call(&sendto_args, sys_method, &[], true)?;

        // shadow needs to run events
        std::thread::sleep(std::time::Duration::from_millis(10));

        // peek 10 bytes (we can call it multiple times)
        recvfrom_args.flags |= libc::MSG_PEEK;
        check_recv_call(&mut recvfrom_args, sys_method, &[], true)?;
        check_recv_call(&mut recvfrom_args, sys_method, &[], true)?;
        check_recv_call(&mut recvfrom_args, sys_method, &[], true)?;
        check_recv_call(&mut recvfrom_args, sys_method, &[], true)?;

        // pop 5 bytes
        recvfrom_args.flags &= !libc::MSG_PEEK;
        recvfrom_args.len = 5;
        check_recv_call(&mut recvfrom_args, sys_method, &[], true)?;

        // peek 10 bytes (we can call it multiple times); will return EAGAIN for dgram sockets or 5
        // for stream sockets
        recvfrom_args.flags |= libc::MSG_PEEK;
        recvfrom_args.len = 10;
        let errnos = match sock_type {
            libc::SOCK_STREAM => vec![],
            libc::SOCK_DGRAM | libc::SOCK_SEQPACKET => vec![libc::EAGAIN],
            _ => unimplemented!(),
        };
        let (rv, _) = check_recv_call(&mut recvfrom_args, sys_method, &errnos, false)?;
        assert!(!errnos.is_empty() || rv == 5);
        let (rv, _) = check_recv_call(&mut recvfrom_args, sys_method, &errnos, false)?;
        assert!(!errnos.is_empty() || rv == 5);
        let (rv, _) = check_recv_call(&mut recvfrom_args, sys_method, &errnos, false)?;
        assert!(!errnos.is_empty() || rv == 5);

        // pop 5 bytes; will return EAGAIN for dgram sockets or 5 for stream sockets
        recvfrom_args.flags &= !libc::MSG_PEEK;
        recvfrom_args.len = 5;
        check_recv_call(&mut recvfrom_args, sys_method, &errnos, true)?;

        // peek 10 bytes from the empty recv buffer
        recvfrom_args.flags |= libc::MSG_PEEK;
        recvfrom_args.len = 10;
        check_recv_call(&mut recvfrom_args, sys_method, &[libc::EAGAIN], true)?;

        Ok(())
    })
}

/// Test sendto() and recvfrom() with a blocking socket.
fn test_blocking(
    sys_method: SendRecvMethod,
    init_method: SocketInitMethod,
    sock_type: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_server) =
        socket_init_helper(init_method, sock_type, 0, /* bind_client = */ false);

    let outbuf_10_bytes: Vec<u8> = vec![1u8; 10];
    let mut inbuf_10_bytes: Vec<u8> = vec![0u8; 10];

    let sendto_args = SendtoArguments {
        fd: fd_client,
        len: outbuf_10_bytes.len(),
        buf: Some(&outbuf_10_bytes),
        ..Default::default()
    };

    let mut recvfrom_args = RecvfromArguments {
        fd: fd_server,
        len: inbuf_10_bytes.len(),
        buf: Some(&mut inbuf_10_bytes),
        ..Default::default()
    };

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        std::thread::scope(|scope| {
            // wait 100 ms and then send
            let handle = scope.spawn(move || {
                std::thread::sleep(std::time::Duration::from_millis(100));
                check_send_call(&sendto_args, sys_method, &[], true)
            });

            // recv on the socket and make sure it didn't return immediately
            let time_start = std::time::Instant::now();
            check_recv_call(&mut recvfrom_args, sys_method, &[], true)?;
            assert!(time_start.elapsed() > std::time::Duration::from_millis(70));

            handle.join().unwrap()?;

            Ok(())
        })
    })
}

/// Test sendto() and recvfrom() using a non-blocking stream socket.
fn test_nonblocking_stream(
    sys_method: SendRecvMethod,
    init_method: SocketInitMethod,
) -> Result<(), String> {
    let (fd_client, fd_peer) = socket_init_helper(
        init_method,
        libc::SOCK_STREAM,
        libc::SOCK_NONBLOCK,
        /* bind_client = */ false,
    );

    test_utils::run_and_close_fds(&[fd_client, fd_peer], || {
        // try to read 10 bytes; an EAGAIN error expected
        assert!(!test_utils::is_readable(fd_peer, 0).unwrap());
        simple_recvfrom_helper(sys_method, fd_peer, &mut [0u8; 10], &[libc::EAGAIN], true)?;

        // send 10 bytes; no errors expected
        assert!(test_utils::is_writable(fd_client, 0).unwrap());
        simple_sendto_helper(sys_method, fd_client, &[1u8; 10], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read 10 bytes into a 20 byte buffer; no errors expected
        assert!(test_utils::is_readable(fd_peer, 0).unwrap());
        simple_recvfrom_helper(sys_method, fd_peer, &mut [0u8; 20], &[], false)?;

        // try to read 10 bytes; an EAGAIN error expected
        assert!(!test_utils::is_readable(fd_peer, 0).unwrap());
        simple_recvfrom_helper(sys_method, fd_peer, &mut [0u8; 10], &[libc::EAGAIN], false)?;

        let mut send_hash = std::hash::DefaultHasher::new();
        let mut recv_hash = std::hash::DefaultHasher::new();

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
    sys_method: SendRecvMethod,
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
        check_send_call(&sendto_args, sys_method, &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read 3 bytes; no errors expected
        check_recv_call(&mut recvfrom_args, sys_method, &[], true)?;

        Ok(())
    })
}

/// Test sendto() and recvfrom() using a socket that is not connected, while using a null address
/// argument.
fn test_null_addr_not_connected(
    sys_method: SendRecvMethod,
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
        check_send_call(&sendto_args_1, sys_method, &expected_err, true)?;

        // send 3 bytes using a non-null sockaddr; no error expected for dgram sockets
        let expected_err = match (init_method.domain(), sock_type) {
            (_, libc::SOCK_DGRAM) => vec![],
            (libc::AF_UNIX, libc::SOCK_STREAM) => vec![libc::EOPNOTSUPP],
            (libc::AF_UNIX, libc::SOCK_SEQPACKET) => vec![libc::ENOTCONN],
            _ => vec![libc::EPIPE],
        };
        check_send_call(&sendto_args_2, sys_method, &expected_err, true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read 3 bytes using a null sockaddr; no error expected for dgram sockets
        let expected_err = match (init_method.domain(), sock_type) {
            (_, libc::SOCK_DGRAM) => vec![],
            (libc::AF_UNIX, libc::SOCK_STREAM) => vec![libc::EINVAL],
            _ => vec![libc::ENOTCONN],
        };
        check_recv_call(&mut recvfrom_args, sys_method, &expected_err, true)?;

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
        simple_sendto_helper(SendRecvMethod::ToFrom, fd_client, &[1u8, 2, 3], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read 3 bytes using a non-null sockaddr, but a null sockaddr length
        // an EFAULT error expected
        check_recv_call(
            &mut recvfrom_args,
            SendRecvMethod::ToFrom,
            &[libc::EFAULT],
            true,
        )?;
        Ok(())
    })
}

/// Test recvfrom() using a null sockaddr and a null sockaddr length.
fn test_null_both(
    sys_method: SendRecvMethod,
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
        simple_sendto_helper(sys_method, fd_client, &[1u8, 2, 3], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read 3 bytes using a null sockaddr and a null sockaddr length
        // no error expected
        check_recv_call(&mut recvfrom_args, sys_method, &[], true)?;
        Ok(())
    })
}

/// Test sendto() using a connected socket, but also with a different non-null destination address.
fn test_nonnull_addr(
    sys_method: SendRecvMethod,
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

        check_send_call(&sendto_args, sys_method, &expected_err, true)?;
        Ok(())
    })
}

/// Test recvfrom() using a socket and verify that the returned sockaddr is correct.
fn test_recv_addr(
    sys_method: SendRecvMethod,
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
                std::ptr::from_mut(&mut client_addr) as *mut _,
                std::ptr::from_mut(&mut client_addr_len) as *mut _,
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
        simple_sendto_helper(sys_method, fd_client, &[1, 2, 3], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read 3 bytes at the server; no error expected
        check_recv_call(&mut recvfrom_args, sys_method, &[], true)?;

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

fn test_recv_flag_trunc(
    sys_method: SendRecvMethod,
    init_method: SocketInitMethod,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_server) =
        socket_init_helper(init_method, sock_type, flag, /* bind_client = */ false);

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // If the socket is non-blocking, make sure that recv with MSG_TRUNC on an empty socket
        // returns EAGAIN and not 0. Blocking sockets should block instead, but we don't test that.
        if (flag & libc::SOCK_NONBLOCK) != 0 {
            let mut buf = vec![0u8; 10];
            let mut args = RecvfromArguments {
                fd: fd_server,
                len: buf.len(),
                buf: Some(&mut buf),
                flags: libc::MSG_TRUNC,
                ..Default::default()
            };

            check_recv_call(&mut args, sys_method, &[libc::EAGAIN], false)?;
        }

        simple_sendto_helper(sys_method, fd_client, &vec![1u8; 500], &[], true)?;

        // shadow needs to run events
        std::thread::sleep(std::time::Duration::from_millis(10));

        let mut buf = vec![0u8; 200];
        let mut args = RecvfromArguments {
            fd: fd_server,
            len: buf.len(),
            buf: Some(&mut buf),
            flags: libc::MSG_TRUNC,
            ..Default::default()
        };

        let (rv, msg_flags) = check_recv_call(&mut args, sys_method, &[], false)?;

        if sock_type == libc::SOCK_STREAM {
            test_utils::result_assert_eq(rv, 200, "Expected to read the buffer size")?;
            if init_method.domain() == libc::AF_INET {
                test_utils::result_assert(
                    buf.iter().all(|&x| x == 0),
                    "Expected the buffer to be unchanged",
                )?;
            } else {
                // unix stream sockets don't support MSG_TRUNC, so the buffer should be changed
                test_utils::result_assert(
                    buf.iter().all(|&x| x == 1),
                    "Expected the buffer to be changed",
                )?;
            }

            if sys_method != SendRecvMethod::ToFrom {
                // MSG_TRUNC should not be set in msg_flags
                test_utils::result_assert(
                    libc::MSG_TRUNC & msg_flags.unwrap() == 0,
                    "MSG_TRUNC was unexpectedly set",
                )?;
            }
        } else {
            test_utils::result_assert_eq(rv, 500, "Expected to read the original msg size")?;
            test_utils::result_assert(
                buf.iter().all(|&x| x == 1),
                "Expected the buffer to be changed",
            )?;

            if sys_method != SendRecvMethod::ToFrom {
                // MSG_TRUNC should be set in msg_flags
                test_utils::result_assert(
                    libc::MSG_TRUNC & msg_flags.unwrap() != 0,
                    "MSG_TRUNC was not set",
                )?;
            }
        }

        Ok(())
    })
}

fn test_send_flag_trunc(
    sys_method: SendRecvMethod,
    init_method: SocketInitMethod,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_server) =
        socket_init_helper(init_method, sock_type, flag, /* bind_client = */ false);

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        let buf_send = vec![1u8; 200];
        let args = SendtoArguments {
            fd: fd_client,
            len: buf_send.len(),
            buf: Some(&buf_send),
            flags: libc::MSG_TRUNC,
            ..Default::default()
        };

        // we expect the MSG_TRUNC flag to be ignored
        check_send_call(&args, sys_method, &[], true)?;

        // shadow needs to run events
        std::thread::sleep(std::time::Duration::from_millis(10));

        let mut buf_recv = [0u8; 500];
        let rv = simple_recvfrom_helper(sys_method, fd_server, &mut buf_recv, &[], false)?;
        test_utils::result_assert_eq(rv, 200, "Expected to read the original msg size")?;
        test_utils::result_assert_eq(
            &buf_send[..],
            &buf_recv[..(rv as usize)],
            "Expected the buffers to be equal",
        )?;

        Ok(())
    })
}

/// Test sendto()/recvfrom() on a socket after its peer has been closed, with no buffered data.
fn test_after_peer_close_empty_buf(
    sys_method: SendRecvMethod,
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
        let rv = simple_recvfrom_helper(
            sys_method,
            fd_client,
            &mut [1u8, 2, 3],
            expected_errnos,
            false,
        )?;
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
        simple_sendto_helper(sys_method, fd_client, &[1u8, 2, 3], expected_errnos, true)?;

        Ok(())
    })
}

/// Test sendto()/recvfrom() on a socket after its peer has been closed, with some buffered data.
fn test_after_peer_close_nonempty_buf(
    sys_method: SendRecvMethod,
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
    simple_sendto_helper(sys_method, fd_client, &[1u8, 2], &[], true)?;
    simple_sendto_helper(sys_method, fd_peer, &[1u8, 2], &[], true)?;

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
        let rv = simple_recvfrom_helper(
            sys_method,
            fd_client,
            &mut [1u8, 2, 3],
            expected_errnos,
            false,
        )?;
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
        simple_sendto_helper(sys_method, fd_client, &[1u8, 2, 3], expected_errnos, true)?;

        Ok(())
    })
}

/// Test that recvfrom() on an inet dgram socket returns ECONNREFUSED if a previous sendto() failed.
fn test_recvfrom_econnrefused_after_sendto(
    sys_method: SendRecvMethod,
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
        simple_sendto_helper(sys_method, fd_client, &[1u8, 2, 3], expected_errnos, true)?;

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
        let rv = simple_recvfrom_helper(
            sys_method,
            fd_client,
            &mut [1u8, 2, 3],
            expected_errnos,
            false,
        )?;
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
    sys_method: SendRecvMethod,
    init_method: SocketInitMethod,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_server) =
        socket_init_helper(init_method, sock_type, flag, /* bind_client = */ false);

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // send 2000 bytes; no error expected
        simple_sendto_helper(sys_method, fd_client, &vec![1u8; 2000], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read 1999 bytes; last byte will be discarded and should not cause an error
        simple_recvfrom_helper(sys_method, fd_server, &mut vec![0u8; 1999], &[], true)?;

        Ok(())
    })
}

/// Test sendto() and recvfrom() using a dgram socket and make sure that all sent
/// messages are received as expected.
fn test_msg_order_dgram(
    sys_method: SendRecvMethod,
    init_method: SocketInitMethod,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let (fd_client, fd_server) =
        socket_init_helper(init_method, sock_type, flag, /* bind_client = */ false);

    // read and write messages and see if something unexpected happens
    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // send 1000 bytes; no error expected
        simple_sendto_helper(sys_method, fd_client, &vec![1u8; 1000], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read 500 bytes of the 1000 byte message; no error expected
        simple_recvfrom_helper(sys_method, fd_server, &mut [0u8; 500], &[], true)?;

        // send 200 bytes; no error expected
        simple_sendto_helper(sys_method, fd_client, &[1u8; 200], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read 200 bytes with a 500 byte buffer; no error expected
        let received_bytes =
            simple_recvfrom_helper(sys_method, fd_server, &mut vec![0u8; 500], &[], false)?;
        test_utils::result_assert_eq(received_bytes, 200, "Unexpected number of bytes read")?;

        // send 3 messages of different lengths; no error expected
        simple_sendto_helper(sys_method, fd_client, &[1u8; 1], &[], true)?;
        simple_sendto_helper(sys_method, fd_client, &[1u8; 3], &[], true)?;
        simple_sendto_helper(sys_method, fd_client, &[1u8; 5], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // read the 3 messages and make sure the lengths are correct; no error expected
        let received_bytes =
            simple_recvfrom_helper(sys_method, fd_server, &mut vec![0u8; 500], &[], false)?;
        test_utils::result_assert_eq(received_bytes, 1, "Unexpected number of bytes read")?;
        let received_bytes =
            simple_recvfrom_helper(sys_method, fd_server, &mut vec![0u8; 500], &[], false)?;
        test_utils::result_assert_eq(received_bytes, 3, "Unexpected number of bytes read")?;
        let received_bytes =
            simple_recvfrom_helper(sys_method, fd_server, &mut vec![0u8; 500], &[], false)?;
        test_utils::result_assert_eq(received_bytes, 5, "Unexpected number of bytes read")?;

        Ok(())
    })
}

/// Test sendto() and recvfrom() for sockets using large buffers (10^6 bytes).
fn test_large_buf(
    sys_method: SendRecvMethod,
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
        simple_sendto_helper(
            sys_method,
            fd_client,
            &[1u8; 1_000_000],
            expected_err,
            false,
        )?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // try to read 1_000_000 bytes, but may read fewer
        let expected_err = match (init_method.domain(), sock_type) {
            (_, libc::SOCK_DGRAM) => &[libc::EAGAIN][..],
            (_, libc::SOCK_SEQPACKET) => &[libc::EAGAIN][..],
            _ => &[],
        };
        simple_recvfrom_helper(
            sys_method,
            fd_peer,
            &mut [0u8; 1_000_000],
            expected_err,
            false,
        )?;

        Ok(())
    })
}

fn test_bound_to_inaddr_any(
    sys_method: SendRecvMethod,
    sock_type: libc::c_int,
) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(libc::AF_INET, sock_type, 0) };
    let mut fd_server = unsafe { libc::socket(libc::AF_INET, sock_type, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    /// Binds the socket to a `INADDR_ANY` address.
    fn inet_autobind_helper(fd: libc::c_int) -> libc::sockaddr_in {
        let mut server_addr = libc::sockaddr_in {
            sin_family: libc::AF_INET as u16,
            sin_port: 0u16.to_be(),
            sin_addr: libc::in_addr {
                s_addr: libc::INADDR_ANY.to_be(),
            },
            sin_zero: [0; 8],
        };

        // bind on the autobind address
        {
            let server_addr_len = std::mem::size_of_val(&server_addr) as libc::socklen_t;
            let server_addr = std::ptr::from_mut(&mut server_addr) as *mut libc::sockaddr;

            let rv = unsafe { libc::bind(fd, server_addr, server_addr_len) };
            assert_eq!(rv, 0);
        }

        // get the assigned address
        {
            let mut server_addr_len = std::mem::size_of_val(&server_addr) as libc::socklen_t;
            let server_addr = std::ptr::from_mut(&mut server_addr) as *mut libc::sockaddr;

            let rv = unsafe { libc::getsockname(fd, server_addr, &mut server_addr_len) };
            assert_eq!(rv, 0);
            assert_eq!(
                server_addr_len as usize,
                std::mem::size_of::<libc::sockaddr_in>()
            );
        }

        server_addr
    }

    // bind the server socket to some unused address
    let server_addr = inet_autobind_helper(fd_server);
    let client_addr = inet_autobind_helper(fd_client);

    if sock_type == libc::SOCK_DGRAM {
        dgram_connect_helper(
            fd_client,
            SockAddr::Inet(server_addr),
            std::mem::size_of_val(&server_addr) as libc::socklen_t,
        );
        dgram_connect_helper(
            fd_server,
            SockAddr::Inet(client_addr),
            std::mem::size_of_val(&client_addr) as libc::socklen_t,
        );
    } else if sock_type == libc::SOCK_STREAM {
        // connect the client to the server and get the accepted socket
        let fd_peer = stream_connect_helper(
            fd_client,
            fd_server,
            SockAddr::Inet(server_addr),
            std::mem::size_of_val(&server_addr) as libc::socklen_t,
            0,
        );

        // close the server
        assert_eq!(0, unsafe { libc::close(fd_server) });

        fd_server = fd_peer;
    }

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // send from the client
        simple_sendto_helper(sys_method, fd_client, &[1u8, 2, 3], &[], true)?;

        // send from the server
        simple_sendto_helper(sys_method, fd_server, &[1u8, 2, 3], &[], true)?;

        // shadow needs to run events
        std::thread::sleep(std::time::Duration::from_millis(10));

        // recv at the server
        let received_bytes =
            simple_recvfrom_helper(sys_method, fd_server, &mut [0u8; 5], &[], false)?;
        test_utils::result_assert_eq(received_bytes, 3, "Unexpected number of bytes read")?;

        // recv at the client
        let received_bytes =
            simple_recvfrom_helper(sys_method, fd_client, &mut [0u8; 5], &[], false)?;
        test_utils::result_assert_eq(received_bytes, 3, "Unexpected number of bytes read")?;

        Ok(())
    })
}

/// Test sendto() and recvfrom() for UDP sockets just above the message limit of 65507 bytes.
fn test_large_buf_udp(sys_method: SendRecvMethod) -> Result<(), String> {
    let (fd_client, fd_server) = socket_init_helper(
        SocketInitMethod::Inet,
        libc::SOCK_DGRAM,
        libc::SOCK_NONBLOCK,
        /* bind_client = */ false,
    );

    test_utils::run_and_close_fds(&[fd_client, fd_server], || {
        // try sending a buffer slightly too large; an EMSGSIZE error is expected
        simple_sendto_helper(
            sys_method,
            fd_client,
            &vec![1u8; 65_508],
            &[libc::EMSGSIZE],
            true,
        )?;

        // try sending a buffer at the max size; no error expected
        simple_sendto_helper(sys_method, fd_client, &vec![1u8; 65_507], &[], true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10000) }, 0);

        // try receiving using a large buffer; no error expected
        let received_bytes =
            simple_recvfrom_helper(sys_method, fd_server, &mut vec![0u8; 65_508], &[], false)?;

        test_utils::result_assert_eq(received_bytes, 65_507, "Unexpected number of bytes read")
    })
}

/// Test connecting a dgram socket to a bound socket, closing the bound socket, creating a new
/// socket and binding it to that same bind address, and then writing to the connected socket.
fn test_send_after_dgram_peer_close(
    sys_method: SendRecvMethod,
    domain: libc::c_int,
) -> Result<(), String> {
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

        simple_sendto_helper(sys_method, fd_client, &[1u8; 100], expected_err, true)?;

        // shadow needs to run events
        assert_eq!(unsafe { libc::usleep(10_000) }, 0);

        let expected_err = match domain {
            // since the unix socket send was unsuccessful, the recv will be as well
            libc::AF_UNIX => &[libc::EWOULDBLOCK][..],
            // non-unix sockets will successfully read the message on the new peer
            _ => &[],
        };

        simple_recvfrom_helper(sys_method, fd_new_peer, &mut [0u8; 100], expected_err, true)?;

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
    sys_method: SendRecvMethod,
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

    check_send_call(&sendto_args, sys_method, sendto_errnos, true)?;

    // shadow needs to run events
    assert_eq!(unsafe { libc::usleep(10000) }, 0);

    check_recv_call(&mut recvfrom_args, sys_method, recvfrom_errnos, true)?;

    Ok(())
}

/// Call `sendto()` with a provided fd and buffer and check that the result is as
/// expected. An empty flag mask and a null sockaddr are used.
/// Returns an `Error` if the errno doesn't match an errno in `errnos`, or if
/// `verify_num_bytes` is `true` and the number of bytes sent (the return value) does
/// not match the buffer length.
fn simple_sendto_helper(
    sys_method: SendRecvMethod,
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

    check_send_call(&args, sys_method, errnos, verify_num_bytes)
}

/// Call `recvfrom()` with a provided fd and buffer and check that the result is as
/// expected. An empty flag mask and a null sockaddr are used.
/// Returns an `Error` if the errno doesn't match an errno in `errnos`, or if
/// `verify_num_bytes` is `true` and the number of bytes received (the return value) does
/// not match the buffer length.
fn simple_recvfrom_helper(
    sys_method: SendRecvMethod,
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

    Ok(check_recv_call(&mut args, sys_method, errnos, verify_num_bytes)?.0)
}

fn check_send_call(
    args: &SendtoArguments,
    sys_method: SendRecvMethod,
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
        assert!(args.len <= args.buf.unwrap().len());
    }
    if args.addr.is_some() {
        assert!(args.addr_len <= addr_max_len);
    }

    let buf_ptr = match args.buf {
        Some(slice) => slice.as_ptr(),
        None => std::ptr::null(),
    };

    let rv = match sys_method {
        SendRecvMethod::ToFrom => test_utils::check_system_call!(
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
        )?,
        SendRecvMethod::Msg => {
            let mut iov = libc::iovec {
                // casting a const pointer to a mut pointer, but syscall should not mutate data
                iov_base: buf_ptr as *mut core::ffi::c_void,
                iov_len: args.len,
            };
            let msg = libc::msghdr {
                // casting a const pointer to a mut pointer, but syscall should not mutate data
                msg_name: addr_ptr as *mut _,
                msg_namelen: args.addr_len,
                msg_iov: &mut iov,
                msg_iovlen: 1,
                msg_control: std::ptr::null_mut(),
                msg_controllen: 0,
                msg_flags: 0,
            };
            test_utils::check_system_call!(
                || unsafe { libc::sendmsg(args.fd, &msg, args.flags) },
                expected_errnos,
            )?
        }
        SendRecvMethod::Mmsg => {
            todo!();
        }
    };

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

/// Returns the return value of the recv, and the `msg_flags` if recvmsg or recvmmsg were used.
fn check_recv_call(
    args: &mut RecvfromArguments,
    sys_method: SendRecvMethod,
    expected_errnos: &[libc::c_int],
    verify_num_bytes: bool,
) -> Result<(libc::ssize_t, Option<libc::c_int>), String> {
    let (addr_ptr, addr_max_len) = match args.addr {
        Some(ref mut x) => (x.as_mut_ptr(), x.ptr_size()),
        None => (std::ptr::null_mut(), 0),
    };

    // if the pointers will be non-null, make sure the length is not greater than the actual data size
    // so that we don't segfault
    if args.buf.is_some() {
        assert!(args.len <= args.buf.as_ref().unwrap().len());
    }
    if args.addr.is_some() && args.addr_len.is_some() {
        assert!(args.addr_len.unwrap() <= addr_max_len);
    }

    let buf_ptr = match &mut args.buf {
        Some(slice) => slice.as_mut_ptr(),
        None => std::ptr::null_mut(),
    };

    let (rv, msg_flags) = match sys_method {
        SendRecvMethod::ToFrom => {
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
            (rv, None)
        }
        SendRecvMethod::Msg => {
            let mut iov = libc::iovec {
                iov_base: buf_ptr as *mut core::ffi::c_void,
                iov_len: args.len,
            };
            let mut msg = libc::msghdr {
                msg_name: addr_ptr as *mut libc::c_void,
                msg_namelen: args.addr_len.unwrap_or(0),
                msg_iov: &mut iov,
                msg_iovlen: 1,
                msg_control: std::ptr::null_mut(),
                msg_controllen: 0,
                msg_flags: 0,
            };
            let rv = test_utils::check_system_call!(
                || unsafe { libc::recvmsg(args.fd, &mut msg, args.flags) },
                expected_errnos,
            )?;
            if let Some(ref mut addr_len) = args.addr_len {
                *addr_len = msg.msg_namelen;
            }
            (rv, Some(msg.msg_flags))
        }
        SendRecvMethod::Mmsg => {
            todo!();
        }
    };

    // only check that all bytes were received (recv buffer was filled) if there
    // were no expected errors
    if verify_num_bytes && expected_errnos.is_empty() {
        // check that recvfrom() returned the number of bytes requested
        test_utils::result_assert_eq(rv, args.len as isize, "Not all bytes were received")?;
    }

    Ok((rv, msg_flags))
}
