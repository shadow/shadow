use std::error::Error;
use std::sync::Arc;

use libc::{c_int, c_void, siginfo_t, CLD_EXITED};
use linux_api::errno::Errno;
use linux_api::posix_types::Pid;
use linux_api::sched::{CloneFlags, CloneResult};
use linux_api::signal::Signal;
use nix::sys::signal::{SaFlags, SigAction, SigHandler, SigmaskHow};
use nix::sys::signalfd::SigSet;
use test_utils::{ensure_ord, TestEnvironment as TestEnv};
use test_utils::{set, ShadowTest};

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

fn main() -> Result<(), Box<dyn Error>> {
    // should we restrict the tests we run?
    let filter_shadow_passing = std::env::args().any(|x| x == "--shadow-passing");
    let filter_libc_passing = std::env::args().any(|x| x == "--libc-passing");
    // should we summarize the results rather than exit on a failed test
    let summarize = std::env::args().any(|x| x == "--summarize");

    let all_envs = set![TestEnv::Libc, TestEnv::Shadow];
    let libc_only = set![TestEnv::Libc];

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

    let mut tests: Vec<test_utils::ShadowTest<(), anyhow::Error>> = Vec::new();
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
