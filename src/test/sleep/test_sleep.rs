/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use std::time::{Duration, Instant};

use nix::unistd;
use test_utils::time::duration_abs_diff;

type SleepFn = (fn(u32) -> Option<Duration>, &'static str);
type TimeFn = (fn(libc::clockid_t) -> Duration, &'static str);

const SLEEP_FNS: [SleepFn; 2] = [(sleep, "sleep"), (usleep, "usleep")];
const TIME_FNS: [TimeFn; 2] = [
    (call_clock_gettime, "call_clock_gettime"),
    (syscall_clock_gettime, "syscall_clock_gettime"),
];

fn main() {
    sleep_and_test();
    sleep_and_signal_test();
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
                "{sleep_name:>9}, {time_name:>21} -- Duration: {duration:?} (sleep_duration {sleep_duration:?}, tolerance +/- {tolerance:?})",
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
        "*** Interrupted sleep tests. Sleeping for {sleeper_sleep_duration:?} but interrupted after {signaler_sleep_duration:?} ***",
    );
    for (sleep_fn, sleep_name) in SLEEP_FNS.iter() {
        let (sender, receiver) = std::sync::mpsc::channel::<unistd::Pid>();
        let sleeper: std::thread::JoinHandle<_> = std::thread::spawn(move || {
            sender.send(unistd::gettid()).unwrap();

            let start_time = call_clock_gettime(libc::CLOCK_MONOTONIC);
            let rem = sleep_fn(sleeper_sleep_duration.as_secs().try_into().unwrap());
            let end_time = call_clock_gettime(libc::CLOCK_MONOTONIC);

            let duration = end_time - start_time;
            println!("{sleep_name:>9} -- Slept for: {duration:?}, rem: {rem:?}");

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
