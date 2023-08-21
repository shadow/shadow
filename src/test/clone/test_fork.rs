use std::error::Error;
use std::sync::Arc;

use linux_api::errno::Errno;
use linux_api::posix_types::Pid;
use linux_api::sched::{CloneFlags, CloneResult};
use linux_api::signal::Signal;
use test_utils::TestEnvironment as TestEnv;
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

fn test_fork_runs(
    fork_fn: impl FnOnce() -> Result<CloneResult, Errno>,
) -> Result<(), Box<dyn Error>> {
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

fn test_clone_parent(set_clone_parent: bool) -> Result<(), Box<dyn Error>> {
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

    // Close our copy of the writer-end of the pipe, so that parent hang trying
    // to read from the pipe if the child exited abnormally.
    drop(writer);

    // Because waitpid isn't implemented yet, we get the "exit code"
    // from a pipe.
    // TODO: once waitpid is implemented, use that instead.
    let mut exit_code = [0xff_u8];
    assert_eq!(rustix::io::read(&reader, &mut exit_code), Ok(1));
    assert_eq!(exit_code[0], 0);

    Ok(())
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

    let mut tests: Vec<test_utils::ShadowTest<(), Box<dyn Error>>> = Vec::new();
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
