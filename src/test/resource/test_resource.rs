use linux_api::{errno::Errno, resource::Resource};

// Returns a slightly smaller value than `rlim`, suitable for use in e.g.
// `linux_api::resource::rlimit`.  This is primarily to work around some subtle
// behavior around RLIM_INFINITY and rlim_cur sometimes being interpreted as a
// signed value.
//
// Requires that rlim_cur isn't 0.
fn decrement_rlim(rlim: u64) -> u64 {
    assert_ne!(rlim, 0);
    if rlim == linux_api::resource::RLIM_INFINITY {
        // RLIM_INFINITY is u64::MAX, but using values other than RLIM_INFINITY
        // that are > i64::MAX can lead to surprising behavior.
        // In particular, `RLIMIT_FSIZE` ends up getting interpreted as an
        // `off_t`, which is *signed*.
        i64::MAX as u64
    } else {
        rlim - 1
    }
}

trait GetRLimit: std::fmt::Debug {
    fn getrlimit(&self, resource: u32) -> Result<linux_api::resource::rlimit, Errno>;
}

#[derive(Debug)]
struct LibcGetRLimit;
impl GetRLimit for LibcGetRLimit {
    fn getrlimit(&self, resource: u32) -> Result<linux_api::resource::rlimit, Errno> {
        let mut lim = libc::rlimit {
            rlim_cur: 0,
            rlim_max: 0,
        };
        Errno::result_from_libc_errno(-1, unsafe { libc::getrlimit(resource, &mut lim) })?;
        Ok(linux_api::resource::rlimit {
            rlim_cur: lim.rlim_cur,
            rlim_max: lim.rlim_max,
        })
    }
}

#[derive(Debug)]
struct LibcGetRLimitViaPrLimit;
impl GetRLimit for LibcGetRLimitViaPrLimit {
    fn getrlimit(&self, resource: u32) -> Result<linux_api::resource::rlimit, Errno> {
        let mut lim = libc::rlimit {
            rlim_cur: 0,
            rlim_max: 0,
        };
        Errno::result_from_libc_errno(-1, unsafe {
            libc::prlimit(0, resource, std::ptr::null(), &mut lim)
        })?;
        Ok(linux_api::resource::rlimit {
            rlim_cur: lim.rlim_cur,
            rlim_max: lim.rlim_max,
        })
    }
}

#[derive(Debug)]
struct SysGetRLimit;
impl GetRLimit for SysGetRLimit {
    fn getrlimit(&self, resource: u32) -> Result<linux_api::resource::rlimit, Errno> {
        let mut lim = linux_api::resource::rlimit {
            rlim_cur: 0,
            rlim_max: 0,
        };
        Errno::result_from_libc_errno(-1, unsafe {
            libc::syscall(libc::SYS_getrlimit, resource, &mut lim)
        })?;
        Ok(lim)
    }
}

#[derive(Debug)]
struct SysGetRLimitViaPrLimit64;
impl GetRLimit for SysGetRLimitViaPrLimit64 {
    fn getrlimit(&self, resource: u32) -> Result<linux_api::resource::rlimit, Errno> {
        let mut lim = linux_api::resource::rlimit64 {
            rlim_cur: 0,
            rlim_max: 0,
        };
        unsafe { linux_api::resource::prlimit64_raw(0, resource, std::ptr::null(), &mut lim) }?;
        Ok(linux_api::resource::rlimit {
            rlim_cur: lim.rlim_cur,
            rlim_max: lim.rlim_max,
        })
    }
}

const GETRLIMITS: [&'static dyn GetRLimit; 4] = [
    &LibcGetRLimit,
    &LibcGetRLimitViaPrLimit,
    &SysGetRLimit,
    &SysGetRLimitViaPrLimit64,
];
const RESOURCES: [linux_api::resource::Resource; 16] = [
    Resource::RLIMIT_CPU,
    Resource::RLIMIT_FSIZE,
    Resource::RLIMIT_DATA,
    Resource::RLIMIT_STACK,
    Resource::RLIMIT_CORE,
    Resource::RLIMIT_RSS,
    Resource::RLIMIT_NPROC,
    Resource::RLIMIT_NOFILE,
    Resource::RLIMIT_MEMLOCK,
    Resource::RLIMIT_AS,
    Resource::RLIMIT_LOCKS,
    Resource::RLIMIT_SIGPENDING,
    Resource::RLIMIT_MSGQUEUE,
    Resource::RLIMIT_NICE,
    Resource::RLIMIT_RTPRIO,
    Resource::RLIMIT_RTTIME,
];

fn test_getrlimits_every_resource() {
    for resource in RESOURCES {
        println!("resource: {resource:?}");
        let baseline = LibcGetRLimit.getrlimit(resource.into()).unwrap();
        println!("  baseline: {baseline:?}");
        assert!(baseline.rlim_cur <= baseline.rlim_max);
        for g in GETRLIMITS {
            println!("  testing fn: {g:?}");
            assert_eq!(g.getrlimit(resource.into()).unwrap(), baseline);
        }
    }
}

fn test_getrlimits_errors() {
    for g in GETRLIMITS {
        println!("  testing fn: {g:?}");
        assert_eq!(g.getrlimit(1234), Err(Errno::EINVAL));
    }
}

trait SetRLimit: std::fmt::Debug {
    fn setrlimit(&self, resource: u32, lim: &linux_api::resource::rlimit) -> Result<(), Errno>;
}

#[derive(Debug)]
struct LibcSetRLimit;
impl SetRLimit for LibcSetRLimit {
    fn setrlimit(&self, resource: u32, lim: &linux_api::resource::rlimit) -> Result<(), Errno> {
        let lim = libc::rlimit {
            rlim_cur: lim.rlim_cur,
            rlim_max: lim.rlim_max,
        };
        Errno::result_from_libc_errno(-1, unsafe { libc::setrlimit(resource, &lim) })?;
        Ok(())
    }
}

#[derive(Debug)]
struct LibcSetRLimitViaPrLimit;
impl SetRLimit for LibcSetRLimitViaPrLimit {
    fn setrlimit(&self, resource: u32, lim: &linux_api::resource::rlimit) -> Result<(), Errno> {
        let lim = libc::rlimit {
            rlim_cur: lim.rlim_cur,
            rlim_max: lim.rlim_max,
        };
        Errno::result_from_libc_errno(-1, unsafe {
            libc::prlimit(0, resource, &lim, std::ptr::null_mut())
        })?;
        Ok(())
    }
}

#[derive(Debug)]
struct SysSetRLimit;
impl SetRLimit for SysSetRLimit {
    fn setrlimit(&self, resource: u32, lim: &linux_api::resource::rlimit) -> Result<(), Errno> {
        Errno::result_from_libc_errno(-1, unsafe {
            libc::syscall(libc::SYS_setrlimit, resource, lim)
        })?;
        Ok(())
    }
}

#[derive(Debug)]
struct SysSetRLimitViaPrLimit64;
impl SetRLimit for SysSetRLimitViaPrLimit64 {
    fn setrlimit(&self, resource: u32, lim: &linux_api::resource::rlimit) -> Result<(), Errno> {
        let lim = linux_api::resource::rlimit64 {
            rlim_cur: lim.rlim_cur,
            rlim_max: lim.rlim_max,
        };
        unsafe { linux_api::resource::prlimit64_raw(0, resource, &lim, core::ptr::null_mut()) }
    }
}

const SETRLIMITS: [&'static dyn SetRLimit; 4] = [
    &LibcSetRLimit,
    &LibcSetRLimitViaPrLimit,
    &SysSetRLimit,
    &SysSetRLimitViaPrLimit64,
];

fn test_setrlimits_every_resource() {
    for resource in RESOURCES {
        let initial = LibcGetRLimit.getrlimit(resource.into()).unwrap();
        println!("resource: {resource:?}: {initial:?}");
        for g in SETRLIMITS {
            println!("  testing fn: {g:?}");
            // Setting to the same value again should be a no-op and succeed.
            g.setrlimit(resource.into(), &initial).unwrap();
            assert_eq!(LibcGetRLimit.getrlimit(resource.into()).unwrap(), initial);

            if initial.rlim_cur < initial.rlim_max {
                println!("  incrementing");
                let new_val = linux_api::resource::rlimit {
                    rlim_cur: initial.rlim_cur + 1,
                    rlim_max: initial.rlim_max,
                };
                g.setrlimit(resource.into(), &new_val).unwrap();
                assert_eq!(LibcGetRLimit.getrlimit(resource.into()).unwrap(), new_val);

                g.setrlimit(resource.into(), &initial).unwrap();
            } else {
                println!("  not incrementing; already at max");
            }

            if initial.rlim_cur > 0 {
                println!("  decrementing cur");
                let new_val = linux_api::resource::rlimit {
                    rlim_cur: decrement_rlim(initial.rlim_cur),
                    rlim_max: initial.rlim_max,
                };
                g.setrlimit(resource.into(), &new_val).unwrap();
                assert_eq!(LibcGetRLimit.getrlimit(resource.into()).unwrap(), new_val);

                g.setrlimit(resource.into(), &initial).unwrap();
            } else {
                println!("  not decrementing cur; already at 0");
            }

            // Not tested:
            // * increasing rlim_max: natively, this only works if we're running
            // with CAP_SYS_RESOURCE or root; we'd have to either probe for
            // whether this was the case, or only support running one way or the
            // other.  Likewise shadow would try to natively increase the
            // maximum in the handler, which would again depend on permissions.
            // * decreasing rlim_max: we *could* test this, but wouldn't be able
            // to reliably restore it again as above. Since we're testing
            // multiple functions for each resource, we'd have to decrease it
            // several times without restoring it. Seems too fragile to be
            // worthwhile.
        }
    }
}

fn test_setrlimits_errors() {
    for g in SETRLIMITS {
        println!("  testing fn: {g:?}");

        // Bad resource
        assert_eq!(
            g.setrlimit(
                1234,
                &linux_api::resource::rlimit {
                    rlim_cur: 0,
                    rlim_max: 0
                }
            ),
            Err(Errno::EINVAL)
        );

        // reducing rlim_max below rlim_cur
        let cur = LibcGetRLimit
            .getrlimit(linux_api::resource::Resource::RLIMIT_NOFILE.into())
            .unwrap();
        // Can't decrement `rlim_cur` if it's already 0, but `RLIMIT_NOFILE`
        // shouldn't be 0.
        assert!(cur.rlim_cur > 0);
        assert_eq!(
            g.setrlimit(
                linux_api::resource::Resource::RLIMIT_NOFILE.into(),
                &linux_api::resource::rlimit {
                    rlim_cur: cur.rlim_cur,
                    rlim_max: decrement_rlim(cur.rlim_cur),
                }
            ),
            Err(Errno::EINVAL)
        );

        // not tested:
        // * increasing rlim_max. This would only error if we don't have CAP_SYS_RESOURCE or root.
    }
}

// Get and set an rlimit for another process. (This can't be done with getrlimit and setrlimit)
fn test_prlimit_child() {
    let res = unsafe { libc::fork() };
    match res.cmp(&0) {
        std::cmp::Ordering::Equal => {
            // child
            std::thread::sleep(std::time::Duration::from_secs(1));
            std::process::exit(0);
        }
        std::cmp::Ordering::Greater => {
            // parent
            let child_pid = linux_api::posix_types::Pid::from_raw(res).unwrap();
            println!("got child pid {child_pid:?}");
            let resource = linux_api::resource::Resource::RLIMIT_FSIZE;

            let mut initial_rlim = linux_api::resource::rlimit64 {
                rlim_cur: 0,
                rlim_max: 0,
            };
            unsafe {
                linux_api::resource::prlimit64(child_pid, resource, None, Some(&mut initial_rlim))
            }
            .unwrap();
            println!("got child rlimit {initial_rlim:?}");

            let new_rlim = linux_api::resource::rlimit64 {
                rlim_cur: decrement_rlim(initial_rlim.rlim_cur),
                rlim_max: initial_rlim.rlim_max,
            };
            unsafe { linux_api::resource::prlimit64(child_pid, resource, Some(&new_rlim), None) }
                .unwrap();

            let child_status = rustix::process::waitpid(
                Some(rustix::process::Pid::from_raw(child_pid.as_raw_nonzero().get()).unwrap()),
                rustix::process::WaitOptions::empty(),
            )
            .unwrap();
            assert_eq!(child_status.unwrap().exit_status(), Some(0));
        }
        std::cmp::Ordering::Less => {
            panic!("fork failed: {:?}", Errno::from_libc_errno());
        }
    }
}

// This is essentially a regression test for
// <https://github.com/shadow/shadow/issues/3681>.
fn test_set_fsize_zero() {
    let initial = LibcGetRLimit
        .getrlimit(linux_api::resource::Resource::RLIMIT_FSIZE.into())
        .unwrap();
    // Set file-write limit to 0.
    // This should be ok as long as we don't try writing to any files while this is set.
    LibcSetRLimit.setrlimit(linux_api::resource::Resource::RLIMIT_FSIZE.into(), &linux_api::resource::rlimit{ rlim_cur: 0, rlim_max: initial.rlim_max }).unwrap();

    // Do some syscall that should be handled shim-side, causing the shim
    // to try writing to the strace log file.
    linux_api::time::clock_gettime(linux_api::time::ClockId::CLOCK_REALTIME).unwrap();

    // Restore the normal limit.
    LibcSetRLimit.setrlimit(linux_api::resource::Resource::RLIMIT_FSIZE.into(),&initial).unwrap();
}

fn main() {
    test_getrlimits_every_resource();
    test_getrlimits_errors();

    test_setrlimits_every_resource();
    test_setrlimits_errors();

    test_prlimit_child();

    test_set_fsize_zero();
}
