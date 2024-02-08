use std::collections::HashSet;
use std::time::Duration;

use test_utils::time::*;
use test_utils::{ensure_ord, set, FuzzArg, FuzzError, FuzzOrder, TestEnvironment};

// For most clocks, Linux only checks TIMER_ABSTIME and ignores other bits that are set in the flags
// arg (see kernel/time/posix-timers.c). But for the *_ALARM clocks, Linux returns EINVAL if you set
// undocumented bits in flags. Linux also returns EPERM for these if the caller does not have
// CAP_WAKE_ALARM, which we do not emulate in Shadow. (Verified experimentally.)
const SPECIAL_ALARM_CLOCKIDS: [libc::c_int; 2] =
    [libc::CLOCK_REALTIME_ALARM, libc::CLOCK_BOOTTIME_ALARM];

fn main() -> anyhow::Result<()> {
    // should we restrict the tests we run?
    let filter_shadow_passing = std::env::args().any(|x| x == "--shadow-passing");
    let filter_libc_passing = std::env::args().any(|x| x == "--libc-passing");
    // should we summarize the results rather than exit on a failed test
    let summarize = std::env::args().any(|x| x == "--summarize");

    let mut tests = get_tests();

    if filter_shadow_passing {
        tests.retain(|x| x.passing(TestEnvironment::Shadow));
    }
    if filter_libc_passing {
        tests.retain(|x| x.passing(TestEnvironment::Libc));
    }

    test_utils::run_tests(&tests, summarize)?;

    Ok(())
}

fn get_tests() -> Vec<test_utils::ShadowTest<(), anyhow::Error>> {
    let mut tests: Vec<test_utils::ShadowTest<(), anyhow::Error>> = vec![];

    // Encodes how Linux checks for invalid args, which we found experimentally.
    let clockids = vec![
        FuzzArg::new(libc::CLOCK_REALTIME, Ok(())),
        FuzzArg::new(libc::CLOCK_TAI, Ok(())),
        FuzzArg::new(libc::CLOCK_MONOTONIC, Ok(())),
        FuzzArg::new(libc::CLOCK_BOOTTIME, Ok(())),
        FuzzArg::new(libc::CLOCK_REALTIME_ALARM, Ok(())),
        FuzzArg::new(libc::CLOCK_BOOTTIME_ALARM, Ok(())),
        FuzzArg::new(libc::CLOCK_PROCESS_CPUTIME_ID, Ok(())),
        FuzzArg::new(
            libc::CLOCK_THREAD_CPUTIME_ID,
            Err(FuzzError::new(FuzzOrder::First, Some(libc::EINVAL), None)),
        ),
        FuzzArg::new(
            libc::CLOCK_MONOTONIC_RAW,
            Err(FuzzError::new(FuzzOrder::First, Some(libc::ENOTSUP), None)),
        ),
        FuzzArg::new(
            libc::CLOCK_REALTIME_COARSE,
            Err(FuzzError::new(FuzzOrder::First, Some(libc::ENOTSUP), None)),
        ),
        FuzzArg::new(
            libc::CLOCK_MONOTONIC_COARSE,
            Err(FuzzError::new(FuzzOrder::First, Some(libc::ENOTSUP), None)),
        ),
    ];

    let flags = vec![
        FuzzArg::new(0, Ok(())),
        FuzzArg::new(libc::TIMER_ABSTIME, Ok(())),
    ];

    let requests: Vec<FuzzArg<Option<libc::timespec>>> = vec![
        FuzzArg::new(
            Some(libc::timespec {
                tv_sec: 0,
                tv_nsec: 0,
            }),
            Ok(()),
        ),
        FuzzArg::new(
            Some(libc::timespec {
                tv_sec: -1,
                tv_nsec: 0,
            }),
            Err(FuzzError::new(FuzzOrder::Second, Some(libc::EINVAL), None)),
        ),
        FuzzArg::new(
            Some(libc::timespec {
                tv_sec: 0,
                tv_nsec: -1,
            }),
            Err(FuzzError::new(FuzzOrder::Second, Some(libc::EINVAL), None)),
        ),
        FuzzArg::new(
            Some(libc::timespec {
                tv_sec: 0,
                tv_nsec: 1_000_000_000,
            }),
            Err(FuzzError::new(FuzzOrder::Second, Some(libc::EINVAL), None)),
        ),
        FuzzArg::new(
            None,
            Err(FuzzError::new(FuzzOrder::Second, Some(libc::EFAULT), None)),
        ),
    ];

    let remains: Vec<FuzzArg<Option<libc::timespec>>> = vec![
        FuzzArg::new(
            Some(libc::timespec {
                tv_sec: 0,
                tv_nsec: 0,
            }),
            Ok(()),
        ),
        FuzzArg::new(None, Ok(())),
    ];

    // Test all combinations of valid/invalid args.
    for &clockid in clockids.iter() {
        for &flag in get_flags(clockid.value, &flags).iter() {
            for &request in requests.iter() {
                for &remain in remains.iter() {
                    let append_args = |s| {
                        format!(
                            "{} <clockid={:?},flags={:?},request={:?},remain={:?}>",
                            s, clockid.value, flag.value, request.value, remain.value
                        )
                    };

                    // The test should succeed if there are no invalid args.
                    let should_succeed = test_utils::filter_discard_valid(&[
                        clockid.expected_result,
                        flag.expected_result,
                        request.expected_result,
                        remain.expected_result,
                    ])
                    .is_empty();

                    tests.extend(vec![test_utils::ShadowTest::new(
                        &append_args("return_values"),
                        move || test_return_values(clockid, flag, request, remain),
                        get_passing_test_envs(clockid.value, should_succeed),
                    )]);
                }
            }
        }
    }

    // Test only valid syscall behavior.
    for &clockid in clockids.iter().filter(|c| c.expected_result.is_ok()) {
        for &flag in flags.iter().filter(|f| f.expected_result.is_ok()) {
            let append_args =
                |s| format!("{} <clockid={:?},flags={:?}", s, clockid.value, flag.value);

            tests.extend(vec![
                test_utils::ShadowTest::new(
                    &append_args("sleep_duration"),
                    move || test_sleep_duration(clockid.value, flag.value),
                    get_passing_test_envs(clockid.value, true),
                ),
                test_utils::ShadowTest::new(
                    &append_args("interrupted_sleep"),
                    move || test_interrupted_sleep(clockid.value, flag.value),
                    get_passing_test_envs(clockid.value, true),
                ),
            ]);

            if flag.value == libc::TIMER_ABSTIME {
                tests.extend(vec![test_utils::ShadowTest::new(
                    &append_args("abstime_in_past"),
                    move || test_abstime_in_past(clockid.value),
                    get_passing_test_envs(clockid.value, true),
                )]);
            }
        }
    }

    tests
}

fn get_flags(
    clockid: libc::clockid_t,
    flags: &[FuzzArg<libc::c_int>],
) -> Vec<FuzzArg<libc::c_int>> {
    // See doc for `SPECIAL_ALARM_CLOCKIDS`
    let flag_with_unspec_bits = libc::TIMER_ABSTIME | 0x1111;

    let new_arg = if SPECIAL_ALARM_CLOCKIDS.contains(&clockid) {
        FuzzArg::new(
            flag_with_unspec_bits,
            Err(FuzzError::new(FuzzOrder::Third, Some(libc::EINVAL), None)),
        )
    } else {
        FuzzArg::new(flag_with_unspec_bits, Ok(()))
    };

    let mut new_flags = flags.to_owned();
    new_flags.push(new_arg);
    new_flags
}

fn get_passing_test_envs(
    clockid: libc::clockid_t,
    should_test_succeed: bool,
) -> HashSet<TestEnvironment> {
    // Skip testing CLOCK_PROCESS_CPUTIME_ID: it advances too slowly and causes timeouts in Linux.
    let mut skip_linux = vec![libc::CLOCK_PROCESS_CPUTIME_ID];

    // Even if all of the args are valid, the *_ALARM variants will still cause EPERM on Linux (see
    // doc for `SPECIAL_ALARM_CLOCKIDS`). We check the error cases, but filter the EPERM case.
    if should_test_succeed {
        skip_linux.extend(SPECIAL_ALARM_CLOCKIDS);
    }

    // Shadow does not yet implement CLOCK_PROCESS_CPUTIME_ID.
    let skip_shadow = [libc::CLOCK_PROCESS_CPUTIME_ID];

    let mut test_envs = set![];
    if !skip_linux.contains(&clockid) {
        test_envs.insert(TestEnvironment::Libc);
    }
    if !skip_shadow.contains(&clockid) {
        test_envs.insert(TestEnvironment::Shadow);
    }
    test_envs
}

fn test_return_values(
    clockid: FuzzArg<libc::clockid_t>,
    flags: FuzzArg<libc::c_int>,
    request: FuzzArg<Option<libc::timespec>>,
    mut remain: FuzzArg<Option<libc::timespec>>,
) -> anyhow::Result<()> {
    // Notably, errors are returned as positive return value, not in errno.
    let (rv, errno) = {
        let request_ptr = request
            .value
            .as_ref()
            .map_or(std::ptr::null(), std::ptr::from_ref);
        let remain_ptr = remain
            .value
            .as_mut()
            .map_or(std::ptr::null_mut(), std::ptr::from_mut);

        unsafe {
            (
                libc::clock_nanosleep(clockid.value, flags.value, request_ptr, remain_ptr),
                *libc::__errno_location(),
            )
        }
    };

    // Args may be valid or invalid.
    let results = vec![
        clockid.expected_result,
        flags.expected_result,
        request.expected_result,
        remain.expected_result,
    ];

    // `clock_nanosleep` returns 0 on success.
    test_utils::verify_syscall_result(results, 0, rv, errno)?;

    Ok(())
}

fn test_sleep_duration(clockid: libc::clockid_t, flags: libc::c_int) -> anyhow::Result<()> {
    let relative_dur = Duration::from_millis(1100);
    let request = create_sleep_request(clockid, flags, relative_dur)?;
    test_utils::check_fn_exec_duration(relative_dur, SLEEP_TOLERANCE, || {
        let rv = unsafe { libc::clock_nanosleep(clockid, flags, &request, std::ptr::null_mut()) };
        ensure_ord!(0, ==, rv);
        Ok(())
    })
}

/// Calling clock_nanosleep with flag TIMER_ABSTIME where the sleep request specifies a time in the
/// past results in a zero sleep duration (immediate return).
fn test_abstime_in_past(clockid: libc::clockid_t) -> anyhow::Result<()> {
    // Get an absolute sleep time slightly in the past.
    let past = clock_now_duration(clockid)?.saturating_sub(Duration::from_nanos(1_000_000_001));
    let request = duration_to_timespec(past);

    // clock_nanosleep should return immediately, but allow some tolerance for syscall execution.
    test_utils::check_fn_exec_duration(Duration::ZERO, SYSCALL_EXEC_TOLERANCE, || {
        let rv = unsafe {
            libc::clock_nanosleep(clockid, libc::TIMER_ABSTIME, &request, std::ptr::null_mut())
        };
        ensure_ord!(0, ==, rv);
        Ok(())
    })
}

/// A clock_nanosleep interrupted by a signal handler should return EINTR.
fn test_interrupted_sleep(clockid: libc::clockid_t, flags: libc::c_int) -> anyhow::Result<()> {
    // The signaler sleeps and then interrupts a sleeping sleeper.
    let intr_dur = Duration::from_millis(300);
    let sleep_dur = Duration::from_millis(900);

    test_utils::interrupt_fn_exec(intr_dur, || {
        let request = create_sleep_request(clockid, flags, sleep_dur)?;
        let mut remain = libc::timespec {
            tv_sec: 0,
            tv_nsec: 0,
        };

        // Should be interrupted after the interrupt signal duration.
        test_utils::check_fn_exec_duration(intr_dur, SLEEP_TOLERANCE, || {
            let rv = unsafe { libc::clock_nanosleep(clockid, flags, &request, &mut remain) };
            ensure_ord!(libc::EINTR, ==, rv);
            Ok(())
        })?;

        if flags != libc::TIMER_ABSTIME {
            // Check reported time remaining.
            let remain_expected = sleep_dur.checked_sub(intr_dur).unwrap();
            let remain_actual = timespec_to_duration(remain);
            let remain_diff = duration_abs_diff(remain_expected, remain_actual);
            ensure_ord!(remain_diff, <=, SLEEP_TOLERANCE);
        }

        Ok(())
    })
}

fn create_sleep_request(
    clockid: libc::clockid_t,
    flags: libc::c_int,
    relative_dur: Duration,
) -> anyhow::Result<libc::timespec> {
    let absolute_offset = match flags {
        libc::TIMER_ABSTIME => clock_now_duration(clockid)?,
        _ => Duration::ZERO,
    };
    let request_dur = absolute_offset.checked_add(relative_dur).unwrap();
    Ok(duration_to_timespec(request_dur))
}
