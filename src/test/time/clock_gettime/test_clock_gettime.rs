use test_utils::{set, FuzzArg, FuzzError, FuzzOrder, TestEnvironment};

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

    let clockids = vec![
        FuzzArg::new(libc::CLOCK_REALTIME, Ok(())),
        FuzzArg::new(libc::CLOCK_REALTIME_ALARM, Ok(())),
        FuzzArg::new(libc::CLOCK_REALTIME_COARSE, Ok(())),
        FuzzArg::new(libc::CLOCK_TAI, Ok(())),
        FuzzArg::new(libc::CLOCK_MONOTONIC, Ok(())),
        FuzzArg::new(libc::CLOCK_MONOTONIC_COARSE, Ok(())),
        FuzzArg::new(libc::CLOCK_MONOTONIC_RAW, Ok(())),
        FuzzArg::new(libc::CLOCK_BOOTTIME, Ok(())),
        FuzzArg::new(libc::CLOCK_BOOTTIME_ALARM, Ok(())),
        FuzzArg::new(libc::CLOCK_PROCESS_CPUTIME_ID, Ok(())),
        FuzzArg::new(libc::CLOCK_THREAD_CPUTIME_ID, Ok(())),
        FuzzArg::new(
            128,
            Err(FuzzError::new(
                FuzzOrder::First,
                Some(-1),
                Some(libc::EINVAL),
            )),
        ),
        FuzzArg::new(
            -1,
            Err(FuzzError::new(
                FuzzOrder::First,
                Some(-1),
                Some(libc::EINVAL),
            )),
        ),
    ];

    let timespecs: Vec<FuzzArg<Option<libc::timespec>>> = vec![
        FuzzArg::new(
            Some(libc::timespec {
                tv_sec: 0,
                tv_nsec: 0,
            }),
            Ok(()),
        ),
        FuzzArg::new(
            None,
            Err(FuzzError::new(
                FuzzOrder::Second,
                Some(-1),
                Some(libc::EFAULT),
            )),
        ),
    ];

    for &clockid in clockids.iter() {
        for &ts in timespecs.iter() {
            let append_args = |s| {
                format!(
                    "{} <clockid={:?},timespec={:?}>",
                    s, clockid.value, ts.value
                )
            };

            // NULL ts causes SEGFAULT in glibc on Linux (but the direct syscall works OK).
            let mut passing = set![TestEnvironment::Shadow];
            if ts.value.is_some() {
                passing.insert(TestEnvironment::Libc);
            }

            tests.extend(vec![
                test_utils::ShadowTest::new(
                    &append_args("clock_gettime"),
                    move || test_clock_gettime(clockid, ts),
                    passing,
                ),
                test_utils::ShadowTest::new(
                    &append_args("syscall_clock_gettime"),
                    move || test_syscall_clock_gettime(clockid, ts),
                    set![TestEnvironment::Libc, TestEnvironment::Shadow],
                ),
            ]);
        }
    }

    tests
}

fn test_clock_gettime(
    clockid: FuzzArg<libc::clockid_t>,
    mut ts: FuzzArg<Option<libc::timespec>>,
) -> anyhow::Result<()> {
    let (rv, errno) = {
        let ts_ptr = ts
            .value
            .as_mut()
            .map_or(std::ptr::null_mut(), std::ptr::from_mut);
        unsafe {
            (
                libc::clock_gettime(clockid.value, ts_ptr),
                *libc::__errno_location(),
            )
        }
    };

    // `clock_gettime` returns 0 on success.
    test_utils::verify_syscall_result(
        vec![clockid.expected_result, ts.expected_result],
        0,
        rv,
        errno,
    )?;
    Ok(())
}

fn test_syscall_clock_gettime(
    clockid: FuzzArg<libc::clockid_t>,
    mut ts: FuzzArg<Option<libc::timespec>>,
) -> anyhow::Result<()> {
    let (rv, errno) = {
        let ts_ptr = ts
            .value
            .as_mut()
            .map_or(std::ptr::null_mut(), std::ptr::from_mut);
        unsafe {
            (
                libc::syscall(libc::SYS_clock_gettime, clockid.value, ts_ptr),
                *libc::__errno_location(),
            )
        }
    };

    // `clock_gettime` returns 0 on success.
    test_utils::verify_syscall_result(
        vec![clockid.expected_result, ts.expected_result],
        0,
        rv.try_into().unwrap(),
        errno,
    )?;
    Ok(())
}
