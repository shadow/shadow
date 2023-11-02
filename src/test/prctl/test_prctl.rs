use test_utils::TestEnvironment as TestEnv;
use test_utils::{assert_with_errno, set};

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
    let tests: Vec<test_utils::ShadowTest<_, _>> = vec![
        test_utils::ShadowTest::new(
            "test_zero_option",
            test_zero_option,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_dumpable",
            test_dumpable,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_tid_addr",
            test_tid_addr,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new("test_name", test_name, set![TestEnv::Libc, TestEnv::Shadow]),
    ];

    tests
}

fn test_zero_option() -> Result<(), String> {
    assert_eq!(-1, unsafe { libc::prctl(0) });
    assert_eq!(libc::EINVAL, test_utils::get_errno());

    Ok(())
}

fn test_dumpable() -> Result<(), String> {
    // In kernels up to and including 2.6.12, arg2 must be either 0 (SUID_DUMP_DISABLE, process is
    // not dumpable) or 1 (SUID_DUMP_USER, process is dumpable).
    const SUID_DUMP_DISABLE: u64 = 0;
    const SUID_DUMP_USER: u64 = 1;

    // should initially be enabled
    assert_with_errno!(unsafe { libc::prctl(libc::PR_GET_DUMPABLE) } == SUID_DUMP_USER as i32);

    // set as disabled
    assert_with_errno!(unsafe { libc::prctl(libc::PR_SET_DUMPABLE, SUID_DUMP_DISABLE) } == 0);

    assert_with_errno!(unsafe { libc::prctl(libc::PR_GET_DUMPABLE) } == SUID_DUMP_DISABLE as i32);

    Ok(())
}

fn test_tid_addr() -> Result<(), String> {
    let mut addr: *mut libc::pid_t = std::ptr::null_mut();
    assert_with_errno!(unsafe { libc::prctl(libc::PR_GET_TID_ADDRESS, &mut addr) } == 0);

    // it seems to be null in shadow and non-null outside of shadow

    // check that the pointer is readable
    if !addr.is_null() {
        // printing the value so that DCE doesn't optimize out the read (`read_volatile` and
        // `black_box` aren't enough)
        println!("{}", unsafe { *addr });
    }

    Ok(())
}

fn test_name() -> Result<(), String> {
    let mut buffer = [0u8; 16];
    assert_with_errno!(unsafe { libc::prctl(libc::PR_GET_NAME, &mut buffer) } == 0);

    // make sure it's null terminated
    assert_eq!(buffer[buffer.len() - 1], 0);

    Ok(())
}
