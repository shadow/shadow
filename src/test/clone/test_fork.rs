use std::error::Error;

use linux_api::sched::{CloneFlags, CloneResult};
use linux_api::signal::Signal;
use test_utils::TestEnvironment as TestEnv;
use test_utils::{set, ShadowTest};

fn test_fork() -> Result<(), Box<dyn Error>> {
    let (reader, writer) = rustix::pipe::pipe().unwrap();

    let flags = CloneFlags::empty();
    let res = unsafe {
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

fn main() -> Result<(), Box<dyn Error>> {
    // should we restrict the tests we run?
    let filter_shadow_passing = std::env::args().any(|x| x == "--shadow-passing");
    let filter_libc_passing = std::env::args().any(|x| x == "--libc-passing");
    // should we summarize the results rather than exit on a failed test
    let summarize = std::env::args().any(|x| x == "--summarize");

    let all_envs = set![TestEnv::Libc, TestEnv::Shadow];
    let libc_only = set![TestEnv::Libc];

    let mut tests: Vec<test_utils::ShadowTest<(), Box<dyn Error>>> =
        vec![ShadowTest::new("fork_runs", test_fork, all_envs.clone())];

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
