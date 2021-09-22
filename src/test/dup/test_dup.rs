/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use test_utils::check_system_call;
use test_utils::set;
use test_utils::TestEnvironment as TestEnv;

fn main() -> Result<(), String> {
    // should we restrict the tests we run?
    let filter_shadow_passing = std::env::args().any(|x| x == "--shadow-passing");
    let filter_libc_passing = std::env::args().any(|x| x == "--libc-passing");
    // should we summarize the results rather than exit on a failed test
    let summarize = std::env::args().any(|x| x == "--summarize");

    let mut tests = get_tests();
    if filter_shadow_passing {
        tests = tests
            .into_iter()
            .filter(|x| x.passing(TestEnv::Shadow))
            .collect()
    }
    if filter_libc_passing {
        tests = tests
            .into_iter()
            .filter(|x| x.passing(TestEnv::Libc))
            .collect()
    }

    test_utils::run_tests(&tests, summarize)?;

    println!("Success.");
    Ok(())
}

fn get_tests() -> Vec<test_utils::ShadowTest<(), String>> {
    let tests: Vec<test_utils::ShadowTest<_, _>> = vec![
        test_utils::ShadowTest::new("test_dup", test_dup, set![TestEnv::Libc, TestEnv::Shadow]),
        test_utils::ShadowTest::new("test_dup2", test_dup2, set![TestEnv::Libc, TestEnv::Shadow]),
        test_utils::ShadowTest::new("test_dup3", test_dup3, set![TestEnv::Libc, TestEnv::Shadow]),
    ];

    tests
}

fn test_dup() -> Result<(), String> {
    let (read_fd, write_fd) = nix::unistd::pipe().unwrap();

    test_utils::run_and_close_fds(&[write_fd, read_fd], || {
        let write_fd_dup = check_system_call!(|| unsafe { libc::dup(write_fd) }, &[])?;
        assert_eq!(unsafe { libc::close(write_fd_dup) }, 0);

        check_system_call!(|| unsafe { libc::dup(-1) }, &[libc::EBADF])?;
        check_system_call!(|| unsafe { libc::dup(5000) }, &[libc::EBADF])?;

        Ok(())
    })
}

fn test_dup2() -> Result<(), String> {
    let (read_fd, write_fd) = nix::unistd::pipe().unwrap();
    let target = 1000;
    assert!(target != read_fd && target != write_fd);

    test_utils::run_and_close_fds(&[write_fd, read_fd], || {
        let write_fd_dup = check_system_call!(|| unsafe { libc::dup2(write_fd, target) }, &[])?;
        assert_eq!(write_fd_dup, target);
        assert_eq!(unsafe { libc::close(write_fd_dup) }, 0);

        check_system_call!(|| unsafe { libc::dup2(-1, target) }, &[libc::EBADF])?;
        check_system_call!(|| unsafe { libc::dup2(5000, target) }, &[libc::EBADF])?;
        check_system_call!(|| unsafe { libc::dup2(write_fd, -1) }, &[libc::EBADF])?;

        Ok(())
    })
}

fn test_dup3() -> Result<(), String> {
    let (read_fd, write_fd) = nix::unistd::pipe().unwrap();
    let flag = libc::O_CLOEXEC;
    let target = 1000;
    assert!(target != read_fd && target != write_fd);

    test_utils::run_and_close_fds(&[write_fd, read_fd], || {
        let write_fd_dup =
            check_system_call!(|| unsafe { libc::dup3(write_fd, target, flag) }, &[])?;
        assert_eq!(write_fd_dup, target);
        assert_eq!(unsafe { libc::close(write_fd_dup) }, 0);

        check_system_call!(|| unsafe { libc::dup3(-1, target, flag) }, &[libc::EBADF])?;
        check_system_call!(|| unsafe { libc::dup3(5000, target, flag) }, &[libc::EBADF])?;
        check_system_call!(|| unsafe { libc::dup3(write_fd, -1, flag) }, &[libc::EBADF])?;

        Ok(())
    })
}
