/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use test_utils::TestEnvironment as TestEnv;
use test_utils::set;

// The number of random values to generate with each method.
const RGENLEN: usize = 200;
// The number of buckets to use when checking random value distribution.
const BUCKETLEN: usize = 10;

fn main() -> Result<(), String> {
    // should we restrict the tests we run?
    let filter_shadow_passing = std::env::args().any(|x| x == "--shadow-passing");
    let filter_libc_passing = std::env::args().any(|x| x == "--libc-passing");
    // should we summarize the results rather than exit on a failed test
    let summarize = std::env::args().any(|x| x == "--summarize");

    let mut tests: Vec<test_utils::ShadowTest<_, _>> = vec![
        test_utils::ShadowTest::new(
            "test_dev_urandom",
            test_dev_urandom,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        // Outside of Shadow, this test could block indefinitely.
        test_utils::ShadowTest::new("test_dev_random", test_dev_random, set![TestEnv::Shadow]),
        test_utils::ShadowTest::new("test_rand", test_rand, set![TestEnv::Libc, TestEnv::Shadow]),
        test_utils::ShadowTest::new(
            "test_getrandom",
            test_getrandom,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
    ];
    if filter_shadow_passing {
        tests.retain(|x| x.passing(TestEnv::Shadow))
    }
    if filter_libc_passing {
        tests.retain(|x| x.passing(TestEnv::Libc))
    }

    test_utils::run_tests(&tests, summarize)?;

    println!("Success.");
    Ok(())
}

// This is just a quick check that the 0<=f<=100 fractions that are generated using
// the random APIs are "plausibly random"; its primary purpose is to test that the
// randomness API plumbing is working, but not to test the quality of the underlying
// RNGs. We just check that each decile of the distribution has at least one entry.
fn check_randomness(fracs: &[f64]) -> Result<(), String> {
    let mut buckets = [0_u8; BUCKETLEN];

    for f in fracs {
        let percent = (f * 100_f64) as u8;
        assert!(percent <= 100, "invalid random percent value: {}", percent,);
        let j = percent as usize % BUCKETLEN;
        buckets[j] += 1;
    }

    let fail = buckets.iter().any(|&i| i == 0);
    println!("bucket values:");
    for (i, val) in buckets.iter().enumerate() {
        println!("bucket[{}] = {}", i, val);
    }

    if fail {
        Err("failed to get random values across entire range".to_string())
    } else {
        Ok(())
    }
}

fn test_path_helper(path: &str) -> Result<(), String> {
    use std::io::Read;

    let mut file =
        std::fs::File::open(path).map_err(|e| format!("error: cannot open file: {:?}", e))?;

    let mut values = [0_f64; RGENLEN];

    for val in values.iter_mut() {
        let mut rv = [0_u8; 4];
        file.read_exact(&mut rv)
            .map_err(|_| "error reading file".to_string())?;
        *val = u32::from_be_bytes([rv[0], rv[1], rv[2], rv[3]]) as f64 / u32::MAX as f64;
    }

    check_randomness(&values)
}

fn test_dev_urandom() -> Result<(), String> {
    test_path_helper("/dev/urandom")
}

fn test_dev_random() -> Result<(), String> {
    test_path_helper("/dev/random")
}

fn test_rand() -> Result<(), String> {
    let mut values = [0_f64; RGENLEN];

    for val in values.iter_mut() {
        let random_value = unsafe { libc::rand() };

        #[allow(clippy::absurd_extreme_comparisons)]
        if !(0..=libc::RAND_MAX).contains(&random_value) {
            return Err("error: rand returned bytes outside of expected range".to_string());
        }

        *val = random_value as f64 / libc::RAND_MAX as f64;
    }

    check_randomness(&values)
}

fn test_getrandom() -> Result<(), String> {
    let mut values = [0_f64; RGENLEN];

    for val in values.iter_mut() {
        let mut rv = [0_u8; 4];

        // getrandom() was only added in glibc 2.25, so use syscall until all of
        // our supported OS targets pick up the new libc call
        // https://sourceware.org/legacy-ml/libc-alpha/2017-02/msg00079.html
        let num_bytes = unsafe {
            libc::syscall(
                libc::SYS_getrandom,
                rv.as_mut_ptr() as *mut libc::c_void,
                rv.len(),
                0,
            )
        };

        if num_bytes <= 0 {
            return Err("error: getrandom returned bytes outside of expected range".to_string());
        }

        *val = u32::from_be_bytes([rv[0], rv[1], rv[2], rv[3]]) as f64 / u32::MAX as f64;
    }

    check_randomness(&values)
}
