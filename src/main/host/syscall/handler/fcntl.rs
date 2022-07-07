use crate::cshadow;
use crate::host::context::ThreadContext;
use crate::host::descriptor::{CompatFile, DescriptorFlags, File, FileStatus};
use crate::host::syscall::handler::SyscallHandler;
use crate::host::syscall_types::{SysCallArgs, SysCallReg, SyscallResult};
use log::warn;
use nix::errno::Errno;
use nix::fcntl::OFlag;
use std::os::unix::prelude::RawFd;

use syscall_logger::log_syscall;

impl SyscallHandler {
    #[log_syscall(/* rv */ libc::c_int, /* fd */ libc::c_int, /* cmd */ libc::c_int)]
    pub fn fcntl(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let fd: RawFd = args.args[0].into();
        let cmd: i32 = args.args[1].into();

        // NOTE: this function should *not* run the C syscall handler if the cmd modifies the
        // descriptor

        // helper function to run the C syscall handler
        let legacy_syscall_fn = |ctx: &mut ThreadContext, args: &SysCallArgs| {
            SyscallResult::from(unsafe {
                cshadow::syscallhandler_fcntl(
                    ctx.thread.csyscallhandler(),
                    args as *const cshadow::SysCallArgs,
                )
            })
        };

        // get the descriptor, or return early if it doesn't exist
        let desc = Self::get_descriptor_mut(ctx.process, fd)?;

        Ok(match cmd {
            libc::F_SETLK
            | libc::F_SETLKW
            | libc::F_OFD_SETLKW
            | libc::F_GETLK
            | libc::F_OFD_GETLK => {
                match desc.file() {
                    CompatFile::New(_) => {
                        warn!("fcntl({}) unimplemented for {:?}", cmd, desc.file());
                        return Err(Errno::ENOSYS.into());
                    }
                    CompatFile::Legacy(_) => {
                        warn!("Using fcntl({}) implementation that assumes no lock contention. See https://github.com/shadow/shadow/issues/2258", cmd);
                        return legacy_syscall_fn(ctx, args);
                    }
                };
            }
            libc::F_GETFL => {
                let file = match desc.file() {
                    CompatFile::New(d) => d,
                    // if it's a legacy descriptor, use the C syscall handler instead
                    CompatFile::Legacy(_) => return legacy_syscall_fn(ctx, args),
                };

                let file = file.inner_file().borrow();
                // combine the file status and access mode flags
                let flags = file.get_status().as_o_flags() | file.mode().as_o_flags();
                SysCallReg::from(flags.bits())
            }
            libc::F_SETFL => {
                let file = match desc.file() {
                    CompatFile::New(d) => d,
                    // if it's a legacy descriptor, use the C syscall handler instead
                    CompatFile::Legacy(_) => return legacy_syscall_fn(ctx, args),
                };

                let mut status = OFlag::from_bits(i32::from(args.args[2])).ok_or(Errno::EINVAL)?;
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
                let old_flags = file.get_status().as_o_flags();

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
                SysCallReg::from(0)
            }
            libc::F_GETFD => {
                let flags = desc.flags().bits();
                // the only descriptor flag supported by Linux is FD_CLOEXEC, so let's make sure
                // we're returning the correct value
                debug_assert!(flags == 0 || flags == libc::FD_CLOEXEC);
                SysCallReg::from(flags)
            }
            libc::F_SETFD => {
                let flags =
                    DescriptorFlags::from_bits(i32::from(args.args[2])).ok_or(Errno::EINVAL)?;
                desc.set_flags(flags);
                SysCallReg::from(0)
            }
            libc::F_DUPFD => {
                let min_fd: i32 = args.args[2].into();
                let min_fd: u32 = min_fd.try_into().map_err(|_| nix::errno::Errno::EINVAL)?;

                let new_desc = desc.dup(DescriptorFlags::empty());
                let new_fd = ctx
                    .process
                    .register_descriptor_with_min_fd(new_desc, min_fd);
                SysCallReg::from(i32::try_from(new_fd).unwrap())
            }
            libc::F_DUPFD_CLOEXEC => {
                let min_fd: i32 = args.args[2].into();
                let min_fd: u32 = min_fd.try_into().map_err(|_| nix::errno::Errno::EINVAL)?;

                let new_desc = desc.dup(DescriptorFlags::CLOEXEC);
                let new_fd = ctx
                    .process
                    .register_descriptor_with_min_fd(new_desc, min_fd);
                SysCallReg::from(i32::try_from(new_fd).unwrap())
            }
            libc::F_GETPIPE_SZ => {
                let file = match desc.file() {
                    CompatFile::New(d) => d,
                    // if it's a legacy descriptor, use the C syscall handler instead
                    CompatFile::Legacy(_) => return legacy_syscall_fn(ctx, args),
                };

                #[allow(irrefutable_let_patterns)]
                if let File::Pipe(pipe) = file.inner_file() {
                    SysCallReg::from(i32::try_from(pipe.borrow().max_size()).unwrap())
                } else {
                    return Err(Errno::EINVAL.into());
                }
            }
            _ => return Err(Errno::EINVAL.into()),
        })
    }
}
