use std::error::Error;

use nix::errno::Errno;
use test_utils::ShadowTest;
use test_utils::TestEnvironment as TestEnv;
use test_utils::set;

fn test_unaligned_read() -> Result<(), Box<dyn Error>> {
    // Force Shadow to read an *unaligned* timespec struct.

    // First create a normal, aligned struct.
    let t = libc::timespec {
        tv_sec: 1,
        tv_nsec: 2,
    };

    // Create a buf to hold the unaligned version.  We declare this as a buf of
    // timespecs so that it'll be properly aligned, guaranteeing that adding an
    // offset < alignment will give us an unaligned pointer.
    let mut buf = [libc::timespec {
        tv_sec: 0,
        tv_nsec: 0,
    }; 2];

    let src = unsafe {
        std::slice::from_raw_parts(
            std::ptr::from_ref(&t) as *const u8,
            std::mem::size_of::<libc::timeval>(),
        )
    };
    let unaligned_t = unsafe {
        std::slice::from_raw_parts_mut(
            (std::ptr::from_mut(&mut buf) as *mut u8).add(1),
            std::mem::size_of::<libc::timeval>(),
        )
    };
    unaligned_t.copy_from_slice(src);
    let unaligned_t = unaligned_t.as_ptr() as *const libc::timespec;

    // Double-check that that we actually ended up with an unaligned pointer.
    assert_ne!((unaligned_t as usize) % std::mem::align_of_val(&t), 0);

    // Just validate that nanosleep returns without an error or crashing.
    Errno::result(unsafe { libc::nanosleep(unaligned_t, std::ptr::null_mut()) })?;

    Ok(())
}

fn test_unaligned_write() -> Result<(), Box<dyn Error>> {
    // Force Shadow to write an *unaligned* timeval struct.

    // We declare this as a buf of timevals so that it'll be properly aligned,
    // guaranteeing that adding an offset < alignment will give us an unaligned
    // pointer.
    let mut buf = [libc::timeval {
        tv_sec: 0,
        tv_usec: 0,
    }; 2];
    let unaligned_tv = unsafe { (buf.as_mut_ptr() as *mut u8).add(1) as *mut libc::timeval };
    // Double-check that that we actually ended up with an unaligned pointer.
    assert_ne!((unaligned_tv as usize) % std::mem::align_of_val(&buf[0]), 0);

    // Just validate that gettimeofday returns without an error or crashing.
    Errno::result(unsafe { libc::gettimeofday(unaligned_tv, std::ptr::null_mut()) })?;

    Ok(())
}

fn main() -> Result<(), Box<dyn Error>> {
    // should we restrict the tests we run?
    let filter_shadow_passing = std::env::args().any(|x| x == "--shadow-passing");
    let filter_libc_passing = std::env::args().any(|x| x == "--libc-passing");
    // should we summarize the results rather than exit on a failed test
    let summarize = std::env::args().any(|x| x == "--summarize");

    let mut tests = vec![
        ShadowTest::new(
            "test_unaligned_read",
            test_unaligned_read,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        ShadowTest::new(
            "test_unaligned_write",
            test_unaligned_write,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
    ];
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
