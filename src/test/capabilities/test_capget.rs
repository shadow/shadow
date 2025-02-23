use linux_api::capability::{LINUX_CAPABILITY_VERSION_3, user_cap_data, user_cap_header};

use test_utils::{ShadowTest, TestEnvironment, set};

fn test_capget() -> anyhow::Result<()> {
    let hdr = user_cap_header {
        version: LINUX_CAPABILITY_VERSION_3,
        pid: 0,
    };
    // Make some non-empty capabilities
    let nonempty = user_cap_data {
        effective: 1,
        permitted: 1,
        inheritable: 1,
    };
    // Put the non-empty to the array so that we check that it will be
    // written to zeroes later
    let mut data: [user_cap_data; 2] = [nonempty, nonempty];
    assert_eq!(linux_api::capability::capget(&hdr, Some(&mut data)), Ok(()));

    for item in &data {
        assert_eq!(
            *item,
            user_cap_data {
                effective: 0,
                permitted: 0,
                inheritable: 0,
            }
        );
    }
    Ok(())
}

fn test_capget_null_datap() -> anyhow::Result<()> {
    let hdr = user_cap_header {
        version: LINUX_CAPABILITY_VERSION_3,
        pid: 0,
    };
    assert_eq!(linux_api::capability::capget(&hdr, None), Ok(()));
    Ok(())
}

fn main() -> anyhow::Result<()> {
    // should we restrict the tests we run?
    let filter_shadow_passing = std::env::args().any(|x| x == "--shadow-passing");
    let filter_libc_passing = std::env::args().any(|x| x == "--libc-passing");
    // should we summarize the results rather than exit on a failed test
    let summarize = std::env::args().any(|x| x == "--summarize");

    let all_envs = set![TestEnvironment::Libc, TestEnvironment::Shadow];

    let mut tests: Vec<test_utils::ShadowTest<(), anyhow::Error>> = vec![
        // We do test_capget only in Shadow because, if it's run in Linux as root, capget can give
        // more capabilities than what non-root users have.
        ShadowTest::new("capget", test_capget, set![TestEnvironment::Shadow]),
        ShadowTest::new(
            "capget-null-datap",
            test_capget_null_datap,
            all_envs.clone(),
        ),
    ];

    if filter_shadow_passing {
        tests.retain(|x| x.passing(TestEnvironment::Shadow));
    }
    if filter_libc_passing {
        tests.retain(|x| x.passing(TestEnvironment::Libc));
    }

    test_utils::run_tests(&tests, summarize)?;

    println!("Success.");
    Ok(())
}
