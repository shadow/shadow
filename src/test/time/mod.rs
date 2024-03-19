use super::*;

/// When we go to sleep, this is the tolerance we allow when checking that we slept the correct
/// amount of time (allows for imprecise kernel wakeups or thread preemption).
pub const SLEEP_TOLERANCE: Duration = Duration::from_millis(30);

/// When we make a sleep syscall that returns immediately, this is the tolerance we allow when
/// checking that we did not sleep (allows for syscall execution time).
pub const SYSCALL_EXEC_TOLERANCE: Duration = Duration::from_micros(500);

pub fn clock_now_duration(clockid: libc::clockid_t) -> anyhow::Result<Duration> {
    let now = clock_now_timespec(clockid)?;
    Ok(timespec_to_duration(now))
}

pub fn clock_now_timespec(clockid: libc::clockid_t) -> anyhow::Result<libc::timespec> {
    let mut now = libc::timespec {
        tv_sec: 0,
        tv_nsec: 0,
    };
    let rv = unsafe { libc::clock_gettime(clockid, &mut now) };
    ensure_ord!(rv, ==, 0);
    Ok(now)
}

pub fn timespec_to_duration(ts: libc::timespec) -> Duration {
    let secs = Duration::from_secs(ts.tv_sec.try_into().unwrap());
    let nanos = Duration::from_nanos(ts.tv_nsec.try_into().unwrap());
    secs + nanos
}

pub fn duration_to_timespec(dur: Duration) -> libc::timespec {
    libc::timespec {
        tv_sec: dur.as_secs().try_into().unwrap(),
        tv_nsec: dur.subsec_nanos().into(),
    }
}

pub fn duration_abs_diff(t1: Duration, t0: Duration) -> Duration {
    let res = t1.checked_sub(t0);
    match res {
        Some(d) => d,
        None => t0.checked_sub(t1).unwrap(),
    }
}
