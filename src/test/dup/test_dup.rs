/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use test_utils::TestEnvironment as TestEnv;
use test_utils::check_system_call;
use test_utils::set;

#[derive(Debug, Copy, Clone, PartialEq, Eq)]
enum DupFn {
    Dup,
    Dup2,
    Dup3,
    Fcntl,
    FcntlCloExec,
}

fn main() -> Result<(), String> {
    // should we restrict the tests we run?
    let filter_shadow_passing = std::env::args().any(|x| x == "--shadow-passing");
    let filter_libc_passing = std::env::args().any(|x| x == "--libc-passing");
    // should we summarize the results rather than exit on a failed test
    let summarize = std::env::args().any(|x| x == "--summarize");

    let mut tests = get_tests();
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

fn get_tests() -> Vec<test_utils::ShadowTest<(), String>> {
    let mut tests: Vec<test_utils::ShadowTest<_, _>> = vec![
        test_utils::ShadowTest::new("test_dup", test_dup, set![TestEnv::Libc, TestEnv::Shadow]),
        test_utils::ShadowTest::new("test_dup2", test_dup2, set![TestEnv::Libc, TestEnv::Shadow]),
        test_utils::ShadowTest::new("test_dup3", test_dup3, set![TestEnv::Libc, TestEnv::Shadow]),
        test_utils::ShadowTest::new(
            "test_fcntl",
            test_fcntl,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
    ];

    for dup_fn in &[
        DupFn::Dup,
        DupFn::Dup2,
        DupFn::Dup3,
        DupFn::Fcntl,
        DupFn::FcntlCloExec,
    ] {
        tests.push(test_utils::ShadowTest::new(
            &format!("test_dup_io <dup_fn={dup_fn:?}>"),
            move || test_dup_io(dup_fn),
            set![TestEnv::Libc, TestEnv::Shadow],
        ));
    }

    tests
}

fn test_dup() -> Result<(), String> {
    let test_fd = |fd| -> Result<(), String> {
        let fd_dup = check_system_call!(|| unsafe { libc::dup(fd) }, &[])?;
        assert_eq!(unsafe { libc::close(fd_dup) }, 0);

        check_system_call!(|| unsafe { libc::dup(-1) }, &[libc::EBADF])?;
        check_system_call!(|| unsafe { libc::dup(5000) }, &[libc::EBADF])?;

        Ok(())
    };

    // test a pipe
    let (read_fd, write_fd) = nix::unistd::pipe().unwrap();
    test_utils::run_and_close_fds(&[write_fd, read_fd], || test_fd(write_fd))?;

    // test a regular file
    let file_fd = nix::fcntl::open(
        "/dev/null",
        nix::fcntl::OFlag::empty(),
        nix::sys::stat::Mode::empty(),
    )
    .unwrap();
    test_utils::run_and_close_fds(&[file_fd], || test_fd(file_fd))?;

    Ok(())
}

fn test_dup2() -> Result<(), String> {
    let target = 1000;

    let test_fd = |fd| -> Result<(), String> {
        let fd_dup = check_system_call!(|| unsafe { libc::dup2(fd, target) }, &[])?;
        assert_eq!(fd_dup, target);
        assert_eq!(unsafe { libc::close(fd_dup) }, 0);

        check_system_call!(|| unsafe { libc::dup2(-1, target) }, &[libc::EBADF])?;
        check_system_call!(|| unsafe { libc::dup2(5000, target) }, &[libc::EBADF])?;
        check_system_call!(|| unsafe { libc::dup2(fd, -1) }, &[libc::EBADF])?;

        Ok(())
    };

    // test a pipe
    let (read_fd, write_fd) = nix::unistd::pipe().unwrap();
    assert!(target != read_fd && target != write_fd);
    test_utils::run_and_close_fds(&[write_fd, read_fd], || test_fd(write_fd))?;

    // test a regular file
    let file_fd = nix::fcntl::open(
        "/dev/null",
        nix::fcntl::OFlag::empty(),
        nix::sys::stat::Mode::empty(),
    )
    .unwrap();
    assert_ne!(target, file_fd);
    test_utils::run_and_close_fds(&[file_fd], || test_fd(file_fd))?;

    Ok(())
}

fn test_dup3() -> Result<(), String> {
    let flag = libc::O_CLOEXEC;
    let target = 1000;

    let test_fd = |fd| -> Result<(), String> {
        let fd_dup = check_system_call!(|| unsafe { libc::dup3(fd, target, flag) }, &[])?;
        assert_eq!(fd_dup, target);
        assert_eq!(unsafe { libc::close(fd_dup) }, 0);

        check_system_call!(|| unsafe { libc::dup3(-1, target, flag) }, &[libc::EBADF])?;
        check_system_call!(|| unsafe { libc::dup3(5000, target, flag) }, &[libc::EBADF])?;
        check_system_call!(|| unsafe { libc::dup3(fd, -1, flag) }, &[libc::EBADF])?;

        Ok(())
    };

    // test a pipe
    let (read_fd, write_fd) = nix::unistd::pipe().unwrap();
    assert!(target != read_fd && target != write_fd);
    test_utils::run_and_close_fds(&[write_fd, read_fd], || test_fd(write_fd))?;

    // test a regular file
    let file_fd = nix::fcntl::open(
        "/dev/null",
        nix::fcntl::OFlag::empty(),
        nix::sys::stat::Mode::empty(),
    )
    .unwrap();
    assert_ne!(target, file_fd);
    test_utils::run_and_close_fds(&[file_fd], || test_fd(file_fd))?;

    Ok(())
}

fn test_fcntl() -> Result<(), String> {
    for command in &[libc::F_DUPFD, libc::F_DUPFD_CLOEXEC] {
        let min_fd = 1000;

        let test_fd = |fd| -> Result<(), String> {
            let fd_dup_1 =
                check_system_call!(|| unsafe { libc::fcntl(fd, *command, min_fd) }, &[]).unwrap();
            assert_eq!(fd_dup_1, min_fd);

            let fd_dup_2 =
                check_system_call!(|| unsafe { libc::fcntl(fd, *command, min_fd) }, &[]).unwrap();
            assert_eq!(fd_dup_2, min_fd + 1);

            let fd_dup_3 =
                check_system_call!(|| unsafe { libc::fcntl(fd, *command, 0) }, &[]).unwrap();
            assert!(fd_dup_3 < min_fd);

            assert_eq!(unsafe { libc::close(fd_dup_1) }, 0);
            assert_eq!(unsafe { libc::close(fd_dup_2) }, 0);
            assert_eq!(unsafe { libc::close(fd_dup_3) }, 0);

            check_system_call!(
                || unsafe { libc::fcntl(-1, libc::F_DUPFD, min_fd) },
                &[libc::EBADF]
            )?;
            check_system_call!(
                || unsafe { libc::fcntl(5000, libc::F_DUPFD, min_fd) },
                &[libc::EBADF]
            )?;
            check_system_call!(
                || unsafe { libc::fcntl(fd, libc::F_DUPFD, -1) },
                &[libc::EINVAL]
            )?;

            Ok(())
        };

        // test a pipe
        let (read_fd, write_fd) = nix::unistd::pipe().unwrap();
        assert!(min_fd != read_fd && min_fd != write_fd);
        test_utils::run_and_close_fds(&[write_fd, read_fd], || test_fd(write_fd))?;

        // test a regular file
        let file_fd = nix::fcntl::open(
            "/dev/null",
            nix::fcntl::OFlag::empty(),
            nix::sys::stat::Mode::empty(),
        )
        .unwrap();
        assert_ne!(min_fd, file_fd);
        test_utils::run_and_close_fds(&[file_fd], || test_fd(file_fd))?;
    }

    Ok(())
}

fn test_dup_io(dup_fn: &DupFn) -> Result<(), String> {
    let (read_fd, write_fd) = nix::unistd::pipe().unwrap();

    // TODO: use nix instead here
    fn write(fd: libc::c_int, buf: &[u8]) -> Result<libc::ssize_t, String> {
        test_utils::check_system_call!(
            || unsafe { libc::write(fd, buf.as_ptr() as *const libc::c_void, buf.len()) },
            &[],
        )
    }

    fn read(fd: libc::c_int, buf: &mut [u8]) -> Result<libc::ssize_t, String> {
        test_utils::check_system_call!(
            || unsafe { libc::read(fd, buf.as_mut_ptr() as *mut libc::c_void, buf.len()) },
            &[],
        )
    }

    test_utils::run_and_close_fds(&[write_fd, read_fd], || {
        let write_fd_dup = test_utils::check_system_call!(
            move || match dup_fn {
                DupFn::Dup => unsafe { libc::dup(write_fd) },
                DupFn::Dup2 => unsafe { libc::dup2(write_fd, 1000) },
                DupFn::Dup3 => unsafe { libc::dup3(write_fd, 1000, libc::O_CLOEXEC) },
                DupFn::Fcntl => unsafe { libc::fcntl(write_fd, libc::F_DUPFD, 1000) },
                DupFn::FcntlCloExec => unsafe {
                    libc::fcntl(write_fd, libc::F_DUPFD_CLOEXEC, 1000)
                },
            },
            &[]
        )?;

        test_utils::run_and_close_fds(&[write_fd_dup], || {
            let write_buf = [1u8, 2, 3, 4];

            // write 4 bytes to original fd
            let rv = write(write_fd, &write_buf)?;
            test_utils::result_assert_eq(rv, 4, "Expected to write 4 bytes")?;

            // write 4 bytes to duped fd
            let rv = write(write_fd_dup, &write_buf)?;
            test_utils::result_assert_eq(rv, 4, "Expected to write 4 bytes")?;

            // read 8 bytes
            let mut read_buf = [0u8; 8];
            let rv = read(read_fd, &mut read_buf)?;
            test_utils::result_assert_eq(rv, 8, "Expected to read 8 bytes")?;

            test_utils::result_assert_eq(&write_buf[..], &read_buf[..4], "First 4 bytes differ")?;
            test_utils::result_assert_eq(&write_buf[..], &read_buf[4..8], "Last 4 bytes differ")?;

            Ok(())
        })
    })
}
