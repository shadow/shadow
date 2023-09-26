use std::collections::HashSet;
use std::error::Error;
use std::ffi::{CStr, CString};
use std::os::unix::prelude::OsStrExt;
use std::path::Path;
use std::sync::Arc;

use libc::{c_int, c_void, siginfo_t, CLD_EXITED};
use linux_api::errno::Errno;
use linux_api::posix_types::Pid;
use linux_api::sched::{CloneFlags, CloneResult};
use linux_api::signal::{LinuxDefaultAction, Signal};
use nix::sys::signal::{SaFlags, SigAction, SigHandler, SigmaskHow};
use nix::sys::signalfd::SigSet;
use rustix::fd::{AsFd, AsRawFd};
use test_utils::{ensure_ord, running_in_shadow, TestEnvironment as TestEnv};
use test_utils::{set, ShadowTest};

fn execv_argvec(args: &[impl AsRef<CStr>]) -> Vec<*const i8> {
    args.iter()
        .map(|c| c.as_ref().as_ptr())
        .chain(std::iter::once(std::ptr::null()))
        .collect()
}

fn fork_via_clone_syscall() -> Result<CloneResult, Errno> {
    let flags = CloneFlags::empty();
    unsafe {
        linux_api::sched::clone(
            flags,
            Some(Signal::SIGCHLD),
            core::ptr::null_mut(),
            core::ptr::null_mut(),
            core::ptr::null_mut(),
            core::ptr::null_mut(),
        )
    }
}

fn fork_via_fork_syscall() -> Result<CloneResult, Errno> {
    unsafe { linux_api::sched::fork() }
}

fn fork_via_libc() -> Result<CloneResult, Errno> {
    let res = unsafe { libc::fork() };
    match res.cmp(&0) {
        std::cmp::Ordering::Equal => Ok(CloneResult::CallerIsChild),
        std::cmp::Ordering::Greater => Ok(CloneResult::CallerIsParent(Pid::from_raw(res).unwrap())),
        std::cmp::Ordering::Less => {
            Err(Errno::try_from(unsafe { *libc::__errno_location() }).unwrap())
        }
    }
}

fn test_fork_runs(fork_fn: impl FnOnce() -> Result<CloneResult, Errno>) -> anyhow::Result<()> {
    let (reader, writer) = rustix::pipe::pipe().unwrap();

    let res = fork_fn()?;

    match res {
        CloneResult::CallerIsChild => {
            assert_eq!(rustix::io::write(&writer, &[42]), Ok(1));
            linux_api::exit::exit_group(0);
        }
        CloneResult::CallerIsParent(_pid) => (),
    };

    let mut buf = [0];
    assert_eq!(rustix::io::read(&reader, &mut buf), Ok(1));
    assert_eq!(buf[0], 42);

    Ok(())
}

fn test_clone_parent(set_clone_parent: bool) -> anyhow::Result<()> {
    let (reader, writer) = rustix::pipe::pipe().unwrap();

    let flags = if set_clone_parent {
        CloneFlags::CLONE_PARENT
    } else {
        CloneFlags::empty()
    };

    let parent_pid = unsafe { libc::getpid() };
    let parent_ppid = unsafe { libc::getppid() };

    let clone_res = unsafe {
        linux_api::sched::clone(
            flags,
            Some(Signal::SIGCHLD),
            core::ptr::null_mut(),
            core::ptr::null_mut(),
            core::ptr::null_mut(),
            core::ptr::null_mut(),
        )
    }
    .unwrap();
    match clone_res {
        CloneResult::CallerIsChild => {
            // Ensure we exit with non-zero exit code on panic.
            std::panic::set_hook(Box::new(|info| {
                eprintln!("panic: {info:?}");
                unsafe { libc::exit(1) };
            }));
            let expected_ppid = if set_clone_parent {
                parent_ppid
            } else {
                parent_pid
            };
            assert_eq!(unsafe { libc::getppid() }, expected_ppid);

            assert_eq!(rustix::io::write(&writer, &[0]), Ok(1));
            unsafe { libc::exit(0) };
        }
        CloneResult::CallerIsParent(_) => (),
    };

    // Close our copy of the writer-end of the pipe, so that parent doesn't hang
    // trying to read from the pipe if the child exited abnormally.
    drop(writer);

    // Because waitpid isn't implemented yet, we get the "exit code"
    // from a pipe.
    // TODO: once waitpid is implemented, use that instead.
    let mut exit_code = [0xff_u8];
    assert_eq!(rustix::io::read(&reader, &mut exit_code), Ok(1));
    assert_eq!(exit_code[0], 0);

    Ok(())
}

fn test_child_change_session() -> anyhow::Result<()> {
    let (reader, writer) = rustix::pipe::pipe().unwrap();

    let parent_sid = unsafe { libc::getsid(0) };
    let parent_pgrp = unsafe { libc::getpgrp() };

    let clone_res = unsafe { linux_api::sched::fork() }.unwrap();
    let child_pid = match clone_res {
        CloneResult::CallerIsChild => {
            // Ensure we exit with non-zero exit code on panic.
            std::panic::set_hook(Box::new(|info| {
                eprintln!("panic: {info:?}");
                unsafe { libc::exit(1) };
            }));

            // Should initially be in same session and group as parent.
            assert_eq!(unsafe { libc::getpgrp() }, parent_pgrp);
            assert_eq!(unsafe { libc::getsid(0) }, parent_sid);

            // Change session
            let pid = unsafe { libc::getpid() };
            assert_eq!(unsafe { libc::setsid() }, pid);

            // Should now be session and group leader.
            assert_eq!(unsafe { libc::getpgrp() }, pid);
            assert_eq!(unsafe { libc::getsid(0) }, pid);

            assert_eq!(rustix::io::write(&writer, &[0]), Ok(1));
            unsafe { libc::exit(0) };
        }
        CloneResult::CallerIsParent(child_pid) => child_pid,
    };

    // Close our copy of the writer-end of the pipe, so that parent doesn't hang
    // trying to read from the pipe if the child exited abnormally.
    drop(writer);

    // Because waitpid isn't implemented yet, we get the "exit code"
    // from a pipe.
    // TODO: once waitpid is implemented, use that instead.
    let mut exit_code = [0xff_u8];
    assert_eq!(rustix::io::read(&reader, &mut exit_code), Ok(1));
    assert_eq!(exit_code[0], 0);

    let child_pid_c: libc::pid_t = child_pid.as_raw_nonzero().into();
    assert_eq!(unsafe { libc::getpgid(child_pid_c) }, child_pid_c);
    assert_eq!(unsafe { libc::getsid(child_pid_c) }, child_pid_c);

    Ok(())
}

fn test_child_change_group() -> anyhow::Result<()> {
    let (reader, writer) = rustix::pipe::pipe().unwrap();

    let parent_sid = unsafe { libc::getsid(0) };
    let parent_pgrp = unsafe { libc::getpgrp() };

    let clone_res = unsafe { linux_api::sched::fork() }.unwrap();
    let child_pid = match clone_res {
        CloneResult::CallerIsChild => {
            // Ensure we exit with non-zero exit code on panic.
            std::panic::set_hook(Box::new(|info| {
                eprintln!("panic: {info:?}");
                unsafe { libc::exit(1) };
            }));

            // Should initially be in same session and group as parent.
            assert_eq!(unsafe { libc::getpgrp() }, parent_pgrp);
            assert_eq!(unsafe { libc::getsid(0) }, parent_sid);

            // Change group
            assert_eq!(unsafe { libc::setpgid(0, 0) }, 0);

            // Should now be group leader.
            let pid = unsafe { libc::getpid() };
            assert_eq!(unsafe { libc::getpgrp() }, pid);

            // Should still be in parent's session
            assert_eq!(unsafe { libc::getsid(0) }, parent_sid);

            assert_eq!(rustix::io::write(&writer, &[0]), Ok(1));
            unsafe { libc::exit(0) };
        }
        CloneResult::CallerIsParent(child_pid) => child_pid,
    };

    // Close our copy of the writer-end of the pipe, so that parent doesn't hang
    // trying to read from the pipe if the child exited abnormally.
    drop(writer);

    // Because waitpid isn't implemented yet, we get the "exit code"
    // from a pipe.
    // TODO: once waitpid is implemented, use that instead.
    let mut exit_code = [0xff_u8];
    assert_eq!(rustix::io::read(&reader, &mut exit_code), Ok(1));
    assert_eq!(exit_code[0], 0);

    let child_pid_c: libc::pid_t = child_pid.as_raw_nonzero().into();
    assert_eq!(unsafe { libc::getpgid(child_pid_c) }, child_pid_c);
    assert_eq!(unsafe { libc::getsid(child_pid_c) }, parent_sid);

    Ok(())
}

/// Helper to run the given test function in a child process. This is helpful to
/// avoid cross-test interference. e.g. `f` can manipulate signal handlers and
/// masks without having to restore them, and will only have child processes
/// that it spawns itself.
///
/// Returns `Ok(())` if `f` completes without panicking, and an error if `f`
/// panicked.
fn run_test_in_subprocess(f: impl FnOnce()) -> anyhow::Result<()> {
    let (reader, writer) = rustix::pipe::pipe().unwrap();
    let clone_res = unsafe { linux_api::sched::fork() }.unwrap();
    let _child_pid = match clone_res {
        CloneResult::CallerIsChild => {
            // Ensure we exit with non-zero exit code on panic.
            std::panic::set_hook(Box::new(|info| {
                eprintln!("panic: {info:?}");
                unsafe { libc::exit(1) };
            }));
            f();
            assert_eq!(rustix::io::write(&writer, &[0]), Ok(1));
            unsafe { libc::exit(0) };
        }
        CloneResult::CallerIsParent(child_pid) => child_pid,
    };

    // Close our copy of the writer-end of the pipe, so that parent doesn't hang trying
    // to read from the pipe if the child exited abnormally.
    drop(writer);

    // Because waitpid isn't implemented yet, we get the "exit code"
    // from a pipe.
    // TODO: once waitpid is implemented, use that instead.
    let mut exit_code = [0xff_u8];
    ensure_ord!(rustix::io::read(&reader, &mut exit_code), ==, Ok(1));
    ensure_ord!(exit_code[0], ==, 0);

    Ok(())
}

fn test_exit_signal_normal_exit(exit_signal: nix::sys::signal::Signal) -> anyhow::Result<()> {
    run_test_in_subprocess(|| {
        static mut SIGNO: c_int = 0;
        static mut INFO: Option<siginfo_t> = None;
        extern "C" fn sigchld_handler(signo: c_int, info: *mut siginfo_t, _ctx: *mut c_void) {
            unsafe { SIGNO = signo };
            unsafe { INFO = Some(*info) };
        }
        unsafe {
            nix::sys::signal::sigaction(
                exit_signal,
                &SigAction::new(
                    SigHandler::SigAction(sigchld_handler),
                    SaFlags::SA_SIGINFO,
                    SigSet::empty(),
                ),
            )
        }
        .unwrap();

        let mut sigset = SigSet::empty();
        sigset.add(exit_signal);
        nix::sys::signal::sigprocmask(SigmaskHow::SIG_UNBLOCK, Some(&sigset), None).unwrap();

        const CHILD_EXIT_STATUS: i32 = 42;
        let clone_res = unsafe {
            linux_api::sched::clone(
                CloneFlags::empty(),
                Some(Signal::try_from(exit_signal as i32).unwrap()),
                core::ptr::null_mut(),
                core::ptr::null_mut(),
                core::ptr::null_mut(),
                core::ptr::null_mut(),
            )
        }
        .unwrap();
        let child_pid = match clone_res {
            CloneResult::CallerIsChild => {
                unsafe { libc::exit(CHILD_EXIT_STATUS) };
            }
            CloneResult::CallerIsParent(child_pid) => child_pid,
        };

        match rustix::thread::nanosleep(&rustix::fs::Timespec {
            tv_sec: 2,
            tv_nsec: 0,
        }) {
            rustix::thread::NanosleepRelativeResult::Interrupted(_) => (),
            other => panic!("Unexpected nanosleep result: {other:?}"),
        }

        let signo = unsafe { SIGNO };
        let info = unsafe { INFO.unwrap() };
        assert_eq!(nix::sys::signal::Signal::try_from(signo), Ok(exit_signal));
        assert_eq!(info.si_signo, signo);
        assert_eq!(info.si_errno, 0);
        assert_eq!(info.si_code, CLD_EXITED);
        assert_eq!(unsafe { info.si_pid() }, child_pid.as_raw_nonzero().get());
        assert_eq!(unsafe { info.si_status() }, CHILD_EXIT_STATUS);
    })
}

fn test_exit_signal_with_fatal_signal(exit_signal: nix::sys::signal::Signal) -> anyhow::Result<()> {
    run_test_in_subprocess(|| {
        static mut SIGNO: c_int = 0;
        static mut INFO: Option<siginfo_t> = None;
        extern "C" fn sigchld_handler(signo: c_int, info: *mut siginfo_t, _ctx: *mut c_void) {
            unsafe { SIGNO = signo };
            unsafe { INFO = Some(*info) };
        }
        unsafe {
            nix::sys::signal::sigaction(
                exit_signal,
                &SigAction::new(
                    SigHandler::SigAction(sigchld_handler),
                    SaFlags::SA_SIGINFO,
                    SigSet::empty(),
                ),
            )
        }
        .unwrap();

        let mut sigset = SigSet::empty();
        sigset.add(exit_signal);
        nix::sys::signal::sigprocmask(SigmaskHow::SIG_UNBLOCK, Some(&sigset), None).unwrap();

        let clone_res = unsafe {
            linux_api::sched::clone(
                CloneFlags::empty(),
                Some(Signal::try_from(exit_signal as i32).unwrap()),
                core::ptr::null_mut(),
                core::ptr::null_mut(),
                core::ptr::null_mut(),
                core::ptr::null_mut(),
            )
        }
        .unwrap();
        let child_pid = match clone_res {
            CloneResult::CallerIsChild => {
                unsafe { libc::kill(libc::getpid(), libc::SIGKILL) };
                unreachable!()
            }
            CloneResult::CallerIsParent(child_pid) => child_pid,
        };

        match rustix::thread::nanosleep(&rustix::fs::Timespec {
            tv_sec: 2,
            tv_nsec: 0,
        }) {
            rustix::thread::NanosleepRelativeResult::Interrupted(_) => (),
            other => panic!("Unexpected nanosleep result: {other:?}"),
        }

        let signo = unsafe { SIGNO };
        let info = unsafe { INFO.unwrap() };
        assert_eq!(nix::sys::signal::Signal::try_from(signo), Ok(exit_signal));
        assert_eq!(info.si_signo, signo);
        assert_eq!(info.si_errno, 0);
        assert_eq!(info.si_code, libc::CLD_KILLED);
        assert_eq!(unsafe { info.si_pid() }, child_pid.as_raw_nonzero().get());
        assert_eq!(unsafe { info.si_status() }, libc::SIGKILL);
    })
}

/// Validate that `waitfn` reaps a zombie child process, in the case that the
/// current process has a single child that has exited but not yet been reaped.
fn test_waitfn_reaps(waitfn: impl FnOnce()) -> anyhow::Result<()> {
    run_test_in_subprocess(|| {
        let clone_res = unsafe { linux_api::sched::fork() }.unwrap();
        let child_pid = match clone_res {
            CloneResult::CallerIsChild => {
                unsafe { libc::exit(0) };
            }
            CloneResult::CallerIsParent(child_pid) => child_pid,
        };

        // Let the child run and exit
        match rustix::thread::nanosleep(&rustix::fs::Timespec {
            tv_sec: 0,
            tv_nsec: 100_000_000,
        }) {
            rustix::thread::NanosleepRelativeResult::Ok => (),
            other => panic!("Unexpected nanosleep result: {other:?}"),
        }

        // Child should still exist, as a zombie.
        assert_eq!(linux_api::signal::kill_process(child_pid, None), Ok(()));

        waitfn();

        // Child should no longer exist.
        assert_eq!(
            linux_api::signal::kill_process(child_pid, None),
            Err(Errno::ESRCH)
        );
    })
}

/// Validate that `waitfn` only targets children of the current process.
fn test_waitfn_ignores_non_children(waitfn: impl Fn() -> Option<Pid>) -> anyhow::Result<()> {
    run_test_in_subprocess(|| {
        let child_pid = match unsafe { linux_api::sched::fork() }.unwrap() {
            CloneResult::CallerIsChild => {
                match unsafe { linux_api::sched::fork() }.unwrap() {
                    CloneResult::CallerIsChild => {
                        // This is *grandchild* to the main test process.
                        unsafe { libc::exit(0) };
                    }
                    CloneResult::CallerIsParent(_) =>
                    // Stay alive for a bit after the grandchild exits.
                    {
                        match rustix::thread::nanosleep(&rustix::fs::Timespec {
                            tv_sec: 0,
                            tv_nsec: 10_000_000,
                        }) {
                            rustix::thread::NanosleepRelativeResult::Ok => (),
                            other => panic!("Unexpected nanosleep result: {other:?}"),
                        }
                    }
                };
                unsafe { libc::exit(0) };
            }
            CloneResult::CallerIsParent(child_pid) => child_pid,
        };

        // First call should reap the child process.
        assert_eq!(waitfn(), Some(child_pid));

        // Subsequent calls shouldn't find any eligible children.
        assert_eq!(waitfn(), None);
    })
}

/// Validate that `waitfn` waits for a child with the given Pid, and only for
/// that child.
fn test_waitfn_selects_by_pid(waitfn: impl Fn(Pid) -> Option<Pid>) -> anyhow::Result<()> {
    run_test_in_subprocess(|| {
        let child1 = match unsafe { linux_api::sched::fork() }.unwrap() {
            CloneResult::CallerIsChild => {
                unsafe { libc::exit(0) };
            }
            CloneResult::CallerIsParent(child_pid) => child_pid,
        };
        let child2 = match unsafe { linux_api::sched::fork() }.unwrap() {
            CloneResult::CallerIsChild => {
                unsafe { libc::exit(0) };
            }
            CloneResult::CallerIsParent(child_pid) => child_pid,
        };
        let child3 = match unsafe { linux_api::sched::fork() }.unwrap() {
            CloneResult::CallerIsChild => {
                unsafe { libc::exit(0) };
            }
            CloneResult::CallerIsParent(child_pid) => child_pid,
        };

        assert_eq!(waitfn(child2), Some(child2));
        assert_eq!(waitfn(child1), Some(child1));
        assert_eq!(waitfn(child3), Some(child3));
    })
}

/// Validate that `waitfn` waits for a child with the given process group ID,
/// and only for such children.
fn test_waitfn_selects_by_pgid(waitfn: impl Fn(Pid) -> Option<Pid>) -> anyhow::Result<()> {
    run_test_in_subprocess(|| {
        // Put ourselves in our own process group to avoid interference with other tests.
        unsafe { libc::setpgid(0, 0) };

        let child1 = match unsafe { linux_api::sched::fork() }.unwrap() {
            CloneResult::CallerIsChild => {
                unsafe { libc::exit(0) };
            }
            CloneResult::CallerIsParent(child_pid) => child_pid,
        };

        // Give child2 its own pgid.
        // We set it both from the parent and child to ensure that we don't
        // wait before the pgid has been changed.
        let child2 = match unsafe { linux_api::sched::fork() }.unwrap() {
            CloneResult::CallerIsChild => {
                unsafe { libc::setpgid(0, 0) };
                unsafe { libc::exit(0) };
            }
            CloneResult::CallerIsParent(child_pid) => {
                unsafe { libc::setpgid(child_pid.as_raw_nonzero().get(), 0) };
                child_pid
            }
        };

        let child3 = match unsafe { linux_api::sched::fork() }.unwrap() {
            CloneResult::CallerIsChild => {
                unsafe { libc::exit(0) };
            }
            CloneResult::CallerIsParent(child_pid) => child_pid,
        };

        // child2's pgid is equal to its pid.
        let child2_pgid = child2;
        assert_eq!(waitfn(child2_pgid), Some(child2));
        // No more children in that process group.
        assert_eq!(waitfn(child2_pgid), None);

        // The other children have the same process group as us.
        let pgid = Pid::from_raw(unsafe { libc::getpgid(0) }).unwrap();
        assert_eq!(
            HashSet::from([waitfn(pgid), waitfn(pgid)]),
            HashSet::from([Some(child1), Some(child3)])
        );
        // No more children
        assert_eq!(waitfn(child2_pgid), None);
    })
}

/// Validate that `waitfn` waits for a child with the given process group ID (pgid),
/// and only for such children.
fn test_waitfn_selects_by_self_pgid(waitfn: impl Fn() -> Option<Pid>) -> anyhow::Result<()> {
    run_test_in_subprocess(|| {
        // Put ourselves in our own process group to avoid interference with other tests.
        unsafe { libc::setpgid(0, 0) };

        let child1 = match unsafe { linux_api::sched::fork() }.unwrap() {
            CloneResult::CallerIsChild => {
                unsafe { libc::exit(0) };
            }
            CloneResult::CallerIsParent(child_pid) => child_pid,
        };

        // Give child2 its own pgid.
        // We set it both from the parent and child to ensure that we don't
        // wait before the pgid has been changed.
        let _child2 = match unsafe { linux_api::sched::fork() }.unwrap() {
            CloneResult::CallerIsChild => {
                unsafe { libc::setpgid(0, 0) };
                unsafe { libc::exit(0) };
            }
            CloneResult::CallerIsParent(child_pid) => {
                unsafe { libc::setpgid(child_pid.as_raw_nonzero().get(), 0) };
                child_pid
            }
        };

        let child3 = match unsafe { linux_api::sched::fork() }.unwrap() {
            CloneResult::CallerIsChild => {
                unsafe { libc::exit(0) };
            }
            CloneResult::CallerIsParent(child_pid) => child_pid,
        };

        // The other children have the same process group as us.
        assert_eq!(
            HashSet::from([waitfn(), waitfn()]),
            HashSet::from([Some(child1), Some(child3)])
        );
        // No more children in our process group.
        assert_eq!(waitfn(), None);
    })
}

/// Validate that `waitfn` returns without blocking if WNOHANG is provided in
/// its options parameter.
fn test_waitfn_honors_wnohang(
    waitfn: impl FnOnce(/*options*/ i32) -> Result<i32, nix::errno::Errno>,
) -> anyhow::Result<()> {
    run_test_in_subprocess(|| {
        let clone_res = unsafe { linux_api::sched::fork() }.unwrap();
        let child_pid = match clone_res {
            CloneResult::CallerIsChild => {
                // Sleep forever
                match rustix::thread::nanosleep(&rustix::fs::Timespec {
                    tv_sec: i64::MAX,
                    tv_nsec: 0,
                }) {
                    rustix::thread::NanosleepRelativeResult::Ok => (),
                    other => panic!("Unexpected nanosleep result: {other:?}"),
                }
                unsafe { libc::exit(0) };
            }
            CloneResult::CallerIsParent(child_pid) => child_pid,
        };

        assert_eq!(waitfn(libc::WNOHANG), Ok(0));

        // Don't leave child process alive. No need to also reap it; it'll get cleaned up
        // when its parent exits.
        assert_eq!(
            linux_api::signal::kill_process(child_pid, Some(Signal::SIGKILL)),
            Ok(())
        );
    })
}

/// Validate that `waitfn` does not reap if WNOWAIT is provided in its options
/// parameter.
fn test_waitfn_honors_wnowait(
    waitfn: impl Fn(/*options*/ i32) -> Option<Pid>,
) -> anyhow::Result<()> {
    run_test_in_subprocess(|| {
        let clone_res = unsafe { linux_api::sched::fork() }.unwrap();
        let child_pid = match clone_res {
            CloneResult::CallerIsChild => {
                unsafe { libc::exit(0) };
            }
            CloneResult::CallerIsParent(child_pid) => child_pid,
        };

        // Should return the dead child, but not reap it.
        assert_eq!(waitfn(libc::WNOWAIT), Some(child_pid));
        // Should return the dead child and reap it.
        assert_eq!(waitfn(0), Some(child_pid));
        // Should be no eligible children left.
        assert_eq!(waitfn(0), None);
    })
}

/// Validate that `waitfn` waits only for "clone children" if __WCLONE is
/// provided in its options parameter. (See `waitid(2)`).
fn test_waitfn_honors_wclone(
    use_wclone: bool,
    waitfn: impl Fn(/*options*/ i32) -> Option<Pid>,
) -> anyhow::Result<()> {
    run_test_in_subprocess(|| {
        // `waitid(2)`: A "clone" child is  one  which delivers  no signal, or a
        // signal other than SIGCHLD to its parent upon termination.

        let clone_child_no_signal = {
            let res = unsafe {
                linux_api::sched::clone(
                    CloneFlags::empty(),
                    None,
                    core::ptr::null_mut(),
                    core::ptr::null_mut(),
                    core::ptr::null_mut(),
                    core::ptr::null_mut(),
                )
            }
            .unwrap();
            match res {
                CloneResult::CallerIsChild => {
                    unsafe { libc::exit(0) };
                }
                CloneResult::CallerIsParent(child_pid) => child_pid,
            }
        };
        let clone_child_alt_signal = {
            let res = unsafe {
                linux_api::sched::clone(
                    CloneFlags::empty(),
                    Some(Signal::SIGURG),
                    core::ptr::null_mut(),
                    core::ptr::null_mut(),
                    core::ptr::null_mut(),
                    core::ptr::null_mut(),
                )
            }
            .unwrap();
            match res {
                CloneResult::CallerIsChild => {
                    unsafe { libc::exit(0) };
                }
                CloneResult::CallerIsParent(child_pid) => child_pid,
            }
        };
        let reg_child = match unsafe { linux_api::sched::fork() }.unwrap() {
            CloneResult::CallerIsChild => {
                unsafe { libc::exit(0) };
            }
            CloneResult::CallerIsParent(child_pid) => child_pid,
        };

        let options = if use_wclone { libc::__WCLONE } else { 0 };
        let results = HashSet::from([waitfn(options), waitfn(options), waitfn(options)]);
        let expected = if use_wclone {
            HashSet::from([
                Some(clone_child_no_signal),
                Some(clone_child_alt_signal),
                None,
            ])
        } else {
            HashSet::from([Some(reg_child), None, None])
        };
        assert_eq!(results, expected);
    })
}

/// Validate that `waitfn` waits for both regular and "clone" children if __WALL
/// is provided in its options parameter. (See `waitid(2)`).
fn test_waitfn_honors_wall(
    use_wall: bool,
    waitfn: impl Fn(/*options*/ i32) -> Option<Pid>,
) -> anyhow::Result<()> {
    run_test_in_subprocess(|| {
        // `waitid(2)`: A "clone" child is  one  which delivers  no signal, or a
        // signal other than SIGCHLD to its parent upon termination.

        let clone_child_no_signal = {
            let res = unsafe {
                linux_api::sched::clone(
                    CloneFlags::empty(),
                    None,
                    core::ptr::null_mut(),
                    core::ptr::null_mut(),
                    core::ptr::null_mut(),
                    core::ptr::null_mut(),
                )
            }
            .unwrap();
            match res {
                CloneResult::CallerIsChild => {
                    unsafe { libc::exit(0) };
                }
                CloneResult::CallerIsParent(child_pid) => child_pid,
            }
        };
        let clone_child_alt_signal = {
            let res = unsafe {
                linux_api::sched::clone(
                    CloneFlags::empty(),
                    Some(Signal::SIGURG),
                    core::ptr::null_mut(),
                    core::ptr::null_mut(),
                    core::ptr::null_mut(),
                    core::ptr::null_mut(),
                )
            }
            .unwrap();
            match res {
                CloneResult::CallerIsChild => {
                    unsafe { libc::exit(0) };
                }
                CloneResult::CallerIsParent(child_pid) => child_pid,
            }
        };
        let reg_child = match unsafe { linux_api::sched::fork() }.unwrap() {
            CloneResult::CallerIsChild => {
                unsafe { libc::exit(0) };
            }
            CloneResult::CallerIsParent(child_pid) => child_pid,
        };

        let options = if use_wall { libc::__WALL } else { 0 };
        let results = HashSet::from([waitfn(options), waitfn(options), waitfn(options)]);
        let expected = if use_wall {
            HashSet::from([
                Some(clone_child_no_signal),
                Some(clone_child_alt_signal),
                Some(reg_child),
            ])
        } else {
            HashSet::from([Some(reg_child), None, None])
        };
        assert_eq!(results, expected);
    })
}

/// Validate that `waitfn` creates a correct status integer (`wstatus` in
/// `waitid(2)`) for a child that has exited normally.
fn test_waitfn_sets_normal_exit_wstatus(waitfn: impl FnOnce() -> i32) -> anyhow::Result<()> {
    run_test_in_subprocess(|| {
        const CHILD_EXIT_STATUS: i32 = 42;
        let clone_res = unsafe { linux_api::sched::fork() }.unwrap();
        match clone_res {
            CloneResult::CallerIsChild => {
                unsafe { libc::exit(CHILD_EXIT_STATUS) };
            }
            CloneResult::CallerIsParent(_child_pid) => (),
        };

        let wstatus = waitfn();

        assert!(libc::WIFEXITED(wstatus));
        assert_eq!(libc::WEXITSTATUS(wstatus), CHILD_EXIT_STATUS);
        assert!(!libc::WIFSIGNALED(wstatus));
        assert!(!libc::WIFSTOPPED(wstatus));
        assert!(!libc::WIFCONTINUED(wstatus));
    })
}

/// Validate that `waitfn` creates a correct status integer (`wstatus` in
/// `waitid(2)`).
fn test_waitfn_sets_signal_death_wstatus(
    fatal_signal: Signal,
    waitfn: impl FnOnce() -> i32,
) -> anyhow::Result<()> {
    match linux_api::signal::defaultaction(fatal_signal) {
        linux_api::signal::LinuxDefaultAction::TERM
        | linux_api::signal::LinuxDefaultAction::CORE => (),
        a => {
            panic!("Test unexpectedly invoked with a signal with default action {a:?}")
        }
    }
    run_test_in_subprocess(|| {
        let clone_res = unsafe { linux_api::sched::fork() }.unwrap();
        match clone_res {
            CloneResult::CallerIsChild => {
                unsafe { libc::raise(fatal_signal.as_i32()) };
                unreachable!()
            }
            CloneResult::CallerIsParent(_child_pid) => (),
        };

        let wstatus = waitfn();

        assert!(libc::WIFSIGNALED(wstatus));
        if running_in_shadow()
            || linux_api::signal::defaultaction(fatal_signal) == LinuxDefaultAction::TERM
        {
            // In shadow, this is always false for sake of determinism.
            //
            // If the signal action is TERM rather than CORE, we
            // know no core will have been generated.
            assert!(!libc::WCOREDUMP(wstatus));
        } else {
            // Whether WCOREDUMP is set depends on various system conditions
            // documented in `core(5)`; don't try to validate.
        }
        assert_eq!(libc::WTERMSIG(wstatus), fatal_signal.as_i32());
        assert!(!libc::WIFEXITED(wstatus));
        assert!(!libc::WIFSTOPPED(wstatus));
        assert!(!libc::WIFCONTINUED(wstatus));
    })
}

/// Validate that `waitid` correctly sets the `infop` parameter for a child
/// that has exited normally.
fn test_waitid_sets_normal_exit_info() -> anyhow::Result<()> {
    run_test_in_subprocess(|| {
        const CHILD_EXIT_STATUS: i32 = 42;
        let clone_res = unsafe { linux_api::sched::fork() }.unwrap();
        let child_pid = match clone_res {
            CloneResult::CallerIsChild => {
                unsafe { libc::exit(CHILD_EXIT_STATUS) };
            }
            CloneResult::CallerIsParent(child_pid) => child_pid,
        };

        let mut info: siginfo_t = unsafe { std::mem::zeroed() };
        let rv = nix::errno::Errno::result(unsafe {
            libc::waitid(
                libc::P_PID,
                child_pid.as_raw_nonzero().get().try_into().unwrap(),
                &mut info,
                libc::WEXITED,
            )
        });
        assert_eq!(rv, Ok(0));

        assert_eq!(
            info.si_code,
            i32::from(linux_api::signal::SigInfoCodeCld::CLD_EXITED)
        );
        assert_eq!(info.si_signo, Signal::SIGCHLD.as_i32());
        assert_eq!(unsafe { info.si_pid() }, child_pid.as_raw_nonzero().get());
        assert_eq!(unsafe { info.si_status() }, CHILD_EXIT_STATUS);
    })
}

/// Validate that `waitid` correctly sets the `infop` parameter for a child
/// that has been killed by a signal.
fn test_waitid_sets_signal_death_info(fatal_signal: Signal) -> anyhow::Result<()> {
    match linux_api::signal::defaultaction(fatal_signal) {
        linux_api::signal::LinuxDefaultAction::TERM
        | linux_api::signal::LinuxDefaultAction::CORE => (),
        a => {
            panic!("Test unexpectedly invoked with a signal with default action {a:?}")
        }
    }
    run_test_in_subprocess(|| {
        const FATAL_SIGNAL: Signal = Signal::SIGKILL;
        let clone_res = unsafe { linux_api::sched::fork() }.unwrap();
        let child_pid = match clone_res {
            CloneResult::CallerIsChild => {
                unsafe { libc::raise(FATAL_SIGNAL.as_i32()) };
                unreachable!()
            }
            CloneResult::CallerIsParent(child_pid) => child_pid,
        };

        let mut info: siginfo_t = unsafe { std::mem::zeroed() };
        let rv = nix::errno::Errno::result(unsafe {
            libc::waitid(
                libc::P_PID,
                child_pid.as_raw_nonzero().get().try_into().unwrap(),
                &mut info,
                libc::WEXITED,
            )
        });
        assert_eq!(rv, Ok(0));

        if running_in_shadow()
            || linux_api::signal::defaultaction(fatal_signal) == LinuxDefaultAction::TERM
        {
            // Always CLD_KILLED under shadow, for sake of determinism.
            //
            // If the signal action is TERM rather than CORE, we know this will be CLD_KILLED.
            assert_eq!(
                info.si_code,
                i32::from(linux_api::signal::SigInfoCodeCld::CLD_KILLED)
            );
        } else {
            // Will be one of CLD_KILLED or CLD_DUMPED; we can't reliably
            // predict which. See `core(5)`.
            assert!([
                linux_api::signal::SigInfoCodeCld::CLD_KILLED,
                linux_api::signal::SigInfoCodeCld::CLD_DUMPED
            ]
            .map(i32::from)
            .contains(&info.si_code));
        }
        assert_eq!(info.si_signo, Signal::SIGCHLD.as_i32());
        assert_eq!(unsafe { info.si_pid() }, child_pid.as_raw_nonzero().get());
        assert_eq!(unsafe { info.si_status() }, FATAL_SIGNAL.as_i32());
    })
}

/// Core, minimal functionality for fork+exec
fn test_fork_exec_and_reap(python_path: &Path, exit_code: i32) -> anyhow::Result<()> {
    run_test_in_subprocess(|| {
        let clone_res = unsafe { linux_api::sched::fork() }.unwrap();
        let child_pid = match clone_res {
            CloneResult::CallerIsChild => {
                let path = CString::new(python_path.as_os_str().as_bytes()).unwrap();
                let script = CString::new(format!(
                    r#"
import os
os._exit({exit_code})
                "#
                ))
                .unwrap();
                let args = vec![path.clone(), CString::new("-c").unwrap(), script];
                unsafe { libc::execv(path.as_ptr(), execv_argvec(&args).as_ptr()) };
                unreachable!()
            }
            CloneResult::CallerIsParent(child_pid) => child_pid,
        };

        let child_pid = nix::unistd::Pid::from_raw(child_pid.as_raw_nonzero().get());
        assert_eq!(
            nix::sys::wait::waitpid(Some(child_pid), None).unwrap(),
            nix::sys::wait::WaitStatus::Exited(child_pid, exit_code)
        );
    })
}

/// If `exec` is performed from a non-thread-group-leader, that thread becomes the
/// new thread-group-leader. Its thread-id is updated to match the pid.
fn test_fork_exec_from_thread_reassigns_tid(python_path: &Path) -> anyhow::Result<()> {
    run_test_in_subprocess(|| {
        let clone_res = unsafe { linux_api::sched::fork() }.unwrap();
        let child_pid = match clone_res {
            CloneResult::CallerIsChild => {
                let path = CString::new(python_path.as_os_str().as_bytes()).unwrap();
                std::thread::spawn(move || {
                    let pid = unsafe { libc::getpid() };
                    // Older libc's don't have libc::gettid, so we use the syscal wrapper instead.
                    let tid = i32::try_from(unsafe { libc::syscall(libc::SYS_gettid) }).unwrap();
                    assert!(pid != tid);
                    let script = CString::new(format!(
                        r#"
# python doesn't expost gettid; use ctypes to invoke libc
print("execve'd from pid:{pid} tid:{tid}")
import ctypes
libc = ctypes.CDLL("libc.so.6", use_errno=True)
py_pid = libc.getpid()
py_tid = libc.syscall({SYS_gettid})
print(f"to pid:{{py_pid}} tid:{{py_tid}}")
# pid should be unchanged
assert py_pid == {pid}, f"Expected pid {pid}; got {{py_pid}}"
# tid should now equal pid
assert py_tid == {pid}, f"Expected tid {pid}; got {{py_tid}}""#,
                        SYS_gettid = libc::SYS_gettid
                    ))
                    .unwrap();
                    let args = vec![path.clone(), CString::new("-c").unwrap(), script];
                    unsafe { libc::execv(path.as_ptr(), execv_argvec(&args).as_ptr()) };
                    unreachable!("execv shouldn't have returned");
                })
                .join()
                .unwrap();
                // The current thread will be terminated when the other thread execve's.
                unreachable!("join shouldn't have returned")
            }
            CloneResult::CallerIsParent(child_pid) => child_pid,
        };

        let child_pid = nix::unistd::Pid::from_raw(child_pid.as_raw_nonzero().get());
        assert_eq!(
            nix::sys::wait::waitpid(Some(child_pid), None).unwrap(),
            nix::sys::wait::WaitStatus::Exited(child_pid, 0)
        );
    })
}

/// Descriptors should still be open after execve iff they don't have FD_CLOEXEC set.
fn test_fork_exec_cloexec(python_path: &Path, use_cloexec: bool) -> anyhow::Result<()> {
    run_test_in_subprocess(|| {
        let pipeflags = if use_cloexec {
            rustix::pipe::PipeFlags::CLOEXEC
        } else {
            rustix::pipe::PipeFlags::empty()
        };
        let (_reader, writer) = rustix::pipe::pipe_with(pipeflags).unwrap();

        let clone_res = unsafe { linux_api::sched::fork() }.unwrap();
        let child_pid = match clone_res {
            CloneResult::CallerIsChild => {
                let path = CString::new(python_path.as_os_str().as_bytes()).unwrap();
                let writer_raw = writer.as_fd().as_raw_fd();
                let use_cloexec_python_str = if use_cloexec { "True" } else { "False" };
                let script = CString::new(format!(
                    r#"
import ctypes
libc = ctypes.CDLL("libc.so.6", use_errno=True)
s = ctypes.create_string_buffer(10)
s.value = b'hello\n'
rv = libc.write({writer_raw}, s, 6)
if {use_cloexec_python_str}:
    assert rv == -1, f"write call unexpectedly returned {{rv}} instead of -1"
else:
    assert rv == 6, f"write call unexpectedly returned {{rv}} instead of 6"
                                "#
                ))
                .unwrap();
                let args = vec![path.clone(), CString::new("-c").unwrap(), script];
                unsafe { libc::execv(path.as_ptr(), execv_argvec(&args).as_ptr()) };
                unreachable!("execv shouldn't have returned");
            }
            CloneResult::CallerIsParent(child_pid) => child_pid,
        };

        let child_pid = nix::unistd::Pid::from_raw(child_pid.as_raw_nonzero().get());
        assert_eq!(
            nix::sys::wait::waitpid(Some(child_pid), None).unwrap(),
            nix::sys::wait::WaitStatus::Exited(child_pid, 0)
        );
    })
}

/// After exec, the process should get its own copy of its DescriptorTable,
/// undoing the effect of CLONE_FILES.
fn test_fork_exec_desc_table_unshared(python_path: &Path) -> anyhow::Result<()> {
    run_test_in_subprocess(|| {
        let (_reader, writer_closed_after_fork) = rustix::pipe::pipe().unwrap();
        let (_reader, writer_closed_after_exec) = rustix::pipe::pipe().unwrap();
        let res = unsafe {
            linux_api::sched::clone(
                CloneFlags::CLONE_FILES,
                Some(Signal::SIGCHLD),
                core::ptr::null_mut(),
                core::ptr::null_mut(),
                core::ptr::null_mut(),
                core::ptr::null_mut(),
            )
        }
        .unwrap();
        let child_pid = match res {
            CloneResult::CallerIsChild => {
                unsafe { rustix::io::close(writer_closed_after_fork.as_raw_fd()) };
                let path = CString::new(python_path.as_os_str().as_bytes()).unwrap();
                let writer_closed_after_exec_raw = writer_closed_after_exec.as_raw_fd();
                let script = CString::new(format!(
                    r#"
import ctypes
libc = ctypes.CDLL("libc.so.6", use_errno=True)
assert libc.close({writer_closed_after_exec_raw}) == 0, "close failed""#
                ))
                .unwrap();
                let args = vec![path.clone(), CString::new("-c").unwrap(), script];
                unsafe { libc::execv(path.as_ptr(), execv_argvec(&args).as_ptr()) };
                unreachable!("execv shouldn't have returned");
            }
            CloneResult::CallerIsParent(child_pid) => child_pid,
        };

        let child_pid = nix::unistd::Pid::from_raw(child_pid.as_raw_nonzero().get());
        assert_eq!(
            nix::sys::wait::waitpid(Some(child_pid), None).unwrap(),
            nix::sys::wait::WaitStatus::Exited(child_pid, 0)
        );

        // The writer closed after forking should be closed here too, because we used CLONE_FILES.
        assert!(rustix::io::write(&writer_closed_after_fork, &[0]).is_err());
        // Avoid rustix panicking when it tries to close the owned fd.
        std::mem::forget(writer_closed_after_fork);

        // We should still be able to write to the pipe despite it being closed in the
        // child after exec.
        assert_eq!(rustix::io::write(&writer_closed_after_exec, &[1]), Ok(1));
    })
}

/// man pages and various online sources are unclear on this point, but experimentally,
/// pending signals are *not* cleared by exec.
fn test_fork_exec_pending_signals_uncleared(python_path: &Path) -> anyhow::Result<()> {
    run_test_in_subprocess(|| {
        let clone_res = unsafe { linux_api::sched::fork() }.unwrap();
        let child_pid = match clone_res {
            CloneResult::CallerIsChild => {
                // block SIGUSR1
                let mut sigset = nix::sys::signal::SigSet::empty();
                sigset.add(nix::sys::signal::Signal::SIGUSR1);
                sigset.thread_block().unwrap();

                // raise SIGUSR1; no immediate effect since it's blocked.
                nix::sys::signal::raise(nix::sys::signal::Signal::SIGUSR1).unwrap();

                let path = CString::new(python_path.as_os_str().as_bytes()).unwrap();
                let script = CString::new(
                    r#"
import signal
import time

handler_counter = 0

def handler(sig, frame):
  global handler_counter
  handler_counter += 1

# Install the handler.
signal.signal(signal.SIGUSR1, handler)

# Handler should not have run yet.
assert handler_counter == 0, "Handler shouldn't have run yet, but appears to have"

# Unblock the signal. The SIGUSR1 that was raised before exec should be handled
# before this returns.
oldmask = signal.pthread_sigmask(signal.SIG_UNBLOCK, [signal.SIGUSR1])
print("old mask: ", oldmask)

# Handler should have run
assert handler_counter == 1, "Handler should have run, but appears not to have""#,
                )
                .unwrap();
                let args = vec![path.clone(), CString::new("-c").unwrap(), script];
                unsafe { libc::execv(path.as_ptr(), execv_argvec(&args).as_ptr()) };
                unreachable!("execv shouldn't have returned");
            }
            CloneResult::CallerIsParent(child_pid) => child_pid,
        };

        let child_pid = nix::unistd::Pid::from_raw(child_pid.as_raw_nonzero().get());
        assert_eq!(
            nix::sys::wait::waitpid(Some(child_pid), None).unwrap(),
            nix::sys::wait::WaitStatus::Exited(child_pid, 0)
        );
    })
}

fn main() -> Result<(), Box<dyn Error>> {
    // FIXME: take as a command-line arg
    let python_path = Path::new("/usr/bin/python3");

    // should we restrict the tests we run?
    let filter_shadow_passing = std::env::args().any(|x| x == "--shadow-passing");
    let filter_libc_passing = std::env::args().any(|x| x == "--libc-passing");
    // should we summarize the results rather than exit on a failed test
    let summarize = std::env::args().any(|x| x == "--summarize");

    let all_envs = set![TestEnv::Libc, TestEnv::Shadow];
    let libc_only = set![TestEnv::Libc];

    let mut tests: Vec<test_utils::ShadowTest<(), anyhow::Error>> = Vec::new();

    #[allow(clippy::type_complexity)]
    let fork_fns: [(&str, Arc<dyn Fn() -> Result<CloneResult, Errno>>); 3] = [
        (
            stringify!(fork_via_clone_syscall),
            Arc::new(fork_via_clone_syscall),
        ),
        (
            stringify!(fork_via_fork_syscall),
            Arc::new(fork_via_fork_syscall),
        ),
        (stringify!(fork_via_libc), Arc::new(fork_via_libc)),
    ];

    for (fork_fn_name, fork_fn) in &fork_fns {
        let fork_fn = fork_fn.clone();
        tests.push(ShadowTest::new(
            &format!("{fork_fn_name}-fork_runs"),
            move || test_fork_runs(&*fork_fn),
            all_envs.clone(),
        ));
    }
    for value in [true, false] {
        tests.push(ShadowTest::new(
            &format!("clone-parent={value}"),
            move || test_clone_parent(value),
            all_envs.clone(),
        ));
    }
    tests.push(ShadowTest::new(
        stringify!(test_child_change_session),
        test_child_change_session,
        all_envs.clone(),
    ));
    tests.push(ShadowTest::new(
        stringify!(test_child_change_group),
        test_child_change_group,
        all_envs.clone(),
    ));

    for exit_signal in &[nix::sys::signal::SIGCHLD, nix::sys::signal::SIGUSR1] {
        tests.push(ShadowTest::new(
            &format!("test_exit_signal_normal_exit-{exit_signal:?}"),
            move || test_exit_signal_normal_exit(*exit_signal),
            all_envs.clone(),
        ));
        tests.push(ShadowTest::new(
            &format!("test_exit_signal_with_fatal_signal-{exit_signal:?}"),
            move || test_exit_signal_with_fatal_signal(*exit_signal),
            all_envs.clone(),
        ));
    }

    tests.push(ShadowTest::new(
        "test_waitfn_reaps:waitpid",
        || {
            test_waitfn_reaps(|| {
                nix::errno::Errno::result(unsafe { libc::waitpid(-1, std::ptr::null_mut(), 0) })
                    .unwrap();
            })
        },
        all_envs.clone(),
    ));
    tests.push(ShadowTest::new(
        "test_waitfn_reaps:wait",
        || {
            test_waitfn_reaps(|| {
                nix::errno::Errno::result(unsafe { libc::wait(std::ptr::null_mut()) }).unwrap();
            })
        },
        all_envs.clone(),
    ));
    tests.push(ShadowTest::new(
        "test_waitfn_reaps:waitid",
        || {
            test_waitfn_reaps(|| {
                nix::errno::Errno::result(unsafe {
                    libc::waitid(libc::P_ALL, 0, std::ptr::null_mut(), libc::WEXITED)
                })
                .unwrap();
            })
        },
        all_envs.clone(),
    ));

    tests.push(ShadowTest::new(
        "test_waitfn_ignores_non_children:waitpid",
        || {
            test_waitfn_ignores_non_children(|| {
                match nix::errno::Errno::result(unsafe {
                    libc::waitpid(-1, std::ptr::null_mut(), 0)
                }) {
                    Ok(pid) => Pid::from_raw(pid),
                    Err(nix::errno::Errno::ECHILD) => None,
                    Err(e) => panic!("Unexpected error {e:?}"),
                }
            })
        },
        all_envs.clone(),
    ));
    tests.push(ShadowTest::new(
        "test_waitfn_ignores_non_children:wait",
        || {
            test_waitfn_ignores_non_children(|| {
                match nix::errno::Errno::result(unsafe { libc::wait(std::ptr::null_mut()) }) {
                    Ok(pid) => Pid::from_raw(pid),
                    Err(nix::errno::Errno::ECHILD) => None,
                    Err(e) => panic!("Unexpected error {e:?}"),
                }
            })
        },
        all_envs.clone(),
    ));
    tests.push(ShadowTest::new(
        "test_waitfn_ignores_non_children:waitid",
        || {
            test_waitfn_ignores_non_children(|| {
                let mut info: siginfo_t = unsafe { std::mem::zeroed() };
                match nix::errno::Errno::result(unsafe {
                    libc::waitid(libc::P_ALL, 0, &mut info, libc::WEXITED)
                }) {
                    Ok(rv) => {
                        assert_eq!(rv, 0);
                        Pid::from_raw(unsafe { info.si_pid() })
                    }
                    Err(nix::errno::Errno::ECHILD) => None,
                    Err(e) => panic!("Unexpected error {e:?}"),
                }
            })
        },
        all_envs.clone(),
    ));

    tests.push(ShadowTest::new(
        "test_waitfn_selects_by_pid:waitpid",
        || {
            test_waitfn_selects_by_pid(|pid| {
                match nix::errno::Errno::result(unsafe {
                    libc::waitpid(pid.as_raw_nonzero().get(), std::ptr::null_mut(), 0)
                }) {
                    Ok(pid) => Pid::from_raw(pid),
                    Err(nix::errno::Errno::ECHILD) => None,
                    Err(e) => panic!("Unexpected error {e:?}"),
                }
            })
        },
        all_envs.clone(),
    ));
    tests.push(ShadowTest::new(
        "test_waitfn_selects_by_pid:waitid",
        || {
            test_waitfn_selects_by_pid(|pid| {
                let mut info: siginfo_t = unsafe { std::mem::zeroed() };
                match nix::errno::Errno::result(unsafe {
                    libc::waitid(
                        libc::P_PID,
                        pid.as_raw_nonzero().get().try_into().unwrap(),
                        &mut info,
                        libc::WEXITED,
                    )
                }) {
                    Ok(rv) => {
                        assert_eq!(rv, 0);
                        Pid::from_raw(unsafe { info.si_pid() })
                    }
                    Err(nix::errno::Errno::ECHILD) => None,
                    Err(e) => panic!("Unexpected error {e:?}"),
                }
            })
        },
        all_envs.clone(),
    ));

    tests.push(ShadowTest::new(
        "test_waitfn_selects_by_pgid:waitpid",
        || {
            test_waitfn_selects_by_pgid(|pid| {
                match nix::errno::Errno::result(unsafe {
                    libc::waitpid(-pid.as_raw_nonzero().get(), std::ptr::null_mut(), 0)
                }) {
                    Ok(pid) => Pid::from_raw(pid),
                    Err(nix::errno::Errno::ECHILD) => None,
                    Err(e) => panic!("Unexpected error {e:?}"),
                }
            })
        },
        all_envs.clone(),
    ));
    tests.push(ShadowTest::new(
        "test_waitfn_selects_by_pgid:waitid",
        || {
            test_waitfn_selects_by_pgid(|pid| {
                let mut info: siginfo_t = unsafe { std::mem::zeroed() };
                match nix::errno::Errno::result(unsafe {
                    libc::waitid(
                        libc::P_PGID,
                        pid.as_raw_nonzero().get().try_into().unwrap(),
                        &mut info,
                        libc::WEXITED,
                    )
                }) {
                    Ok(rv) => {
                        assert_eq!(rv, 0);
                        Pid::from_raw(unsafe { info.si_pid() })
                    }
                    Err(nix::errno::Errno::ECHILD) => None,
                    Err(e) => panic!("Unexpected error {e:?}"),
                }
            })
        },
        all_envs.clone(),
    ));

    tests.push(ShadowTest::new(
        "test_waitfn_selects_by_self_pgid:waitpid",
        || {
            test_waitfn_selects_by_self_pgid(|| {
                match nix::errno::Errno::result(unsafe {
                    libc::waitpid(0, std::ptr::null_mut(), 0)
                }) {
                    Ok(pid) => Pid::from_raw(pid),
                    Err(nix::errno::Errno::ECHILD) => None,
                    Err(e) => panic!("Unexpected error {e:?}"),
                }
            })
        },
        all_envs.clone(),
    ));
    tests.push(ShadowTest::new(
        "test_waitfn_selects_by_self_pgid:waitid",
        || {
            test_waitfn_selects_by_self_pgid(|| {
                let mut info: siginfo_t = unsafe { std::mem::zeroed() };
                match nix::errno::Errno::result(unsafe {
                    libc::waitid(libc::P_PGID, 0, &mut info, libc::WEXITED)
                }) {
                    Ok(rv) => {
                        assert_eq!(rv, 0);
                        Pid::from_raw(unsafe { info.si_pid() })
                    }
                    Err(nix::errno::Errno::ECHILD) => None,
                    Err(e) => panic!("Unexpected error {e:?}"),
                }
            })
        },
        all_envs.clone(),
    ));

    tests.push(ShadowTest::new(
        "test_waitfn_honors_wnohang:waitpid",
        || {
            test_waitfn_honors_wnohang(|options| {
                nix::errno::Errno::result(unsafe {
                    libc::waitpid(-1, std::ptr::null_mut(), options)
                })
            })
        },
        all_envs.clone(),
    ));
    tests.push(ShadowTest::new(
        "test_waitfn_honors_wnohang:waitid",
        || {
            test_waitfn_honors_wnohang(|options| {
                nix::errno::Errno::result(unsafe {
                    libc::waitid(
                        libc::P_ALL,
                        0,
                        std::ptr::null_mut(),
                        options | libc::WEXITED,
                    )
                })
            })
        },
        all_envs.clone(),
    ));

    /* waitpid does *not* implement WNOWAIT */
    /*
    tests.push(ShadowTest::new(
        "test_waitfn_honors_wnowait:waitpid",
        || {
            test_waitfn_honors_wnowait(|options| {
                match nix::errno::Errno::result(unsafe {
                    libc::waitpid(-1, std::ptr::null_mut(), options)
                }) {
                    Ok(pid) => Some(Pid::from_raw(pid).unwrap()),
                    Err(nix::errno::Errno::ECHILD) => None,
                    Err(e) => panic!("Unexpected error: {e:?}"),
                }
            })
        },
        all_envs.clone(),
    ));
    */
    tests.push(ShadowTest::new(
        "test_waitfn_honors_wnowait:waitid",
        || {
            test_waitfn_honors_wnowait(|options| {
                let mut info: siginfo_t = unsafe { std::mem::zeroed() };
                match nix::errno::Errno::result(unsafe {
                    libc::waitid(libc::P_PGID, 0, &mut info, options | libc::WEXITED)
                }) {
                    Ok(rv) => {
                        assert_eq!(rv, 0);
                        Pid::from_raw(unsafe { info.si_pid() })
                    }
                    Err(nix::errno::Errno::ECHILD) => None,
                    Err(e) => panic!("Unexpected error {e:?}"),
                }
            })
        },
        all_envs.clone(),
    ));

    for use_wclone in [true, false] {
        tests.push(ShadowTest::new(
            &format!(
                "test_waitfn_honors_wclone:waitid:{}",
                if use_wclone { "set" } else { "unset" }
            ),
            move || {
                test_waitfn_honors_wclone(use_wclone, |options| {
                    let mut info: siginfo_t = unsafe { std::mem::zeroed() };
                    match nix::errno::Errno::result(unsafe {
                        libc::waitid(libc::P_PGID, 0, &mut info, options | libc::WEXITED)
                    }) {
                        Ok(rv) => {
                            assert_eq!(rv, 0);
                            Pid::from_raw(unsafe { info.si_pid() })
                        }
                        Err(nix::errno::Errno::ECHILD) => None,
                        Err(e) => panic!("Unexpected error {e:?}"),
                    }
                })
            },
            all_envs.clone(),
        ));
        tests.push(ShadowTest::new(
            &format!(
                "test_waitfn_honors_wclone:waitpid:{}",
                if use_wclone { "set" } else { "unset" }
            ),
            move || {
                test_waitfn_honors_wclone(use_wclone, |options| {
                    match nix::errno::Errno::result(unsafe {
                        libc::waitpid(-1, std::ptr::null_mut(), options)
                    }) {
                        Ok(rv) => Pid::from_raw(rv),
                        Err(nix::errno::Errno::ECHILD) => None,
                        Err(e) => panic!("Unexpected error {e:?}"),
                    }
                })
            },
            all_envs.clone(),
        ));
    }

    for use_wall in [true, false] {
        tests.push(ShadowTest::new(
            &format!(
                "test_waitfn_honors_wall:waitid:{}",
                if use_wall { "set" } else { "unset" }
            ),
            move || {
                test_waitfn_honors_wall(use_wall, |options| {
                    let mut info: siginfo_t = unsafe { std::mem::zeroed() };
                    match nix::errno::Errno::result(unsafe {
                        libc::waitid(libc::P_PGID, 0, &mut info, options | libc::WEXITED)
                    }) {
                        Ok(rv) => {
                            assert_eq!(rv, 0);
                            Pid::from_raw(unsafe { info.si_pid() })
                        }
                        Err(nix::errno::Errno::ECHILD) => None,
                        Err(e) => panic!("Unexpected error {e:?}"),
                    }
                })
            },
            all_envs.clone(),
        ));
        tests.push(ShadowTest::new(
            &format!(
                "test_waitfn_honors_wall:waitpid:{}",
                if use_wall { "set" } else { "unset" }
            ),
            move || {
                test_waitfn_honors_wall(use_wall, |options| {
                    match nix::errno::Errno::result(unsafe {
                        libc::waitpid(-1, std::ptr::null_mut(), options)
                    }) {
                        Ok(rv) => Pid::from_raw(rv),
                        Err(nix::errno::Errno::ECHILD) => None,
                        Err(e) => panic!("Unexpected error {e:?}"),
                    }
                })
            },
            all_envs.clone(),
        ));
    }

    tests.push(ShadowTest::new(
        "test_waitfn_sets_normal_exit_wstatus:waitpid",
        || {
            test_waitfn_sets_normal_exit_wstatus(|| {
                let mut wstatus = 0;
                nix::errno::Errno::result(unsafe { libc::waitpid(-1, &mut wstatus, 0) }).unwrap();
                wstatus
            })
        },
        all_envs.clone(),
    ));
    tests.push(ShadowTest::new(
        "test_waitfn_sets_normal_exit_wstatus:wait",
        || {
            test_waitfn_sets_normal_exit_wstatus(|| {
                let mut wstatus = 0;
                nix::errno::Errno::result(unsafe { libc::wait(&mut wstatus) }).unwrap();
                wstatus
            })
        },
        all_envs.clone(),
    ));

    for fatal_signal in [Signal::SIGKILL, Signal::SIGABRT] {
        tests.push(ShadowTest::new(
            "test_waitfn_sets_signal_death_wstatus:waitpid:{fatal_signal:?}",
            move || {
                test_waitfn_sets_signal_death_wstatus(fatal_signal, || {
                    let mut wstatus = 0;
                    nix::errno::Errno::result(unsafe { libc::waitpid(-1, &mut wstatus, 0) })
                        .unwrap();
                    wstatus
                })
            },
            all_envs.clone(),
        ));
        tests.push(ShadowTest::new(
            "test_waitfn_sets_signal_death_wstatus:wait:{fatal_signal:?}",
            move || {
                test_waitfn_sets_signal_death_wstatus(fatal_signal, || {
                    let mut wstatus = 0;
                    nix::errno::Errno::result(unsafe { libc::wait(&mut wstatus) }).unwrap();
                    wstatus
                })
            },
            all_envs.clone(),
        ));
    }

    tests.push(ShadowTest::new(
        "test_waitid_sets_normal_exit_info",
        test_waitid_sets_normal_exit_info,
        all_envs.clone(),
    ));
    for fatal_signal in [Signal::SIGKILL, Signal::SIGABRT] {
        tests.push(ShadowTest::new(
            "test_waitid_sets_signal_death_info:{fatal_signal:?}",
            move || test_waitid_sets_signal_death_info(fatal_signal),
            all_envs.clone(),
        ));
    }

    for exit_code in [0, 1].into_iter() {
        let python_path = python_path.to_path_buf();
        tests.push(ShadowTest::new(
            &format!("test_fork_exec_and_reap-{exit_code}"),
            move || test_fork_exec_and_reap(&python_path, exit_code),
            all_envs.clone(),
        ));
    }

    tests.push(ShadowTest::new(
        "test_fork_exec_from_thread_reassigns_tid",
        {
            let python_path = python_path.to_path_buf();
            move || test_fork_exec_from_thread_reassigns_tid(&python_path)
        },
        all_envs.clone(),
    ));

    for use_cloexec in [false, true] {
        tests.push(ShadowTest::new(
            &format!("test_fork_exec_use_cloexec:{use_cloexec}"),
            {
                let python_path = python_path.to_path_buf();
                move || test_fork_exec_cloexec(&python_path, use_cloexec)
            },
            all_envs.clone(),
        ));
    }

    tests.push(ShadowTest::new(
        "test_fork_exec_desc_table_unshared",
        {
            let python_path = python_path.to_path_buf();
            move || test_fork_exec_desc_table_unshared(&python_path)
        },
        all_envs.clone(),
    ));

    tests.push(ShadowTest::new(
        "test_fork_exec_pending_signals_uncleared",
        {
            let python_path = python_path.to_path_buf();
            move || test_fork_exec_pending_signals_uncleared(&python_path)
        },
        all_envs.clone(),
    ));

    // It'd be good to test signal config across exec, but this is tricky since
    // python re-initializes it at startup. We might have to write specialized
    // programs in C to exec, and have them verify or otherwise output the
    // relevant signal state.
    //
    // e.g.
    // * actions should all be reset to default (except for ignored).
    // * signal mask should be preserved
    // * unclear whether currently-pending signals should still be pending
    //   post-exec or whether they're lost.
    // Explicitly reference these to avoid clippy warning about unnecessary
    // clone at point of last usage above.
    drop(all_envs);
    drop(libc_only);

    if filter_shadow_passing {
        tests.retain(|x| x.passing(TestEnv::Shadow));
    }
    if filter_libc_passing {
        tests.retain(|x| x.passing(TestEnv::Libc));
    }

    test_utils::run_tests(&tests, summarize)?;

    println!("Success.");
    Ok(())
}
