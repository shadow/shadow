use linux_api::errno::Errno;
use num_enum::{IntoPrimitive, TryFromPrimitive};
use std::{
    io::{Seek as _, SeekFrom, Write as _},
    ops::Neg as _,
    os::fd::AsRawFd,
};
use test_utils::{ForkedChild, ShadowTest, TestEnvironment, ensure_in, ensure_ord, set};

// fcntl commands that set (or clear) a range lock, and that should behave
// identically when the requested lock is uncontested.
#[allow(non_camel_case_types)]
#[repr(i32)]
#[derive(Copy, Clone, Debug, IntoPrimitive, TryFromPrimitive)]
enum FcntlPosixSetlkUncontestedCommand {
    F_SETLK = libc::F_SETLK,
    F_SETLKW = libc::F_SETLKW,
}

// fcntl commands whose third argument is a pointer to `libc::flock`.
#[allow(non_camel_case_types)]
#[repr(i32)]
#[derive(Copy, Clone, Debug, IntoPrimitive, TryFromPrimitive)]
enum FcntlFlockCommand {
    F_SETLK = libc::F_SETLK,
    F_SETLKW = libc::F_SETLKW,
    F_GETLK = libc::F_GETLK,
    F_OFD_SETLK = libc::F_OFD_SETLK,
    F_OFD_SETLKW = libc::F_OFD_SETLKW,
    F_OFD_GETLK = libc::F_OFD_GETLK,
}

impl From<FcntlPosixSetlkUncontestedCommand> for FcntlFlockCommand {
    fn from(value: FcntlPosixSetlkUncontestedCommand) -> Self {
        Self::try_from(i32::from(value)).unwrap()
    }
}

/// safer, convenient wrapper around fcntl for lock operations.
///
/// TODO: take a Rust file descriptor type, once we've upgraded to a rustix
/// version that uses them instead of its own.
fn fcntl_lock(
    raw_fd: libc::c_int,
    cmd: FcntlFlockCommand,
    flock: &libc::flock,
) -> Result<libc::flock, Errno> {
    let mut res = *flock;
    Errno::result_from_libc_errno(-1, unsafe { libc::fcntl(raw_fd, cmd.into(), &mut res) })?;
    Ok(res)
}

/// test taking uncontested locks, using either F_SETLK or F_SETLKW.
fn test_uncontested_pid_locks(cmd: FcntlPosixSetlkUncontestedCommand) -> anyhow::Result<()> {
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
    {
        let out_flock = fcntl_lock(file.as_raw_fd(), cmd.into(), &rd_flock)?;
        ensure_ord!(out_flock, ==, rd_flock);
    }

    // Taking a read lock where we already have one still succeeds.
    {
        let out_flock = fcntl_lock(file.as_raw_fd(), cmd.into(), &rd_flock)?;
        ensure_ord!(out_flock, ==, rd_flock);
    }

    // Upgrading a read lock to a write lock succeeds.
    let wr_flock = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        ..rd_flock
    };
    {
        let out_flock = fcntl_lock(file.as_raw_fd(), cmd.into(), &wr_flock)?;
        ensure_ord!(out_flock, ==, wr_flock);
    }

    // GETLK, when there is no conflicting lock (which is the case when we own the only existing lock)...
    {
        // operation should succeed.
        let out_flock = fcntl_lock(file.as_raw_fd(), FcntlFlockCommand::F_GETLK, &rd_flock)?;
        // the flock struct should have l_type be set to F_UNLCK, and otherwise unchanged.
        ensure_ord!(out_flock, ==, libc::flock {
            l_type: i16::try_from(libc::F_UNLCK).unwrap(),
            ..rd_flock
        });
    }

    let unlck = libc::flock {
        l_type: libc::F_UNLCK.try_into().unwrap(),
        ..rd_flock
    };
    {
        // unlock should succeed.
        let out_flock = fcntl_lock(file.as_raw_fd(), cmd.into(), &unlck)?;
        // flock should be unmodified.
        ensure_ord!(out_flock, ==, unlck);
    }
    {
        // unlock should still succeed with nothing holding the lock.
        let out_flock = fcntl_lock(file.as_raw_fd(), cmd.into(), &unlck)?;
        // flock should again be unmodified.
        ensure_ord!(out_flock, ==, unlck);
    }

    let named_file = tempfile::NamedTempFile::new().unwrap();
    let read_only_file = std::fs::File::open(named_file.path()).unwrap();

    // We can take a read lock from a read-only descriptor.
    fcntl_lock(read_only_file.as_raw_fd(), cmd.into(), &rd_flock)?;

    // We *cannot* take a write lock from a read-only descriptor.
    {
        let res = fcntl_lock(read_only_file.as_raw_fd(), cmd.into(), &wr_flock);
        ensure_ord!(res, ==, Err(Errno::EBADF));
    }

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

impl SerializedLockResponse {
    fn to_result(self) -> Result<libc::flock, Errno> {
        if self.rv == -1 {
            return Err(Errno::from_libc_errnum(self.errno).expect("Bad errno"));
        }
        if self.rv != 0 {
            panic!("Unexpected rv: {}", self.rv);
        }
        Ok(self.flock)
    }
}

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

fn test_contested_pid_locks(cmd: FcntlPosixSetlkUncontestedCommand) -> anyhow::Result<()> {
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
    fcntl_lock(file.as_raw_fd(), cmd.into(), &rd_flock)?;

    {
        let req = SerializedLockRequest {
            fd: file.as_raw_fd(),
            cmd: cmd.into(),
            flock: rd_flock,
        };
        // Child should be able to take the same read lock.
        let out_flock = child.send_recv(&req)?.to_result()?;
        // flock struct should be unmodified.
        ensure_ord!(out_flock, ==, req.flock);
    }

    // We should *not* be able to upgrade to a write lock, since the child now
    // has a read lock.
    let wr_flock = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        ..rd_flock
    };

    {
        // GETLK should succeed...
        let out_flock = fcntl_lock(file.as_raw_fd(), FcntlFlockCommand::F_GETLK, &wr_flock)?;
        // ... telling us about the child's conflicting lock.
        ensure_ord!(out_flock, ==, libc::flock {
            l_pid: child.pid(),
            ..rd_flock
        });
    }

    // Trying to take a write lock anyway should fail (as GETLK just told is it would).
    // (We only test F_SETLK here rather than `cmd`; blocking behavior with F_SETLKW is in another test).
    {
        let res = fcntl_lock(file.as_raw_fd(), FcntlFlockCommand::F_SETLK, &wr_flock);
        ensure_in!(&res, [Err(Errno::EACCES), Err(Errno::EAGAIN)]);
    }

    // Tell the child to release its lock, which should succeed.
    let unlck_flock = libc::flock {
        l_type: libc::F_UNLCK.try_into().unwrap(),
        ..rd_flock
    };
    {
        // The child should successfully release the lock.
        let out_flock = child
            .send_recv(&SerializedLockRequest {
                fd: file.as_raw_fd(),
                cmd: cmd.into(),
                flock: unlck_flock,
            })?
            .to_result()?;
        // output flock should be unmodified.
        ensure_ord!(out_flock, ==, unlck_flock);
    }

    {
        // We should now be free to upgrade to a write lock.
        let out_flock = fcntl_lock(file.as_raw_fd(), cmd.into(), &wr_flock)?;
        // output flock should be unmodified.
        ensure_ord!(out_flock, ==, wr_flock);
    }

    Ok(())
}

fn test_pid_lock_overflow(cmd: FcntlPosixSetlkUncontestedCommand) -> anyhow::Result<()> {
    // Open file before forking, so that child also has the descriptor.
    let file = tempfile::tempfile().unwrap();

    // Experimentally, max start+len is (i64::MAX+1). Presumably Linux
    // internally uses a "closed" calculated end-offset, such that it is exactly
    // `i64` in this case.
    let wr_flock_max = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        l_whence: libc::SEEK_SET.try_into().unwrap(),
        l_start: i64::MAX,
        l_len: 1,
        l_pid: 0,
    };

    // One beyond what we think is the max should result in an overflow error.
    {
        let res = fcntl_lock(
            file.as_raw_fd(),
            cmd.into(),
            &libc::flock {
                l_len: wr_flock_max.l_len + 1,
                ..wr_flock_max
            },
        );
        ensure_ord!(res, ==, Err(Errno::EOVERFLOW));
    }

    // Taking `wr_flock_max` should succeed.
    fcntl_lock(file.as_raw_fd(), cmd.into(), &wr_flock_max)?;

    Ok(())
}

fn test_pid_lock_max_range(cmd: FcntlPosixSetlkUncontestedCommand) -> anyhow::Result<()> {
    let file = tempfile::tempfile().unwrap();

    // lock 0..i64::MAX
    fcntl_lock(
        file.as_raw_fd(),
        cmd.into(),
        &libc::flock {
            l_type: libc::F_WRLCK.try_into().unwrap(),
            l_whence: libc::SEEK_SET.try_into().unwrap(),
            l_start: 0,
            l_len: i64::MAX,
            l_pid: 0,
        },
    )?;

    // We can lock one more byte beyond that (see overflow test above),
    // such that the coalesced lock has length i64::MAX+1.
    fcntl_lock(
        file.as_raw_fd(),
        cmd.into(),
        &libc::flock {
            l_type: libc::F_WRLCK.try_into().unwrap(),
            l_whence: libc::SEEK_SET.try_into().unwrap(),
            l_start: i64::MAX,
            l_len: 1,
            l_pid: 0,
        },
    )?;

    // Get the coalesced conflicting lock.
    let mut child = ForkedChild::new(handle_lock_request)?;
    let conflict_flock = {
        // The child should successfully release the lock.
        child
            .send_recv(&SerializedLockRequest {
                fd: file.as_raw_fd(),
                cmd: FcntlFlockCommand::F_GETLK.into(),
                flock: libc::flock {
                    l_type: libc::F_WRLCK.try_into().unwrap(),
                    l_whence: libc::SEEK_SET.try_into().unwrap(),
                    l_start: 0,
                    l_len: 1,
                    l_pid: 0,
                },
            })?
            .to_result()?
    };

    {
        let expected = libc::flock {
            l_type: libc::F_WRLCK.try_into().unwrap(),
            l_whence: libc::SEEK_SET.try_into().unwrap(),
            l_start: 0,
            // effective length of the coalesced lock is i64::MAX+1. This gets
            // mapped back to length=0.
            l_len: 0,
            l_pid: std::process::id().try_into().unwrap(),
        };
        ensure_ord!(conflict_flock, ==, expected);
    }

    Ok(())
}

#[derive(Debug, Copy, Clone, Eq, PartialEq)]
enum CloseDescriptor {
    Original,
    Secondary,
}

fn test_pid_owner_closes_descriptor(
    cmd: FcntlPosixSetlkUncontestedCommand,
    close_descriptor: CloseDescriptor,
) -> anyhow::Result<()> {
    let mut file1 = Some(tempfile::NamedTempFile::new().unwrap());
    let fd1 = file1.as_ref().unwrap().as_raw_fd();

    // Get another descriptor to the same file.
    let mut file2 = Some(
        std::fs::OpenOptions::new()
            .read(true)
            .write(true)
            .open(file1.as_ref().unwrap().path())
            .unwrap(),
    );
    let fd2 = file2.as_ref().unwrap().as_raw_fd();

    let mut child = ForkedChild::new(handle_lock_request)?;

    // Take a write lock
    let wr_flock = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        l_whence: libc::SEEK_SET.try_into().unwrap(),
        l_start: 0,
        l_len: 100,
        l_pid: 0,
    };
    fcntl_lock(fd1, cmd.into(), &wr_flock)?;

    // child should see the lock, through either descriptor.
    for fd in [fd1, fd2] {
        let out_flock = child
            .send_recv(&SerializedLockRequest {
                fd,
                cmd: FcntlFlockCommand::F_GETLK.into(),
                flock: wr_flock,
            })?
            .to_result()?;
        ensure_ord!(out_flock.l_pid, ==, i32::try_from(std::process::id()).unwrap());
    }

    // close one of our descriptors to the file
    match close_descriptor {
        CloseDescriptor::Original => {
            file1.take().unwrap();
        }
        CloseDescriptor::Secondary => {
            file2.take().unwrap();
        }
    }

    // whichever descriptor we closed, our locks should be gone.
    // fcntl(2): "If a process closes any file descriptor referring to a file,
    // then all of the process's locks on that file are released, regardless of
    // the file descriptor(s) on which the locks were obtained."
    //
    // child should be able to take the lock, through either descriptor.
    for fd in [fd1, fd2] {
        let out_flock = child
            .send_recv(&SerializedLockRequest {
                fd,
                cmd: FcntlFlockCommand::F_GETLK.into(),
                flock: wr_flock,
            })?
            .to_result()?;
        ensure_ord!(out_flock.l_pid, ==, 0);
    }

    Ok(())
}

fn test_pid_owner_exits(cmd: FcntlPosixSetlkUncontestedCommand) -> anyhow::Result<()> {
    let file = tempfile::tempfile().unwrap();
    let mut child = ForkedChild::new(handle_lock_request)?;

    // Have child take a write lock.
    let wr_flock = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        l_whence: libc::SEEK_SET.try_into().unwrap(),
        l_start: 0,
        l_len: 100,
        l_pid: 0,
    };
    child
        .send_recv(&SerializedLockRequest {
            fd: file.as_raw_fd(),
            cmd: cmd.into(),
            flock: wr_flock,
        })?
        .to_result()?;

    // Double-check that we see the lock.
    {
        let out_flock = fcntl_lock(file.as_raw_fd(), FcntlFlockCommand::F_GETLK, &wr_flock)?;
        ensure_ord!(out_flock, ==, libc::flock {l_pid: child.pid(), ..wr_flock });
        ensure_ord!(out_flock.l_pid, ==, child.pid());
    }

    // Have child exit, which should cause the lock to be released.
    drop(child);

    // We should no longer see the lock.
    {
        let out_flock = fcntl_lock(file.as_raw_fd(), FcntlFlockCommand::F_GETLK, &wr_flock)?;
        ensure_ord!(out_flock, ==, libc::flock {l_type: libc::F_UNLCK.try_into().unwrap(), ..wr_flock });
    }

    Ok(())
}

fn test_pid_lock_threads(cmd: FcntlPosixSetlkUncontestedCommand) -> anyhow::Result<()> {
    let file = tempfile::tempfile().unwrap();

    // Take a write lock
    let wr_flock = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        l_whence: libc::SEEK_SET.try_into().unwrap(),
        l_start: 0,
        l_len: 100,
        l_pid: 0,
    };
    fcntl_lock(file.as_raw_fd(), cmd.into(), &wr_flock)?;

    // child thread should be able to take the same lock, since it's owned by the *process*.
    let fd = file.as_raw_fd();
    std::thread::spawn(move || fcntl_lock(fd, cmd.into(), &wr_flock))
        .join()
        .unwrap()?;

    // child *process* should see conflicting lock; i.e. should not have been
    // dropped as a result of tearing down the thread.
    let mut child = ForkedChild::new(handle_lock_request)?;
    let out_flock = child
        .send_recv(&SerializedLockRequest {
            fd,
            cmd: FcntlFlockCommand::F_GETLK.into(),
            flock: wr_flock,
        })?
        .to_result()?;
    ensure_ord!(out_flock, ==, libc::flock {
        l_pid: std::process::id().try_into().unwrap(),
        ..wr_flock
    });

    Ok(())
}

fn test_pid_lock_seek_end(cmd: FcntlPosixSetlkUncontestedCommand) -> anyhow::Result<()> {
    // Open file before forking, so that child also has the descriptor.
    let mut file = tempfile::tempfile().unwrap();

    let mut child = ForkedChild::new(handle_lock_request)?;

    // Push EOF to 10 bytes.
    file.write_all(&[0u8; 10])?;

    // Seek back 5 bytes, so that EOF != current position.
    file.seek(SeekFrom::Current(-5))?;

    // Take a write lock, from EOF + positive offset.
    let wr_flock = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        l_whence: libc::SEEK_END.try_into().unwrap(),
        l_start: 1,
        l_len: 1,
        l_pid: 0,
    };
    fcntl_lock(file.as_raw_fd(), cmd.into(), &wr_flock)?;

    // This should overlap the lock taken by the parent.
    let child_attempted_flock = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        l_whence: libc::SEEK_SET.try_into().unwrap(),
        l_start: 10 + 1,
        l_len: 100,
        l_pid: 0,
    };
    // parent's lock should conflict, and it should be specified in absolute
    // terms - relative to SEEK_SET not SEEK_END.
    let expected_conflict = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        l_whence: libc::SEEK_SET.try_into().unwrap(),
        l_start: 10 + 1,
        l_len: 1,
        l_pid: std::process::id().try_into().unwrap(),
    };
    {
        let out_flock = child
            .send_recv(&SerializedLockRequest {
                fd: file.as_raw_fd(),
                cmd: FcntlFlockCommand::F_GETLK.into(),
                flock: child_attempted_flock,
            })?
            .to_result()?;
        ensure_ord!(out_flock, ==, expected_conflict);
    }

    // Writing more data, and thus moving the EOF, should not move the lock.
    // The above lock attempt shoudl still return the exact same conflicting lock.
    file.seek(SeekFrom::End(0))?;
    file.write_all(&[0u8; 10])?;
    {
        let out_flock = child
            .send_recv(&SerializedLockRequest {
                fd: file.as_raw_fd(),
                cmd: FcntlFlockCommand::F_GETLK.into(),
                flock: child_attempted_flock,
            })?
            .to_result()?;
        ensure_ord!(out_flock, ==, expected_conflict);
    }

    // Release previous locks.
    fcntl_lock(
        file.as_raw_fd(),
        cmd.into(),
        &libc::flock {
            l_type: libc::F_UNLCK.try_into().unwrap(),
            l_whence: libc::SEEK_SET.try_into().unwrap(),
            l_start: 0,
            l_len: i64::MAX,
            l_pid: 0,
        },
    )?;

    // Take a write lock, from EOF + negative offset.
    let wr_flock = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        l_whence: libc::SEEK_END.try_into().unwrap(),
        l_start: -5,
        l_len: 1,
        l_pid: 0,
    };
    fcntl_lock(file.as_raw_fd(), cmd.into(), &wr_flock)?;

    // This should overlap the lock taken by the parent.
    let child_attempted_flock = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        l_whence: libc::SEEK_SET.try_into().unwrap(),
        l_start: 20 - 5,
        l_len: 100,
        l_pid: 0,
    };
    // parent's lock should conflict, and it should be specified in absolute
    // terms - relative to SEEK_SET not SEEK_END.
    let expected_conflict = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        l_whence: libc::SEEK_SET.try_into().unwrap(),
        l_start: 20 - 5,
        l_len: 1,
        l_pid: std::process::id().try_into().unwrap(),
    };
    {
        let out_flock = child
            .send_recv(&SerializedLockRequest {
                fd: file.as_raw_fd(),
                cmd: FcntlFlockCommand::F_GETLK.into(),
                flock: child_attempted_flock,
            })?
            .to_result()?;
        ensure_ord!(out_flock, ==, expected_conflict);
    }

    // A negative start that goes beyond beginning of file should fail.
    let wr_flock = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        l_whence: libc::SEEK_END.try_into().unwrap(),
        l_start: i64::try_from(file.metadata()?.len()).unwrap().neg() - 1,
        l_len: 1,
        l_pid: 0,
    };
    {
        let lock_res = fcntl_lock(file.as_raw_fd(), cmd.into(), &wr_flock);
        ensure_ord!(lock_res, ==, Err(Errno::EINVAL));
    }

    Ok(())
}

fn test_pid_lock_seek_cur(cmd: FcntlPosixSetlkUncontestedCommand) -> anyhow::Result<()> {
    // Open file before forking, so that child also has the descriptor.
    let mut file = tempfile::tempfile().unwrap();

    let mut child = ForkedChild::new(handle_lock_request)?;

    // Push EOF to 10 bytes.
    file.write_all(&[0u8; 10])?;

    // Seek back to byte 5, so that EOF != current position.
    file.seek(SeekFrom::Start(5))?;

    // Take a write lock, from SEEK_CUR + positive offset.
    let wr_flock = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        l_whence: libc::SEEK_CUR.try_into().unwrap(),
        l_start: 1,
        l_len: 1,
        l_pid: 0,
    };
    fcntl_lock(file.as_raw_fd(), cmd.into(), &wr_flock)?;

    // This should overlap the lock taken by the parent.
    let child_attempted_flock = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        l_whence: libc::SEEK_SET.try_into().unwrap(),
        l_start: 5 + 1,
        l_len: 100,
        l_pid: 0,
    };
    // parent's lock should conflict, and it should be specified in absolute
    // terms - relative to SEEK_SET not SEEK_CUR.
    let expected_conflict = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        l_whence: libc::SEEK_SET.try_into().unwrap(),
        l_start: 5 + 1,
        l_len: 1,
        l_pid: std::process::id().try_into().unwrap(),
    };
    {
        let flock_out = child
            .send_recv(&SerializedLockRequest {
                fd: file.as_raw_fd(),
                cmd: FcntlFlockCommand::F_GETLK.into(),
                flock: child_attempted_flock,
            })?
            .to_result()?;
        ensure_ord!(flock_out, ==, expected_conflict);
    }

    // Seeking to a different position shouldn't move the lock.
    file.seek(SeekFrom::Start(1))?;
    {
        let flock_out = child
            .send_recv(&SerializedLockRequest {
                fd: file.as_raw_fd(),
                cmd: FcntlFlockCommand::F_GETLK.into(),
                flock: child_attempted_flock,
            })?
            .to_result()?;
        ensure_ord!(flock_out, ==, expected_conflict);
    }

    // Release previous locks.
    fcntl_lock(
        file.as_raw_fd(),
        cmd.into(),
        &libc::flock {
            l_type: libc::F_UNLCK.try_into().unwrap(),
            l_whence: libc::SEEK_SET.try_into().unwrap(),
            l_start: 0,
            l_len: i64::MAX,
            l_pid: 0,
        },
    )?;

    // Take a write lock, from current position + negative offset.
    file.seek(SeekFrom::Start(2))?;
    let wr_flock = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        l_whence: libc::SEEK_CUR.try_into().unwrap(),
        l_start: -1,
        l_len: 1,
        l_pid: 0,
    };
    fcntl_lock(file.as_raw_fd(), cmd.into(), &wr_flock)?;

    // This should overlap the lock taken by the parent.
    let child_attempted_flock = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        l_whence: libc::SEEK_SET.try_into().unwrap(),
        l_start: 1,
        l_len: 100,
        l_pid: 0,
    };
    // parent's lock should conflict, and it should be specified in absolute
    // terms - relative to SEEK_SET not SEEK_CUR.
    let expected_conflict = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        l_whence: libc::SEEK_SET.try_into().unwrap(),
        l_start: 1,
        l_len: 1,
        l_pid: std::process::id().try_into().unwrap(),
    };
    {
        let flock_out = child
            .send_recv(&SerializedLockRequest {
                fd: file.as_raw_fd(),
                cmd: FcntlFlockCommand::F_GETLK.into(),
                flock: child_attempted_flock,
            })?
            .to_result()?;
        ensure_ord!(flock_out, ==, expected_conflict);
    }

    // A negative start that goes beyond beginning of file should fail.
    let wr_flock = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        l_whence: libc::SEEK_CUR.try_into().unwrap(),
        l_start: -3,
        l_len: 1,
        l_pid: 0,
    };
    {
        let lock_res = fcntl_lock(file.as_raw_fd(), cmd.into(), &wr_flock);
        ensure_ord!(lock_res, ==, Err(Errno::EINVAL));
    }
    Ok(())
}

/// Tests setting a lock with `l_len=0` using `cmd`. Queries are done with F_GETLK.
fn test_zero_len_pid_locks(cmd: FcntlPosixSetlkUncontestedCommand) -> anyhow::Result<()> {
    let file = tempfile::tempfile().unwrap();
    let mut child = ForkedChild::new(handle_lock_request)?;

    // fcntl(2):
    // "Specifying 0 for l_len has the special meaning: > lock all bytes
    // starting at the location specified by l_whence and l_start through to the
    // end of file, no matter how large the file grows."
    let zerolen_flock = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        l_whence: libc::SEEK_SET.try_into().unwrap(),
        l_start: 100,
        l_len: 0,
        l_pid: 0,
    };
    {
        let out_flock = child
            .send_recv(&SerializedLockRequest {
                fd: file.as_raw_fd(),
                cmd: cmd.into(),
                flock: zerolen_flock,
            })?
            .to_result()?;
        ensure_ord!(out_flock, ==, zerolen_flock);
    }

    // first 100 bytes should still be available to lock
    let begin_flock = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        l_whence: libc::SEEK_SET.try_into().unwrap(),
        l_start: 0,
        l_len: 100,
        l_pid: 0,
    };
    {
        let out_flock = fcntl_lock(file.as_raw_fd(), FcntlFlockCommand::F_GETLK, &begin_flock)?;
        ensure_ord!(out_flock.l_type, ==, i16::try_from(libc::F_UNLCK).unwrap());
    }

    // next byte should conflict with child's lock
    {
        let out_flock = fcntl_lock(
            file.as_raw_fd(),
            FcntlFlockCommand::F_GETLK,
            &libc::flock {
                l_type: libc::F_WRLCK.try_into().unwrap(),
                l_whence: libc::SEEK_SET.try_into().unwrap(),
                l_start: 100,
                l_len: 1,
                l_pid: 0,
            },
        )?;
        ensure_ord!(out_flock, ==, libc::flock{
            l_pid: child.pid(),
            ..zerolen_flock}
        );
    }

    // last byte should conflict with child's lock
    {
        let out_flock = fcntl_lock(
            file.as_raw_fd(),
            FcntlFlockCommand::F_GETLK,
            &libc::flock {
                l_type: libc::F_WRLCK.try_into().unwrap(),
                l_whence: libc::SEEK_SET.try_into().unwrap(),
                l_start: i64::MAX,
                l_len: 1,
                l_pid: 0,
            },
        )?;
        ensure_ord!(out_flock, ==, libc::flock{
            l_pid: child.pid(),
            ..zerolen_flock}
        );
    }

    Ok(())
}

/// Tests setting a lock with `l_len<0` using `cmd`. Queries are done with F_GETLK.
fn test_negative_len_pid_locks(cmd: FcntlPosixSetlkUncontestedCommand) -> anyhow::Result<()> {
    let file = tempfile::tempfile().unwrap();
    let mut child = ForkedChild::new(handle_lock_request)?;

    // fcntl(2): if l_len is negative, the interval described by lock covers
    // bytes l_start+l_len  up  to  and  including  l_start-1.

    let negative_len_flock = libc::flock {
        l_type: libc::F_WRLCK.try_into().unwrap(),
        l_whence: libc::SEEK_SET.try_into().unwrap(),
        l_start: 100,
        l_len: -10,
        l_pid: 0,
    };

    // Take the lock, which should succeed.
    fcntl_lock(file.as_raw_fd(), cmd.into(), &negative_len_flock)?;

    // Use GETLK from child to inspect the lock.
    {
        let out_flock = child
            .send_recv(&SerializedLockRequest {
                fd: file.as_raw_fd(),
                cmd: FcntlFlockCommand::F_GETLK.into(),
                flock: negative_len_flock,
            })?
            .to_result()?;
        ensure_ord!(out_flock, ==, libc::flock {
            l_type: libc::F_WRLCK.try_into().unwrap(),
            l_whence: libc::SEEK_SET.try_into().unwrap(),
            l_start: 90,
            l_len: 10,
            l_pid: std::process::id().try_into().unwrap(),
        });
    }

    Ok(())
}

fn test_coalesce_and_split_pid_locks(cmd: FcntlPosixSetlkUncontestedCommand) -> anyhow::Result<()> {
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
    fcntl_lock(file.as_raw_fd(), cmd.into(), &wr100_flock)?;

    // Lock next 100 bytes
    let wr_next_100_flock = libc::flock {
        l_start: wr100_flock.l_start + wr100_flock.l_len,
        ..wr100_flock
    };
    fcntl_lock(file.as_raw_fd(), cmd.into(), &wr_next_100_flock)?;

    // Child querying any part of the locked region should see a coalesced lock.
    {
        let out_flock = child
            .send_recv(&SerializedLockRequest {
                fd: file.as_raw_fd(),
                cmd: FcntlFlockCommand::F_GETLK.into(),
                flock: wr100_flock,
            })?
            .to_result()?;
        ensure_ord!(out_flock, ==, libc::flock {
            l_len: wr100_flock.l_len*2,
            l_pid: std::process::id().try_into().unwrap(),
            ..wr100_flock
        });
    }

    // Unlock middle of the region
    let unlock_middle_flock = libc::flock {
        l_type: libc::F_UNLCK.try_into().unwrap(),
        l_start: wr100_flock.l_len / 2,
        ..wr100_flock
    };
    fcntl_lock(file.as_raw_fd(), cmd.into(), &unlock_middle_flock)?;

    // Querying byte 0 should return a conflicting lock extending to the
    // beginning of where we unlocked.
    {
        let out_flock = child
            .send_recv(&SerializedLockRequest {
                fd: file.as_raw_fd(),
                cmd: FcntlFlockCommand::F_GETLK.into(),
                flock: libc::flock {
                    l_type: libc::F_WRLCK.try_into().unwrap(),
                    l_whence: libc::SEEK_SET.try_into().unwrap(),
                    l_start: 0,
                    l_len: 1,
                    l_pid: 0,
                },
            })?
            .to_result()?;
        ensure_ord!(out_flock, ==, libc::flock {
            l_len: unlock_middle_flock.l_start,
            l_pid: std::process::id().try_into().unwrap(),
            ..wr100_flock
        });
    }

    // Querying byte 0 of the unlocked region should indicate no conflicting lock.
    {
        let query_flock = libc::flock {
            l_type: libc::F_WRLCK.try_into().unwrap(),
            l_whence: libc::SEEK_SET.try_into().unwrap(),
            l_start: unlock_middle_flock.l_start,
            l_len: 1,
            l_pid: 0,
        };
        let out_flock = child
            .send_recv(&SerializedLockRequest {
                fd: file.as_raw_fd(),
                cmd: FcntlFlockCommand::F_GETLK.into(),
                flock: query_flock,
            })?
            .to_result()?;
        ensure_ord!(out_flock, ==, libc::flock {
            l_type: libc::F_UNLCK.try_into().unwrap(),
            ..query_flock
        });
    }

    // Querying first byte after the locked region should return a conflicting lock extending to the
    // end of the still-locked region.
    {
        let out_flock = child
            .send_recv(&SerializedLockRequest {
                fd: file.as_raw_fd(),
                cmd: FcntlFlockCommand::F_GETLK.into(),
                flock: libc::flock {
                    l_type: libc::F_WRLCK.try_into().unwrap(),
                    l_whence: libc::SEEK_SET.try_into().unwrap(),
                    l_start: unlock_middle_flock.l_start + unlock_middle_flock.l_len,
                    l_len: 1,
                    l_pid: 0,
                },
            })?
            .to_result()?;
        ensure_ord!(out_flock, ==, libc::flock {
            l_start: unlock_middle_flock.l_start+unlock_middle_flock.l_len,
            l_len: wr_next_100_flock.l_len/2,
            l_pid: std::process::id().try_into().unwrap(),
            ..wr_next_100_flock
        });
    }

    Ok(())
}

fn test_overlapping_pid_locks(cmd: FcntlPosixSetlkUncontestedCommand) -> anyhow::Result<()> {
    // Open file before forking, so that child also has the descriptor.
    let file = tempfile::tempfile()?;

    // child1 takes a reader lock of bytes 0..100
    let child1_flock = libc::flock {
        l_type: libc::F_RDLCK.try_into().unwrap(),
        l_whence: libc::SEEK_SET.try_into().unwrap(),
        l_start: 0,
        l_len: 100,
        l_pid: 0,
    };
    let mut child1 = ForkedChild::new(handle_lock_request)?;
    child1
        .send_recv(&SerializedLockRequest {
            fd: file.as_raw_fd(),
            cmd: cmd.into(),
            flock: child1_flock,
        })?
        .to_result()?;

    // child2 takes an overlapping reader lock of bytes 25..50
    let child2_flock = libc::flock {
        l_type: libc::F_RDLCK.try_into()?,
        l_whence: libc::SEEK_SET.try_into()?,
        l_start: 25,
        l_len: 25,
        l_pid: 0,
    };
    let mut child2 = ForkedChild::new(handle_lock_request)?;
    child2
        .send_recv(&SerializedLockRequest {
            fd: file.as_raw_fd(),
            cmd: cmd.into(),
            flock: child2_flock,
        })?
        .to_result()?;

    // querying the first byte of the child1 lock should describe the *whole*
    // child1 lock, even though some of the lock's range is shared. (e.g. it
    // can't return just the first, unshared, part of the child1 lock).
    {
        let out_flock = fcntl_lock(
            file.as_raw_fd(),
            FcntlFlockCommand::F_GETLK,
            &libc::flock {
                l_type: libc::F_WRLCK.try_into()?,
                l_whence: libc::SEEK_SET.try_into()?,
                l_start: child1_flock.l_start,
                l_len: 1,
                l_pid: 0,
            },
        )?;
        ensure_ord!(out_flock, ==, libc::flock{
            l_pid: child1.pid(),
            ..child1_flock
        });
    }

    // querying the first byte of the child2 lock can return *either* of the two
    // locks covering that byte.
    {
        let out_flock = fcntl_lock(
            file.as_raw_fd(),
            FcntlFlockCommand::F_GETLK,
            &libc::flock {
                l_type: libc::F_WRLCK.try_into()?,
                l_whence: libc::SEEK_SET.try_into()?,
                l_start: child2_flock.l_start,
                l_len: 1,
                l_pid: 0,
            },
        )?;
        ensure_in!(&out_flock.l_pid, [child1.pid(), child2.pid()]);
    }
    // querying the last byte of the child1 lock should return that one (again
    // describing the whole range covered by that lock)
    {
        let out_flock = fcntl_lock(
            file.as_raw_fd(),
            FcntlFlockCommand::F_GETLK,
            &libc::flock {
                l_type: libc::F_WRLCK.try_into()?,
                l_whence: libc::SEEK_SET.try_into()?,
                l_start: child1_flock.l_start + child1_flock.l_len - 1,
                l_len: 1,
                l_pid: 0,
            },
        )?;
        ensure_ord!(out_flock, ==, libc::flock{
            l_pid: child1.pid(),
            ..child1_flock
        });
    }

    Ok(())
}

fn test_unlock_pid_locks_edge_cases(cmd: FcntlPosixSetlkUncontestedCommand) -> anyhow::Result<()> {
    let file = tempfile::tempfile()?;

    // Lock a range
    let wr_flock = libc::flock {
        l_type: libc::F_WRLCK.try_into()?,
        l_whence: libc::SEEK_SET.try_into()?,
        l_start: 0,
        l_len: 100,
        l_pid: 0,
    };
    fcntl_lock(file.as_raw_fd(), cmd.into(), &wr_flock)?;

    let unlk_flock = libc::flock {
        l_type: libc::F_UNLCK.try_into()?,
        ..wr_flock
    };
    // Unlocking the locked range should succeed.
    {
        let out_flock = fcntl_lock(file.as_raw_fd(), cmd.into(), &unlk_flock)?;
        ensure_ord!(out_flock, ==,unlk_flock);
    }

    // Unlocking a range where there are no locks should still work.
    {
        let out_flock = fcntl_lock(file.as_raw_fd(), cmd.into(), &unlk_flock)?;
        ensure_ord!(out_flock, ==,unlk_flock);
    }

    // Take a write lock in a child process
    let mut child = ForkedChild::new(handle_lock_request)?;
    child
        .send_recv(&SerializedLockRequest {
            fd: file.as_raw_fd(),
            cmd: cmd.into(),
            flock: wr_flock,
        })?
        .to_result()?;

    // Unlocking a range where we don't have a lock *and others do*, should still succeed.
    {
        let out_flock = fcntl_lock(file.as_raw_fd(), cmd.into(), &unlk_flock)?;
        ensure_ord!(out_flock, ==,unlk_flock);
    }

    // The above unlock should not have affected the child's lock.
    // `getlk` should still tell us about it.
    {
        let out_flock = fcntl_lock(file.as_raw_fd(), FcntlFlockCommand::F_GETLK, &wr_flock)?;
        ensure_ord!(out_flock, ==, libc::flock {
            l_pid: child.pid(),
            ..wr_flock
        });
    }

    Ok(())
}

fn test_block_on_pid_locks(setlk_cmd: FcntlPosixSetlkUncontestedCommand) -> anyhow::Result<()> {
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
        LOCK => match fcntl_lock(file.as_raw_fd(), setlk_cmd.into(), &flock) {
            Ok(_) => OK,
            Err(_) => ERR,
        },
        UNLOCK => {
            match fcntl_lock(
                file.as_raw_fd(),
                setlk_cmd.into(),
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
    {
        let child_response = child.send_recv(&LOCK)?;
        ensure_ord!(child_response, ==, OK);
    }

    let t0 = std::time::Instant::now();

    // Tell child to sleep, but don't wait for the response yet
    child.send(&SLEEP).unwrap();
    // Tell child to unlock (after it's done sleeping)
    child.send(&UNLOCK).unwrap();

    // Try to take the lock ourselves, using F_SETLKW to block.
    // This should block until after the child wakes up and unlocks.
    {
        let out_flock = fcntl_lock(file.as_raw_fd(), FcntlFlockCommand::F_SETLKW, &flock)?;
        ensure_ord!(out_flock, ==, flock);
    }

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
    let posix_uncontested_setlk_commands = [
        FcntlPosixSetlkUncontestedCommand::F_SETLK,
        FcntlPosixSetlkUncontestedCommand::F_SETLKW,
    ];
    let mut tests: Vec<test_utils::ShadowTest<(), anyhow::Error>> = Vec::new();
    // We test both F_SETLK and F_SETLKW in cases where we expect the operation
    // to immediately succeed, in which case they should behave identically.
    for cmd in posix_uncontested_setlk_commands {
        tests.extend([
            ShadowTest::new(
                &format!("uncontested-pid-locks {cmd:?}"),
                move || test_uncontested_pid_locks(cmd),
                all_envs.clone(),
            ),
            ShadowTest::new(
                &format!("pid-lock-seek-end {cmd:?}"),
                move || test_pid_lock_seek_end(cmd),
                all_envs.clone(),
            ),
            ShadowTest::new(
                &format!("pid-lock-seek-cur {cmd:?}"),
                move || test_pid_lock_seek_cur(cmd),
                all_envs.clone(),
            ),
            ShadowTest::new(
                &format!("zero-len-pid-locks {cmd:?}"),
                move || test_zero_len_pid_locks(cmd),
                all_envs.clone(),
            ),
            ShadowTest::new(
                &format!("negative-len-pid-locks {cmd:?}"),
                move || test_negative_len_pid_locks(cmd),
                all_envs.clone(),
            ),
            ShadowTest::new(
                &format!("contested-pid-locks {cmd:?}"),
                move || test_contested_pid_locks(cmd),
                all_envs.clone(),
            ),
            ShadowTest::new(
                &format!("pid-owner_exits {cmd:?}"),
                move || test_pid_owner_exits(cmd),
                all_envs.clone(),
            ),
            ShadowTest::new(
                &format!("pid-lock-threads {cmd:?}"),
                move || test_pid_lock_threads(cmd),
                all_envs.clone(),
            ),
            ShadowTest::new(
                &format!("coalesce-and-split-pid-locks {cmd:?}"),
                move || test_coalesce_and_split_pid_locks(cmd),
                all_envs.clone(),
            ),
            ShadowTest::new(
                &format!("overlapping-pid-locks {cmd:?}"),
                move || test_overlapping_pid_locks(cmd),
                all_envs.clone(),
            ),
            ShadowTest::new(
                &format!("block-on-pid-locks {cmd:?}"),
                move || test_block_on_pid_locks(cmd),
                // TODO: <https://github.com/shadow/shadow/issues/2258>
                no_shadow_envs.clone(),
            ),
            ShadowTest::new(
                &format!("unlock-pid-locks-edge-cases {cmd:?}"),
                move || test_unlock_pid_locks_edge_cases(cmd),
                all_envs.clone(),
            ),
            ShadowTest::new(
                &format!("pid-lock-overflow {cmd:?}"),
                move || test_pid_lock_overflow(cmd),
                all_envs.clone(),
            ),
            ShadowTest::new(
                &format!("pid-lock-max-range {cmd:?}"),
                move || test_pid_lock_max_range(cmd),
                all_envs.clone(),
            ),
        ]);
    }
    for cmd in posix_uncontested_setlk_commands {
        for close_descriptor in [CloseDescriptor::Original, CloseDescriptor::Secondary] {
            tests.extend([ShadowTest::new(
                &format!("pid-owner-closes-descriptor {cmd:?} {close_descriptor:?}"),
                move || test_pid_owner_closes_descriptor(cmd, close_descriptor),
                all_envs.clone(),
            )])
        }
    }
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
