use anyhow::ensure;
use linux_api::errno::Errno;
use std::os::fd::AsRawFd;
use test_utils::{ForkedChild, ShadowTest, TestEnvironment, ensure_ord, set};

/// safer, convenient wrapper around fcntl for lock operations.
///
/// TODO: take a Rust file descriptor type, once we've upgraded to a rustix
/// version that uses them instead of its own.
fn fcntl_lock(
    raw_fd: libc::c_int,
    cmd: libc::c_int,
    flock: &libc::flock,
) -> Result<libc::flock, Errno> {
    assert!(
        [libc::F_SETLK, libc::F_SETLKW, libc::F_GETLK].contains(&cmd),
        "Unhandled command"
    );
    let mut res = *flock;
    Errno::result_from_libc_errno(-1, unsafe { libc::fcntl(raw_fd, cmd, &mut res) })?;
    Ok(res)
}

/// test taking uncontested locks, using either F_SETLK or F_SETLKW.
fn test_uncontested_pid_locks(cmd: libc::c_int) -> anyhow::Result<()> {
    assert!(
        [libc::F_SETLK, libc::F_SETLKW].contains(&cmd),
        "Unhandled command"
    );
    let file = tempfile::tempfile().unwrap();

    let rd_flock = libc::flock {
        l_type: libc::F_RDLCK.try_into().unwrap(),
        l_whence: libc::SEEK_SET.try_into().unwrap(),
        l_start: 0,
        l_len: 100,
        l_pid: 0,
    };

    // Taking a read lock succeeds. (Doesn't matter that the file is empty; non-existent ranges can be locked).
    // flock struct should be unmodified.
    ensure_ord!(fcntl_lock(file.as_raw_fd(), cmd, &rd_flock)?, ==, rd_flock);

    // Taking a read lock where we already have one still succeeds.
    ensure_ord!(fcntl_lock(file.as_raw_fd(), cmd, &rd_flock)?, ==, rd_flock);

    // Upgrading a read lock to a write lock succeeds.
    let wr_flock = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        ..rd_flock
    };
    ensure_ord!(fcntl_lock(file.as_raw_fd(), cmd, &wr_flock)?, ==, wr_flock);

    // GETLK, when there is no conflicting lock (which is the case when we own the only existing lock)...
    {
        let mut res_flock = fcntl_lock(file.as_raw_fd(), libc::F_GETLK, &rd_flock)?;
        // ... should set l_type to F_UNLCK
        ensure_ord!(res_flock.l_type, ==, i16::try_from(libc::F_UNLCK).unwrap());
        // ... and leave other fields unchanged.
        res_flock.l_type = rd_flock.l_type;
        ensure_ord!(rd_flock, ==, res_flock);
    }

    // unlock should succeed.
    let unlck = libc::flock {
        l_type: libc::F_UNLCK.try_into().unwrap(),
        ..rd_flock
    };
    ensure_ord!(fcntl_lock(file.as_raw_fd(), cmd, &unlck)?, ==, unlck);
    // should still succeed with nothing holding the lock.
    ensure_ord!(fcntl_lock(file.as_raw_fd(), cmd, &unlck)?, ==, unlck);

    let named_file = tempfile::NamedTempFile::new().unwrap();
    let read_only_file = std::fs::File::open(named_file.path()).unwrap();

    // We can take a read lock from a read-only descriptor.
    Errno::result_from_libc_errno(-1, unsafe {
        libc::fcntl(read_only_file.as_raw_fd(), cmd, &rd_flock)
    })?;

    // We *cannot* take a write lock from a read-only descriptor.
    ensure_ord!(Errno::result_from_libc_errno(-1, unsafe { libc::fcntl(read_only_file.as_raw_fd(), cmd, &wr_flock)}), ==, Err(Errno::EBADF));

    Ok(())
}

#[derive(Copy, Clone, Debug)]
#[repr(C)]
struct SerializedLockRequest {
    fd: libc::c_int,
    cmd: libc::c_int,
    flock: libc::flock,
}
unsafe impl shadow_pod::Pod for SerializedLockRequest {}

#[derive(Copy, Clone, Debug)]
#[repr(C)]
struct SerializedLockResponse {
    rv: libc::c_int,
    errno: libc::c_int,
    flock: libc::flock,
}
unsafe impl shadow_pod::Pod for SerializedLockResponse {}

/// Perform the operation specified by `req`.
fn handle_lock_request(req: &SerializedLockRequest) -> SerializedLockResponse {
    let mut flock = req.flock;
    let rv = unsafe { libc::fcntl(req.fd, req.cmd, &mut flock) };
    SerializedLockResponse {
        rv,
        flock,
        errno: unsafe { *libc::__errno_location() },
    }
}

fn test_contested_pid_locks() -> anyhow::Result<()> {
    // Open file before forking, so that child also has the descriptor.
    let file = tempfile::tempfile().unwrap();

    let mut child = ForkedChild::new(handle_lock_request)?;

    // Take a read lock
    let rd_flock = libc::flock {
        l_type: libc::F_RDLCK.try_into().unwrap(),
        l_whence: libc::SEEK_SET.try_into().unwrap(),
        l_start: 0,
        l_len: 100,
        l_pid: 0,
    };
    ensure_ord!(fcntl_lock(file.as_raw_fd(), libc::F_SETLK, &rd_flock)?, ==, rd_flock);

    // Child should be able to take the same read lock.
    let res = child
        .send_recv(&SerializedLockRequest {
            fd: file.as_raw_fd(),
            cmd: libc::F_SETLK,
            flock: rd_flock,
        })
        .unwrap();
    ensure_ord!(res.rv, ==, 0);
    ensure_ord!(res.flock, ==, rd_flock);

    // We should *not* be able to upgrade to a write lock, since the child now
    // has a read lock.
    let wr_flock = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        ..rd_flock
    };

    // GETLK should tell us about the child's conflicting lock.
    let res = fcntl_lock(file.as_raw_fd(), libc::F_GETLK, &wr_flock);
    ensure_ord!(res, ==, Ok(libc::flock {
        l_pid: child.pid(),
        ..rd_flock
    }));

    // Trying to take the lock anyway should fail.
    let res = fcntl_lock(file.as_raw_fd(), libc::F_SETLK, &wr_flock);
    ensure!(
        res == Err(Errno::EACCES) || res == Err(Errno::EAGAIN),
        "Expected EACCES or EGAIN, got {res:?}"
    );

    // Tell the child to release its lock, which should succeed.
    let unlck_flock = libc::flock {
        l_type: libc::F_UNLCK.try_into().unwrap(),
        ..rd_flock
    };
    let res = child
        .send_recv(&SerializedLockRequest {
            fd: file.as_raw_fd(),
            cmd: libc::F_SETLK,
            flock: unlck_flock,
        })
        .unwrap();
    ensure_ord!(res.rv, ==, 0);
    ensure_ord!(res.flock, ==, unlck_flock);

    // We should now be free to upgrade to a write lock.
    ensure_ord!(fcntl_lock(file.as_raw_fd(), libc::F_SETLK, &wr_flock), ==, Ok(wr_flock));

    Ok(())
}

fn test_coalesce_and_split_pid_locks() -> anyhow::Result<()> {
    // Open file before forking, so that child also has the descriptor.
    let file = tempfile::tempfile().unwrap();

    let mut child = ForkedChild::new(handle_lock_request)?;

    // Lock first 100 bytes
    let wr100_flock = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        l_whence: libc::SEEK_SET.try_into().unwrap(),
        l_start: 0,
        l_len: 100,
        l_pid: 0,
    };
    ensure_ord!(fcntl_lock(file.as_raw_fd(), libc::F_SETLK, &wr100_flock)?, ==, wr100_flock);

    // Lock next 100 bytes
    let wr_next_100_flock = libc::flock {
        l_start: wr100_flock.l_start + wr100_flock.l_len,
        ..wr100_flock
    };
    ensure_ord!(fcntl_lock(file.as_raw_fd(), libc::F_SETLK, &wr_next_100_flock)?, ==, wr_next_100_flock);

    // Child querying any part of the locked region should see a coalesced lock.
    let res = child
        .send_recv(&SerializedLockRequest {
            fd: file.as_raw_fd(),
            cmd: libc::F_GETLK,
            flock: wr100_flock,
        })
        .unwrap();
    ensure_ord!(res.rv, ==, 0);
    ensure_ord!(res.flock, ==, libc::flock {
        l_len: wr100_flock.l_len*2,
        l_pid: std::process::id().try_into().unwrap(),
        ..wr100_flock
    });

    // Unlock middle of the region
    let unlock_middle_flock = libc::flock {
        l_type: libc::F_UNLCK.try_into().unwrap(),
        l_start: wr100_flock.l_len / 2,
        ..wr100_flock
    };
    ensure_ord!(fcntl_lock(file.as_raw_fd(), libc::F_SETLK, &unlock_middle_flock)?, ==, unlock_middle_flock);

    // Querying byte 0 should return a conflicting lock extending to the
    // beginning of where we unlocked.
    let res = child
        .send_recv(&SerializedLockRequest {
            fd: file.as_raw_fd(),
            cmd: libc::F_GETLK,
            flock: libc::flock {
                l_type: libc::F_WRLCK.try_into().unwrap(),
                l_whence: libc::SEEK_SET.try_into().unwrap(),
                l_start: 0,
                l_len: 1,
                l_pid: 0,
            },
        })
        .unwrap();
    ensure_ord!(res.rv, ==, 0);
    ensure_ord!(res.flock, ==, libc::flock {
        l_len: unlock_middle_flock.l_start,
        l_pid: std::process::id().try_into().unwrap(),
        ..wr100_flock
    });

    // Querying byte 0 of the unlocked region should indicate no conflicting lock.
    let query_flock = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        l_whence: libc::SEEK_SET.try_into().unwrap(),
        l_start: unlock_middle_flock.l_start,
        l_len: 1,
        l_pid: 0,
    };
    let res = child
        .send_recv(&SerializedLockRequest {
            fd: file.as_raw_fd(),
            cmd: libc::F_GETLK,
            flock: query_flock,
        })
        .unwrap();
    ensure_ord!(res.rv, ==, 0);
    ensure_ord!(res.flock, ==, libc::flock {
        l_type: libc::F_UNLCK.try_into().unwrap(),
        ..query_flock
    });

    // Querying first byte after the locked region should return a conflicting lock extending to the
    // end of the still-locked region.
    let res = child
        .send_recv(&SerializedLockRequest {
            fd: file.as_raw_fd(),
            cmd: libc::F_GETLK,
            flock: libc::flock {
                l_type: libc::F_WRLCK.try_into().unwrap(),
                l_whence: libc::SEEK_SET.try_into().unwrap(),
                l_start: unlock_middle_flock.l_start + unlock_middle_flock.l_len,
                l_len: 1,
                l_pid: 0,
            },
        })
        .unwrap();
    ensure_ord!(res.rv, ==, 0);
    ensure_ord!(res.flock, ==, libc::flock {
        l_start: unlock_middle_flock.l_start+unlock_middle_flock.l_len,
        l_len: wr_next_100_flock.l_len/2,
        l_pid: std::process::id().try_into().unwrap(),
        ..wr_next_100_flock
    });

    Ok(())
}

fn test_block_on_pid_locks() -> anyhow::Result<()> {
    // Open file before forking, so that child also has the descriptor.
    let file = tempfile::tempfile().unwrap();

    let flock = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        l_whence: libc::SEEK_SET.try_into().unwrap(),
        l_start: 0,
        l_len: 100,
        l_pid: 0,
    };

    // Commands for child
    const LOCK: u8 = 0;
    const UNLOCK: u8 = 1;
    const SLEEP: u8 = 2;

    // Responses from child
    const OK: u8 = 0;
    const ERR: u8 = 1;

    let sleep_duration = std::time::Duration::from_secs(1);

    let mut child = ForkedChild::<u8, u8>::new(|cmd| match *cmd {
        LOCK => match fcntl_lock(file.as_raw_fd(), libc::F_SETLK, &flock) {
            Ok(_) => OK,
            Err(_) => ERR,
        },
        UNLOCK => {
            match fcntl_lock(
                file.as_raw_fd(),
                libc::F_SETLK,
                &libc::flock {
                    l_type: libc::F_UNLCK.try_into().unwrap(),
                    ..flock
                },
            ) {
                Ok(_) => OK,
                Err(_) => ERR,
            }
        }
        SLEEP => {
            std::thread::sleep(sleep_duration);
            OK
        }
        _ => ERR,
    })?;

    // Have child take the lock
    assert_eq!(child.send_recv(&LOCK).unwrap(), OK);

    let t0 = std::time::Instant::now();

    // Tell child to sleep, but don't wait for the response yet
    child.send(&SLEEP).unwrap();
    // Tell child to unlock (after it's done sleeping)
    child.send(&UNLOCK).unwrap();

    // Try to take the lock ourselves, using F_SETLKW to block.
    // This should block until after the child wakes up and unlocks.
    ensure_ord!(fcntl_lock(file.as_raw_fd(), libc::F_SETLKW, &flock), ==, Ok(flock));

    let t1 = std::time::Instant::now();

    // child response to SLEEP
    assert_eq!(child.recv().unwrap(), OK);
    // child response to UNLOCK
    assert_eq!(child.recv().unwrap(), OK);

    let dt = t1 - t0;

    // Should have blocked for roughly `sleep_duration`.
    // Be pretty lenient here for busy machines.
    ensure_ord!(dt.abs_diff(sleep_duration), <=, std::time::Duration::from_millis(100));

    Ok(())
}

fn main() -> anyhow::Result<()> {
    // should we restrict the tests we run?
    let filter_shadow_passing = std::env::args().any(|x| x == "--shadow-passing");
    let filter_libc_passing = std::env::args().any(|x| x == "--libc-passing");
    // should we summarize the results rather than exit on a failed test
    let summarize = std::env::args().any(|x| x == "--summarize");
    let all_envs = set![TestEnvironment::Libc, TestEnvironment::Shadow];
    let no_shadow_envs = set![TestEnvironment::Libc];
    let mut tests: Vec<test_utils::ShadowTest<(), anyhow::Error>> = vec![
        ShadowTest::new(
            "uncontested-pid-locks SETLK",
            || test_uncontested_pid_locks(libc::F_SETLK),
            all_envs.clone(),
        ),
        ShadowTest::new(
            "uncontested-pid-locks SETLKW",
            || test_uncontested_pid_locks(libc::F_SETLKW),
            all_envs.clone(),
        ),
        ShadowTest::new(
            "contested-pid-locks",
            test_contested_pid_locks,
            // TODO: <https://github.com/shadow/shadow/issues/2258>
            no_shadow_envs.clone(),
        ),
        ShadowTest::new(
            "coalese-and-split-pid-locks",
            test_coalesce_and_split_pid_locks,
            // TODO: <https://github.com/shadow/shadow/issues/2258>
            no_shadow_envs.clone(),
        ),
        ShadowTest::new(
            "block-on-pid-locks",
            test_block_on_pid_locks,
            // TODO: <https://github.com/shadow/shadow/issues/2258>
            no_shadow_envs.clone(),
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
