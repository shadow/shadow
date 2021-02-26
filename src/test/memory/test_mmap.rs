/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use std::error::Error;

use test_utils::set;
use test_utils::TestEnvironment as TestEnv;

const MAPLEN: usize = 16;

fn validate_shadow_access(buf: &[u8]) -> Result<(), Box<dyn Error>> {
    let template = b"test_mmapXXXXXX";

    /* Get a file that we can mmap and write into. */
    let (temp_fd, path) = nix::unistd::mkstemp(template.as_ref())?;
    nix::errno::Errno::result(temp_fd)?;

    // Write buffer contents to file.
    nix::unistd::write(temp_fd, &buf)?;

    // Seek to beginning of file.
    let rv = nix::unistd::lseek(temp_fd, 0, nix::unistd::Whence::SeekSet)?;
    nix::errno::Errno::result(rv)?;

    // Read contents from file.
    let mut mapbuf = vec![0_u8; buf.len()];
    let rv = nix::unistd::read(temp_fd, mapbuf.as_mut_slice())?;
    nix::errno::Errno::result(rv)?;

    nix::unistd::unlink(&path)?;
    nix::unistd::close(temp_fd)?;

    Ok(())
}

fn test_mmap_file(offset: libc::off_t, unlink_before_mmap: bool) -> Result<(), Box<dyn Error>> {
    let template = b"test_mmapXXXXXX";

    /* Get a file that we can mmap and write into. */
    let (temp_fd, path) = nix::unistd::mkstemp(template.as_ref())?;
    nix::errno::Errno::result(temp_fd)?;

    // We can unlink the path now and temp_fd should still remain valid
    if unlink_before_mmap {
        nix::unistd::unlink(&path)?;
    }

    /* Make sure there is enough space to write after the mmap. */
    nix::fcntl::posix_fallocate(temp_fd, offset, MAPLEN as i64)?;

    /* Init a msg to write. */
    let msg = b"Hello new world!";

    /* Do the mmap and write the message into the resulting mem location. */
    let mapbuf = unsafe {
        libc::mmap(
            std::ptr::null_mut(),
            MAPLEN,
            libc::PROT_READ | libc::PROT_WRITE,
            libc::MAP_SHARED,
            temp_fd,
            offset,
        )
    };

    assert!(mapbuf != libc::MAP_FAILED);

    /* `map` is no longer valid after call to munmap, so limit the scope. */
    {
        let map = unsafe { std::slice::from_raw_parts_mut::<u8>(mapbuf as *mut u8, MAPLEN) };
        map.copy_from_slice(msg.as_ref());

        let rv = unsafe { libc::munmap(mapbuf, MAPLEN) };
        nix::errno::Errno::result(rv)?;
    }

    let rv = nix::unistd::lseek(temp_fd, offset, nix::unistd::Whence::SeekSet)?;
    nix::errno::Errno::result(rv)?;

    let mut rdbuf = [0_u8; MAPLEN];
    let rv = nix::unistd::read(temp_fd, &mut rdbuf[..])?;
    nix::errno::Errno::result(rv)?;

    assert_eq!(msg, &rdbuf);

    // If we didn't already unlink, do that now
    if !unlink_before_mmap {
        nix::unistd::unlink(&path)?;
    }
    nix::unistd::close(temp_fd)?;

    Ok(())
}

fn page_size() -> usize {
    nix::unistd::sysconf(nix::unistd::SysconfVar::PAGE_SIZE)
        .unwrap()
        .unwrap() as usize
}

fn init_buf(buf: &mut [u8]) {
    for (i, byte) in buf.iter_mut().enumerate() {
        *byte = (i / page_size()) as u8;
    }
}

fn mmap_and_init_buf(size: usize) -> *mut libc::c_void {
    let buf_ptr = unsafe {
        libc::mmap(
            std::ptr::null_mut(),
            size,
            libc::PROT_READ | libc::PROT_WRITE,
            libc::MAP_PRIVATE | libc::MAP_ANONYMOUS,
            -1,
            0,
        )
    };
    test_utils::assert_true_else_errno(buf_ptr != libc::MAP_FAILED);

    let mut buf = unsafe { std::slice::from_raw_parts_mut::<u8>(buf_ptr as *mut u8, size) };
    init_buf(&mut buf);

    buf_ptr
}

fn check_buf(buf: &[u8]) {
    for (i, byte) in buf.iter().enumerate() {
        assert_eq!(*byte, (i / page_size()) as u8);
    }
}

fn test_mmap_anon() -> Result<(), Box<dyn Error>> {
    let initial_size = 2 * page_size();
    let mut buf_ptr = mmap_and_init_buf(initial_size);

    validate_shadow_access(&unsafe {
        std::slice::from_raw_parts::<u8>(buf_ptr as *const u8, initial_size)
    })?;

    // Grow the buffer.
    let grown_size = 2 * initial_size;

    // We have to allow the buffer to move to potentially guarantee the allocation succeeds.
    buf_ptr = unsafe { libc::mremap(buf_ptr, initial_size, grown_size, libc::MREMAP_MAYMOVE) };
    test_utils::assert_true_else_errno(buf_ptr != libc::MAP_FAILED);

    let mut buf = unsafe { std::slice::from_raw_parts_mut::<u8>(buf_ptr as *mut u8, grown_size) };

    // Validate that initial contents are still there.
    check_buf(&buf[..initial_size]);

    validate_shadow_access(&buf[..initial_size])?;

    // Fill the new portion of the buffer.
    init_buf(&mut buf);

    // Validate the whole contents of the buffer.
    check_buf(&buf);

    validate_shadow_access(&buf)?;

    // Shrink the buffer.
    let shrunk_size = initial_size / 2;
    let shrunk_buf = unsafe { libc::mremap(buf_ptr, grown_size, shrunk_size, 0) };
    test_utils::assert_true_else_errno(shrunk_buf != libc::MAP_FAILED);
    // Shouldn't have moved.
    assert_eq!(buf_ptr, shrunk_buf);

    let buf = unsafe { std::slice::from_raw_parts::<u8>(buf_ptr as *const u8, shrunk_size) };

    check_buf(&buf);

    validate_shadow_access(&buf)?;

    // Unmap allocated memory
    let rv = unsafe { libc::munmap(buf_ptr, shrunk_size) };
    nix::errno::Errno::result(rv)?;

    Ok(())
}

fn test_mremap_clobber() -> Result<(), Box<dyn Error>> {
    let bigbuf = mmap_and_init_buf(3 * page_size());
    let smolbuf = mmap_and_init_buf(page_size());

    // mremap smolbuf into the middle of bigbuf, clobbering it.
    let req_new_address = unsafe { bigbuf.offset(page_size() as isize) as *mut libc::c_void };
    let actual_new_address = unsafe {
        libc::mremap(
            smolbuf,
            page_size(),
            page_size(),
            libc::MREMAP_MAYMOVE | libc::MREMAP_FIXED,
            req_new_address,
        )
    };

    test_utils::assert_true_else_errno(actual_new_address != libc::MAP_FAILED);
    assert_eq!(actual_new_address, req_new_address);

    // First page of bigbuf should be untouched.
    let bb = unsafe { std::slice::from_raw_parts::<u8>(bigbuf as *const u8, 3 * page_size()) };
    for byte in bb[..page_size()].iter() {
        assert_eq!(*byte, 0);
    }
    validate_shadow_access(&bb[..page_size()])?;

    // Next page should have been overwritten by smolbuf
    for byte in bb[page_size()..2 * page_size()].iter() {
        assert_eq!(*byte, 0);
    }
    validate_shadow_access(&bb[page_size()..2 * page_size()])?;

    // Last page should be untouched.
    for byte in bb[2 * page_size()..3 * page_size()].iter() {
        assert_eq!(*byte, 2);
    }
    validate_shadow_access(&bb[2 * page_size()..])?;

    // Validate Shadow access of the whole buffer (which crosses mmap'd regions) at once.
    validate_shadow_access(&bb[..])?;

    // Unmap allocated memory
    let rv = unsafe { libc::munmap(bigbuf, 3 * page_size()) };
    nix::errno::Errno::result(rv)?;

    Ok(())
}

// Exercises features used by libpthread when allocating a stack.
// This includes:
//   * using PROT_NONE (and then following up with an mprotect to make it accessible).
//   * using MAP_STACK.
fn test_mmap_prot_none_mprotect() -> Result<(), Box<dyn Error>> {
    let size = 8 * 1 << 20;

    // Initially mapped with PROT_NONE, making it inaccessible.
    let buf_ptr = unsafe {
        libc::mmap(
            0 as *mut libc::c_void,
            size,
            libc::PROT_NONE,
            libc::MAP_PRIVATE | libc::MAP_ANONYMOUS | libc::MAP_STACK,
            -1,
            0,
        )
    };
    test_utils::assert_true_else_errno(buf_ptr != libc::MAP_FAILED);

    // Update protections to make it accessible.
    let rv = unsafe { libc::mprotect(buf_ptr, size, libc::PROT_READ | libc::PROT_WRITE) };
    test_utils::assert_true_else_errno(rv == 0);

    let mut buf = unsafe { std::slice::from_raw_parts_mut::<u8>(buf_ptr as *mut u8, size) };
    // Validate that it's accessible both to the plugin and to Shadow.
    init_buf(&mut buf);
    validate_shadow_access(&buf)?;

    // Unmap allocated memory
    let rv = unsafe { libc::munmap(buf_ptr, size) };

    nix::errno::Errno::result(rv)?;

    Ok(())
}

fn test_mmap_file_low(unlink_before_mmap: bool) -> Result<(), Box<dyn Error>> {
    test_mmap_file(0, unlink_before_mmap)
}

fn test_mmap_file_high32(unlink_before_mmap: bool) -> Result<(), Box<dyn Error>> {
    test_mmap_file(1 << 20, unlink_before_mmap)
}

// mmap2(2) says highest supported file size is 2**44. Use an offset a bit smaller than
// that. On a 64-bit system, presumably mmap can handle higher than that, but it's
// unclear what the limit is. Assume it can handle at least as much as mmap2.
fn test_mmap_file_high64(unlink_before_mmap: bool) -> Result<(), Box<dyn Error>> {
    test_mmap_file(1 << 43, unlink_before_mmap)
}

fn main() -> Result<(), Box<dyn Error>> {
    // should we restrict the tests we run?
    let filter_shadow_passing = std::env::args().any(|x| x == "--shadow-passing");
    let filter_libc_passing = std::env::args().any(|x| x == "--libc-passing");
    // should we summarize the results rather than exit on a failed test
    let summarize = std::env::args().any(|x| x == "--summarize");

    let mut tests: Vec<test_utils::ShadowTest<_, _>> = vec![
        test_utils::ShadowTest::new(
            "test_mmap_anon",
            test_mmap_anon,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_mremap_clobber",
            test_mremap_clobber,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_mmap_prot_none_mprotect",
            test_mmap_prot_none_mprotect,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
    ];

    for &unlink_before_mmap in [false, true].iter() {
        tests.push(test_utils::ShadowTest::new(
            &format!(
                "test_mmap_file_low <unlink_before_mmap={}>",
                unlink_before_mmap
            ),
            move || test_mmap_file_low(unlink_before_mmap),
            set![TestEnv::Libc, TestEnv::Shadow],
        ));
        tests.push(test_utils::ShadowTest::new(
            &format!(
                "test_mmap_file_high32 <unlink_before_mmap={}>",
                unlink_before_mmap
            ),
            move || test_mmap_file_high32(unlink_before_mmap),
            set![TestEnv::Libc, TestEnv::Shadow],
        ));
        tests.push(test_utils::ShadowTest::new(
            &format!(
                "test_mmap_file_high64 <unlink_before_mmap={}>",
                unlink_before_mmap
            ),
            move || test_mmap_file_high64(unlink_before_mmap),
            set![TestEnv::Libc, TestEnv::Shadow],
        ));
    }

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
