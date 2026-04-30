/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use std::sync::mpsc;

fn main() {
    // The calling thread should be able to operate on itself both through the special tid 0 and
    // through an explicit thread id.
    check_scheduler_state();

    // A thread should also be able to operate on itself after it has been created with pthreads.
    check_scheduler_state_in_thread();

    // One thread should also be able to read and update the tracked scheduler state of a sibling
    // thread in the same process.
    check_scheduler_state_across_threads();

    println!("Success.");
}

fn check_scheduler_state() {
    let tid = unsafe { libc::syscall(libc::SYS_gettid) as libc::pid_t };
    check_scheduler_state_for(0);
    check_scheduler_state_for(tid);
}

fn check_scheduler_state_in_thread() {
    std::thread::spawn(|| {
        let tid = unsafe { libc::syscall(libc::SYS_gettid) as libc::pid_t };
        check_scheduler_state_for(0);
        check_scheduler_state_for(tid);
    })
    .join()
    .unwrap();
}

fn check_scheduler_state_across_threads() {
    let (tid_sender, tid_receiver) = mpsc::channel();
    let (done_sender, done_receiver) = mpsc::channel();

    let thread = std::thread::spawn(move || {
        let tid = unsafe { libc::syscall(libc::SYS_gettid) as libc::pid_t };
        tid_sender.send(tid).unwrap();
        done_receiver.recv().unwrap();
    });

    let tid = tid_receiver.recv().unwrap();
    check_scheduler_state_for(tid);

    done_sender.send(()).unwrap();
    thread.join().unwrap();
}

fn check_scheduler_state_for(tid: libc::pid_t) {
    let mut param = libc::sched_param { sched_priority: -1 };

    let policy = unsafe { libc::sched_getscheduler(tid) };
    assert_ne!(policy, -1);

    let rv = unsafe { libc::sched_getparam(tid, &mut param) };
    assert_eq!(rv, 0);

    let rv = unsafe { libc::sched_setparam(tid, &param) };
    assert_eq!(rv, 0);

    let rv = unsafe { libc::sched_setscheduler(tid, policy, &param) };
    assert_eq!(rv, 0);

    let rv = unsafe { libc::sched_getparam(tid, std::ptr::null_mut()) };
    assert_eq!(rv, -1);
}
