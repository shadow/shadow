/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use nix::sched::{sched_getaffinity, sched_setaffinity, CpuSet};
use nix::unistd::Pid;

fn main() {
    get_affinity();
    set_affinity();
    println!("Success.");
}

fn get_affinity() {
    for pid in [Pid::from_raw(0), Pid::this()] {
        let cpu_set = sched_getaffinity(pid).unwrap();
        // on Linux this could in theory be false if the test are not allowed to schedule on every
        // core
        assert!(cpu_set.is_set(0).unwrap());
    }
}

fn set_affinity() {
    let mut cpu_set = CpuSet::new();
    cpu_set.set(0).unwrap();

    for pid in [Pid::from_raw(0), Pid::this()] {
        sched_setaffinity(pid, &cpu_set).unwrap();
        let new_cpu_set = sched_getaffinity(pid).unwrap();
        assert_eq!(new_cpu_set, cpu_set);
    }

    cpu_set.unset(0).unwrap();
    sched_setaffinity(Pid::from_raw(0), &cpu_set).unwrap_err();
}
