/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use std::convert::TryFrom;

type SleepFn<'a> = (fn(u32), &'a str);
type TimeFn<'a> = (fn() -> std::time::Duration, &'a str);

fn main() {
    let sleep_fns: [SleepFn; 3] = [
        (sleep, "sleep"),
        (usleep, "usleep"),
        (nanosleep, "nanosleep"),
    ];
    let time_fns: [TimeFn; 2] = [
        (call_clock_gettime, "call_clock_gettime"),
        (syscall_clock_gettime, "syscall_clock_gettime"),
    ];

    sleep_and_test(&sleep_fns, &time_fns);
    println!("Success.");
}

fn sleep_and_test(sleep_fns: &[SleepFn], time_fns: &[TimeFn]) {
    let sleep_duration_sec = 1;
    let tolerance_ms = 30;

    for (sleep_fn, sleep_name) in sleep_fns.iter() {
        for (time_fn, time_name) in time_fns.iter() {
            let start_time = time_fn();
            sleep_fn(sleep_duration_sec);
            let end_time = call_clock_gettime();

            let duration = end_time - start_time;
            println!(
                "{:>9}, {:>21} -- Duration: {} ms (sleep_duration {} ms, tolerance +/- {} ms)",
                sleep_name,
                time_name,
                duration.as_millis(),
                sleep_duration_sec * 1000,
                tolerance_ms
            );

            let duration_ms = i32::try_from(duration.as_millis()).unwrap();
            assert!(((sleep_duration_sec as i32) * 1000 - duration_ms).abs() < tolerance_ms);
        }
    }
}

fn sleep(seconds: u32) {
    let rv;
    unsafe {
        rv = libc::sleep(seconds);
    }
    assert_eq!(rv, 0);
}

fn usleep(seconds: u32) {
    let rv;
    unsafe {
        rv = libc::usleep(seconds * 1000000);
    }
    assert_eq!(rv, 0);
}

fn nanosleep(seconds: u32) {
    let stop = libc::timespec {
        tv_sec: seconds as i64,
        tv_nsec: 0,
    };
    let rv;
    unsafe {
        rv = libc::nanosleep(&stop, std::ptr::null_mut());
    }
    assert_eq!(rv, 0);
}

fn call_clock_gettime() -> std::time::Duration {
    let mut ts = libc::timespec {
        tv_sec: 0,
        tv_nsec: 0,
    };
    let rv;
    unsafe {
        rv = libc::clock_gettime(libc::CLOCK_MONOTONIC, &mut ts);
    }
    assert!(rv >= 0);
    // valid tv_nsec values are [0, 999999999], which fit in a u32
    return std::time::Duration::new(ts.tv_sec as u64, ts.tv_nsec as u32);
}

fn syscall_clock_gettime() -> std::time::Duration {
    let mut ts = libc::timespec {
        tv_sec: 0,
        tv_nsec: 0,
    };
    let rv;
    unsafe {
        rv = libc::syscall(libc::SYS_clock_gettime, libc::CLOCK_MONOTONIC, &mut ts);
    }
    assert!(rv >= 0);
    // valid tv_nsec values are [0, 999999999], which fit in a u32
    return std::time::Duration::new(ts.tv_sec as u64, ts.tv_nsec as u32);
}
