use std::{
    mem::MaybeUninit,
    ops::Sub,
    sync::atomic::{AtomicU64, Ordering},
};

use nix::sys::{
    signal::{SaFlags, SigAction, SigHandler, SigSet, Signal},
    time::{TimeVal, TimeValLike},
};
use test_utils::{ensure_ord, set, ShadowTest, TestEnvironment};

#[derive(Copy, Clone, Eq, PartialEq, Debug)]
struct ITimer {
    pub interval: TimeVal,
    pub value: TimeVal,
}

impl From<libc::itimerval> for ITimer {
    fn from(val: libc::itimerval) -> Self {
        Self {
            interval: TimeVal::from(val.it_interval),
            value: TimeVal::from(val.it_value),
        }
    }
}

// Neither `libc` nor `nix` wrap `getitimer`.
fn getitimer(which: i32) -> nix::Result<ITimer> {
    let mut old_value: libc::itimerval = unsafe { MaybeUninit::zeroed().assume_init() };
    if unsafe { libc::syscall(libc::SYS_getitimer, which, &mut old_value as *mut _) } == -1 {
        return Err(nix::errno::Errno::last());
    }
    Ok(old_value.into())
}

// Neither `libc` nor `nix` wrap `setitimer`.
fn setitimer(which: i32, new_value: &libc::itimerval) -> nix::Result<ITimer> {
    let mut old_value: libc::itimerval = unsafe { MaybeUninit::zeroed().assume_init() };
    if unsafe { libc::syscall(libc::SYS_setitimer, which, new_value, &mut old_value) } == -1 {
        return Err(nix::errno::Errno::last());
    }
    Ok(old_value.into())
}

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
    ensure_ord!(diff,<, TimeVal::microseconds(100));

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
        ShadowTest::new("set_then_set", test_set_then_set, all_envs.clone()),
        ShadowTest::new("set_oneshot", test_oneshot, all_envs.clone()),
        ShadowTest::new("set_interval", test_interval, all_envs.clone()),
        ShadowTest::new("set_interval_zero", test_interval_zero, all_envs.clone()),
    ];

    if filter_shadow_passing {
        tests = tests
            .into_iter()
            .filter(|x| x.passing(TestEnvironment::Shadow))
            .collect()
    }
    if filter_libc_passing {
        tests = tests
            .into_iter()
            .filter(|x| x.passing(TestEnvironment::Libc))
            .collect()
    }

    test_utils::run_tests(&tests, summarize)?;

    Ok(())
}
