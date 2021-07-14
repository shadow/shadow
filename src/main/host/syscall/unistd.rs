use crate::cshadow as c;
use crate::host::context::{ThreadContext, ThreadContextObjs};
use crate::host::descriptor::pipe;
use crate::host::descriptor::{
    CompatDescriptor, Descriptor, DescriptorFlags, FileFlags, FileMode, FileStatus, PosixFile,
};
use crate::host::syscall::{self, Trigger};
use crate::host::syscall_condition::SysCallCondition;
use crate::host::syscall_types::{PluginPtr, SysCallArgs, TypedPluginPtr};
use crate::host::syscall_types::{SyscallError, SyscallResult};
use crate::utility::event_queue::EventQueue;

use std::sync::Arc;

use atomic_refcell::AtomicRefCell;
use log::*;
use nix::errno::Errno;

pub fn close(ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
    let fd = libc::c_int::from(args.get(0));

    trace!("Trying to close fd {}", fd);

    // scope used to make sure that desc cannot be used after deregistering it
    {
        // get the descriptor, or return early if it doesn't exist
        let desc = unsafe { &*syscall::get_descriptor(fd, ctx.process.raw_mut())? };

        // if it's a legacy descriptor, use the C syscall handler instead
        if let CompatDescriptor::Legacy(_) = desc {
            return unsafe {
                c::syscallhandler_close(ctx.thread.csyscallhandler(), args as *const c::SysCallArgs)
            }
            .into();
        }
    }

    // according to "man 2 close", in Linux any errors that may occur will happen after the fd is
    // released, so we should always deregister the descriptor
    let desc = unsafe { c::process_deregisterCompatDescriptor(ctx.process.raw_mut(), fd) };
    let desc = CompatDescriptor::from_raw(desc).unwrap();
    let desc = match *desc {
        CompatDescriptor::New(d) => d,
        _ => unreachable!(),
    };

    let result = EventQueue::queue_and_run(|event_queue| desc.close(event_queue));

    if let Some(result) = result {
        result.into()
    } else {
        Ok(0.into())
    }
}

pub fn dup(ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
    let fd = libc::c_int::from(args.get(0));

    // get the descriptor, or return early if it doesn't exist
    let desc = unsafe { &*syscall::get_descriptor(fd, ctx.process.raw_mut())? };

    match desc {
        CompatDescriptor::New(desc) => dup_helper(ctx, fd, desc),
        // if it's a legacy descriptor, use the C syscall handler instead
        CompatDescriptor::Legacy(_) => unsafe {
            c::syscallhandler_dup(ctx.thread.csyscallhandler(), args as *const c::SysCallArgs)
                .into()
        },
    }
}

pub fn dup_helper(ctx: &mut ThreadContext, fd: libc::c_int, desc: &Descriptor) -> SyscallResult {
    trace!("Duping fd {} ({:?})", fd, desc);

    // clone the descriptor (but not the flags)
    let mut new_desc = desc.clone();
    new_desc.set_flags(DescriptorFlags::empty());

    // register the descriptor
    let new_desc = CompatDescriptor::New(new_desc);
    let new_fd = unsafe {
        c::process_registerCompatDescriptor(
            ctx.process.raw_mut(),
            CompatDescriptor::into_raw(Box::new(new_desc)),
        )
    };

    // return the new fd
    Ok(new_fd.into())
}

pub fn read(ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
    let fd = libc::c_int::from(args.get(0));
    let buf_ptr = PluginPtr::from(args.get(1));
    let buf_size = libc::size_t::from(args.get(2));
    let offset = 0;

    // get the descriptor, or return early if it doesn't exist
    let desc = unsafe { &*syscall::get_descriptor(fd, ctx.process.raw_mut())? };

    match desc {
        CompatDescriptor::New(desc) => read_helper(ctx, fd, desc, buf_ptr, buf_size, offset),
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
    let desc = unsafe { &*syscall::get_descriptor(fd, ctx.process.raw_mut())? };

    match desc {
        CompatDescriptor::New(desc) => read_helper(ctx, fd, desc, buf_ptr, buf_size, offset),
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
    desc: &Descriptor,
    buf_ptr: PluginPtr,
    buf_size: libc::size_t,
    offset: libc::off_t,
) -> SyscallResult {
    let posix_file = desc.get_file();
    let file_flags = posix_file.borrow().get_flags();

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
    if result == Err(Errno::EWOULDBLOCK.into()) && !file_flags.contains(FileFlags::NONBLOCK) {
        let trigger = Trigger::from_posix_file(posix_file, FileStatus::READABLE);

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
    let desc = unsafe { &*syscall::get_descriptor(fd, ctx.process.raw_mut())? };

    match desc {
        CompatDescriptor::New(desc) => write_helper(ctx, fd, desc, buf_ptr, buf_size, offset),
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
    let desc = unsafe { &*syscall::get_descriptor(fd, ctx.process.raw_mut())? };

    match desc {
        CompatDescriptor::New(desc) => write_helper(ctx, fd, desc, buf_ptr, buf_size, offset),
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
    desc: &Descriptor,
    buf_ptr: PluginPtr,
    buf_size: libc::size_t,
    offset: libc::off_t,
) -> SyscallResult {
    let posix_file = desc.get_file();
    let file_flags = posix_file.borrow().get_flags();

    let result=
        // call the file's write(), and run any resulting events
        EventQueue::queue_and_run(|event_queue| {
            posix_file.borrow_mut().write(
                ctx.process.memory().reader(TypedPluginPtr::<u8>::new(buf_ptr, buf_size)),
                offset,
                event_queue,
            )
        });

    // if the syscall would block and it's a blocking descriptor
    if result == Err(Errno::EWOULDBLOCK.into()) && !file_flags.contains(FileFlags::NONBLOCK) {
        let trigger = Trigger::from_posix_file(posix_file, FileStatus::WRITABLE);

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

    let mut file_flags = FileFlags::empty();
    let mut descriptor_flags = DescriptorFlags::empty();

    // keep track of which flags we use
    let mut remaining_flags = flags;

    if flags & libc::O_NONBLOCK != 0 {
        file_flags.insert(FileFlags::NONBLOCK);
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
    let reader = pipe::PipeFile::new(Arc::clone(&buffer), FileMode::READ, file_flags);
    let reader = Arc::new(AtomicRefCell::new(PosixFile::Pipe(reader)));

    // reference-counted file object for write end of the pipe
    let writer = pipe::PipeFile::new(Arc::clone(&buffer), FileMode::WRITE, file_flags);
    let writer = Arc::new(AtomicRefCell::new(PosixFile::Pipe(writer)));

    // set the file objects to listen for events on the buffer
    pipe::PipeFile::enable_notifications(&reader);
    pipe::PipeFile::enable_notifications(&writer);

    // file descriptors for the read and write file objects
    let mut reader_desc = Descriptor::new(reader);
    let mut writer_desc = Descriptor::new(writer);

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
