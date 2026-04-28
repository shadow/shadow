/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

fn main() {
    check_scheduler_state();
    check_scheduler_state_in_thread();
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
