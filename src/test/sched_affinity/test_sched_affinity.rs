/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use nix::sched::{CpuSet, sched_getaffinity, sched_setaffinity};
use nix::unistd::Pid;

fn main() {
    let shadow_passing = std::env::args().any(|x| x == "--shadow-passing");

    get_affinity(shadow_passing);
    set_affinity();
    sysconf(shadow_passing);
    println!("Success.");
}

fn get_affinity(shadow: bool) {
    for pid in [Pid::from_raw(0), Pid::this()] {
        let cpu_set = sched_getaffinity(pid).unwrap();
        // on Linux this could in theory be false if the test are not allowed to schedule on some
        // cores
        assert!(cpu_set.is_set(0).unwrap());
        if shadow {
            assert_eq!(cpu_set_count(&cpu_set), 1);
        }
    }
    assert_eq!(
        unsafe { libc::sched_getaffinity(0, 0, std::ptr::null_mut()) },
        -1,
    );
    assert_eq!(test_utils::get_errno(), libc::EINVAL,);
}

fn cpu_set_count(cpu_set: &CpuSet) -> usize {
    (0..CpuSet::count())
        .filter(|index| cpu_set.is_set(*index).unwrap())
        .count()
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
    assert_eq!(
        unsafe { libc::sched_setaffinity(0, 0, std::ptr::null()) },
        -1
    );
    assert_eq!(test_utils::get_errno(), libc::EINVAL);
}

fn sysconf(shadow: bool) {
    let online = nix::unistd::sysconf(nix::unistd::SysconfVar::_NPROCESSORS_ONLN)
        .unwrap()
        .unwrap();
    let _configured = nix::unistd::sysconf(nix::unistd::SysconfVar::_NPROCESSORS_CONF)
        .unwrap()
        .unwrap();
    if shadow {
        assert_eq!(online, 1);
        // TODO this works only on some linux depending on where sysconf looks at.
        // cat /sys/devices/system/cpu/possible ok
        // ls /sys/devices/system/cpu ko
        // cat /proc/stat ko
        // others?
        //assert_eq!(configured, 1);
    }
}
