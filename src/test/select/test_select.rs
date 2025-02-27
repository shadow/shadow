/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */
#![allow(clippy::too_many_arguments)]

use std::mem;
use std::time::Duration;

use test_utils::TestEnvironment as TestEnv;
use test_utils::set;

#[derive(Debug, Copy, Clone, PartialEq, Eq)]
enum SelectFn {
    Select,
    PSelect,
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
    // Create a set of pipefds
    let (pfd_read, pfd_write) = nix::unistd::pipe().map_err(|e| e.to_string())?;

    test_utils::run_and_close_fds(&[pfd_read, pfd_write], || {
        // select will check when pipe has info to read
        let mut readfds = unsafe {
            let mut raw_fd_set = mem::MaybeUninit::<libc::fd_set>::uninit();
            libc::FD_ZERO(raw_fd_set.as_mut_ptr());
            libc::FD_SET(pfd_read, raw_fd_set.as_mut_ptr());
            raw_fd_set.assume_init()
        };

        // First make sure there's nothing there
        let mut ready = unsafe {
            libc::select(
                pfd_read + 1,
                std::ptr::from_mut(&mut readfds),
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                &mut libc::timeval {
                    tv_sec: 0,
                    tv_usec: 1000,
                },
            )
        };
        let mut fd_is_readable = unsafe { libc::FD_ISSET(pfd_read, &readfds) };

        if ready < 0 {
            return Err("error: select failed".to_string());
        } else if ready > 0 && fd_is_readable {
            return Err(format!(
                "error: select unexpectedly marked fd {} readable. result={}",
                pfd_read, ready
            ));
        }

        // Now put information in pipe to be read
        fd_write(pfd_write)?;

        // Check again, should be something to read
        readfds = unsafe {
            let mut raw_fd_set = mem::MaybeUninit::<libc::fd_set>::uninit();
            libc::FD_ZERO(raw_fd_set.as_mut_ptr());
            libc::FD_SET(pfd_read, raw_fd_set.as_mut_ptr());
            raw_fd_set.assume_init()
        };

        ready = unsafe {
            libc::select(
                pfd_read + 1,
                std::ptr::from_mut(&mut readfds),
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                &mut libc::timeval {
                    tv_sec: 0,
                    tv_usec: 1000,
                },
            )
        };

        // Make sure we got a read event
        if ready != 1 {
            return Err(format!("error: select returned {} instead of 1", ready));
        }

        // Make sure the event is set for the correct fd
        fd_is_readable = unsafe { libc::FD_ISSET(pfd_read, &readfds) };

        if !fd_is_readable {
            return Err(format!(
                "error: select did not mark fd {} as readable",
                pfd_read
            ));
        }

        // Make sure we got what expected back
        fd_read_cmp(pfd_read)
    })
}

fn test_regular_file() -> Result<(), String> {
    let (fd, path) = nix::unistd::mkstemp(&b"testselect_XXXXXX"[..]).map_err(|e| e.to_string())?;

    test_utils::run_and_close_fds(&[fd], || {
        // select will check when file has info to read
        let mut readfds = unsafe {
            let mut raw_fd_set = mem::MaybeUninit::<libc::fd_set>::uninit();
            libc::FD_ZERO(raw_fd_set.as_mut_ptr());
            libc::FD_SET(fd, raw_fd_set.as_mut_ptr());
            raw_fd_set.assume_init()
        };

        // Check if the fd is readable
        let ready = unsafe {
            libc::select(
                fd + 1,
                std::ptr::from_mut(&mut readfds),
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                &mut libc::timeval {
                    tv_sec: 0,
                    tv_usec: 1000,
                },
            )
        };
        let fd_is_readable = unsafe { libc::FD_ISSET(fd, &readfds) };

        if ready < 0 {
            return Err("error: select on empty file failed".to_string());
        } else if ready == 0 || !fd_is_readable {
            // Note: Even though the file is 0 bytes, has no data inside of it, it is still instantly
            // available for 'reading' the EOF.
            return Err(format!(
                "error: expected EOF to be readable from empty file fd={}",
                fd
            ));
        }

        // write to file
        fd_write(fd)
    })?;

    // Check again, should be something to read
    let fd = nix::fcntl::open(
        &path,
        nix::fcntl::OFlag::O_RDONLY,
        nix::sys::stat::Mode::empty(),
    )
    .map_err(|e| e.to_string())?;

    nix::unistd::unlink(&path).map_err(|e| e.to_string())?;

    test_utils::run_and_close_fds(&[fd], || {
        // select will check when file has info to read
        let mut readfds = unsafe {
            let mut raw_fd_set = mem::MaybeUninit::<libc::fd_set>::uninit();
            libc::FD_ZERO(raw_fd_set.as_mut_ptr());
            libc::FD_SET(fd, raw_fd_set.as_mut_ptr());
            raw_fd_set.assume_init()
        };

        // Check if the fd is readable
        let ready = unsafe {
            libc::select(
                fd + 1,
                std::ptr::from_mut(&mut readfds),
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                &mut libc::timeval {
                    tv_sec: 0,
                    tv_usec: 1000,
                },
            )
        };
        let fd_is_readable = unsafe { libc::FD_ISSET(fd, &readfds) };

        if ready != 1 {
            return Err(format!("error: select returned {} instead of 1", ready));
        }

        if !fd_is_readable {
            return Err(format!("error: fd {} is not readable", fd));
        }

        // Make sure we got what expected back
        fd_read_cmp(fd)
    })
}

fn get_selectable_fd() -> Result<libc::c_int, String> {
    // Get an fd we can select
    let fd = test_utils::check_system_call!(
        || { unsafe { libc::socket(libc::AF_INET, libc::SOCK_DGRAM, 0) } },
        &[]
    )?;
    test_utils::result_assert(fd >= 0, "fd from socket() is not set")?;
    Ok(fd)
}

fn test_select_args_common(
    select_fn: SelectFn,
    readfds_null: bool,
    writefds_null: bool,
    exceptfds_null: bool,
    fd_is_set: bool,
    fd_inval: bool,
    timeout: Duration,
    nfds: libc::c_int,
    exp_result: libc::c_int,
    exp_error: libc::c_int,
) -> Result<(), String> {
    let fd = get_selectable_fd()?;

    test_utils::run_and_close_fds(&[fd], || {
        // The main structs for the select syscall
        let mut readfds = unsafe {
            let mut raw_fd_set = mem::MaybeUninit::<libc::fd_set>::uninit();
            libc::FD_ZERO(raw_fd_set.as_mut_ptr());
            raw_fd_set.assume_init()
        };
        let mut writefds = unsafe {
            let mut raw_fd_set = mem::MaybeUninit::<libc::fd_set>::uninit();
            libc::FD_ZERO(raw_fd_set.as_mut_ptr());
            raw_fd_set.assume_init()
        };
        let mut exceptfds = unsafe {
            let mut raw_fd_set = mem::MaybeUninit::<libc::fd_set>::uninit();
            libc::FD_ZERO(raw_fd_set.as_mut_ptr());
            raw_fd_set.assume_init()
        };

        // An invalid fd is a closed fd less than the max open fd.
        let fd_invalid = get_selectable_fd()?;
        let fd_max = get_selectable_fd()?;

        if fd_is_set {
            unsafe {
                libc::FD_SET(fd, &mut readfds);
                libc::FD_SET(fd, &mut writefds);
                libc::FD_SET(fd, &mut exceptfds);
            }
            if fd_inval {
                unsafe {
                    libc::FD_SET(fd_invalid, &mut readfds);
                    libc::FD_SET(fd_invalid, &mut writefds);
                    libc::FD_SET(fd_invalid, &mut exceptfds);
                }
            }
        }

        unsafe {
            libc::close(fd_invalid);
        }

        // Get mutable pointer references so we can pass them as a non-const libc pointer
        let readfds_ptr = if readfds_null {
            std::ptr::null_mut()
        } else {
            std::ptr::from_mut(&mut readfds)
        };
        let writefds_ptr = if writefds_null {
            std::ptr::null_mut()
        } else {
            std::ptr::from_mut(&mut writefds)
        };
        let exceptfds_ptr = if exceptfds_null {
            std::ptr::null_mut()
        } else {
            std::ptr::from_mut(&mut exceptfds)
        };

        // Our expected errno
        let exp_error_vec = if exp_error != 0 {
            vec![exp_error]
        } else {
            vec![]
        };

        let instant_before = std::time::Instant::now();

        // Run the select or pselect system call while checking the errno
        let result = match select_fn {
            SelectFn::Select => {
                // Setup the timeval
                let mut timeout_tv = libc::timeval {
                    tv_sec: timeout.as_secs().try_into().unwrap(),
                    tv_usec: timeout.subsec_micros().into(),
                };

                test_utils::check_system_call!(
                    || {
                        unsafe {
                            libc::select(
                                nfds,
                                readfds_ptr,
                                writefds_ptr,
                                exceptfds_ptr,
                                &mut timeout_tv,
                            )
                        }
                    },
                    &exp_error_vec
                )?
            }

            SelectFn::PSelect => {
                // Setup the timespec (mutable not needed because it's a const libc pointer)
                let timeout_ts = libc::timespec {
                    tv_sec: timeout.as_secs().try_into().unwrap(),
                    tv_nsec: timeout.subsec_nanos().into(),
                };

                test_utils::check_system_call!(
                    || {
                        unsafe {
                            libc::pselect(
                                nfds,
                                readfds_ptr,
                                writefds_ptr,
                                exceptfds_ptr,
                                &timeout_ts,
                                std::ptr::null(),
                            )
                        }
                    },
                    &exp_error_vec
                )?
            }
        };

        let result_string = format!(
            "{:?} returned an unexpected result: expected {}, got {}",
            select_fn, exp_result, result
        );
        test_utils::result_assert_eq(exp_result, result, &result_string)?;

        if result == 0 && exp_error == 0 {
            // No bits set and no error. If we provided a timeout, then time should have
            // advanced by at least that much.
            let elapsed = instant_before.elapsed();
            test_utils::result_assert(
                elapsed >= timeout,
                &format!(
                    "No events with timeout of {:?}, but only {:?} elapsed",
                    timeout, elapsed
                ),
            )?;
        }

        unsafe {
            libc::close(fd_max);
        }

        Ok(())
    })
}

fn get_select_args_test(
    select_fn: SelectFn,
    readfds_null: bool,
    writefds_null: bool,
    exceptfds_null: bool,
    fd_is_set: bool,
    fd_inval: bool,
    timeout: Duration,
    nfds: libc::c_int,
    exp_result: libc::c_int,
    exp_error: libc::c_int,
) -> test_utils::ShadowTest<(), String> {
    let test_name = format!(
        "test_select_args\n\t<fn={:?},readfds_null={},writefds_null={},exceptfds_null={},fd_is_set={},fd_inval={},timeout={:?},nfds={}>\n\t-> <exp_result={},exp_errno={}>",
        select_fn,
        readfds_null,
        writefds_null,
        exceptfds_null,
        fd_is_set,
        fd_inval,
        timeout,
        nfds,
        exp_result,
        exp_error
    );
    test_utils::ShadowTest::new(
        &test_name,
        move || {
            test_select_args_common(
                select_fn,
                readfds_null,
                writefds_null,
                exceptfds_null,
                fd_is_set,
                fd_inval,
                timeout,
                nfds,
                exp_result,
                exp_error,
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

    // For each combination of args, test both select and pselect
    for &select_fn in [SelectFn::Select, SelectFn::PSelect].iter() {
        for &readfds_null in [true, false].iter() {
            for &writefds_null in [true, false].iter() {
                for &exceptfds_null in [true, false].iter() {
                    for &fd_is_set in [true, false].iter() {
                        for &fd_inval in [true, false].iter() {
                            for &timeout in
                                [Duration::from_millis(0), Duration::from_millis(1)].iter()
                            {
                                for &nfds in [-1, 3, 100, 1023, 1024, 1025, i32::MAX].iter() {
                                    // For the expected outcomes
                                    let mut exp_result = 0;
                                    let mut exp_error = 0;

                                    // Encodes the linux failure logic
                                    if nfds < 0 {
                                        exp_result = -1;
                                        exp_error = libc::EINVAL;
                                    } else if fd_inval
                                        && fd_is_set
                                        && nfds > 3
                                        && (!readfds_null || !writefds_null || !exceptfds_null)
                                    {
                                        exp_result = -1;
                                        exp_error = libc::EBADF;
                                    } else if nfds > 4 && !writefds_null && fd_is_set {
                                        // Our fd is only writeable (because we don't receive data on it)
                                        exp_result = 1;
                                    }

                                    // Add the test case
                                    tests.push(get_select_args_test(
                                        select_fn,
                                        readfds_null,
                                        writefds_null,
                                        exceptfds_null,
                                        fd_is_set,
                                        fd_inval,
                                        timeout,
                                        nfds,
                                        exp_result,
                                        exp_error,
                                    ));
                                }
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
