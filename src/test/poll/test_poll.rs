/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */
#![allow(clippy::too_many_arguments)]

use std::cmp::Ordering;
use std::time::Duration;

use nix::sys::signal;
use nix::sys::signal::Signal;
use test_utils::TestEnvironment as TestEnv;
use test_utils::set;

#[derive(Debug, Copy, Clone, PartialEq, Eq)]
enum PollFn {
    Poll,
    PPoll,
}

const TEST_STR: &[u8; 4] = b"test";

fn fd_write(fd: i32) -> Result<(), String> {
    let num = nix::unistd::write(fd, TEST_STR).map_err(|e| e.to_string())?;
    if num != TEST_STR.len() {
        return Err(format!(
            "wrote {} bytes, but expected to write {}",
            num,
            TEST_STR.len()
        ));
    }
    Ok(())
}

fn fd_read_cmp(fd: i32) -> Result<(), String> {
    let mut buf = [0_u8; 4];
    let num = nix::unistd::read(fd, &mut buf).map_err(|e| e.to_string())?;
    if num != TEST_STR.len() || &buf != TEST_STR {
        return Err(format!(
            "read bytes {:?} instead of {:?}",
            &buf[..num],
            TEST_STR
        ));
    }
    Ok(())
}

fn test_pipe() -> Result<(), String> {
    /* Create a set of pipefds */
    let (pfd_read, pfd_write) = nix::unistd::pipe().map_err(|e| e.to_string())?;

    test_utils::run_and_close_fds(&[pfd_read, pfd_write], || {
        /* poll will check when pipe has info to read */
        let mut read_poll = libc::pollfd {
            fd: pfd_read,
            events: libc::POLLIN,
            revents: 0,
        };

        /* First make sure there's nothing there */
        let mut ready = unsafe { libc::poll(std::ptr::from_mut(&mut read_poll), 1, 100) };
        match ready.cmp(&0) {
            Ordering::Less => {
                return Err("error: poll failed".to_string());
            }
            Ordering::Greater => {
                return Err(format!(
                    "error: pipe marked readable. revents={}",
                    read_poll.revents
                ));
            }
            _ => (),
        }

        /* Now put information in pipe to be read */
        fd_write(pfd_write)?;

        /* Check again, should be something to read */
        read_poll.fd = pfd_read;
        read_poll.events = libc::POLLIN;
        read_poll.revents = 0;
        ready = unsafe { libc::poll(std::ptr::from_mut(&mut read_poll), 1, 100) };
        if ready != 1 {
            return Err(format!("error: poll returned {ready} instead of 1"));
        }

        if read_poll.revents & libc::POLLIN == 0 {
            return Err(format!(
                "error: read_poll has wrong revents: {}",
                read_poll.revents
            ));
        }

        /* Make sure we got what expected back */
        fd_read_cmp(pfd_read)
    })
}

fn test_regular_file() -> Result<(), String> {
    let (fd, path) = nix::unistd::mkstemp(&b"testpoll_XXXXXX"[..]).map_err(|e| e.to_string())?;

    test_utils::run_and_close_fds(&[fd], || {
        /* poll will check when testpoll has info to read */
        let mut read_poll = libc::pollfd {
            fd,
            events: libc::POLLIN,
            revents: 0,
        };
        let ready = unsafe { libc::poll(std::ptr::from_mut(&mut read_poll), 1, 100) };
        match ready.cmp(&0) {
            Ordering::Less => {
                return Err("error: poll on empty file failed".to_string());
            }
            Ordering::Equal => {
                /* Note: Even though the file is 0 bytes, has no data inside of it, it is still instantly
                 * available for 'reading' the EOF. */
                return Err(format!(
                    "error: expected EOF to be readable from empty file. revents={}",
                    read_poll.revents
                ));
            }
            _ => (),
        }

        /* write to file */
        fd_write(fd)
    })?;

    /* Check again, should be something to read */
    let fd = nix::fcntl::open(
        &path,
        nix::fcntl::OFlag::O_RDONLY,
        nix::sys::stat::Mode::empty(),
    )
    .map_err(|e| e.to_string())?;

    nix::unistd::unlink(&path).map_err(|e| e.to_string())?;

    test_utils::run_and_close_fds(&[fd], || {
        /* poll will check when testpoll has info to read */
        let mut read_poll = libc::pollfd {
            fd,
            events: libc::POLLIN,
            revents: 0,
        };
        let ready = unsafe { libc::poll(std::ptr::from_mut(&mut read_poll), 1, 100) };
        if ready != 1 {
            return Err(format!("error: poll returned {ready} instead of 1"));
        }

        if read_poll.revents & libc::POLLIN == 0 {
            return Err(format!(
                "error: read_poll has wrong revents: {}",
                read_poll.revents
            ));
        }

        /* Make sure we got what expected back */
        fd_read_cmp(fd)
    })
}

fn get_pollable_fd() -> Result<libc::c_int, String> {
    // Get an fd we can poll
    let fd = test_utils::check_system_call!(
        || { unsafe { libc::socket(libc::AF_INET, libc::SOCK_DGRAM, 0) } },
        &[]
    )?;
    test_utils::result_assert(fd > 0, "fd from socket() is not set")?;
    Ok(fd)
}

fn test_poll_args_common(
    poll_fn: PollFn,
    pfd_null: bool,
    fd_inval: bool,
    events: i16,
    signal_time: Option<Duration>,
    timeout: Duration,
    nfds: u64,
    exp_result: libc::c_int,
    exp_error: libc::c_int,
    exp_revents: i16,
) -> Result<(), String> {
    let fd = get_pollable_fd()?;

    test_utils::run_and_close_fds(&[fd], || {
        // The main struct for the poll syscall
        let mut pfd = libc::pollfd {
            fd,
            events,
            revents: 0,
        };
        if fd_inval {
            pfd.fd = i32::MAX;
        }

        // Get mutable pointer reference so we can pass it as a non-const libc pointer
        let pfd_ptr = if pfd_null {
            std::ptr::null_mut()
        } else {
            std::ptr::from_mut(&mut pfd)
        };

        // Our expected errno
        let exp_error_vec = if exp_error != 0 {
            vec![exp_error]
        } else {
            vec![]
        };

        let instant_before = std::time::Instant::now();

        unsafe {
            signal::sigaction(
                Signal::SIGALRM,
                &signal::SigAction::new(
                    test_utils::nop_sig_handler(),
                    signal::SaFlags::empty(),
                    signal::SigSet::empty(),
                ),
            )
        }
        .unwrap();
        let interruptor =
            signal_time.map(|t| test_utils::Interruptor::new(t, signal::Signal::SIGALRM));

        // Run the poll or ppoll system call while checking the errno
        let ready = match poll_fn {
            PollFn::Poll => test_utils::check_system_call!(
                || {
                    unsafe { libc::poll(pfd_ptr, nfds, timeout.as_millis().try_into().unwrap()) }
                },
                &exp_error_vec
            )?,

            PollFn::PPoll => {
                // Setup the timespec (mutable not needed because it's a const libc pointer)
                let timeout_ts = libc::timespec {
                    tv_sec: timeout.as_secs().try_into().unwrap(),
                    tv_nsec: timeout.subsec_nanos().into(),
                };

                test_utils::check_system_call!(
                    || { unsafe { libc::ppoll(pfd_ptr, nfds, &timeout_ts, std::ptr::null()) } },
                    &exp_error_vec
                )?
            }
        };

        // Cancel the interruptor, in case it hasn't already fired.
        drop(interruptor);

        let ready_string = format!(
            "{poll_fn:?} returned an unexpected result: expected {exp_result}, got {ready}",
        );
        test_utils::result_assert_eq(exp_result, ready, &ready_string)?;

        if ready > 0 {
            let revents_string = format!(
                "{:?} returned unexpected revents: expected {}, got {}",
                poll_fn, exp_revents, pfd.revents
            );
            test_utils::result_assert_eq(exp_revents, pfd.revents, &revents_string)?;
        } else if exp_error == 0 {
            // No events ready, and no error. If we provided a timeout, then time should have
            // advanced by at least that much.
            let elapsed = instant_before.elapsed();
            test_utils::result_assert(
                elapsed >= timeout,
                &format!("No events with timeout of {timeout:?}, but only {elapsed:?} elapsed"),
            )?;
        }

        Ok(())
    })
}

fn get_poll_args_test(
    poll_fn: PollFn,
    pfd_null: bool,
    fd_inval: bool,
    events: i16,
    signal_time: Option<Duration>,
    timeout: Duration,
    nfds: u64,
    exp_result: libc::c_int,
    exp_error: libc::c_int,
    exp_revents: i16,
) -> test_utils::ShadowTest<(), String> {
    let test_name = format!(
        "test_poll_args\n\t<fn={poll_fn:?},pfd_null={pfd_null},fd_inval={fd_inval},\
        events={events},signal_time={signal_time:?},timeout={timeout:?},nfds={nfds}>\n\t-> \
        <exp_result={exp_result},exp_errno={exp_error},exp_revents={exp_revents}>",
    );
    test_utils::ShadowTest::new(
        &test_name,
        move || {
            test_poll_args_common(
                poll_fn,
                pfd_null,
                fd_inval,
                events,
                signal_time,
                timeout,
                nfds,
                exp_result,
                exp_error,
                exp_revents,
            )
        },
        set![TestEnv::Libc, TestEnv::Shadow],
    )
}

fn main() -> Result<(), String> {
    // should we restrict the tests we run?
    let filter_shadow_passing = std::env::args().any(|x| x == "--shadow-passing");
    let filter_libc_passing = std::env::args().any(|x| x == "--libc-passing");
    // should we summarize the results rather than exit on a failed test
    let summarize = std::env::args().any(|x| x == "--summarize");

    let mut tests: Vec<test_utils::ShadowTest<_, _>> = vec![
        test_utils::ShadowTest::new("test_pipe", test_pipe, set![TestEnv::Libc, TestEnv::Shadow]),
        test_utils::ShadowTest::new(
            "test_regular_file",
            test_regular_file,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
    ];

    // For each combination of args, test both poll and ppoll
    for &poll_fn in [PollFn::Poll, PollFn::PPoll].iter() {
        for &pfd_null in [true, false].iter() {
            for &fd_inval in [true, false].iter() {
                for &events in [0, libc::POLLIN, libc::POLLOUT].iter() {
                    for &timeout in [Duration::from_millis(0), Duration::from_millis(100)].iter() {
                        for &signal_time in [None, Some(Duration::from_millis(10))].iter() {
                            for &nfds in [0, 1, u64::MAX].iter() {
                                // For the expected outcomes
                                let mut exp_result = 0;
                                let mut exp_error = 0;
                                let mut exp_revents = 0;

                                // Encodes the linux failure logic
                                if pfd_null && nfds == 1 {
                                    exp_result = -1;
                                    exp_error = libc::EFAULT;
                                } else if nfds == u64::MAX {
                                    exp_result = -1;
                                    exp_error = libc::EINVAL;
                                } else if fd_inval && nfds == 1 {
                                    exp_result = 1;
                                    exp_revents = libc::POLLNVAL;
                                } else if events == libc::POLLOUT && nfds == 1 {
                                    exp_result = 1;
                                    exp_revents = libc::POLLOUT;
                                } else if signal_time.is_some() && timeout != Duration::ZERO {
                                    assert!(signal_time.unwrap() < timeout);
                                    exp_result = -1;
                                    exp_error = libc::EINTR;
                                }

                                // Add the test case
                                tests.push(get_poll_args_test(
                                    poll_fn,
                                    pfd_null,
                                    fd_inval,
                                    events,
                                    signal_time,
                                    timeout,
                                    nfds,
                                    exp_result,
                                    exp_error,
                                    exp_revents,
                                ));
                            }
                        }
                    }
                }
            }
        }
    }

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
