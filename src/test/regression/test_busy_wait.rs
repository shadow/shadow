use core::arch::x86_64::_rdtsc as rdtsc;
use std::sync::Arc;
use std::sync::atomic::AtomicBool;
use std::time::{Duration, Instant};

// We've found several real-world examples where the process does a busy wait
// until either some timeout has passed, or work shows up to do. Having Shadow
// eventually move time forward even when the only thing being done is to check
// the time (and maybe make other non-blocking syscalls) prevents these examples
// from deadlocking.
//
// * the golang runtime (https://github.com/shadow/shadow/issues/1968)
// * openblas (https://github.com/shadow/shadow/issues/1792)
// * older versions of Curl (https://github.com/shadow/shadow/issues/1794#issuecomment-985909442)
fn test_wait_for_timeout() {
    let t0 = Instant::now();
    let target = t0 + Duration::from_millis(1);
    while Instant::now() < target {
        // wait
    }
}

// Same idea as `test_wait_for_timeout`, but for a loop that makes *no* syscalls, only checking the
// time directly via rdtsc. Regression test for
// https://github.com/shadow/shadow/discussions/2299#discussioncomment-3198368
fn test_wait_for_rdtsc_timeout() {
    let t0 = unsafe { rdtsc() };
    let target = t0 + 1000;
    while unsafe { rdtsc() } < target {
        // wait
    }
}

// Regression test for https://github.com/shadow/shadow/issues/2681
fn test_wait_for_other_thread() {
    let ready_flag = Arc::new(<AtomicBool>::new(false));
    let waiter = {
        let ready_flag = ready_flag.clone();
        std::thread::spawn(move || {
            while !ready_flag.load(std::sync::atomic::Ordering::Relaxed) {
                // Inline syscall, not going through libc.
                linux_api::sched::sched_yield().unwrap()
            }
        })
    };
    let waitee = std::thread::spawn(move || {
        std::thread::sleep(Duration::from_millis(1));
        ready_flag.store(true, std::sync::atomic::Ordering::Relaxed);
    });
    waiter.join().unwrap();
    waitee.join().unwrap();
}

fn main() {
    test_wait_for_timeout();
    test_wait_for_rdtsc_timeout();
    test_wait_for_other_thread();
}
