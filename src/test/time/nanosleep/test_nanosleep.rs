use std::time::Duration;

use test_utils::time::*;
use test_utils::{ensure_ord, set, FuzzArg, FuzzError, FuzzOrder, TestEnvironment};

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
            Err(FuzzError::new(
                FuzzOrder::First,
                Some(-1),
                Some(libc::EINVAL),
            )),
        ),
        FuzzArg::new(
            Some(libc::timespec {
                tv_sec: 0,
                tv_nsec: -1,
            }),
            Err(FuzzError::new(
                FuzzOrder::First,
                Some(-1),
                Some(libc::EINVAL),
            )),
        ),
        FuzzArg::new(
            Some(libc::timespec {
                tv_sec: 0,
                tv_nsec: 1_000_000_000,
            }),
            Err(FuzzError::new(
                FuzzOrder::First,
                Some(-1),
                Some(libc::EINVAL),
            )),
        ),
        FuzzArg::new(
            None,
            Err(FuzzError::new(
                FuzzOrder::First,
                Some(-1),
                Some(libc::EFAULT),
            )),
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
    for &request in requests.iter() {
        for &remain in remains.iter() {
            let append_args = |s| {
                format!(
                    "{} <request={:?},remain={:?}>",
                    s, request.value, remain.value
                )
            };

            tests.extend(vec![test_utils::ShadowTest::new(
                &append_args("return_values"),
                move || test_return_values(request, remain),
                set![TestEnvironment::Libc, TestEnvironment::Shadow],
            )]);
        }
    }

    // Test valid syscall behavior.
    tests.extend(vec![
        test_utils::ShadowTest::new(
            "sleep_duration",
            test_sleep_duration,
            set![TestEnvironment::Libc, TestEnvironment::Shadow],
        ),
        test_utils::ShadowTest::new(
            "interrupted_sleep",
            test_interrupted_sleep,
            set![TestEnvironment::Libc, TestEnvironment::Shadow],
        ),
    ]);

    tests
}

fn test_return_values(
    request: FuzzArg<Option<libc::timespec>>,
    mut remain: FuzzArg<Option<libc::timespec>>,
) -> anyhow::Result<()> {
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
                libc::nanosleep(request_ptr, remain_ptr),
                *libc::__errno_location(),
            )
        }
    };

    // Args may be valid or invalid.
    let validation = vec![request.expected_result, remain.expected_result];

    // `nanosleep` returns 0 on success.
    test_utils::verify_syscall_result(validation, 0, rv, errno)?;

    Ok(())
}

fn test_sleep_duration() -> anyhow::Result<()> {
    let sleep_dur = Duration::from_millis(1100);
    let request = duration_to_timespec(sleep_dur);
    test_utils::check_fn_exec_duration(sleep_dur, SLEEP_TOLERANCE, || {
        let rv = unsafe { libc::nanosleep(&request, std::ptr::null_mut()) };
        ensure_ord!(0, ==, rv);
        Ok(())
    })
}

/// A clock_nanosleep interrupted by a signal handler should return EINTR.
fn test_interrupted_sleep() -> anyhow::Result<()> {
    // The signaler sleeps and then interrupts a sleeping sleeper.
    let intr_dur = Duration::from_millis(300);
    let sleep_dur = Duration::from_millis(900);

    test_utils::interrupt_fn_exec(intr_dur, || {
        let request = duration_to_timespec(sleep_dur);
        let mut remain = libc::timespec {
            tv_sec: 0,
            tv_nsec: 0,
        };

        // Should be interrupted after the interrupt signal duration.
        test_utils::check_fn_exec_duration(intr_dur, SLEEP_TOLERANCE, || {
            let (rv, errno) = unsafe {
                (
                    libc::nanosleep(&request, &mut remain),
                    *libc::__errno_location(),
                )
            };
            ensure_ord!(-1, ==, rv);
            ensure_ord!(libc::EINTR, ==, errno);
            Ok(())
        })?;

        // Check reported time remaining.
        let expected = sleep_dur.checked_sub(intr_dur).unwrap();
        let actual = timespec_to_duration(remain);
        let diff = duration_abs_diff(expected, actual);
        ensure_ord!(diff, <=, SLEEP_TOLERANCE);

        Ok(())
    })
}
