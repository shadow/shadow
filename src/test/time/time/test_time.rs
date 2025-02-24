use std::{thread, time};

use test_utils::{TestEnvironment, ensure_ord, set};

fn main() -> anyhow::Result<()> {
    // should we restrict the tests we run?
    let filter_shadow_passing = std::env::args().any(|x| x == "--shadow-passing");
    let filter_libc_passing = std::env::args().any(|x| x == "--libc-passing");
    // should we summarize the results rather than exit on a failed test
    let summarize = std::env::args().any(|x| x == "--summarize");

    let mut tests = get_tests();

    if filter_shadow_passing {
        tests.retain(|x| x.passing(TestEnvironment::Shadow));
    }
    if filter_libc_passing {
        tests.retain(|x| x.passing(TestEnvironment::Libc));
    }

    test_utils::run_tests(&tests, summarize)?;

    Ok(())
}

fn get_tests() -> Vec<test_utils::ShadowTest<(), anyhow::Error>> {
    let all_envs = set![TestEnvironment::Libc, TestEnvironment::Shadow];

    let tests: Vec<test_utils::ShadowTest<(), anyhow::Error>> = vec![
        test_utils::ShadowTest::new("null_arg", test_null_arg, all_envs.clone()),
        test_utils::ShadowTest::new("nonnull_arg", test_nonnull_arg, all_envs.clone()),
        test_utils::ShadowTest::new("forward_tick", test_forward_tick, all_envs),
    ];

    tests
}

fn test_null_arg() -> anyhow::Result<()> {
    let rv = unsafe { libc::time(std::ptr::null_mut()) };
    ensure_ord!(rv, >, 0);
    Ok(())
}

fn test_nonnull_arg() -> anyhow::Result<()> {
    let mut tloc: libc::time_t = 0;
    let rv = unsafe { libc::time(&mut tloc) };
    ensure_ord!(rv, >, 0);
    Ok(())
}

fn test_forward_tick() -> anyhow::Result<()> {
    let rv_first = unsafe { libc::time(std::ptr::null_mut()) };

    // Advance the clock.
    thread::sleep(time::Duration::from_secs(1));

    let rv_second = unsafe { libc::time(std::ptr::null_mut()) };

    ensure_ord!(rv_first, <, rv_second);
    Ok(())
}
