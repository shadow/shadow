/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use std::time::{Duration, Instant};

use nix::unistd;

type SleepFn = (fn(u32) -> Option<Duration>, &'static str);
type TimeFn = (fn(libc::clockid_t) -> Duration, &'static str);

const SLEEP_FNS: [SleepFn; 4] = [
    (sleep, "sleep"),
    (usleep, "usleep"),
    (nanosleep, "nanosleep"),
    (clock_nanosleep, "clock_nanosleep"),
];
const TIME_FNS: [TimeFn; 2] = [
    (call_clock_gettime, "call_clock_gettime"),
    (syscall_clock_gettime, "syscall_clock_gettime"),
];

fn duration_abs_diff(t1: Duration, t0: Duration) -> Duration {
    let res = t1.checked_sub(t0);
    match res {
        Some(d) => d,
        None => t0.checked_sub(t1).unwrap(),
    }
}

fn main() {
    sleep_and_test();
    sleep_and_signal_test();
    clock_nanosleep_with_abstime();
    clock_nanosleep_with_past_abstime();
    println!("Success.");
}

fn sleep_and_test() {
    let sleep_duration = Duration::from_secs(1);
    let tolerance = Duration::from_millis(30);

    println!("*** Basic sleep tests ***");
    for (sleep_fn, sleep_name) in SLEEP_FNS.iter() {
        for (time_fn, time_name) in TIME_FNS.iter() {
            let start_time = time_fn(libc::CLOCK_MONOTONIC);
            assert_eq!(sleep_fn(sleep_duration.as_secs().try_into().unwrap()), None);
            let end_time = call_clock_gettime(libc::CLOCK_MONOTONIC);

            let duration = end_time - start_time;
            println!(
                "{:>9}, {:>21} -- Duration: {:?} (sleep_duration {:?}, tolerance +/- {:?})",
                sleep_name, time_name, duration, sleep_duration, tolerance
            );

            assert!(duration_abs_diff(sleep_duration, duration) < tolerance);
        }
    }
}

extern "C" fn nop(_sig: i32) {}

fn sleep_and_signal_test() {
    unsafe {
        nix::sys::signal::sigaction(
            nix::sys::signal::SIGUSR1,
            &nix::sys::signal::SigAction::new(
                nix::sys::signal::SigHandler::Handler(nop),
                nix::sys::signal::SaFlags::empty(),
                nix::sys::signal::SigSet::empty(),
            ),
        )
        .unwrap()
    };

    let sleeper_sleep_duration = Duration::from_secs(3);
    let signaler_sleep_duration = Duration::from_secs(1);
    let tolerance = Duration::from_millis(30);
    println!(
        "*** Interrupted sleep tests. Sleeping for {:?} but interrupted after {:?} ***",
        sleeper_sleep_duration, signaler_sleep_duration
    );
    for (sleep_fn, sleep_name) in SLEEP_FNS.iter() {
        let (sender, receiver) = std::sync::mpsc::channel::<unistd::Pid>();
        let sleeper: std::thread::JoinHandle<_> = std::thread::spawn(move || {
            sender.send(unistd::gettid()).unwrap();

            let start_time = call_clock_gettime(libc::CLOCK_MONOTONIC);
            let rem = sleep_fn(sleeper_sleep_duration.as_secs().try_into().unwrap());
            let end_time = call_clock_gettime(libc::CLOCK_MONOTONIC);

            let duration = end_time - start_time;
            println!(
                "{:>9} -- Slept for: {:?}, rem: {:?}",
                sleep_name, duration, rem
            );

            // Check time slept
            assert!(duration_abs_diff(signaler_sleep_duration, duration) < tolerance);

            // Check reported time remaining
            let expected_rem = sleeper_sleep_duration
                .checked_sub(signaler_sleep_duration)
                .unwrap();
            let rem_tolerance = if sleep_name == &"sleep" {
                // Sleep only has 1s granularity.
                Duration::from_secs(1)
            } else {
                tolerance
            };
            assert!(duration_abs_diff(rem.unwrap(), expected_rem) <= rem_tolerance);
        });

        std::thread::sleep(signaler_sleep_duration);
        unsafe {
            libc::syscall(
                libc::SYS_tkill,
                receiver.recv().unwrap().as_raw(),
                libc::SIGUSR1,
            );
        }

        sleeper.join().unwrap();
    }
}

fn clock_nanosleep_with_abstime() {
    let clock = libc::CLOCK_MONOTONIC;

    // get time time before sleeping
    let before = call_clock_gettime(clock);

    // sleep until the current absolute time + 500 ms
    let sleep_duration = Duration::from_millis(500);
    let stop = before + sleep_duration;
    let stop = libc::timespec {
        tv_sec: stop.as_secs().try_into().unwrap(),
        tv_nsec: stop.subsec_nanos().try_into().unwrap(),
    };
    let mut rem = libc::timespec {
        tv_sec: 0,
        tv_nsec: 0,
    };
    assert_eq!(0, unsafe {
        libc::clock_nanosleep(clock, libc::TIMER_ABSTIME, &stop, &mut rem)
    });

    // get time after sleeping
    let after = call_clock_gettime(clock);

    let tolerance = Duration::from_millis(30);
    let duration = after - before;
    assert!(duration_abs_diff(sleep_duration, duration) < tolerance);
}

fn clock_nanosleep_with_past_abstime() {
    let clock = libc::CLOCK_MONOTONIC;

    // get time time before sleeping
    let before = call_clock_gettime(clock);

    // sleep until the current absolute time - 500 ms
    let stop = before - Duration::from_millis(500);
    let stop = libc::timespec {
        tv_sec: stop.as_secs().try_into().unwrap(),
        tv_nsec: stop.subsec_nanos().try_into().unwrap(),
    };
    let mut rem = libc::timespec {
        tv_sec: 0,
        tv_nsec: 0,
    };
    assert_eq!(0, unsafe {
        libc::clock_nanosleep(clock, libc::TIMER_ABSTIME, &stop, &mut rem)
    });

    // get time after sleeping
    let after = call_clock_gettime(clock);

    // should have returned immediately, but the syscall itself may have taken a small amount of
    // time
    let tolerance = Duration::from_micros(500);
    let duration = after - before;
    assert!(duration_abs_diff(Duration::ZERO, duration) < tolerance);
}

fn sleep(seconds: u32) -> Option<Duration> {
    let rv;
    unsafe {
        rv = libc::sleep(seconds);
    }
    if rv != 0 {
        Some(Duration::from_secs(rv.into()))
    } else {
        None
    }
}

fn usleep(seconds: u32) -> Option<Duration> {
    let t0 = Instant::now();
    let rv;
    let e;
    unsafe {
        rv = libc::usleep(seconds * 1000000);
        e = *libc::__errno_location();
    }
    let t1 = Instant::now();
    if rv == 0 {
        None
    } else {
        assert_eq!(e, libc::EINTR);
        Some(
            Duration::from_secs(seconds.into())
                .checked_sub(t1.duration_since(t0))
                .unwrap(),
        )
    }
}

fn nanosleep(seconds: u32) -> Option<Duration> {
    let stop = libc::timespec {
        tv_sec: seconds as i64,
        tv_nsec: 0,
    };
    let mut rem = libc::timespec {
        tv_sec: 0,
        tv_nsec: 0,
    };
    let rv;
    let e;
    unsafe {
        rv = libc::nanosleep(&stop, &mut rem);
        e = *libc::__errno_location();
    }
    if rv != 0 {
        assert_eq!(e, libc::EINTR);
        Some(
            Duration::from_secs(rem.tv_sec.try_into().unwrap())
                + Duration::from_nanos(rem.tv_nsec.try_into().unwrap()),
        )
    } else {
        None
    }
}

fn clock_nanosleep(seconds: u32) -> Option<Duration> {
    let stop = libc::timespec {
        tv_sec: seconds as i64,
        tv_nsec: 0,
    };
    let mut rem = libc::timespec {
        tv_sec: 0,
        tv_nsec: 0,
    };
    let rv;
    unsafe {
        rv = libc::clock_nanosleep(libc::CLOCK_MONOTONIC, 0, &stop, &mut rem);
    }
    if rv == 0 {
        None
    } else {
        assert_eq!(rv, libc::EINTR);
        Some(
            Duration::from_secs(rem.tv_sec.try_into().unwrap())
                + Duration::from_nanos(rem.tv_nsec.try_into().unwrap()),
        )
    }
}

fn call_clock_gettime(clock: libc::clockid_t) -> Duration {
    let mut ts = libc::timespec {
        tv_sec: 0,
        tv_nsec: 0,
    };
    let rv = unsafe { libc::clock_gettime(clock, &mut ts) };
    assert!(rv >= 0);
    // valid tv_nsec values are [0, 999999999], which fit in a u32
    Duration::new(ts.tv_sec as u64, ts.tv_nsec as u32)
}

fn syscall_clock_gettime(clock: libc::clockid_t) -> Duration {
    let mut ts = libc::timespec {
        tv_sec: 0,
        tv_nsec: 0,
    };
    let rv = unsafe { libc::syscall(libc::SYS_clock_gettime, clock, &mut ts) };
    assert!(rv >= 0);
    // valid tv_nsec values are [0, 999999999], which fit in a u32
    Duration::new(ts.tv_sec as u64, ts.tv_nsec as u32)
}
