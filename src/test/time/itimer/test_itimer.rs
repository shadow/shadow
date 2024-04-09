use std::{
    ops::Sub,
    sync::atomic::{AtomicU64, Ordering},
};

use nix::sys::{
    signal::{SaFlags, SigAction, SigHandler, SigSet, Signal},
    time::{TimeVal, TimeValLike},
};
use test_utils::{ensure_ord, getitimer, set, setitimer, ITimer, ShadowTest, TestEnvironment};

// Counts how many times the SIGALRM handler ran.
static SIGNAL_CTR: AtomicU64 = AtomicU64::new(0);

// SIGALRM handler.
extern "C" fn sigalrm_handler(sig: i32) {
    assert_eq!(sig, libc::SIGALRM);
    SIGNAL_CTR.fetch_add(1, Ordering::Relaxed);
}

// Reset timer and signal count.
fn reset() -> anyhow::Result<()> {
    setitimer(
        libc::ITIMER_REAL,
        &libc::itimerval {
            it_value: libc::timeval {
                tv_sec: 0,
                tv_usec: 0,
            },
            it_interval: libc::timeval {
                tv_sec: 0,
                tv_usec: 0,
            },
        },
    )?;
    SIGNAL_CTR.store(0, Ordering::Relaxed);
    Ok(())
}

fn test_initially_unset() -> anyhow::Result<()> {
    // Should initially be unset.
    let val = getitimer(libc::ITIMER_REAL)?;
    ensure_ord!(val, ==, ITimer{value: TimeVal::zero(), interval: TimeVal::zero()});
    Ok(())
}

fn test_set_then_get() -> anyhow::Result<()> {
    reset()?;

    let it_value = libc::timeval {
        tv_sec: 1,
        tv_usec: 2,
    };
    let it_interval = libc::timeval {
        tv_sec: 3,
        tv_usec: 4,
    };
    let val = setitimer(
        libc::ITIMER_REAL,
        &libc::itimerval {
            it_value,
            it_interval,
        },
    )?;
    ensure_ord!(val,==, ITimer{value: TimeVal::zero(), interval: TimeVal::zero()});

    let val = getitimer(libc::ITIMER_REAL)?;
    // Interval should be exactly as was set.
    ensure_ord!(val.interval, ==, TimeVal::from(it_interval));
    // Time remaining should be equal to or slightly less than what was set.
    let diff = TimeVal::from(it_value).sub(val.value);
    ensure_ord!(diff, >=, TimeVal::zero());
    ensure_ord!(diff, <, TimeVal::microseconds(100));

    Ok(())
}

fn test_itimers_are_process_wide() -> anyhow::Result<()> {
    reset()?;

    let thread1_val = libc::itimerval {
        it_value: libc::timeval {
            tv_sec: 1,
            tv_usec: 0,
        },
        it_interval: libc::timeval {
            tv_sec: 1,
            tv_usec: 0,
        },
    };
    let thread2_val = libc::itimerval {
        it_value: libc::timeval {
            tv_sec: 1,
            tv_usec: 0,
        },
        it_interval: libc::timeval {
            tv_sec: 2,
            tv_usec: 0,
        },
    };

    setitimer(libc::ITIMER_REAL, &thread1_val)?;

    let res: ITimer = std::thread::spawn(move || {
        // Overwrites timer set in the parent thread.
        setitimer(libc::ITIMER_REAL, &thread2_val)
    })
    .join()
    .unwrap()?;
    // setitimer should have gotten back the first timer.
    ensure_ord!(res.interval, ==, thread1_val.it_interval.into());

    // This thread should now see the timer that was set by the child thread.
    let res: ITimer = getitimer(libc::ITIMER_REAL)?;
    ensure_ord!(res.interval, ==, thread2_val.it_interval.into());

    Ok(())
}

fn test_set_then_set() -> anyhow::Result<()> {
    reset()?;

    let it_value = libc::timeval {
        tv_sec: 1,
        tv_usec: 2,
    };
    let it_interval = libc::timeval {
        tv_sec: 3,
        tv_usec: 4,
    };
    let val = setitimer(
        libc::ITIMER_REAL,
        &libc::itimerval {
            it_value,
            it_interval,
        },
    )?;
    ensure_ord!(val, ==, ITimer{value: TimeVal::zero(), interval: TimeVal::zero()});

    let val = setitimer(
        libc::ITIMER_REAL,
        &libc::itimerval {
            it_value: libc::timeval {
                tv_sec: 0,
                tv_usec: 0,
            },
            it_interval: libc::timeval {
                tv_sec: 0,
                tv_usec: 0,
            },
        },
    )?;

    // Interval should be exactly as was set.
    ensure_ord!(val.interval, ==, TimeVal::from(it_interval));
    // Time remaining should be equal to or slightly less than what was set.
    let diff = TimeVal::from(it_value).sub(val.value);
    ensure_ord!(diff, >=, TimeVal::zero());
    ensure_ord!(diff,<, TimeVal::milliseconds(1));

    Ok(())
}

fn test_oneshot() -> anyhow::Result<()> {
    reset()?;

    // 100 ms
    let it_value = libc::timeval {
        tv_sec: 0,
        tv_usec: 100_000,
    };
    let it_interval = libc::timeval {
        tv_sec: 0,
        tv_usec: 0,
    };
    setitimer(
        libc::ITIMER_REAL,
        &libc::itimerval {
            it_value,
            it_interval,
        },
    )?;

    // Sleep for 50 ms.
    std::thread::sleep(std::time::Duration::from_millis(50));
    // Shouldn't have fired yet.
    ensure_ord!(SIGNAL_CTR.load(Ordering::Relaxed), ==, 0);
    // Should be somewhere between 0 and 100 ms left.
    let val = getitimer(libc::ITIMER_REAL)?;
    ensure_ord!(val.value, >, TimeVal::zero());
    ensure_ord!(val.value, <, TimeVal::milliseconds(100));
    // Interval should still be 0.
    ensure_ord!(val.interval, ==, TimeVal::zero());

    // Sleep another 100 ms, which should put us at about 150ms since setting the timer.
    std::thread::sleep(std::time::Duration::from_millis(100));

    // Should have fired exactly once.
    ensure_ord!(SIGNAL_CTR.load(Ordering::Relaxed), ==, 1);

    // Timer should no longer be enabled.
    ensure_ord!(getitimer(libc::ITIMER_REAL)?, ==, ITimer{value: TimeVal::zero(), interval: TimeVal::zero()});

    // Likewise it shouldn't go off again after sleeping again.
    std::thread::sleep(std::time::Duration::from_millis(150));
    ensure_ord!(SIGNAL_CTR.load(Ordering::Relaxed), ==, 1);
    Ok(())
}

fn test_interval() -> anyhow::Result<()> {
    reset()?;

    // 100 ms
    let it_value = libc::timeval {
        tv_sec: 0,
        tv_usec: 100_000,
    };
    let it_interval = libc::timeval {
        tv_sec: 0,
        tv_usec: 100_000,
    };
    setitimer(
        libc::ITIMER_REAL,
        &libc::itimerval {
            it_value,
            it_interval,
        },
    )?;

    // Sleep for 150 ms.
    std::thread::sleep(std::time::Duration::from_millis(150));
    // Should have fired once.
    ensure_ord!(SIGNAL_CTR.load(Ordering::Relaxed), ==, 1);
    // Should be somewhere between 0 and 100 ms left.
    let val = getitimer(libc::ITIMER_REAL)?;
    ensure_ord!(val.value, >, TimeVal::zero());
    ensure_ord!(val.value, <, TimeVal::milliseconds(100));
    // Interval should still be 100ms.
    ensure_ord!(val.interval, ==, TimeVal::milliseconds(100));

    // Sleep another 100 ms, which should put us at about 250ms since setting the timer.
    std::thread::sleep(std::time::Duration::from_millis(100));

    // Should have fired again.
    ensure_ord!(SIGNAL_CTR.load(Ordering::Relaxed), ==, 2);
    Ok(())
}

fn test_interval_zero() -> anyhow::Result<()> {
    reset()?;

    // Value of 0, but interval of 100ms
    let it_value = libc::timeval {
        tv_sec: 0,
        tv_usec: 0,
    };
    let it_interval = libc::timeval {
        tv_sec: 0,
        tv_usec: 100_000,
    };
    setitimer(
        libc::ITIMER_REAL,
        &libc::itimerval {
            it_value,
            it_interval,
        },
    )?;
    // The man page isn't clear about what should happen here,
    // but experimentally the timer is completely disabled, even though we
    // specified a non-zero interval.
    ensure_ord!(getitimer(libc::ITIMER_REAL)?, ==, ITimer{value: TimeVal::zero(), interval: TimeVal::zero()});
    ensure_ord!(SIGNAL_CTR.load(Ordering::Relaxed), ==, 0);

    Ok(())
}

fn test_leave_running() -> anyhow::Result<()> {
    reset()?;

    // 100 ms
    let it_value = libc::timeval {
        tv_sec: 1,
        tv_usec: 0,
    };
    let it_interval = libc::timeval {
        tv_sec: 1,
        tv_usec: 0,
    };
    setitimer(
        libc::ITIMER_REAL,
        &libc::itimerval {
            it_value,
            it_interval,
        },
    )?;
    Ok(())
}

fn test_alarm() -> anyhow::Result<()> {
    reset()?;

    // The `alarm` syscall is a bit obnoxious to test directly, since it can
    // only operate at 1s increments. However, it is defined to operate on the
    // same timer as `getitimer` with ITIMER_REAL, so we can use that to
    // validate that it's setting the itimer state as we expect without actually
    // waiting for timers to fire, etc.

    let seconds = 10;
    // Return value should be 0 since there was no timer previously set.
    assert_eq!(linux_api::time::alarm(seconds), Ok(0));

    // itimer should be have been set to `seconds`
    let val = getitimer(libc::ITIMER_REAL)?;
    ensure_ord!(val.value, >=, TimeVal::milliseconds(i64::from(seconds) * 1000 - 100));
    ensure_ord!(val.value, <=, TimeVal::seconds(seconds.into()));
    // should be non-repeating
    ensure_ord!(val.interval, ==, TimeVal::zero());

    // calling with 0 should cancel, and return the remaining time in seconds,
    // *rounded to the nearest second*, not truncated.
    std::thread::sleep(std::time::Duration::from_millis(1));
    let remaining_secs = linux_api::time::alarm(0).unwrap();
    ensure_ord!(remaining_secs, ==, seconds);

    // There should no longer be any timer set.
    let val = getitimer(libc::ITIMER_REAL)?;
    ensure_ord!(val.value, ==, TimeVal::zero());
    ensure_ord!(val.interval, ==, TimeVal::zero());

    // When there is less than one second remaining on a timer, `alarm` returns 1
    // instead of 0, so that 0 always indicates no timer is set.
    setitimer(
        libc::ITIMER_REAL,
        &libc::itimerval {
            it_value: libc::timeval {
                tv_sec: 0,
                tv_usec: 100_000,
            },
            it_interval: libc::timeval {
                tv_sec: 0,
                tv_usec: 0,
            },
        },
    )
    .unwrap();
    // Cancels, and we should get a rounded-up value of 1 back.
    ensure_ord!(linux_api::time::alarm(0), ==, Ok(1));

    Ok(())
}

fn test_alarm_fired() -> anyhow::Result<()> {
    reset()?;

    // Set to expire in exactly 1s
    assert_eq!(linux_api::time::alarm(1), Ok(0));

    // Sleep slightly more than 1s
    std::thread::sleep(std::time::Duration::from_millis(1001));

    // Cancel, and get remaining time
    let rem = linux_api::time::alarm(0).unwrap();

    // Timer should have fired
    ensure_ord!(SIGNAL_CTR.load(Ordering::Relaxed), ==, 1);

    // `alarm` should return 0 when there is no timer scheduled, and
    // there should be no timer scheduled since it already fired.
    ensure_ord!(rem, ==, 0);

    Ok(())
}

fn test_alarm_with_zero_remaining() -> anyhow::Result<()> {
    reset()?;

    // Set to expire in exactly 1s
    assert_eq!(linux_api::time::alarm(1), Ok(0));

    // Sleep exactly 1s
    std::thread::sleep(std::time::Duration::from_secs(1));

    // Cancel, and get remaining time
    let rem = linux_api::time::alarm(0).unwrap();

    // We can't guarantee whether or not the timer fired before we canceled it.
    // e.g. under shadow, there should have been both the `sleep` timer
    // and the alarm timer set to expire at exactly the same time, but which
    // one fired first is an implementation detail.
    match SIGNAL_CTR.load(Ordering::Relaxed) {
        0 => {
            // The alarm hadn't fired yet. Even though there was exactly 0 time
            // remaining, the syscall should return 1.
            println!("Hadn't fired");
            ensure_ord!(rem, ==, 1);
        }
        1 => {
            // The alarm had already fired.
            println!("Already fired");
            ensure_ord!(rem, ==, 0);
        }
        x => anyhow::bail!("Unexpected val {x}"),
    };

    Ok(())
}

fn main() -> anyhow::Result<()> {
    // Install a SIGALRM handler that counts how many times it's been received.
    unsafe {
        nix::sys::signal::sigaction(
            Signal::SIGALRM,
            &SigAction::new(
                SigHandler::Handler(sigalrm_handler),
                SaFlags::empty(),
                SigSet::empty(),
            ),
        )
        .unwrap()
    };
    let mut sigset = SigSet::empty();
    sigset.add(Signal::SIGALRM);
    nix::sys::signal::sigprocmask(
        nix::sys::signal::SigmaskHow::SIG_UNBLOCK,
        Some(&sigset),
        None,
    )
    .unwrap();

    // should we restrict the tests we run?
    let filter_shadow_passing = std::env::args().any(|x| x == "--shadow-passing");
    let filter_libc_passing = std::env::args().any(|x| x == "--libc-passing");
    // should we summarize the results rather than exit on a failed test
    let summarize = std::env::args().any(|x| x == "--summarize");

    let all_envs = set![TestEnvironment::Libc, TestEnvironment::Shadow];
    let mut tests: Vec<test_utils::ShadowTest<(), anyhow::Error>> = vec![
        ShadowTest::new("initially_unset", test_initially_unset, all_envs.clone()),
        ShadowTest::new("set_then_get", test_set_then_get, all_envs.clone()),
        ShadowTest::new(
            "itimers_are_process_wide",
            test_itimers_are_process_wide,
            all_envs.clone(),
        ),
        ShadowTest::new("set_then_set", test_set_then_set, all_envs.clone()),
        ShadowTest::new("set_oneshot", test_oneshot, all_envs.clone()),
        ShadowTest::new("set_interval", test_interval, all_envs.clone()),
        ShadowTest::new("set_interval_zero", test_interval_zero, all_envs.clone()),
        ShadowTest::new("alarm", test_alarm, all_envs.clone()),
        ShadowTest::new("alarm_fired", test_alarm_fired, all_envs.clone()),
        ShadowTest::new(
            "alarm_with_zero_remaining",
            test_alarm_with_zero_remaining,
            all_envs.clone(),
        ),
        // Must be last.
        // Validate proper cleanup for a timer that's still running when the
        // process exits.
        ShadowTest::new("leave_running", test_leave_running, all_envs),
    ];

    if filter_shadow_passing {
        tests.retain(|x| x.passing(TestEnvironment::Shadow));
    }
    if filter_libc_passing {
        tests.retain(|x| x.passing(TestEnvironment::Libc));
    }

    test_utils::run_tests(&tests, summarize)?;

    Ok(())
}
