use linux_api::capability::{user_cap_data, user_cap_header, LINUX_CAPABILITY_VERSION_3};

use test_utils::{set, ShadowTest, TestEnvironment};

fn test_capset() -> anyhow::Result<()> {
    let hdr = user_cap_header {
        version: LINUX_CAPABILITY_VERSION_3,
        pid: 0,
    };
    let empty = user_cap_data {
        effective: 0,
        permitted: 0,
        inheritable: 0,
    };
    let data: [user_cap_data; 2] = [empty, empty];
    assert_eq!(linux_api::capability::capset(&hdr, &data), Ok(()));
    Ok(())
}

fn test_capset_nonempty() -> anyhow::Result<()> {
    let hdr = user_cap_header {
        version: LINUX_CAPABILITY_VERSION_3,
        pid: 0,
    };
    let full = user_cap_data {
        effective: u32::MAX,
        permitted: u32::MAX,
        inheritable: u32::MAX,
    };
    let data: [user_cap_data; 2] = [full, full];
    assert!(linux_api::capability::capset(&hdr, &data).is_err());
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
        ShadowTest::new("capset", test_capset, all_envs.clone()),
        ShadowTest::new("capset-nonempty", test_capset_nonempty, all_envs.clone()),
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
