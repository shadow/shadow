use crate::cshadow;
use crate::host::context::{ThreadContext, ThreadContextObjs};
use crate::host::descriptor::{CompatDescriptor, DescriptorFlags, FileStatus, PosixFile};
use crate::host::syscall;
use crate::host::syscall_types::SyscallResult;
use crate::host::syscall_types::{SysCallArgs, SysCallReg};
use log::*;
use nix::errno::Errno;
use nix::fcntl::OFlag;
use std::convert::{TryFrom, TryInto};
use std::os::unix::prelude::RawFd;

fn fcntl(ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
    let fd: RawFd = args.args[0].into();
    let cmd: i32 = args.args[1].into();

    // get the descriptor, or return early if it doesn't exist
    let desc = match syscall::get_descriptor_mut(ctx.process, fd)? {
        CompatDescriptor::New(d) => d,
        // if it's a legacy descriptor, use the C syscall handler instead
        CompatDescriptor::Legacy(_) => {
            return unsafe {
                cshadow::syscallhandler_fcntl(
                    ctx.thread.csyscallhandler(),
                    args as *const cshadow::SysCallArgs,
                )
            }
            .into()
        }
    };

    Ok(match cmd {
        libc::F_GETFL => {
            let file = desc.get_file().borrow();
            // combine the file status and access mode flags
            let flags = file.get_status().as_o_flags() | file.mode().as_o_flags();
            SysCallReg::from(flags.bits())
        }
        libc::F_SETFL => {
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
            let (status, remaining) = FileStatus::from_o_flags(status);

            // check if there are flags that we don't support
            if !remaining.is_empty() {
                return Err(Errno::EINVAL.into());
            }

            desc.get_file().borrow_mut().set_status(status);
            SysCallReg::from(0)
        }
        libc::F_GETFD => {
            let flags = desc.get_flags().bits();
            // the only descriptor flag supported by Linux is FD_CLOEXEC, so let's make sure
            // we're returning the correct value
            debug_assert!(flags == 0 || flags == libc::FD_CLOEXEC);
            SysCallReg::from(flags)
        }
        libc::F_SETFD => {
            let flags = DescriptorFlags::from_bits(i32::from(args.args[2])).ok_or(Errno::EINVAL)?;
            desc.set_flags(flags);
            SysCallReg::from(0)
        }
        libc::F_DUPFD => {
            let min_fd: i32 = args.args[2].into();
            let min_fd: u32 = min_fd.try_into().map_err(|_| nix::errno::Errno::EINVAL)?;

            let new_desc = CompatDescriptor::New(desc.dup(DescriptorFlags::empty()));
            let new_fd = ctx
                .process
                .register_descriptor_with_min_fd(new_desc, min_fd);
            SysCallReg::from(i32::try_from(new_fd).unwrap())
        }
        libc::F_DUPFD_CLOEXEC => {
            let min_fd: i32 = args.args[2].into();
            let min_fd: u32 = min_fd.try_into().map_err(|_| nix::errno::Errno::EINVAL)?;

            let new_desc = CompatDescriptor::New(desc.dup(DescriptorFlags::CLOEXEC));
            let new_fd = ctx
                .process
                .register_descriptor_with_min_fd(new_desc, min_fd);
            SysCallReg::from(i32::try_from(new_fd).unwrap())
        }
        libc::F_GETPIPE_SZ =>
        {
            #[allow(irrefutable_let_patterns)]
            if let PosixFile::Pipe(pipe) = desc.get_file() {
                SysCallReg::from(i32::try_from(pipe.borrow().max_size()).unwrap())
            } else {
                Err(Errno::EINVAL)?
            }
        }
        _ => Err(Errno::EINVAL)?,
    })
}

mod export {
    use super::*;
    use crate::utility::notnull::notnull_mut_debug;

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_fcntl(
        sys: *mut cshadow::SysCallHandler,
        args: *const cshadow::SysCallArgs,
    ) -> cshadow::SysCallReturn {
        let mut objs = unsafe { ThreadContextObjs::from_syscallhandler(notnull_mut_debug(sys)) };
        fcntl(&mut objs.borrow(), unsafe { args.as_ref().unwrap() }).into()
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_fcntl64(
        sys: *mut cshadow::SysCallHandler,
        args: *const cshadow::SysCallArgs,
    ) -> cshadow::SysCallReturn {
        // Our fcntl supports the flock64 struct when any of the F_GETLK64, F_SETLK64, and F_SETLKW64
        // commands are specified, so we can just use our fcntl handler directly.
        trace!("fcntl64 called, forwarding to fcntl handler");
        rustsyscallhandler_fcntl(sys, args)
    }
}
