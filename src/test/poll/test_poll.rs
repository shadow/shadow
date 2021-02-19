/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use test_utils::set;
use test_utils::TestEnvironment as TestEnv;

#[derive(Debug, Copy, Clone, PartialEq, Eq)]
enum PollFn {
    Poll,
    PPoll,
}

const TEST_STR: &[u8; 4] = b"test";

fn fd_write(fd: i32) -> Result<usize, String> {
    nix::unistd::write(fd, TEST_STR).map_err(|e| e.to_string())
}

fn fd_read_cmp(fd: i32) -> Result<(), String> {
    let mut buf = [0_u8; 4];
    nix::unistd::read(fd, &mut buf).map_err(|e| e.to_string())?;
    if &buf != TEST_STR {
        return Err(format!(
            "error: read bytes: {:?} instead of {:?} from pipe.",
            buf, TEST_STR
        ));
    } else {
        return Ok(());
    }
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
        let mut ready = unsafe { libc::poll(&mut read_poll as *mut libc::pollfd, 1, 100) };
        if ready < 0 {
            return Err("error: poll failed".to_string());
        } else if ready > 0 {
            return Err(format!(
                "error: pipe marked readable. revents={}",
                read_poll.revents
            ));
        }

        /* Now put information in pipe to be read */
        fd_write(pfd_write)?;

        /* Check again, should be something to read */
        read_poll.fd = pfd_read;
        read_poll.events = libc::POLLIN;
        read_poll.revents = 0;
        ready = unsafe { libc::poll(&mut read_poll as *mut libc::pollfd, 1, 100) };
        if ready != 1 {
            return Err(format!("error: poll returned {} instead of 1", ready));
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

fn test_creat() -> Result<(), String> {
    let test_file = b"testpoll.txt";
    let test_file = std::ffi::CString::new(*test_file).unwrap();
    let fd = unsafe {
        libc::creat(
            test_file.as_bytes_with_nul() as *const _ as *const libc::c_char,
            0o644,
        )
    };

    nix::errno::Errno::result(fd).map_err(|e| e.to_string())?;

    test_utils::run_and_close_fds(&[fd], || {
        /* poll will check when testpoll has info to read */
        let mut read_poll = libc::pollfd {
            fd: fd,
            events: libc::POLLIN,
            revents: 0,
        };
        let ready = unsafe { libc::poll(&mut read_poll as *mut libc::pollfd, 1, 100) };
        if ready < 0 {
            return Err("error: poll on empty file failed".to_string());
        } else if ready == 0 {
            /* Note: Even though the file is 0 bytes, has no data inside of it, it is still instantly
             * available for 'reading' the EOF. */
            return Err(format!(
                "error: expected EOF to be readable from empty file. revents={}",
                read_poll.revents
            ));
        }

        /* write to file */
        fd_write(fd)
    })?;

    /* Check again, should be something to read */
    let fd = nix::fcntl::open(
        test_file.as_ref(),
        nix::fcntl::OFlag::O_RDONLY,
        nix::sys::stat::Mode::empty(),
    )
    .map_err(|e| e.to_string())?;

    test_utils::run_and_close_fds(&[fd], || {
        /* poll will check when testpoll has info to read */
        let mut read_poll = libc::pollfd {
            fd: fd,
            events: libc::POLLIN,
            revents: 0,
        };
        let ready = unsafe { libc::poll(&mut read_poll as *mut libc::pollfd, 1, 100) };
        if ready != 1 {
            return Err(format!("error: poll returned {} instead of 1", ready));
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
    timeout: i32,
    nfds: u64,
    exp_result: libc::c_int,
    exp_error: libc::c_int,
    exp_revents: i16,
) -> Result<(), String> {
    let fd = get_pollable_fd().unwrap();

    test_utils::run_and_close_fds(&[fd], || {
        // The main struct for the poll syscall
        let mut pfd = libc::pollfd {
            fd: fd,
            events: events,
            revents: 0,
        };
        if fd_inval {
            pfd.fd = std::i32::MAX;
        }

        // Get mutable pointer reference so we can pass it as a non-const libc pointer
        let pfd_ptr = if pfd_null {
            std::ptr::null_mut()
        } else {
            &mut pfd as *mut _
        };

        // Our expected errno
        let exp_error_vec = if exp_error != 0 {
            vec![exp_error]
        } else {
            vec![]
        };

        // Run the poll or ppoll system call while checking the errno
        let ready = match poll_fn {
            PollFn::Poll => test_utils::check_system_call!(
                || { unsafe { libc::poll(pfd_ptr, nfds, timeout) } },
                &exp_error_vec
            )?,

            PollFn::PPoll => {
                // Setup the timespec (mutable not needed because it's a const libc pointer)
                let timeout_ts = libc::timespec {
                    tv_sec: 0,
                    tv_nsec: timeout as i64 * 1000000, // millis to nanos
                };

                test_utils::check_system_call!(
                    || { unsafe { libc::ppoll(pfd_ptr, nfds, &timeout_ts, std::ptr::null()) } },
                    &exp_error_vec
                )?
            }
        };

        let ready_string = format!(
            "{:?} returned an unexpected result: expected {}, got {}",
            poll_fn, exp_result, ready
        );
        test_utils::result_assert_eq(exp_result, ready, &ready_string)?;

        if ready > 0 {
            let revents_string = format!(
                "{:?} returned unexpected revents: expected {}, got {}",
                poll_fn, exp_revents, pfd.revents
            );
            test_utils::result_assert_eq(exp_revents, pfd.revents, &revents_string)?;
        }

        Ok(())
    })
}

fn get_poll_args_test(
    poll_fn: PollFn,
    pfd_null: bool,
    fd_inval: bool,
    events: i16,
    timeout: i32,
    nfds: u64,
    exp_result: libc::c_int,
    exp_error: libc::c_int,
    exp_revents: i16,
) -> test_utils::ShadowTest<(), String> {
    let test_name = format!(
        "test_poll_args\n\t<fn={:?},pfd_null={},fd_inval={},events={},timeout={},nfds={}>\n\t-> <exp_result={},exp_errno={},exp_revents={}>",
        poll_fn, pfd_null, fd_inval, events, timeout, nfds, exp_result, exp_error, exp_revents
    );
    test_utils::ShadowTest::new(
        &test_name,
        move || {
            test_poll_args_common(
                poll_fn,
                pfd_null,
                fd_inval,
                events,
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
            "test_creat",
            test_creat,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
    ];

    // For each combination of args, test both poll and ppoll
    for &poll_fn in [PollFn::Poll, PollFn::PPoll].iter() {
        for &pfd_null in [true, false].iter() {
            for &fd_inval in [true, false].iter() {
                for &events in [0, libc::POLLIN, libc::POLLOUT].iter() {
                    for &timeout in [0, 1].iter() {
                        for &nfds in [0, 1, std::u64::MAX].iter() {
                            // For the expected outcomes
                            let mut exp_result = 0;
                            let mut exp_error = 0;
                            let mut exp_revents = 0;

                            // Encodes the linux failure logic
                            if pfd_null && nfds == 1 {
                                exp_result = -1;
                                exp_error = libc::EFAULT;
                            } else if nfds == std::u64::MAX {
                                exp_result = -1;
                                exp_error = libc::EINVAL;
                            } else if fd_inval && nfds == 1 {
                                exp_result = 1;
                                exp_revents = libc::POLLNVAL;
                            } else if events == libc::POLLOUT && nfds == 1 {
                                exp_result = 1;
                                exp_revents = libc::POLLOUT;
                            }

                            // Add the test case
                            tests.push(get_poll_args_test(
                                poll_fn,
                                pfd_null,
                                fd_inval,
                                events,
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
