use crate::cshadow as c;
use crate::host::context::{ThreadContext, ThreadContextObjs};
use crate::host::descriptor::pipe;
use crate::host::descriptor::{
    CompatDescriptor, Descriptor, DescriptorFlags, FileMode, FileState, FileStatus, PosixFile,
};
use crate::host::syscall::{self, Trigger};
use crate::host::syscall_condition::SysCallCondition;
use crate::host::syscall_types::{PluginPtr, SysCallArgs, TypedPluginPtr};
use crate::host::syscall_types::{SyscallError, SyscallResult};
use crate::utility::event_queue::EventQueue;

use std::convert::{TryFrom, TryInto};
use std::sync::Arc;

use atomic_refcell::AtomicRefCell;
use log::*;
use nix::errno::Errno;

pub fn close(ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
    let fd = libc::c_int::from(args.get(0));

    trace!("Trying to close fd {}", fd);

    let fd: u32 = fd.try_into().map_err(|_| nix::errno::Errno::EBADF)?;

    // according to "man 2 close", in Linux any errors that may occur will happen after the fd is
    // released, so we should always deregister the descriptor even if there's an error while
    // closing
    let desc = ctx
        .process
        .deregister_descriptor(fd)
        .ok_or(nix::errno::Errno::EBADF)?;

    // if there are still valid descriptors to the posix file, close() will do nothing
    // and return None
    EventQueue::queue_and_run(|event_queue| desc.close(ctx.host.chost(), event_queue))
        .unwrap_or(Ok(0.into()))
}

pub fn dup(ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
    let fd = libc::c_int::from(args.get(0));

    // get the descriptor, or return early if it doesn't exist
    let desc = match syscall::get_descriptor(ctx.process, fd)? {
        CompatDescriptor::New(desc) => desc,
        // if it's a legacy descriptor, use the C syscall handler instead
        CompatDescriptor::Legacy(_) => unsafe {
            return c::syscallhandler_dup(
                ctx.thread.csyscallhandler(),
                args as *const c::SysCallArgs,
            )
            .into();
        },
    };

    // duplicate the descriptor
    let new_desc = CompatDescriptor::New(desc.dup(DescriptorFlags::empty()));
    let new_fd = ctx.process.register_descriptor(new_desc);

    // return the new fd
    Ok(libc::c_int::try_from(new_fd).unwrap().into())
}

pub fn dup2(ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
    let old_fd = libc::c_int::from(args.get(0));
    let new_fd = libc::c_int::from(args.get(1));

    // get the descriptor, or return early if it doesn't exist
    let desc = match syscall::get_descriptor(ctx.process, old_fd)? {
        CompatDescriptor::New(desc) => desc,
        // we don't support dup2 for legacy descriptors
        CompatDescriptor::Legacy(_) => {
            warn!(
                "dup2() is not supported for legacy descriptors (fd={})",
                old_fd
            );
            return Err(nix::errno::Errno::ENOSYS.into());
        }
    };

    // from 'man 2 dup2': "If oldfd is a valid file descriptor, and newfd has the same
    // value as oldfd, then dup2() does nothing, and returns newfd"
    if old_fd == new_fd {
        return Ok(new_fd.into());
    }

    let new_fd: u32 = new_fd.try_into().map_err(|_| nix::errno::Errno::EBADF)?;

    // duplicate the descriptor
    let new_desc = CompatDescriptor::New(desc.dup(DescriptorFlags::empty()));
    let replaced_desc = ctx.process.register_descriptor_with_fd(new_desc, new_fd);

    // close the replaced descriptor
    if let Some(replaced_desc) = replaced_desc {
        // from 'man 2 dup2': "If newfd was open, any errors that would have been reported at
        // close(2) time are lost"
        EventQueue::queue_and_run(|event_queue| replaced_desc.close(ctx.host.chost(), event_queue));
    }

    // return the new fd
    Ok(libc::c_int::try_from(new_fd).unwrap().into())
}

pub fn dup3(ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
    let old_fd = libc::c_int::from(args.get(0));
    let new_fd = libc::c_int::from(args.get(1));
    let flags = libc::c_int::from(args.get(2));

    // get the descriptor, or return early if it doesn't exist
    let desc = match syscall::get_descriptor(ctx.process, old_fd)? {
        CompatDescriptor::New(desc) => desc,
        // we don't support dup3 for legacy descriptors
        CompatDescriptor::Legacy(_) => {
            warn!(
                "dup3() is not supported for legacy descriptors (fd={})",
                old_fd
            );
            return Err(nix::errno::Errno::ENOSYS.into());
        }
    };

    // from 'man 2 dup3': "If oldfd equals newfd, then dup3() fails with the error EINVAL"
    if old_fd == new_fd {
        return Err(nix::errno::Errno::EINVAL.into());
    }

    let new_fd: u32 = new_fd.try_into().map_err(|_| nix::errno::Errno::EBADF)?;

    // dup3 only supports the O_CLOEXEC flag
    let flags = match flags {
        libc::O_CLOEXEC => DescriptorFlags::CLOEXEC,
        0 => DescriptorFlags::empty(),
        _ => return Err(nix::errno::Errno::EINVAL.into()),
    };

    // duplicate the descriptor
    let new_desc = CompatDescriptor::New(desc.dup(flags));
    let replaced_desc = ctx.process.register_descriptor_with_fd(new_desc, new_fd);

    // close the replaced descriptor
    if let Some(replaced_desc) = replaced_desc {
        // from 'man 2 dup3': "If newfd was open, any errors that would have been reported at
        // close(2) time are lost"
        EventQueue::queue_and_run(|event_queue| replaced_desc.close(ctx.host.chost(), event_queue));
    }

    // return the new fd
    Ok(libc::c_int::try_from(new_fd).unwrap().into())
}

pub fn read(ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
    let fd = libc::c_int::from(args.get(0));
    let buf_ptr = PluginPtr::from(args.get(1));
    let buf_size = libc::size_t::from(args.get(2));
    let offset = 0;

    // get the descriptor, or return early if it doesn't exist
    match syscall::get_descriptor(ctx.process, fd)? {
        CompatDescriptor::New(desc) => {
            let file = desc.get_file().clone();
            read_helper(ctx, fd, &file, buf_ptr, buf_size, offset)
        }
        // if it's a legacy descriptor, use the C syscall handler instead
        CompatDescriptor::Legacy(_) => unsafe {
            c::syscallhandler_read(ctx.thread.csyscallhandler(), args as *const SysCallArgs).into()
        },
    }
}

pub fn pread64(ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
    let fd = libc::c_int::from(args.get(0));
    let buf_ptr = PluginPtr::from(args.get(1));
    let buf_size = libc::size_t::from(args.get(2));
    let offset = libc::off_t::from(args.get(3));

    // get the descriptor, or return early if it doesn't exist
    match syscall::get_descriptor(ctx.process, fd)? {
        CompatDescriptor::New(desc) => {
            let file = desc.get_file().clone();
            read_helper(ctx, fd, &file, buf_ptr, buf_size, offset)
        }
        // if it's a legacy descriptor, use the C syscall handler instead
        CompatDescriptor::Legacy(_) => unsafe {
            c::syscallhandler_pread64(ctx.thread.csyscallhandler(), args as *const c::SysCallArgs)
                .into()
        },
    }
}

fn read_helper(
    ctx: &mut ThreadContext,
    _fd: libc::c_int,
    posix_file: &PosixFile,
    buf_ptr: PluginPtr,
    buf_size: libc::size_t,
    offset: libc::off_t,
) -> SyscallResult {
    let file_status = posix_file.borrow().get_status();

    let result =
        // call the file's read(), and run any resulting events
        EventQueue::queue_and_run(|event_queue| {
            posix_file.borrow_mut().read(
                ctx.process.memory_mut().writer(TypedPluginPtr::new(buf_ptr, buf_size)),
                offset,
                event_queue,
            )
        });

    // if the syscall would block and it's a blocking descriptor
    if result == Err(Errno::EWOULDBLOCK.into()) && !file_status.contains(FileStatus::NONBLOCK) {
        let trigger = Trigger::from_posix_file(posix_file, FileState::READABLE);

        return Err(SyscallError::Cond(SysCallCondition::new(trigger)));
    }

    result
}

pub fn write(ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
    let fd = libc::c_int::from(args.get(0));
    let buf_ptr = PluginPtr::from(args.get(1));
    let buf_size = libc::size_t::from(args.get(2));
    let offset = 0;

    // get the descriptor, or return early if it doesn't exist
    match syscall::get_descriptor(ctx.process, fd)? {
        CompatDescriptor::New(desc) => {
            let file = desc.get_file().clone();
            write_helper(ctx, fd, &file, buf_ptr, buf_size, offset)
        }
        // if it's a legacy descriptor, use the C syscall handler instead
        CompatDescriptor::Legacy(_) => unsafe {
            c::syscallhandler_write(ctx.thread.csyscallhandler(), args as *const c::SysCallArgs)
                .into()
        },
    }
}

pub fn pwrite64(ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
    let fd = libc::c_int::from(args.get(0));
    let buf_ptr = PluginPtr::from(args.get(1));
    let buf_size = libc::size_t::from(args.get(2));
    let offset = libc::off_t::from(args.get(3));

    // get the descriptor, or return early if it doesn't exist
    match syscall::get_descriptor(ctx.process, fd)? {
        CompatDescriptor::New(desc) => {
            let file = desc.get_file().clone();
            write_helper(ctx, fd, &file, buf_ptr, buf_size, offset)
        }
        // if it's a legacy descriptor, use the C syscall handler instead
        CompatDescriptor::Legacy(_) => unsafe {
            c::syscallhandler_pwrite64(ctx.thread.csyscallhandler(), args as *const SysCallArgs)
                .into()
        },
    }
}

fn write_helper(
    ctx: &mut ThreadContext,
    _fd: libc::c_int,
    posix_file: &PosixFile,
    buf_ptr: PluginPtr,
    buf_size: libc::size_t,
    offset: libc::off_t,
) -> SyscallResult {
    let file_status = posix_file.borrow().get_status();

    let result =
        // call the file's write(), and run any resulting events
        EventQueue::queue_and_run(|event_queue| {
            posix_file.borrow_mut().write(
                ctx.process.memory().reader(TypedPluginPtr::<u8>::new(buf_ptr, buf_size)),
                offset,
                event_queue,
            )
        });

    // if the syscall would block and it's a blocking descriptor
    if result == Err(Errno::EWOULDBLOCK.into()) && !file_status.contains(FileStatus::NONBLOCK) {
        let trigger = Trigger::from_posix_file(posix_file, FileState::WRITABLE);

        return Err(SyscallError::Cond(SysCallCondition::new(trigger)));
    };

    result
}

pub fn pipe(ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
    let fd_ptr: PluginPtr = args.args[0].into();

    pipe_helper(ctx, fd_ptr, 0)
}

pub fn pipe2(ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
    let fd_ptr: PluginPtr = args.args[0].into();
    let flags = unsafe { args.args[1].as_u64 } as libc::c_int;

    pipe_helper(ctx, fd_ptr, flags)
}

fn pipe_helper(ctx: &mut ThreadContext, fd_ptr: PluginPtr, flags: i32) -> SyscallResult {
    // make sure they didn't pass a NULL pointer
    if fd_ptr.is_null() {
        return Err(nix::errno::Errno::EFAULT.into());
    }

    let mut file_flags = FileStatus::empty();
    let mut descriptor_flags = DescriptorFlags::empty();

    // keep track of which flags we use
    let mut remaining_flags = flags;

    if flags & libc::O_NONBLOCK != 0 {
        file_flags.insert(FileStatus::NONBLOCK);
        remaining_flags &= !libc::O_NONBLOCK;
    }

    if flags & libc::O_CLOEXEC != 0 {
        descriptor_flags.insert(DescriptorFlags::CLOEXEC);
        remaining_flags &= !libc::O_CLOEXEC;
    }

    // the user requested flags that we don't support
    if remaining_flags != 0 {
        // exit early if the O_DIRECT flag was set
        if remaining_flags & libc::O_DIRECT != 0 {
            warn!("We don't currently support pipes in 'O_DIRECT' mode");
            return Err(nix::errno::Errno::EOPNOTSUPP.into());
        }
        warn!("Ignoring pipe flags");
    }

    // reference-counted buffer for the pipe
    let buffer = pipe::SharedBuf::new();
    let buffer = Arc::new(AtomicRefCell::new(buffer));

    // reference-counted file object for read end of the pipe
    let reader = pipe::PipeFile::new(FileMode::READ, file_flags);
    let reader = Arc::new(AtomicRefCell::new(reader));

    // reference-counted file object for write end of the pipe
    let writer = pipe::PipeFile::new(FileMode::WRITE, file_flags);
    let writer = Arc::new(AtomicRefCell::new(writer));

    // set the file objects to listen for events on the buffer
    EventQueue::queue_and_run(|event_queue| {
        pipe::PipeFile::connect_to_buffer(&reader, Arc::clone(&buffer), event_queue);
        pipe::PipeFile::connect_to_buffer(&writer, Arc::clone(&buffer), event_queue);
    });

    // file descriptors for the read and write file objects
    let mut reader_desc = Descriptor::new(PosixFile::Pipe(reader));
    let mut writer_desc = Descriptor::new(PosixFile::Pipe(writer));

    // set the file descriptor flags
    reader_desc.set_flags(descriptor_flags);
    writer_desc.set_flags(descriptor_flags);

    // register the file descriptors
    let fds = unsafe {
        [
            c::process_registerCompatDescriptor(
                ctx.process.raw_mut(),
                CompatDescriptor::into_raw(Box::new(CompatDescriptor::New(reader_desc))),
            ),
            c::process_registerCompatDescriptor(
                ctx.process.raw_mut(),
                CompatDescriptor::into_raw(Box::new(CompatDescriptor::New(writer_desc))),
            ),
        ]
    };

    // Try to write them to the caller
    let write_res = ctx
        .process
        .memory_mut()
        .copy_to_ptr(TypedPluginPtr::new(fd_ptr.into(), 2), &fds);

    // Clean up in case of error
    match write_res {
        Ok(_) => Ok(0.into()),
        Err(e) => {
            unsafe {
                c::process_deregisterCompatDescriptor(ctx.process.raw_mut(), fds[0]);
                c::process_deregisterCompatDescriptor(ctx.process.raw_mut(), fds[1]);
            };
            Err(e.into())
        }
    }
}

mod export {
    use crate::utility::notnull::notnull_mut_debug;

    use super::*;

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_close(
        sys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        let mut objs = unsafe { ThreadContextObjs::from_syscallhandler(notnull_mut_debug(sys)) };
        close(&mut objs.borrow(), unsafe { args.as_ref().unwrap() }).into()
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_dup(
        sys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        let mut objs = unsafe { ThreadContextObjs::from_syscallhandler(notnull_mut_debug(sys)) };
        dup(&mut objs.borrow(), unsafe { args.as_ref().unwrap() }).into()
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_dup2(
        sys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        let mut objs = unsafe { ThreadContextObjs::from_syscallhandler(notnull_mut_debug(sys)) };
        dup2(&mut objs.borrow(), unsafe { args.as_ref().unwrap() }).into()
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_dup3(
        sys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        let mut objs = unsafe { ThreadContextObjs::from_syscallhandler(notnull_mut_debug(sys)) };
        dup3(&mut objs.borrow(), unsafe { args.as_ref().unwrap() }).into()
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_read(
        sys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        let mut objs = unsafe { ThreadContextObjs::from_syscallhandler(notnull_mut_debug(sys)) };
        read(&mut objs.borrow(), unsafe { args.as_ref().unwrap() }).into()
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_pread64(
        sys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        let mut objs = unsafe { ThreadContextObjs::from_syscallhandler(notnull_mut_debug(sys)) };
        pread64(&mut objs.borrow(), unsafe { args.as_ref().unwrap() }).into()
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_write(
        sys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        let mut objs = unsafe { ThreadContextObjs::from_syscallhandler(notnull_mut_debug(sys)) };
        write(&mut objs.borrow(), unsafe { args.as_ref().unwrap() }).into()
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_pwrite64(
        sys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        let mut objs = unsafe { ThreadContextObjs::from_syscallhandler(notnull_mut_debug(sys)) };
        pwrite64(&mut objs.borrow(), unsafe { args.as_ref().unwrap() }).into()
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_pipe(
        sys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        let mut objs = unsafe { ThreadContextObjs::from_syscallhandler(notnull_mut_debug(sys)) };
        pipe(&mut objs.borrow(), unsafe { args.as_ref().unwrap() }).into()
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_pipe2(
        sys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        let mut objs = unsafe { ThreadContextObjs::from_syscallhandler(notnull_mut_debug(sys)) };
        pipe2(&mut objs.borrow(), unsafe { args.as_ref().unwrap() }).into()
    }
}
