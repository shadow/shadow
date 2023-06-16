use std::collections::HashSet;
use std::sync::mpsc::Sender;
use std::time::{Duration, SystemTime};

use nix::unistd::Pid;
use test_utils::{ensure_ord, nop_sig_handler, set, TestEnvironment};

#[derive(Debug, Copy, Clone, Eq, Ord, PartialEq, PartialOrd)]
enum VerifyOrder {
    ClockId,
    Request,
    Flags,
}

#[derive(Debug, Copy, Clone, Eq, Ord, PartialEq, PartialOrd)]
struct Verify {
    order: VerifyOrder,
    rv: Option<libc::c_int>,
    errno: Option<libc::c_int>,
}

impl Verify {
    fn new(order: VerifyOrder, rv: Option<libc::c_int>, errno: Option<libc::c_int>) -> Self {
        Verify { order, rv, errno }
    }
}

#[derive(Debug, Copy, Clone, PartialEq)]
enum Validity {
    Valid,
    Invalid(Verify),
}

#[derive(Debug, Copy, Clone)]
pub struct Arg<T> {
    value: T,
    validity: Validity,
}

impl<T> Arg<T> {
    fn new(value: T, validity: Validity) -> Self {
        Arg::<T> { value, validity }
    }
}

/// Returns Verify items for those with invalid Validity.
fn filter_discard_valid(vals: &[Validity]) -> Vec<&Verify> {
    vals.iter()
        .filter_map(|v| match v {
            Validity::Invalid(verify) => Some(verify),
            _ => None,
        })
        .collect()
}

fn verify_syscall_result(
    vals: Vec<Validity>,
    success_rv: libc::c_int,
    rv: libc::c_int,
    errno: libc::c_int,
) -> anyhow::Result<()> {
    // We want to ensure we have the correct error for invalid values.
    let mut check = filter_discard_valid(&vals);

    // Check the error according to the ordering defined by the caller.
    check.sort();

    if let Some(error) = check.first() {
        // Should have been error, which are returned in rv (not in errno)
        if let Some(expected_rv) = error.rv {
            ensure_ord!(expected_rv, ==, rv);
        }
        if let Some(expected_errno) = error.errno {
            ensure_ord!(expected_errno, ==, errno);
        }
    } else {
        // Syscall should have returned success.
        ensure_ord!(success_rv, ==, rv);
    }
    Ok(())
}

/// When we go to sleep, this is the tolerance we allow when checking that we slept the correct
/// amount of time (allows for imprecise kernel wakeups or thread preemption).
const SLEEP_TOLERANCE: Duration = Duration::from_millis(30);

/// When we make a sleep syscall that returns immediately, this is the tolerance we allow when
/// checking that we did not sleep (allows for syscall execution time).
const SYSCALL_EXEC_TOLERANCE: Duration = Duration::from_micros(500);

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
        Arg::new(libc::CLOCK_REALTIME, Validity::Valid),
        Arg::new(libc::CLOCK_TAI, Validity::Valid),
        Arg::new(libc::CLOCK_MONOTONIC, Validity::Valid),
        Arg::new(libc::CLOCK_BOOTTIME, Validity::Valid),
        Arg::new(libc::CLOCK_REALTIME_ALARM, Validity::Valid),
        Arg::new(libc::CLOCK_BOOTTIME_ALARM, Validity::Valid),
        Arg::new(libc::CLOCK_PROCESS_CPUTIME_ID, Validity::Valid),
        Arg::new(
            libc::CLOCK_THREAD_CPUTIME_ID,
            Validity::Invalid(Verify::new(VerifyOrder::ClockId, Some(libc::EINVAL), None)),
        ),
        Arg::new(
            libc::CLOCK_MONOTONIC_RAW,
            Validity::Invalid(Verify::new(VerifyOrder::ClockId, Some(libc::ENOTSUP), None)),
        ),
        Arg::new(
            libc::CLOCK_REALTIME_COARSE,
            Validity::Invalid(Verify::new(VerifyOrder::ClockId, Some(libc::ENOTSUP), None)),
        ),
        Arg::new(
            libc::CLOCK_MONOTONIC_COARSE,
            Validity::Invalid(Verify::new(VerifyOrder::ClockId, Some(libc::ENOTSUP), None)),
        ),
    ];

    let flags = vec![
        Arg::new(0, Validity::Valid),
        Arg::new(libc::TIMER_ABSTIME, Validity::Valid),
    ];

    let requests: Vec<Arg<*const libc::timespec>> = vec![
        Arg::new(
            &libc::timespec {
                tv_sec: 0,
                tv_nsec: 0,
            },
            Validity::Valid,
        ),
        Arg::new(
            &libc::timespec {
                tv_sec: -1,
                tv_nsec: 0,
            },
            Validity::Invalid(Verify::new(VerifyOrder::Request, Some(libc::EINVAL), None)),
        ),
        Arg::new(
            &libc::timespec {
                tv_sec: 0,
                tv_nsec: -1,
            },
            Validity::Invalid(Verify::new(VerifyOrder::Request, Some(libc::EINVAL), None)),
        ),
        Arg::new(
            &libc::timespec {
                tv_sec: 0,
                tv_nsec: 1_000_000_000,
            },
            Validity::Invalid(Verify::new(VerifyOrder::Request, Some(libc::EINVAL), None)),
        ),
        Arg::new(
            std::ptr::null(),
            Validity::Invalid(Verify::new(VerifyOrder::Request, Some(libc::EFAULT), None)),
        ),
    ];

    let remains: Vec<Arg<*mut libc::timespec>> = vec![
        Arg::new(
            &mut libc::timespec {
                tv_sec: 0,
                tv_nsec: 0,
            },
            Validity::Valid,
        ),
        Arg::new(std::ptr::null_mut(), Validity::Valid),
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
                    let should_succeed = filter_discard_valid(&[
                        clockid.validity,
                        flag.validity,
                        request.validity,
                        remain.validity,
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
    for &clockid in clockids.iter().filter(|c| c.validity == Validity::Valid) {
        for &flag in flags.iter().filter(|f| f.validity == Validity::Valid) {
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

fn get_flags(clockid: libc::clockid_t, flags: &[Arg<libc::c_int>]) -> Vec<Arg<libc::c_int>> {
    // See doc for `SPECIAL_ALARM_CLOCKIDS`
    let flag_with_unspec_bits = libc::TIMER_ABSTIME | 0x1111;

    let new_arg = if SPECIAL_ALARM_CLOCKIDS.contains(&clockid) {
        Arg::new(
            flag_with_unspec_bits,
            Validity::Invalid(Verify::new(VerifyOrder::Flags, Some(libc::EINVAL), None)),
        )
    } else {
        Arg::new(flag_with_unspec_bits, Validity::Valid)
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
    clockid: Arg<libc::clockid_t>,
    flags: Arg<libc::c_int>,
    request: Arg<*const libc::timespec>,
    remain: Arg<*mut libc::timespec>,
) -> anyhow::Result<()> {
    // Notably, errors are returned as positive return value, not in errno.
    let (rv, errno) = unsafe {
        (
            libc::clock_nanosleep(clockid.value, flags.value, request.value, remain.value),
            *libc::__errno_location(),
        )
    };

    // Args may be valid or invalid.
    let validation = vec![
        clockid.validity,
        flags.validity,
        request.validity,
        remain.validity,
    ];

    // `clock_nanosleep` returns 0 on success.
    verify_syscall_result(validation, 0, rv, errno)?;

    Ok(())
}

fn test_sleep_duration(clockid: libc::clockid_t, flags: libc::c_int) -> anyhow::Result<()> {
    let relative_dur = Duration::from_millis(1100);
    let request = create_sleep_request(clockid, flags, relative_dur)?;

    let before = SystemTime::now();
    let rv = unsafe { libc::clock_nanosleep(clockid, flags, &request, std::ptr::null_mut()) };
    let after = SystemTime::now();
    ensure_ord!(0, ==, rv);

    let actual_dur = after.duration_since(before)?;
    let diff = duration_abs_diff(relative_dur, actual_dur);
    ensure_ord!(diff, <=, SLEEP_TOLERANCE);

    Ok(())
}

/// Calling clock_nanosleep with flag TIMER_ABSTIME where the sleep request specifies a time in the
/// past results in a zero sleep duration (immediate return).
fn test_abstime_in_past(clockid: libc::clockid_t) -> anyhow::Result<()> {
    // Get an absolute sleep time slightly in the past.
    let past = clock_now_duration(clockid)?.saturating_sub(Duration::from_nanos(1_000_000_001));
    let request = libc::timespec {
        tv_sec: past.as_secs().try_into()?,
        tv_nsec: past.subsec_nanos().into(),
    };

    let before = SystemTime::now();
    let rv = unsafe {
        libc::clock_nanosleep(clockid, libc::TIMER_ABSTIME, &request, std::ptr::null_mut())
    };
    let after = SystemTime::now();
    ensure_ord!(0, ==, rv);

    // Syscall returns immediately, but allow some tolerance for syscall execution.
    let actual_dur = after.duration_since(before)?;
    ensure_ord!(actual_dur, <=, SYSCALL_EXEC_TOLERANCE);

    Ok(())
}

/// A clock_nanosleep interrupted by a signal handler should return EINTR.
fn test_interrupted_sleep(clockid: libc::clockid_t, flags: libc::c_int) -> anyhow::Result<()> {
    // The signaler sleeps and then interrupts a sleeping sleeper.
    let signaler_sleep_duration = Duration::from_millis(300);
    let sleeper_sleep_duration = Duration::from_millis(900);

    install_signal_handler()?;

    let (sender, receiver) = std::sync::mpsc::channel::<Pid>();
    let sleeper: std::thread::JoinHandle<_> = std::thread::spawn(move || {
        run_interrupted_sleeper(
            clockid,
            flags,
            signaler_sleep_duration,
            sleeper_sleep_duration,
            sender,
        )
    });

    std::thread::sleep(signaler_sleep_duration);
    unsafe {
        libc::syscall(libc::SYS_tkill, receiver.recv()?.as_raw(), libc::SIGUSR1);
    }

    sleeper.join().unwrap()
}

fn run_interrupted_sleeper(
    clockid: libc::clockid_t,
    flags: libc::c_int,
    sig_dur: Duration,
    sleep_dur: Duration,
    sender: Sender<Pid>,
) -> anyhow::Result<()> {
    sender.send(nix::unistd::gettid()).unwrap();

    let request = create_sleep_request(clockid, flags, sleep_dur)?;
    let mut remain = libc::timespec {
        tv_sec: 0,
        tv_nsec: 0,
    };

    let before = SystemTime::now();
    let rv = unsafe { libc::clock_nanosleep(clockid, flags, &request, &mut remain) };
    let after = SystemTime::now();
    ensure_ord!(libc::EINTR, ==, rv);

    // Should have been interrupted after the signal duration.
    let actual_dur = after.duration_since(before)?;
    let interrupt_diff = duration_abs_diff(sig_dur, actual_dur);
    ensure_ord!(interrupt_diff, <=, SLEEP_TOLERANCE);

    if flags != libc::TIMER_ABSTIME {
        // Check reported time remaining.
        let remain_expected = sleep_dur.checked_sub(sig_dur).unwrap();
        let remain_actual = timespec_to_duration(remain);
        let remain_diff = duration_abs_diff(remain_expected, remain_actual);
        ensure_ord!(remain_diff, <=, SLEEP_TOLERANCE);
    }

    Ok(())
}

fn install_signal_handler() -> anyhow::Result<()> {
    unsafe {
        nix::sys::signal::sigaction(
            nix::sys::signal::SIGUSR1,
            &nix::sys::signal::SigAction::new(
                nop_sig_handler(),
                nix::sys::signal::SaFlags::empty(),
                nix::sys::signal::SigSet::empty(),
            ),
        )?
    };
    Ok(())
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
    let request = libc::timespec {
        tv_sec: request_dur.as_secs().try_into()?,
        tv_nsec: request_dur.subsec_nanos().into(),
    };
    Ok(request)
}

fn clock_now_duration(clockid: libc::clockid_t) -> anyhow::Result<Duration> {
    let now = clock_now(clockid)?;
    Ok(timespec_to_duration(now))
}

fn clock_now(clockid: libc::clockid_t) -> anyhow::Result<libc::timespec> {
    let mut now = libc::timespec {
        tv_sec: 0,
        tv_nsec: 0,
    };
    let rv = unsafe { libc::clock_gettime(clockid, &mut now) };
    ensure_ord!(rv, ==, 0);
    Ok(now)
}

fn timespec_to_duration(ts: libc::timespec) -> Duration {
    let secs = Duration::from_secs(ts.tv_sec.try_into().unwrap());
    let nanos = Duration::from_nanos(ts.tv_nsec.try_into().unwrap());
    secs + nanos
}

fn duration_abs_diff(t1: Duration, t0: Duration) -> Duration {
    let res = t1.checked_sub(t0);
    match res {
        Some(d) => d,
        None => t0.checked_sub(t1).unwrap(),
    }
}
