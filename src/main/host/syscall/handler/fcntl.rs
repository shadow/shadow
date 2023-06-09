use linux_api::fcntl::{DescriptorFlags, FcntlCommand, OFlag};
use log::debug;
use nix::errno::Errno;
use shadow_shim_helper_rs::syscall_types::SysCallReg;
use syscall_logger::log_syscall;

use crate::cshadow;
use crate::host::descriptor::{CompatFile, File, FileStatus};
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall_types::SyscallResult;

impl SyscallHandler {
    #[log_syscall(/* rv */ std::ffi::c_int, /* fd */ std::ffi::c_int, /* cmd */ std::ffi::c_int)]
    pub fn fcntl(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
        cmd: std::ffi::c_int,
        arg: std::ffi::c_ulong,
    ) -> SyscallResult {
        // NOTE: this function should *not* run the C syscall handler if the cmd modifies the
        // descriptor

        // helper function to run the C syscall handler
        let legacy_syscall_fn =
            |ctx: &mut SyscallContext| Self::legacy_syscall(cshadow::syscallhandler_fcntl, ctx);

        // get the descriptor, or return early if it doesn't exist
        let mut desc_table = ctx.objs.process.descriptor_table_borrow_mut();
        let desc = Self::get_descriptor_mut(&mut desc_table, fd)?;

        let Ok(cmd) = FcntlCommand::try_from(cmd) else {
            debug!("Bad fcntl command: {cmd}");
            return Err(Errno::EINVAL.into());
        };

        Ok(match cmd {
            FcntlCommand::F_SETLK
            | FcntlCommand::F_SETLKW
            | FcntlCommand::F_OFD_SETLKW
            | FcntlCommand::F_GETLK
            | FcntlCommand::F_OFD_GETLK => {
                match desc.file() {
                    CompatFile::New(_) => {
                        warn_once_then_debug!(
                            "(LOG_ONCE) fcntl({cmd:?}) unimplemented for {:?}",
                            desc.file()
                        );
                        return Err(Errno::ENOSYS.into());
                    }
                    CompatFile::Legacy(_) => {
                        warn_once_then_debug!("(LOG_ONCE) Using fcntl({cmd:?}) implementation that assumes no lock contention. See https://github.com/shadow/shadow/issues/2258");
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
                let flags = file.get_status().as_o_flags() | file.mode().as_o_flags();
                SysCallReg::from(flags.bits())
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
            FcntlCommand::F_GETFD => SysCallReg::from(desc.flags().bits()),
            FcntlCommand::F_SETFD => {
                let flags = i32::try_from(arg).or(Err(Errno::EINVAL))?;
                let flags = DescriptorFlags::from_bits(flags).ok_or(Errno::EINVAL)?;
                desc.set_flags(flags);
                SysCallReg::from(0)
            }
            FcntlCommand::F_DUPFD => {
                let min_fd = arg.try_into().or(Err(Errno::EINVAL))?;

                let new_desc = desc.dup(DescriptorFlags::empty());
                let new_fd = desc_table
                    .register_descriptor_with_min_fd(new_desc, min_fd)
                    .or(Err(Errno::EINVAL))?;
                SysCallReg::from(i32::try_from(new_fd).unwrap())
            }
            FcntlCommand::F_DUPFD_CLOEXEC => {
                let min_fd = arg.try_into().or(Err(Errno::EINVAL))?;

                let new_desc = desc.dup(DescriptorFlags::FD_CLOEXEC);
                let new_fd = desc_table
                    .register_descriptor_with_min_fd(new_desc, min_fd)
                    .or(Err(Errno::EINVAL))?;
                SysCallReg::from(i32::try_from(new_fd).unwrap())
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
                    SysCallReg::from(i32::try_from(pipe.borrow().max_size()).unwrap())
                } else {
                    return Err(Errno::EINVAL.into());
                }
            }
            cmd => {
                warn_once_then_debug!("(LOG_ONCE) Unhandled fcntl command: {cmd:?}");
                return Err(Errno::EINVAL.into());
            }
        })
    }
}
