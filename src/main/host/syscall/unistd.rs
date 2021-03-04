use crate::cshadow as c;
use crate::host::descriptor::pipe;
use crate::host::descriptor::{
    CompatDescriptor, Descriptor, DescriptorFlags, FileFlags, FileMode, FileStatus, PosixFile,
    SyscallReturn,
};
use crate::host::syscall;
use crate::host::syscall::Trigger;
use crate::utility::event_queue::EventQueue;

use std::sync::Arc;

use atomic_refcell::AtomicRefCell;
use log::*;

pub fn close(sys: &mut c::SysCallHandler, args: &c::SysCallArgs) -> c::SysCallReturn {
    let fd = unsafe { args.args[0].as_i64 } as libc::c_int;

    debug!("Trying to close fd {}", fd);

    // scope used to make sure that desc cannot be used after deregistering it
    {
        // get the descriptor, or return early if it doesn't exist
        let desc = match syscall::get_descriptor(fd, sys.process) {
            Ok(d) => unsafe { &mut *d },
            Err(errno) => return SyscallReturn::Error(errno).into(),
        };

        // if it's a legacy descriptor, use the C syscall handler instead
        if let CompatDescriptor::Legacy(_) = desc {
            return unsafe {
                c::syscallhandler_close(
                    sys as *mut c::SysCallHandler,
                    args as *const c::SysCallArgs,
                )
            };
        }
    }

    unsafe { c::process_deregisterCompatDescriptor(sys.process, fd) };

    SyscallReturn::Success(0).into()
}

pub fn dup(sys: &mut c::SysCallHandler, args: &c::SysCallArgs) -> c::SysCallReturn {
    let fd = unsafe { args.args[0].as_i64 } as libc::c_int;

    // get the descriptor, or return early if it doesn't exist
    let desc = match syscall::get_descriptor(fd, sys.process) {
        Ok(d) => unsafe { &mut *d },
        Err(errno) => return SyscallReturn::Error(errno).into(),
    };

    // if it's a legacy descriptor, use the C syscall handler instead
    let desc = match desc {
        CompatDescriptor::New(d) => d,
        CompatDescriptor::Legacy(_) => unsafe {
            return c::syscallhandler_dup(
                sys as *mut c::SysCallHandler,
                args as *const c::SysCallArgs,
            );
        },
    };

    dup_helper(sys, fd, desc)
}

pub fn dup_helper(
    sys: &mut c::SysCallHandler,
    fd: libc::c_int,
    desc: &Descriptor,
) -> c::SysCallReturn {
    // clone the descriptor and register it
    let new_desc = CompatDescriptor::New(desc.clone());
    let new_fd = unsafe {
        c::process_registerCompatDescriptor(sys.process, Box::into_raw(Box::new(new_desc)))
    };

    // return the new fd
    SyscallReturn::Success(new_fd).into()
}

pub fn read(sys: &mut c::SysCallHandler, args: &c::SysCallArgs) -> c::SysCallReturn {
    let fd = unsafe { args.args[0].as_i64 } as libc::c_int;
    let buf_ptr = unsafe { args.args[1].as_ptr };
    let buf_size = unsafe { args.args[2].as_u64 } as libc::size_t;
    let offset = 0 as libc::off_t;

    // get the descriptor, or return early if it doesn't exist
    let desc = match syscall::get_descriptor(fd, sys.process) {
        Ok(d) => unsafe { &mut *d },
        Err(errno) => return SyscallReturn::Error(errno).into(),
    };

    // if it's a legacy descriptor, use the C syscall handler instead
    let desc = match desc {
        CompatDescriptor::New(d) => d,
        CompatDescriptor::Legacy(_) => unsafe {
            return c::syscallhandler_read(
                sys as *mut c::SysCallHandler,
                args as *const c::SysCallArgs,
            );
        },
    };

    read_helper(sys, fd, desc, buf_ptr, buf_size, offset)
}

pub fn pread64(sys: &mut c::SysCallHandler, args: &c::SysCallArgs) -> c::SysCallReturn {
    let fd = unsafe { args.args[0].as_i64 } as libc::c_int;
    let buf_ptr = unsafe { args.args[1].as_ptr };
    let buf_size = unsafe { args.args[2].as_u64 } as libc::size_t;
    let offset = unsafe { args.args[3].as_i64 } as libc::off_t;

    // get the descriptor, or return early if it doesn't exist
    let desc = match syscall::get_descriptor(fd, sys.process) {
        Ok(d) => unsafe { &mut *d },
        Err(errno) => return SyscallReturn::Error(errno).into(),
    };

    // if it's a legacy descriptor, use the C syscall handler instead
    let desc = match desc {
        CompatDescriptor::New(d) => d,
        CompatDescriptor::Legacy(_) => unsafe {
            return c::syscallhandler_pread64(
                sys as *mut c::SysCallHandler,
                args as *const c::SysCallArgs,
            );
        },
    };

    read_helper(sys, fd, desc, buf_ptr, buf_size, offset)
}

fn read_helper(
    sys: &mut c::SysCallHandler,
    fd: libc::c_int,
    desc: &Descriptor,
    buf_ptr: c::PluginPtr,
    buf_size: libc::size_t,
    offset: libc::off_t,
) -> c::SysCallReturn {
    // need a non-null buffer
    if buf_ptr.val == 0 {
        return SyscallReturn::Error(nix::errno::Errno::EFAULT).into();
    }

    // we can only seek on files, otherwise its a pipe error
    if offset != 0 {
        return SyscallReturn::Error(nix::errno::Errno::ESPIPE).into();
    }

    // need a non-zero size
    if buf_size == 0 {
        info!("Invalid length {} provided on descriptor {}", buf_size, fd);
        return SyscallReturn::Error(nix::errno::Errno::EINVAL).into();
    }

    // TODO: dynamically compute size based on how much data is actually available in the descriptor
    let size_needed = std::cmp::min(buf_size, c::SYSCALL_IO_BUFSIZE as usize);

    let buf_ptr =
        unsafe { c::process_getWriteablePtr(sys.process, sys.thread, buf_ptr, size_needed as u64) };
    let mut buf = unsafe { std::slice::from_raw_parts_mut(buf_ptr as *mut u8, size_needed) };

    let posix_file = desc.get_file();
    let file_flags = posix_file.borrow().get_flags();

    // call the file's read(), and run any resulting events
    let result = EventQueue::queue_and_run(|event_queue| {
        posix_file.borrow_mut().read(&mut buf, event_queue)
    });

    // if the syscall would block and it's a blocking descriptor
    if result == SyscallReturn::Error(nix::errno::EWOULDBLOCK)
        && !file_flags.contains(FileFlags::NONBLOCK)
    {
        let trigger = Trigger::from_posix_file(posix_file, FileStatus::READABLE);

        return c::SysCallReturn {
            state: c::SysCallReturnState_SYSCALL_BLOCK,
            retval: c::SysCallReg { as_i64: 0i64 },
            cond: unsafe { c::syscallcondition_new(trigger.into(), std::ptr::null_mut()) },
        };
    }

    result.into()
}

pub fn write(sys: &mut c::SysCallHandler, args: &c::SysCallArgs) -> c::SysCallReturn {
    let fd = unsafe { args.args[0].as_i64 } as libc::c_int;
    let buf_ptr = unsafe { args.args[1].as_ptr };
    let buf_size = unsafe { args.args[2].as_u64 } as libc::size_t;
    let offset = 0 as libc::off_t;

    // get the descriptor, or return early if it doesn't exist
    let desc = match syscall::get_descriptor(fd, sys.process) {
        Ok(d) => unsafe { &mut *d },
        Err(errno) => return SyscallReturn::Error(errno).into(),
    };

    // if it's a legacy descriptor, use the C syscall handler instead
    let desc = match desc {
        CompatDescriptor::New(d) => d,
        CompatDescriptor::Legacy(_) => unsafe {
            return c::syscallhandler_write(
                sys as *mut c::SysCallHandler,
                args as *const c::SysCallArgs,
            );
        },
    };

    write_helper(sys, fd, desc, buf_ptr, buf_size, offset)
}

pub fn pwrite64(sys: &mut c::SysCallHandler, args: &c::SysCallArgs) -> c::SysCallReturn {
    let fd = unsafe { args.args[0].as_i64 } as libc::c_int;
    let buf_ptr = unsafe { args.args[1].as_ptr };
    let buf_size = unsafe { args.args[2].as_u64 } as libc::size_t;
    let offset = unsafe { args.args[3].as_i64 } as libc::off_t;

    // get the descriptor, or return early if it doesn't exist
    let desc = match syscall::get_descriptor(fd, sys.process) {
        Ok(d) => unsafe { &mut *d },
        Err(errno) => return SyscallReturn::Error(errno).into(),
    };

    // if it's a legacy descriptor, use the C syscall handler instead
    let desc = match desc {
        CompatDescriptor::New(d) => d,
        CompatDescriptor::Legacy(_) => unsafe {
            return c::syscallhandler_pwrite64(
                sys as *mut c::SysCallHandler,
                args as *const c::SysCallArgs,
            );
        },
    };

    write_helper(sys, fd, desc, buf_ptr, buf_size, offset)
}

fn write_helper(
    sys: &mut c::SysCallHandler,
    _fd: libc::c_int,
    desc: &Descriptor,
    buf_ptr: c::PluginPtr,
    buf_size: libc::size_t,
    offset: libc::off_t,
) -> c::SysCallReturn {
    // need a non-null buffer
    if buf_ptr.val == 0 {
        return SyscallReturn::Error(nix::errno::Errno::EFAULT).into();
    }

    // we can only seek on files, otherwise its a pipe error
    if offset != 0 {
        return SyscallReturn::Error(nix::errno::Errno::ESPIPE).into();
    }

    // TODO: dynamically compute size based on how much data is actually available in the descriptor
    let size_needed = std::cmp::min(buf_size, c::SYSCALL_IO_BUFSIZE as usize);

    let buf_ptr =
        unsafe { c::process_getReadablePtr(sys.process, sys.thread, buf_ptr, size_needed as u64) };
    let buf = unsafe { std::slice::from_raw_parts(buf_ptr as *const u8, size_needed) };

    let posix_file = desc.get_file();
    let file_flags = posix_file.borrow().get_flags();

    // call the file's write(), and run any resulting events
    let result =
        EventQueue::queue_and_run(|event_queue| posix_file.borrow_mut().write(&buf, event_queue));

    // if the syscall would block and it's a blocking descriptor
    if result == SyscallReturn::Error(nix::errno::EWOULDBLOCK)
        && !file_flags.contains(FileFlags::NONBLOCK)
    {
        let trigger = Trigger::from_posix_file(posix_file, FileStatus::WRITABLE);

        return c::SysCallReturn {
            state: c::SysCallReturnState_SYSCALL_BLOCK,
            retval: c::SysCallReg { as_i64: 0i64 },
            cond: unsafe { c::syscallcondition_new(trigger.into(), std::ptr::null_mut()) },
        };
    }

    result.into()
}

pub fn pipe(sys: &mut c::SysCallHandler, args: &c::SysCallArgs) -> c::SysCallReturn {
    let fd_ptr = unsafe { args.args[0].as_ptr };

    pipe_helper(sys, fd_ptr, 0)
}

pub fn pipe2(sys: &mut c::SysCallHandler, args: &c::SysCallArgs) -> c::SysCallReturn {
    let fd_ptr = unsafe { args.args[0].as_ptr };
    let flags = unsafe { args.args[1].as_u64 } as libc::c_int;

    pipe_helper(sys, fd_ptr, flags)
}

fn pipe_helper(sys: &mut c::SysCallHandler, fd_ptr: c::PluginPtr, flags: i32) -> c::SysCallReturn {
    // make sure they didn't pass a NULL pointer
    if fd_ptr.val == 0 {
        return SyscallReturn::Error(nix::errno::Errno::EFAULT).into();
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
            return SyscallReturn::Error(nix::errno::Errno::EOPNOTSUPP).into();
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

    // register the file descriptors and return them to the caller
    let num_items = 2;
    let size_needed = num_items * std::mem::size_of::<libc::c_int>();
    let fd_ptr =
        unsafe { c::process_getWriteablePtr(sys.process, sys.thread, fd_ptr, size_needed as u64) };
    let fds = unsafe { std::slice::from_raw_parts_mut(fd_ptr as *mut libc::c_int, num_items) };

    fds[0] = unsafe {
        c::process_registerCompatDescriptor(
            sys.process,
            Box::into_raw(Box::new(CompatDescriptor::New(reader_desc))),
        )
    };
    fds[1] = unsafe {
        c::process_registerCompatDescriptor(
            sys.process,
            Box::into_raw(Box::new(CompatDescriptor::New(writer_desc))),
        )
    };

    debug!("Created pipe reader fd {} and writer fd {}", fds[0], fds[1]);

    SyscallReturn::Success(0).into()
}

mod export {
    use super::*;

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_close(
        sys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        assert!(!sys.is_null() && !args.is_null());
        close(unsafe { &mut *sys }, unsafe { &*args })
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_dup(
        sys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        assert!(!sys.is_null() && !args.is_null());
        dup(unsafe { &mut *sys }, unsafe { &*args })
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_read(
        sys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        assert!(!sys.is_null() && !args.is_null());
        read(unsafe { &mut *sys }, unsafe { &*args })
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_pread64(
        sys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        assert!(!sys.is_null() && !args.is_null());
        pread64(unsafe { &mut *sys }, unsafe { &*args })
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_write(
        sys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        assert!(!sys.is_null() && !args.is_null());
        write(unsafe { &mut *sys }, unsafe { &*args })
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_pwrite64(
        sys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        assert!(!sys.is_null() && !args.is_null());
        pwrite64(unsafe { &mut *sys }, unsafe { &*args })
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_pipe(
        sys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        assert!(!sys.is_null() && !args.is_null());
        pipe(unsafe { &mut *sys }, unsafe { &*args })
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_pipe2(
        sys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        assert!(!sys.is_null() && !args.is_null());
        pipe2(unsafe { &mut *sys }, unsafe { &*args })
    }
}
