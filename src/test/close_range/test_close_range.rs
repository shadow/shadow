use linux_api::close_range::{close_range, close_range_raw, CloseRangeFlags};
use linux_api::errno::Errno;
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
    // TODO: enable linux tests when shadow supports a minimum kernel version of 5.9
    let tests: Vec<test_utils::ShadowTest<_, _>> = vec![
        test_utils::ShadowTest::new("test_no_flags", test_no_flags, set![TestEnv::Shadow]),
        // TODO: enable linux tests when shadow supports a minimum kernel version of 5.11
        test_utils::ShadowTest::new("test_cloexec", test_cloexec, set![TestEnv::Shadow]),
        test_utils::ShadowTest::new("test_backwards", test_backwards, set![TestEnv::Shadow]),
        test_utils::ShadowTest::new(
            "test_out_of_bounds",
            test_out_of_bounds,
            set![TestEnv::Shadow],
        ),
    ];

    tests
}

fn test_no_flags() -> Result<(), String> {
    let fd_1 = unsafe { libc::eventfd(0, 0) };
    let fd_2 = unsafe { libc::eventfd(0, 0) };
    let fd_3 = unsafe { libc::eventfd(0, 0) };
    let fd_4 = unsafe { libc::eventfd(0, 0) };
    let fd_5 = unsafe { libc::eventfd(0, 0) };

    // assuming that there aren't preexisting fds open, linux should assign them incrementally
    assert_eq!(fd_2, fd_1 + 1);
    assert_eq!(fd_3, fd_2 + 1);
    assert_eq!(fd_4, fd_3 + 1);
    assert_eq!(fd_5, fd_4 + 1);

    assert_eq!(close_range(fd_2, fd_4, CloseRangeFlags::empty()), Ok(0));

    // fcntl should return EBADF for the middle fds
    assert_eq!(unsafe { libc::fcntl(fd_1, libc::F_GETFD) }, 0);
    assert_eq!(unsafe { libc::fcntl(fd_2, libc::F_GETFD) }, -1);
    assert_eq!(unsafe { libc::fcntl(fd_3, libc::F_GETFD) }, -1);
    assert_eq!(unsafe { libc::fcntl(fd_4, libc::F_GETFD) }, -1);
    assert_eq!(unsafe { libc::fcntl(fd_5, libc::F_GETFD) }, 0);

    unsafe { libc::close(fd_1) };
    unsafe { libc::close(fd_5) };

    Ok(())
}

fn test_cloexec() -> Result<(), String> {
    let fd_1 = unsafe { libc::eventfd(0, 0) };
    let fd_2 = unsafe { libc::eventfd(0, 0) };
    let fd_3 = unsafe { libc::eventfd(0, 0) };
    let fd_4 = unsafe { libc::eventfd(0, 0) };
    let fd_5 = unsafe { libc::eventfd(0, 0) };

    test_utils::run_and_close_fds(&[fd_1, fd_2, fd_3, fd_4, fd_5], || {
        // assuming that there aren't preexisting fds open, linux should assign them incrementally
        assert_eq!(fd_2, fd_1 + 1);
        assert_eq!(fd_3, fd_2 + 1);
        assert_eq!(fd_4, fd_3 + 1);
        assert_eq!(fd_5, fd_4 + 1);

        assert_eq!(
            close_range(fd_2, fd_4, CloseRangeFlags::CLOSE_RANGE_CLOEXEC),
            Ok(0),
        );

        // fcntl should return FD_CLOEXEC for the middle fds
        #[rustfmt::skip]
        {
            assert_eq!(unsafe { libc::fcntl(fd_1, libc::F_GETFD) }, 0);
            assert_eq!(unsafe { libc::fcntl(fd_2, libc::F_GETFD) }, libc::FD_CLOEXEC);
            assert_eq!(unsafe { libc::fcntl(fd_3, libc::F_GETFD) }, libc::FD_CLOEXEC);
            assert_eq!(unsafe { libc::fcntl(fd_4, libc::F_GETFD) }, libc::FD_CLOEXEC);
            assert_eq!(unsafe { libc::fcntl(fd_5, libc::F_GETFD) }, 0);
        };
    });

    Ok(())
}

fn test_backwards() -> Result<(), String> {
    const EMPTY: CloseRangeFlags = CloseRangeFlags::empty();

    assert_eq!(close_range(11, 10, EMPTY), Err(Errno::EINVAL));

    Ok(())
}

fn test_out_of_bounds() -> Result<(), String> {
    // File fds are often signed integers, so they shouldn't be larger than i32::MAX. What happens
    // if we do use a larger value like u32::MAX?

    assert_eq!(close_range_raw(10_000, u32::MAX, 0), Ok(0));
    assert_eq!(close_range_raw(u32::MAX - 1, u32::MAX, 0), Ok(0));
    assert_eq!(close_range_raw(u32::MAX, u32::MAX, 0), Ok(0));

    Ok(())
}
