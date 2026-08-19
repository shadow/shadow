use linux_api::errno::Errno;
use linux_api::fcntl::{DescriptorFlags, FcntlCommand, FlockType, FlockWhence, OFlag, flock};
use linux_api::unistd::LSeekWhence;
use log::debug;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::cshadow;
use crate::host::descriptor::{CompatFile, File, FileMode, FileStatus};
use crate::host::fcntl_lock_table::{self, FileId};
use crate::host::process::Process;
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::type_formatting::SyscallNonDeterministicArg;
use crate::host::syscall::types::SyscallError;

#[derive(Debug, Copy, Clone)]
struct CommonFlockParams {
    file_id: fcntl_lock_table::FileId,
    start: usize,
    end: usize,
    owner: fcntl_lock_table::LockOwner,
    access: FlockType,
}

/// Common code for interpreting `flock`
fn flock_params(
    file: &CompatFile,
    process: &Process,
    flock: &linux_api::fcntl::flock,
) -> Result<CommonFlockParams, SyscallError> {
    let requested_access = FlockType::try_from(flock.l_type).map_err(|_| Errno::EINVAL)?;
    let access_compat = match requested_access {
        FlockType::F_RDLCK => file.mode().contains(FileMode::READ),
        FlockType::F_WRLCK => file.mode().contains(FileMode::WRITE),
        FlockType::F_UNLCK => true,
    };
    if !access_compat {
        log::debug!(
            "requested access {requested_access:?} incompatible with file mode {:?}",
            file.mode()
        );
        return Err(Errno::EBADF.into());
    }
    let file_id = FileId::from(&file.stat()?);
    let whence = FlockWhence::try_from(flock.l_whence).map_err(|_| Errno::EINVAL)?;
    let origin = match whence {
        FlockWhence::SEEK_SET => 0,
        FlockWhence::SEEK_CUR => file.lseek(0, LSeekWhence::SEEK_CUR)?,
        FlockWhence::SEEK_END => file.stat()?.lst_size,
    };
    let Some(signed_start) = origin.checked_add(flock.l_start) else {
        return Err(Errno::EINVAL.into());
    };
    let (start, end) = if flock.l_len == 0 {
        // fcntl(2): Specifying 0 for l_len has the special meaning: lock all
        // bytes starting at the location specified by l_whence and l_start
        // through to the end of file, no matter how large the file grows.
        let start = usize::try_from(signed_start).map_err(|_| Errno::EINVAL)?;
        (start, FLOCK_LENGTH_0_END)
    } else if flock.l_len < 0 {
        // fcntl(2): if l_len is negative, the interval described by lock
        // covers bytes l_start+l_len  up  to  and  including  l_start-1.
        let start = usize::try_from(signed_start + flock.l_len).map_err(|_| Errno::EINVAL)?;
        let end = usize::try_from(signed_start).map_err(|_| Errno::EINVAL)?;
        (start, end)
    } else {
        let start = usize::try_from(signed_start).map_err(|_| Errno::EINVAL)?;
        // We know length is positive.
        let len =
            usize::try_from(flock.l_len).expect("Negative length should have been caught earlier");
        let Some(end) = start.checked_add(len) else {
            return Err(Errno::EOVERFLOW.into());
        };
        if end > FLOCK_MAX_RANGE_END {
            return Err(Errno::EOVERFLOW.into());
        }
        (start, end)
    };
    if end <= start {
        return Err(Errno::EINVAL.into());
    }
    let requested_owner = fcntl_lock_table::LockOwner::Process(process.id());
    Ok(CommonFlockParams {
        file_id,
        start,
        end,
        owner: requested_owner,
        access: requested_access,
    })
}

// We internally represent flock ranges using an exclusive range of `usize`s.
// Internally, Linux returns an overflow error when the calculated end is
// greater than i64::MAX + 1.  (Perhaps suggesting that it internally uses an
// *inclusive* range of i64s).
const FLOCK_MAX_RANGE_END: usize = (i64::MAX as usize) + 1;
/// flock(2): Specifying 0 for l_len has the special meaning: lock all bytes
/// starting at the location specified by l_whence and l_start through to the
/// end of file, no matter how large the file grows.
///
/// We need to "round-trip" this behavior - returning a length of 0 in `F_GETLK`
/// for a lock that was set with length 0.
///
/// We do this by internally mapping length 0 locks to end at usize::MAX,
/// which is beyond the end that can be specified otherwise.
const FLOCK_LENGTH_0_END: usize = usize::MAX;
const _: () = const {
    assert!(FLOCK_LENGTH_0_END > FLOCK_MAX_RANGE_END);
};

impl SyscallHandler {
    log_syscall!(
        fcntl,
        /* rv */ std::ffi::c_long,
        /* fd */ std::ffi::c_uint,
        /* cmd */ FcntlCommand,
        /* arg */ SyscallNonDeterministicArg<std::ffi::c_ulong>,
    );
    pub fn fcntl(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_uint,
        cmd: std::ffi::c_uint,
        arg: std::ffi::c_ulong,
    ) -> Result<std::ffi::c_long, SyscallError> {
        // NOTE: this function should *not* run the C syscall handler if the cmd modifies the
        // descriptor

        // helper function to run the C syscall handler
        let legacy_syscall_fn =
            |ctx: &mut SyscallContext| Self::legacy_syscall(cshadow::syscallhandler_fcntl, ctx);

        // get the descriptor, or return early if it doesn't exist
        let mut desc_table = ctx.objs.thread.descriptor_table_borrow_mut(ctx.objs.host);
        let desc = Self::get_descriptor_mut(&mut desc_table, fd)?;

        let Ok(cmd) = FcntlCommand::try_from(cmd) else {
            debug!("Bad fcntl command: {cmd}");
            return Err(Errno::EINVAL.into());
        };

        Ok(match cmd {
            FcntlCommand::F_SETLK | FcntlCommand::F_SETLKW => {
                let flock_ptr = ForeignPtr::<()>::from(arg).cast::<flock>();
                let flock = ctx.objs.process.memory_borrow().read(flock_ptr)?;
                let file = desc.file();
                let params = flock_params(file, ctx.objs.process, &flock)?;
                let mut lock_table = ctx.objs.host.fcntl_lock_table_borrow_mut();
                let res = lock_table
                    .set_lock(
                        params.file_id,
                        params.start..params.end,
                        params.owner,
                        params.access,
                    )
                    .map_err(|()| Errno::EACCES);
                log::trace!("setlk[w] {params:?} -> {res:?}");
                if cmd == FcntlCommand::F_SETLKW
                    && [Err(Errno::EACCES), Err(Errno::EAGAIN)].contains(&res)
                {
                    log::warn!(
                        "SETLKW({params:?}) should block, but blocking is unimplemented. Returning {res:?}"
                    );
                }
                res?;
                0i64
            }
            FcntlCommand::F_GETLK => {
                let flock_ptr = ForeignPtr::<()>::from(arg).cast::<flock>();
                let flock = ctx.objs.process.memory_borrow().read(flock_ptr)?;
                let file = desc.file();
                let params = flock_params(file, ctx.objs.process, &flock)?;
                let lock_table = ctx.objs.host.fcntl_lock_table_borrow();
                let out_flock = match lock_table.get_coalesced_conflicting_lock(
                    params.file_id,
                    params.start..params.end,
                    params.owner,
                    params.access,
                ) {
                    Some((conflict_owner, conflict_range, conflict_access)) => {
                        let length = if conflict_range.end == FLOCK_LENGTH_0_END {
                            // "round-trip" length=0 to mean "through EOF". See `FLOCK_LENGTH_0_END`.
                            0
                        } else {
                            i64::try_from(conflict_range.len()).unwrap_or_else(|_err| {
                                panic!("Current lock range {conflict_range:?} end is out of range")
                            })
                        };
                        let start = i64::try_from(conflict_range.start).unwrap_or_else(|_err| {
                            panic!("Current lock range {conflict_range:?} start is out of range")
                        });
                        flock {
                            l_type: conflict_access.into(),
                            l_whence: FlockWhence::SEEK_SET.into(),
                            l_start: start,
                            l_len: length,
                            l_pid: match conflict_owner {
                                fcntl_lock_table::LockOwner::Process(process_id) => {
                                    process_id.into()
                                }
                            },
                        }
                    }
                    None => flock {
                        l_type: FlockType::F_UNLCK.into(),
                        ..flock
                    },
                };
                ctx.objs
                    .process
                    .memory_borrow_mut()
                    .write(flock_ptr, &out_flock)?;
                log::trace!("getlk {params:?} -> {out_flock:?}");
                0i64
            }
            FcntlCommand::F_OFD_SETLK | FcntlCommand::F_OFD_SETLKW | FcntlCommand::F_OFD_GETLK => {
                match desc.file() {
                    CompatFile::New(_) => {
                        warn_once_then_debug!("fcntl({cmd:?}) unimplemented for {:?}", desc.file());
                        return Err(Errno::ENOSYS.into());
                    }
                    CompatFile::Legacy(_) => {
                        warn_once_then_debug!(
                            "Using fcntl({cmd:?}) implementation that assumes no lock contention. \
                            See https://github.com/shadow/shadow/issues/2258"
                        );
                        drop(desc_table);
                        return legacy_syscall_fn(ctx);
                    }
                };
            }
            FcntlCommand::F_GETFL => {
                let file = match desc.file() {
                    CompatFile::New(d) => d,
                    // if it's a legacy file, use the C syscall handler instead
                    CompatFile::Legacy(_) => {
                        drop(desc_table);
                        return legacy_syscall_fn(ctx);
                    }
                };

                let file = file.inner_file().borrow();
                // combine the file status and access mode flags
                let flags = file.status().as_o_flags() | file.mode().as_o_flags();
                flags.bits().into()
            }
            FcntlCommand::F_SETFL => {
                let file = match desc.file() {
                    CompatFile::New(d) => d,
                    // if it's a legacy file, use the C syscall handler instead
                    CompatFile::Legacy(_) => {
                        drop(desc_table);
                        return legacy_syscall_fn(ctx);
                    }
                };

                let status = i32::try_from(arg).or(Err(Errno::EINVAL))?;
                let mut status = OFlag::from_bits(status).ok_or(Errno::EINVAL)?;
                // remove access mode flags
                status.remove(OFlag::O_RDONLY | OFlag::O_WRONLY | OFlag::O_RDWR | OFlag::O_PATH);
                // remove file creation flags
                status.remove(
                    OFlag::O_CLOEXEC
                        | OFlag::O_CREAT
                        | OFlag::O_DIRECTORY
                        | OFlag::O_EXCL
                        | OFlag::O_NOCTTY
                        | OFlag::O_NOFOLLOW
                        | OFlag::O_TMPFILE
                        | OFlag::O_TRUNC,
                );

                let mut file = file.inner_file().borrow_mut();
                let old_flags = file.status().as_o_flags();

                // fcntl(2): "On Linux, this command can change only the O_APPEND, O_ASYNC, O_DIRECT,
                // O_NOATIME, and O_NONBLOCK flags"
                let update_mask = OFlag::O_APPEND
                    | OFlag::O_ASYNC
                    | OFlag::O_DIRECT
                    | OFlag::O_NOATIME
                    | OFlag::O_NONBLOCK;

                // The proper way for the process to update its flags is to:
                //   int flags = fcntl(fd, F_GETFL);
                //   flags = flags | O_NONBLOCK; // add O_NONBLOCK
                //   fcntl(fd, F_SETFL, flags);
                // So if there are flags that we can't update, we should assume they are leftover
                // from the F_GETFL and we shouldn't return an error. This includes `O_DSYNC` and
                // `O_SYNC`, which fcntl(2) says:
                //   "It is not possible to use F_SETFL to change the state of the O_DSYNC and O_SYNC
                //   flags. Attempts to change the state of these flags are silently ignored."
                // In other words, the following code should always be valid:
                //   int flags = fcntl(fd, F_GETFL);
                //   fcntl(fd, F_SETFL, flags); // set to the current existing flags

                // keep the old flags that we can't change, and use the new flags that we can change
                let status = (old_flags & !update_mask) | (status & update_mask);

                let (status, remaining) = FileStatus::from_o_flags(status);

                // check if there are flags that we don't support but Linux does
                if !remaining.is_empty() {
                    return Err(Errno::EINVAL.into());
                }

                file.set_status(status);
                0
            }
            FcntlCommand::F_GETFD => desc.flags().bits().into(),
            FcntlCommand::F_SETFD => {
                let flags = i32::try_from(arg).or(Err(Errno::EINVAL))?;
                let flags = DescriptorFlags::from_bits(flags).ok_or(Errno::EINVAL)?;
                desc.set_flags(flags);
                0
            }
            FcntlCommand::F_DUPFD => {
                let min_fd = arg.try_into().or(Err(Errno::EINVAL))?;

                let new_desc = desc.dup(DescriptorFlags::empty());
                let new_fd = desc_table
                    .register_descriptor_with_min_fd(new_desc, min_fd)
                    .or(Err(Errno::EINVAL))?;
                new_fd.into()
            }
            FcntlCommand::F_DUPFD_CLOEXEC => {
                let min_fd = arg.try_into().or(Err(Errno::EINVAL))?;

                let new_desc = desc.dup(DescriptorFlags::FD_CLOEXEC);
                let new_fd = desc_table
                    .register_descriptor_with_min_fd(new_desc, min_fd)
                    .or(Err(Errno::EINVAL))?;
                new_fd.into()
            }
            FcntlCommand::F_GETPIPE_SZ => {
                let file = match desc.file() {
                    CompatFile::New(d) => d,
                    // if it's a legacy file, use the C syscall handler instead
                    CompatFile::Legacy(_) => {
                        return legacy_syscall_fn(ctx);
                    }
                };

                if let File::Pipe(pipe) = file.inner_file() {
                    pipe.borrow().max_size().try_into().unwrap()
                } else {
                    return Err(Errno::EINVAL.into());
                }
            }
            cmd => {
                warn_once_then_debug!("Unhandled fcntl command: {cmd:?}");
                return Err(Errno::EINVAL.into());
            }
        })
    }
}
