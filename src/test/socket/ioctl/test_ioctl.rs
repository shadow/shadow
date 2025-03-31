/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use std::time::Duration;

use test_utils::TestEnvironment as TestEnv;
use test_utils::set;
use test_utils::socket_utils::{SocketInitMethod, socket_init_helper};

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

    let domains = [libc::AF_INET];
    let sock_types = [libc::SOCK_STREAM, libc::SOCK_DGRAM];

    for &domain in domains.iter() {
        for &sock_type in sock_types.iter() {
            // add details to the test names to avoid duplicates
            let append_args = |s| format!("{} <domain={},sock_type={}>", s, domain, sock_type);

            tests.extend(vec![test_utils::ShadowTest::new(
                &append_args("test_tty"),
                move || test_tty(domain, sock_type),
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
        let sock_types = match init_method.domain() {
            libc::AF_INET => &[libc::SOCK_STREAM, libc::SOCK_DGRAM][..],
            libc::AF_UNIX => &[libc::SOCK_STREAM, libc::SOCK_DGRAM, libc::SOCK_SEQPACKET][..],
            _ => unimplemented!(),
        };

        for &sock_type in sock_types.iter() {
            // add details to the test names to avoid duplicates
            let append_args =
                |s| format!("{s} <init_method={init_method:?}, sock_type={sock_type}>");

            tests.extend(vec![
                test_utils::ShadowTest::new(
                    &append_args("test_fionread"),
                    move || test_fionread(init_method, sock_type),
                    // TODO: this isn't supported yet in shadow for unix sockets
                    if init_method.domain() == libc::AF_UNIX {
                        set![TestEnv::Libc]
                    } else {
                        set![TestEnv::Libc, TestEnv::Shadow]
                    },
                ),
                test_utils::ShadowTest::new(
                    &append_args("test_siocgstamp"),
                    move || test_siocgstamp(init_method, sock_type),
                    // TODO: this isn't supported yet in shadow for unix sockets
                    if init_method.domain() == libc::AF_UNIX {
                        set![TestEnv::Libc]
                    } else {
                        set![TestEnv::Libc, TestEnv::Shadow]
                    },
                ),
            ]);
        }
    }

    tests
}

/// Test ioctl() using the tty-related ioctl requests.
fn test_tty(domain: libc::c_int, sock_type: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type, 0) };
    assert!(fd >= 0);

    let test_tty_request = |request| {
        // test incorrect fds
        test_utils::check_system_call!(
            || unsafe { libc::ioctl(-1, request, &mut std::mem::zeroed::<libc::termios>()) },
            &[libc::EBADF],
        )?;
        test_utils::check_system_call!(
            || unsafe { libc::ioctl(8934, request, &mut std::mem::zeroed::<libc::termios>()) },
            &[libc::EBADF],
        )?;

        // test a valid fd
        test_utils::check_system_call!(
            || unsafe { libc::ioctl(fd, request, &mut std::mem::zeroed::<libc::termios>()) },
            &[libc::ENOTTY],
        )
    };

    test_utils::run_and_close_fds(&[fd], || {
        test_tty_request(libc::TCGETS)?;
        test_tty_request(libc::TCSETS)?;
        test_tty_request(libc::TCSETSW)?;
        test_tty_request(libc::TCSETSF)?;
        test_tty_request(libc::TCGETA)?;
        test_tty_request(libc::TCSETA)?;
        test_tty_request(libc::TCSETAW)?;
        test_tty_request(libc::TCSETAF)?;
        test_tty_request(libc::TIOCGWINSZ)?;
        test_tty_request(libc::TIOCSWINSZ)?;

        // in glibc, isatty() calls tcgetattr() which makes the ioctl call
        let rv = unsafe { libc::isatty(fd) };
        let errno = test_utils::get_errno();
        test_utils::result_assert_eq(rv, 0, "Unexpected return value from isatty()")?;
        test_utils::result_assert_eq(errno, libc::ENOTTY, "Unexpected errno from isatty()")?;

        Ok(())
    })
}

/// Test ioctl() using the `FIONREAD` ioctl request.
fn test_fionread(init_method: SocketInitMethod, sock_type: libc::c_int) -> Result<(), String> {
    let (fd_client, fd_peer) =
        socket_init_helper(init_method, sock_type, 0, /* bind_client = */ false);

    /// Returns the value if successful, otherwise returns the errno.
    fn ioctl_fionread(fd: libc::c_int) -> Result<libc::c_int, libc::c_int> {
        let mut out: libc::c_int = 0;
        let rv = unsafe { libc::ioctl(fd, libc::FIONREAD, &mut out) };
        if rv != 0 {
            return Err(test_utils::get_errno());
        }
        Ok(out)
    }

    test_utils::run_and_close_fds(&[fd_client, fd_peer], || {
        // socket should have no data in recv buffer
        test_utils::result_assert_eq(
            ioctl_fionread(fd_client).map_err(|e| format!("Failed ioctl with errno {e}"))?,
            0,
            "Unexpected FIONREAD result",
        )?;
        test_utils::result_assert_eq(
            ioctl_fionread(fd_peer).map_err(|e| format!("Failed ioctl with errno {e}"))?,
            0,
            "Unexpected FIONREAD result",
        )?;

        // send 9 bytes to the peer, split among multiple send() calls
        let flags = nix::sys::socket::MsgFlags::empty();
        nix::sys::socket::send(fd_client, &[1, 2, 3], flags).unwrap();
        nix::sys::socket::send(fd_client, &[4, 5], flags).unwrap();
        nix::sys::socket::send(fd_client, &[6, 7, 8, 9], flags).unwrap();

        // shadow needs to run events
        std::thread::sleep(std::time::Duration::from_millis(10));

        // stream/seqpacket sockets return the number of bytes in recv buffer, dgram sockets return
        // number of bytes in next message in recv buffer
        let peer_expected_result = match sock_type {
            libc::SOCK_STREAM | libc::SOCK_SEQPACKET => 9,
            libc::SOCK_DGRAM => 3,
            _ => unimplemented!(),
        };
        test_utils::result_assert_eq(
            ioctl_fionread(fd_client).map_err(|e| format!("Failed ioctl with errno {e}"))?,
            0,
            "Unexpected FIONREAD result",
        )?;
        test_utils::result_assert_eq(
            ioctl_fionread(fd_peer).map_err(|e| format!("Failed ioctl with errno {e}"))?,
            peer_expected_result,
            "Unexpected FIONREAD result",
        )?;

        Ok(())
    })
}

/// Test ioctl() using the `SIOCGSTAMP` ioctl request.
fn test_siocgstamp(init_method: SocketInitMethod, sock_type: libc::c_int) -> Result<(), String> {
    let (fd_client, fd_peer) = socket_init_helper(
        init_method,
        sock_type,
        libc::SOCK_NONBLOCK,
        /* bind_client = */ false,
    );

    /// Returns the value if successful, otherwise returns the errno.
    fn ioctl_siocgstamp(fd: libc::c_int) -> Result<libc::timeval, libc::c_int> {
        // not currently available in the libc crate
        use linux_api::ioctls::IoctlRequest::SIOCGSTAMP;

        let mut out: libc::timeval = unsafe { std::mem::zeroed() };
        let rv = unsafe { libc::ioctl(fd, SIOCGSTAMP as u64, &mut out) };
        if rv != 0 {
            return Err(test_utils::get_errno());
        }
        Ok(out)
    }

    test_utils::run_and_close_fds(&[fd_client, fd_peer], || {
        // neither socket has received any data, so should return ENOENT for inet sockets
        let expected_result = match (init_method.domain(), sock_type) {
            (libc::AF_INET, _) => Err(libc::ENOENT),
            (libc::AF_UNIX, _) => Err(libc::ENOTTY),
            _ => unimplemented!(),
        };
        test_utils::result_assert_eq(
            ioctl_siocgstamp(fd_client),
            expected_result,
            "Unexpected SIOCGSTAMP result",
        )?;
        test_utils::result_assert_eq(
            ioctl_siocgstamp(fd_peer),
            expected_result,
            "Unexpected SIOCGSTAMP result",
        )?;

        // send data from the client to the peer
        let flags = nix::sys::socket::MsgFlags::empty();
        nix::sys::socket::send(fd_client, &[1, 2, 3], flags).unwrap();

        // approximately the time that we sent the message
        let send_time = std::time::SystemTime::now()
            .duration_since(std::time::SystemTime::UNIX_EPOCH)
            .unwrap();

        // shadow needs to run events, but we also sleep so that we make the recv() call much later
        // than the send() call
        std::thread::sleep(Duration::from_millis(50));

        // use use a small threshold when comparing the send time to the recv time below since the
        // message is only travelling over localhost; should be much shorter than the sleep above
        let threshold = Duration::from_millis(1);

        // recv() has not been called on the socket, so should still return ENOENT for inet sockets
        let expected_result = match (init_method.domain(), sock_type) {
            (libc::AF_INET, _) => Err(libc::ENOENT),
            (libc::AF_UNIX, _) => Err(libc::ENOTTY),
            _ => unimplemented!(),
        };
        test_utils::result_assert_eq(
            ioctl_siocgstamp(fd_peer),
            expected_result,
            "Unexpected SIOCGSTAMP result",
        )?;

        // receive data at the peer
        let flags = nix::sys::socket::MsgFlags::empty();
        nix::sys::socket::recv(fd_peer, &mut [0u8; 3], flags).unwrap();

        // check the result of SIOCGSTAMP on the peer; only supported by udp sockets
        let expected_err = match (init_method.domain(), sock_type) {
            (libc::AF_INET, libc::SOCK_DGRAM) => None,
            (libc::AF_INET, _) => Some(libc::ENOENT),
            (libc::AF_UNIX, _) => Some(libc::ENOTTY),
            _ => unimplemented!(),
        };
        match expected_err {
            None => {
                // the receive time reported by the kernel
                let recv_time = ioctl_siocgstamp(fd_peer).unwrap();
                let recv_time = Duration::from_secs(recv_time.tv_sec.try_into().unwrap())
                    + Duration::from_micros(recv_time.tv_usec.try_into().unwrap());

                // Get the time difference between the send and receive. We can't know if the
                // receive or send time will be smaller since the send time was measured after the
                // send() call completed, and the message may have already been received before we
                // take the send-time measurement. For example:
                //
                // 1. `send()` call
                //   a. since it's localhost, the packet is given directly to the receiving socket
                //      within the `send()` call
                //   b. receive time is recorded by the kernel here
                // 2. shadow records the send time with:
                //    let send_time = std::time::SystemTime::now()
                //
                // In this case the send time is later than the receive time.
                let difference = test_utils::time::duration_abs_diff(send_time, recv_time);

                // since it was sent over localhost, the difference between the send time and
                // receive time should be very small
                test_utils::result_assert_lt(
                    difference,
                    threshold,
                    "Time difference was too large",
                )?;
            }
            Some(e) => test_utils::result_assert_eq(
                ioctl_siocgstamp(fd_peer),
                Err(e),
                "Unexpected SIOCGSTAMP result",
            )?,
        }

        Ok(())
    })
}
