/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use test_utils::set;
use test_utils::TestEnvironment as TestEnv;

const TEST_STR: &[u8; 4] = b"test";

fn fd_write(fd: i32) -> Result<usize, String> {
    nix::unistd::write(fd, TEST_STR).map_err(|e| e.to_string())
}

fn fd_read_cmp(fd: i32) -> Result<(), String> {
    let mut buf = [0_u8; 4];
    nix::unistd::read(fd, &mut buf).map_err(|e| e.to_string())?;
    if &buf != TEST_STR {
        return Err(format!(
            "error: read bytes: {:?} instead of {:?} from pipe.",
            buf, TEST_STR
        ));
    } else {
        return Ok(());
    }
}

fn test_pipe() -> Result<(), String> {
    /* Create a set of pipefds */
    let (pfd_read, pfd_write) = nix::unistd::pipe().map_err(|e| e.to_string())?;

    test_utils::run_and_close_fds(&[pfd_read, pfd_write], || {
        /* poll will check when pipe has info to read */
        let mut read_poll = libc::pollfd {
            fd: pfd_read,
            events: libc::POLLIN,
            revents: 0,
        };

        /* First make sure there's nothing there */
        let mut ready = unsafe { libc::poll(&mut read_poll as *mut libc::pollfd, 1, 100) };
        if ready < 0 {
            return Err("error: poll failed".to_string());
        } else if ready > 0 {
            return Err(format!(
                "error: pipe marked readable. revents={}",
                read_poll.revents
            ));
        }

        /* Now put information in pipe to be read */
        fd_write(pfd_write)?;

        /* Check again, should be something to read */
        read_poll.fd = pfd_read;
        read_poll.events = libc::POLLIN;
        read_poll.revents = 0;
        ready = unsafe { libc::poll(&mut read_poll as *mut libc::pollfd, 1, 100) };
        if ready != 1 {
            return Err(format!("error: poll returned {} instead of 1", ready));
        }

        if read_poll.revents & libc::POLLIN == 0 {
            return Err(format!(
                "error: read_poll has wrong revents: {}",
                read_poll.revents
            ));
        }

        /* Make sure we got what expected back */
        fd_read_cmp(pfd_read)
    })
}

fn test_creat() -> Result<(), String> {
    let test_file = b"testpoll.txt";
    let test_file = std::ffi::CString::new(*test_file).unwrap();
    let fd = unsafe {
        libc::creat(
            test_file.as_bytes_with_nul() as *const _ as *const libc::c_char,
            0o644,
        )
    };

    nix::errno::Errno::result(fd).map_err(|e| e.to_string())?;

    test_utils::run_and_close_fds(&[fd], || {
        /* poll will check when testpoll has info to read */
        let mut read_poll = libc::pollfd {
            fd: fd,
            events: libc::POLLIN,
            revents: 0,
        };
        let ready = unsafe { libc::poll(&mut read_poll as *mut libc::pollfd, 1, 100) };
        if ready < 0 {
            return Err("error: poll on empty file failed".to_string());
        } else if ready == 0 {
            /* Note: Even though the file is 0 bytes, has no data inside of it, it is still instantly
             * available for 'reading' the EOF. */
            return Err(format!(
                "error: expected EOF to be readable from empty file. revents={}",
                read_poll.revents
            ));
        }

        /* write to file */
        fd_write(fd)
    })?;

    /* Check again, should be something to read */
    let fd = nix::fcntl::open(
        test_file.as_ref(),
        nix::fcntl::OFlag::O_RDONLY,
        nix::sys::stat::Mode::empty(),
    )
    .map_err(|e| e.to_string())?;

    test_utils::run_and_close_fds(&[fd], || {
        /* poll will check when testpoll has info to read */
        let mut read_poll = libc::pollfd {
            fd: fd,
            events: libc::POLLIN,
            revents: 0,
        };
        let ready = unsafe { libc::poll(&mut read_poll as *mut libc::pollfd, 1, 100) };
        if ready != 1 {
            return Err(format!("error: poll returned {} instead of 1", ready));
        }

        if read_poll.revents & libc::POLLIN == 0 {
            return Err(format!(
                "error: read_poll has wrong revents: {}",
                read_poll.revents
            ));
        }

        /* Make sure we got what expected back */
        fd_read_cmp(fd)
    })
}

fn main() -> Result<(), String> {
    // should we restrict the tests we run?
    let filter_shadow_passing = std::env::args().any(|x| x == "--shadow-passing");
    let filter_libc_passing = std::env::args().any(|x| x == "--libc-passing");
    // should we summarize the results rather than exit on a failed test
    let summarize = std::env::args().any(|x| x == "--summarize");

    let mut tests: Vec<test_utils::ShadowTest<_, _>> = vec![
        test_utils::ShadowTest::new("test_pipe", test_pipe, set![TestEnv::Libc, TestEnv::Shadow]),
        test_utils::ShadowTest::new(
            "test_creat",
            test_creat,
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
