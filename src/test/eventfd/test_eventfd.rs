/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use nix::sys::eventfd::EfdFlags;
use nix::unistd::{close, read, write};
use std::os::unix::io::RawFd;
use test_utils::set;
use test_utils::TestEnvironment as TestEnv;

fn main() -> Result<(), String> {
    // should we restrict the tests we run?
    let filter_shadow_passing = std::env::args().any(|x| x == "--shadow-passing");
    let filter_libc_passing = std::env::args().any(|x| x == "--libc-passing");

    // should we summarize the results rather than exit on a failed test
    let summarize = std::env::args().any(|x| x == "--summarize");

    // TODO test the case where an eventfd is being read and written in separate threads
    // in order to test the cases where the nonblock flag is not used.
    let mut tests: Vec<test_utils::ShadowTest<_, _>> = vec![
        test_utils::ShadowTest::new(
            "test_eventfd_create",
            test_eventfd_create,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_eventfd_read_write_nonblock",
            test_eventfd_read_write_nonblock,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_eventfd_read_write_semaphore_nonblock",
            test_eventfd_read_write_semaphore_nonblock,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
    ];

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

fn call_eventfd(init_val: u32, flags: EfdFlags) -> Result<i32, String> {
    // We use libc here instead of nix to ensure a direct syscall is made without err checking
    test_utils::check_system_call!(|| unsafe { libc::eventfd(init_val, flags.bits()) }, &[])
}

fn test_eventfd_create() -> Result<(), String> {
    let flags = [
        EfdFlags::empty(),
        EfdFlags::EFD_SEMAPHORE,
        EfdFlags::EFD_NONBLOCK,
        EfdFlags::EFD_CLOEXEC,
        EfdFlags::EFD_SEMAPHORE | EfdFlags::EFD_NONBLOCK,
    ];

    for &flag in flags.iter() {
        let efd = call_eventfd(0, flag)?;

        test_utils::result_assert(
            efd > 0,
            &format!(
                "Unexpected return value {} from eventfd syscall with flag {}",
                efd,
                flag.bits()
            ),
        )?;

        close(efd).map_err(|e| e.to_string())?;
    }

    Ok(())
}

fn check_read_success(efd: RawFd, expected_val: u64) -> Result<(), String> {
    let mut bytes: [u8; 8] = [0; 8];

    test_utils::result_assert(
        read(efd, &mut bytes).map_err(|e| e.to_string())? == 8,
        &format!("Unable to read 8 bytes from eventfd {}", efd),
    )?;

    test_utils::result_assert(
        u64::from_ne_bytes(bytes) == expected_val,
        &format!(
            "The value we read from the eventfd {} counter was incorrect",
            efd
        ),
    )?;

    Ok(())
}

fn check_read_eagain(efd: RawFd) -> Result<(), String> {
    let mut bytes: [u8; 8] = [0; 8];

    test_utils::result_assert(
        read(efd, &mut bytes) == Err(nix::Error::Sys(nix::errno::Errno::EAGAIN)),
        &format!(
            "Reading empty counter did not block eventfd {} as expected",
            efd
        ),
    )?;

    Ok(())
}

fn check_write_success(efd: RawFd, val: u64) -> Result<(), String> {
    let bytes: [u8; 8] = val.to_ne_bytes();

    test_utils::result_assert(
        write(efd, &bytes).map_err(|e| e.to_string())? == 8,
        &format!("Unable to write 8 bytes to eventfd {}", efd),
    )?;

    Ok(())
}

fn check_write_einval(efd: RawFd, val: u64) -> Result<(), String> {
    let bytes: [u8; 8] = val.to_ne_bytes();

    test_utils::result_assert(
        write(efd, &bytes) == Err(nix::Error::Sys(nix::errno::Errno::EINVAL)),
        &format!(
            "Overflowing counter did not block eventfd {} as expected",
            efd
        ),
    )?;

    Ok(())
}

fn test_eventfd_read_write_nonblock() -> Result<(), String> {
    // Initialize eventfd with initial value of 2
    let init_val = 2;
    let flag = EfdFlags::EFD_NONBLOCK;
    let efd: RawFd = call_eventfd(init_val, flag)?;

    test_utils::result_assert(
        efd > 0,
        &format!(
            "Unexpected retval {} from eventfd with flag {}",
            efd,
            flag.bits()
        ),
    )?;

    test_utils::run_and_close_fds(&[efd], || {
        // Make sure the initval of 2 was set correctly
        check_read_success(efd, 2)?;

        // Now we should get EAGAIN since the counter was reset
        check_read_eagain(efd)?;

        // Writing values should add them to the counter
        check_write_success(efd, 2)?;
        check_write_success(efd, 3)?;
        check_write_success(efd, 4)?;
        check_read_success(efd, 9)?;
        check_read_eagain(efd)?;

        // Writing u64_max-1 is allowed...
        check_write_success(efd, u64::MAX - 1)?;
        check_read_success(efd, u64::MAX - 1)?;
        check_read_eagain(efd)?;
        // ...but u64_max is not
        check_write_einval(efd, u64::MAX)?;

        Ok(())
    })
}

fn test_eventfd_read_write_semaphore_nonblock() -> Result<(), String> {
    // Initialize eventfd with initial value of 2
    let init_val = 2;
    let flag = EfdFlags::EFD_NONBLOCK | EfdFlags::EFD_SEMAPHORE;
    let efd: RawFd = call_eventfd(init_val, flag)?;

    test_utils::result_assert(
        efd > 0,
        &format!(
            "Unexpected retval {} from eventfd with flag {}",
            efd,
            flag.bits()
        ),
    )?;

    test_utils::run_and_close_fds(&[efd], || {
        // Make sure the initval of 2 was set correctly
        // Semaphore mode reads 1 at a time
        check_read_success(efd, 1)?;
        check_read_success(efd, 1)?;
        check_read_eagain(efd)?;

        // Writing values should add them to the counter
        check_write_success(efd, 2)?;
        check_write_success(efd, 3)?;
        check_write_success(efd, 4)?;
        for _ in 0..9 {
            check_read_success(efd, 1)?;
        }
        check_read_eagain(efd)?;

        // Writing u64_max is still not allowed
        check_write_einval(efd, u64::MAX)?;

        Ok(())
    })
}
