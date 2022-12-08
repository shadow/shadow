/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

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
    let mut tests: Vec<test_utils::ShadowTest<_, _>> = vec![];

    let domains = [libc::AF_INET];
    let sock_types = [libc::SOCK_STREAM, libc::SOCK_DGRAM];

    for &domain in domains.iter() {
        for &sock_type in sock_types.iter() {
            // add details to the test names to avoid duplicates
            let append_args = |s| format!("{} <domain={},sock_type={}>", s, domain, sock_type);

            let more_tests: Vec<test_utils::ShadowTest<_, _>> = vec![test_utils::ShadowTest::new(
                &append_args("test_tty"),
                move || test_tty(domain, sock_type),
                set![TestEnv::Libc, TestEnv::Shadow],
            )];

            tests.extend(more_tests);
        }
    }

    tests
}

/// Test ioctl() using the tty-related ioctl requests.
fn test_tty(domain: libc::c_int, sock_type: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(domain, sock_type, 0) };
    assert!(fd >= 0);

    let test_tty_request = |request| {
        // test incorrect fds
        test_utils::check_system_call!(
            || unsafe { libc::ioctl(-1, request, &mut std::mem::zeroed::<libc::termios>()) },
            &[libc::EBADF],
        )?;
        test_utils::check_system_call!(
            || unsafe { libc::ioctl(8934, request, &mut std::mem::zeroed::<libc::termios>()) },
            &[libc::EBADF],
        )?;

        // test a valid fd
        test_utils::check_system_call!(
            || unsafe { libc::ioctl(fd, request, &mut std::mem::zeroed::<libc::termios>()) },
            &[libc::ENOTTY],
        )
    };

    test_utils::run_and_close_fds(&[fd], || {
        test_tty_request(libc::TCGETS)?;
        test_tty_request(libc::TCSETS)?;
        test_tty_request(libc::TCSETSW)?;
        test_tty_request(libc::TCSETSF)?;
        test_tty_request(libc::TCGETA)?;
        test_tty_request(libc::TCSETA)?;
        test_tty_request(libc::TCSETAW)?;
        test_tty_request(libc::TCSETAF)?;
        test_tty_request(libc::TIOCGWINSZ)?;
        test_tty_request(libc::TIOCSWINSZ)?;

        // in glibc, isatty() calls tcgetattr() which makes the ioctl call
        let rv = unsafe { libc::isatty(fd) };
        let errno = test_utils::get_errno();
        test_utils::result_assert_eq(rv, 0, "Unexpected return value from isatty()")?;
        test_utils::result_assert_eq(errno, libc::ENOTTY, "Unexpected errno from isatty()")?;

        Ok(())
    })
}
