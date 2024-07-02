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
    let tests: Vec<test_utils::ShadowTest<_, _>> = vec![test_utils::ShadowTest::new(
        "test_fstat_pipe",
        test_fstat_pipe,
        set![TestEnv::Libc, TestEnv::Shadow],
    )];

    tests
}

fn test_fstat_pipe() -> Result<(), String> {
    let mut pipefd = [-1, -1];
    assert_eq!(0, unsafe {
        libc::pipe2(pipefd.as_mut_ptr(), libc::O_NONBLOCK)
    });

    let mut statbuf: libc::stat = unsafe { std::mem::zeroed() };
    assert_eq!(0, unsafe { libc::fstat(pipefd[0], &mut statbuf) });

    assert_eq!(statbuf.st_nlink, 1);
    assert_eq!(
        statbuf.st_mode,
        libc::S_IFIFO | libc::S_IRUSR | libc::S_IWUSR,
    );
    assert_eq!(statbuf.st_size, 0);

    // write 5000 bytes to the pipe
    assert_eq!(
        Ok(5000),
        rustix::io::write(
            unsafe { rustix::fd::BorrowedFd::borrow_raw(pipefd[1]) },
            &[0u8; 5000]
        )
    );

    // check the st_size of the readable pipe
    let mut statbuf: libc::stat = unsafe { std::mem::zeroed() };
    assert_eq!(0, unsafe { libc::fstat(pipefd[0], &mut statbuf) });
    assert_eq!(statbuf.st_size, 0);

    // check the st_size of the writable pipe
    let mut statbuf: libc::stat = unsafe { std::mem::zeroed() };
    assert_eq!(0, unsafe { libc::fstat(pipefd[1], &mut statbuf) });
    assert_eq!(statbuf.st_size, 0);

    Ok(())
}
