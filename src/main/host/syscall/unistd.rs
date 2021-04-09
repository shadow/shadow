use crate::core::worker::Worker;
use crate::cshadow as c;
use crate::host::descriptor::pipe;
use crate::host::descriptor::{
    CompatDescriptor, Descriptor, DescriptorFlags, FileFlags, FileMode, FileStatus, PosixFile,
    SyscallError, SyscallReturn,
};
use crate::host::syscall::{self, Trigger};
use crate::host::syscall_types::{PluginPtr, SysCallArgs};
use crate::utility::event_queue::EventQueue;

use std::sync::Arc;

use atomic_refcell::AtomicRefCell;
use log::*;

pub fn close(sys: &mut c::SysCallHandler, args: &SysCallArgs) -> SyscallReturn {
    let fd = libc::c_int::from(args.get(0));

    debug!("Trying to close fd {}", fd);

    // scope used to make sure that desc cannot be used after deregistering it
    {
        // get the descriptor, or return early if it doesn't exist
        let desc = unsafe { &mut *syscall::get_descriptor(fd, sys.process)? };

        // if it's a legacy descriptor, use the C syscall handler instead
        if let CompatDescriptor::Legacy(_) = desc {
            return unsafe {
                c::syscallhandler_close(
                    sys as *mut c::SysCallHandler,
                    args as *const c::SysCallArgs,
                )
            }
            .into();
        }
    }

    // according to "man 2 close", in Linux any errors that may occur will happen after the fd is
    // released, so we should always deregister the descriptor
    let desc = unsafe { c::process_deregisterCompatDescriptor(sys.process, fd) };
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

pub fn dup(sys: &mut c::SysCallHandler, args: &SysCallArgs) -> SyscallReturn {
    let fd = libc::c_int::from(args.get(0));

    // get the descriptor, or return early if it doesn't exist
    let desc = unsafe { &mut *syscall::get_descriptor(fd, sys.process)? };

    match desc {
        CompatDescriptor::New(desc) => dup_helper(sys, fd, desc),
        // if it's a legacy descriptor, use the C syscall handler instead
        CompatDescriptor::Legacy(_) => unsafe {
            c::syscallhandler_dup(sys as *mut c::SysCallHandler, args as *const c::SysCallArgs)
                .into()
        },
    }
}

pub fn dup_helper(
    sys: &mut c::SysCallHandler,
    fd: libc::c_int,
    desc: &Descriptor,
) -> SyscallReturn {
    debug!("Duping fd {} ({:?})", fd, desc);

    // clone the descriptor (but not the flags)
    let mut new_desc = desc.clone();
    new_desc.set_flags(DescriptorFlags::empty());

    // register the descriptor
    let new_desc = CompatDescriptor::New(new_desc);
    let new_fd = unsafe {
        c::process_registerCompatDescriptor(
            sys.process,
            CompatDescriptor::into_raw(Box::new(new_desc)),
        )
    };

    // return the new fd
    Ok(new_fd.into())
}

pub fn read(sys: &mut c::SysCallHandler, args: &SysCallArgs) -> SyscallReturn {
    let fd = libc::c_int::from(args.get(0));
    let buf_ptr = PluginPtr::from(args.get(1));
    let buf_size = libc::size_t::from(args.get(2));
    let offset = 0;

    // get the descriptor, or return early if it doesn't exist
    let desc = unsafe { &mut *syscall::get_descriptor(fd, sys.process)? };

    match desc {
        CompatDescriptor::New(desc) => read_helper(sys, fd, desc, buf_ptr, buf_size, offset),
        // if it's a legacy descriptor, use the C syscall handler instead
        CompatDescriptor::Legacy(_) => unsafe {
            c::syscallhandler_read(sys as *mut c::SysCallHandler, args as *const SysCallArgs).into()
        },
    }
}

pub fn pread64(sys: &mut c::SysCallHandler, args: &SysCallArgs) -> SyscallReturn {
    let fd = libc::c_int::from(args.get(0));
    let buf_ptr = PluginPtr::from(args.get(1));
    let buf_size = libc::size_t::from(args.get(2));
    let offset = libc::off_t::from(args.get(3));

    // get the descriptor, or return early if it doesn't exist
    let desc = unsafe { &mut *syscall::get_descriptor(fd, sys.process)? };

    match desc {
        CompatDescriptor::New(desc) => read_helper(sys, fd, desc, buf_ptr, buf_size, offset),
        // if it's a legacy descriptor, use the C syscall handler instead
        CompatDescriptor::Legacy(_) => unsafe {
            c::syscallhandler_pread64(sys as *mut c::SysCallHandler, args as *const c::SysCallArgs)
                .into()
        },
    }
}

fn read_helper(
    _sys: &mut c::SysCallHandler,
    _fd: libc::c_int,
    desc: &Descriptor,
    buf_ptr: PluginPtr,
    buf_size: libc::size_t,
    offset: libc::off_t,
) -> SyscallReturn {
    // TODO: dynamically compute size based on how much data is actually available in the descriptor
    let size_needed = std::cmp::min(buf_size, c::SYSCALL_IO_BUFSIZE as usize);
    let posix_file = desc.get_file();
    let file_flags = posix_file.borrow().get_flags();

    let result: SyscallReturn = Worker::with_active_process_mut(|process| {
        let buf = process.get_writable_slice::<u8>(buf_ptr, size_needed)?;

        // call the file's read(), and run any resulting events
        EventQueue::queue_and_run(|event_queue| {
            posix_file.borrow_mut().read(Some(buf), offset, event_queue)
        })
    });

    // if the syscall would block and it's a blocking descriptor
    if result == Err(nix::errno::EWOULDBLOCK.into()) && !file_flags.contains(FileFlags::NONBLOCK) {
        let trigger = Trigger::from_posix_file(posix_file, FileStatus::READABLE);

        return Err(SyscallError::Cond(unsafe {
            c::syscallcondition_new(trigger.into(), std::ptr::null_mut())
        }));
    }

    result
}

pub fn write(sys: &mut c::SysCallHandler, args: &SysCallArgs) -> SyscallReturn {
    let fd = libc::c_int::from(args.get(0));
    let buf_ptr = PluginPtr::from(args.get(1));
    let buf_size = libc::size_t::from(args.get(2));
    let offset = 0;

    // get the descriptor, or return early if it doesn't exist
    let desc = unsafe { &mut *syscall::get_descriptor(fd, sys.process)? };

    match desc {
        CompatDescriptor::New(desc) => write_helper(sys, fd, desc, buf_ptr, buf_size, offset),
        // if it's a legacy descriptor, use the C syscall handler instead
        CompatDescriptor::Legacy(_) => unsafe {
            c::syscallhandler_write(sys as *mut c::SysCallHandler, args as *const c::SysCallArgs)
                .into()
        },
    }
}

pub fn pwrite64(sys: &mut c::SysCallHandler, args: &SysCallArgs) -> SyscallReturn {
    let fd = libc::c_int::from(args.get(0));
    let buf_ptr = PluginPtr::from(args.get(1));
    let buf_size = libc::size_t::from(args.get(2));
    let offset = libc::off_t::from(args.get(3));

    // get the descriptor, or return early if it doesn't exist
    let desc = unsafe { &mut *syscall::get_descriptor(fd, sys.process)? };

    match desc {
        CompatDescriptor::New(desc) => write_helper(sys, fd, desc, buf_ptr, buf_size, offset),
        // if it's a legacy descriptor, use the C syscall handler instead
        CompatDescriptor::Legacy(_) => unsafe {
            c::syscallhandler_pwrite64(sys as *mut c::SysCallHandler, args as *const SysCallArgs)
                .into()
        },
    }
}

fn write_helper(
    _sys: &mut c::SysCallHandler,
    _fd: libc::c_int,
    desc: &Descriptor,
    buf_ptr: PluginPtr,
    buf_size: libc::size_t,
    offset: libc::off_t,
) -> SyscallReturn {
    // TODO: dynamically compute size based on how much data is actually available in the descriptor
    let size_needed = std::cmp::min(buf_size, c::SYSCALL_IO_BUFSIZE as usize);

    let posix_file = desc.get_file();
    let file_flags = posix_file.borrow().get_flags();

    let result: SyscallReturn = Worker::with_active_process(|process| {
        let buf = process.get_slice::<u8>(buf_ptr.into(), size_needed)?;

        // call the file's write(), and run any resulting events
        EventQueue::queue_and_run(|event_queue| {
            posix_file
                .borrow_mut()
                .write(Some(buf), offset, event_queue)
        })
    });

    // if the syscall would block and it's a blocking descriptor
    if result == Err(nix::errno::EWOULDBLOCK.into()) && !file_flags.contains(FileFlags::NONBLOCK) {
        let trigger = Trigger::from_posix_file(posix_file, FileStatus::WRITABLE);

        return Err(SyscallError::Cond(unsafe {
            c::syscallcondition_new(trigger.into(), std::ptr::null_mut())
        }));
    };

    result
}

pub fn pipe(sys: &mut c::SysCallHandler, args: &SysCallArgs) -> SyscallReturn {
    let fd_ptr: PluginPtr = args.args[0].into();

    pipe_helper(sys, fd_ptr, 0)
}

pub fn pipe2(sys: &mut c::SysCallHandler, args: &SysCallArgs) -> SyscallReturn {
    let fd_ptr: PluginPtr = args.args[0].into();
    let flags = unsafe { args.args[1].as_u64 } as libc::c_int;

    pipe_helper(sys, fd_ptr, flags)
}

fn pipe_helper(sys: &mut c::SysCallHandler, fd_ptr: PluginPtr, flags: i32) -> SyscallReturn {
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

    // register the file descriptors and return them to the caller
    Worker::with_active_process_mut(|process| -> SyscallReturn {
        // Normally for a small pointer it'd be a little cleaner to create the
        // slice in Rust and then call process.write_ptr_from_slice, allowing us
        // to hold the process for a smaller scope.
        //
        // In this case it'd be a bit awkward though, since we'd then need to
        // unregister the descriptors if the pointer-write failed.
        let fds = process.get_writable_slice::<libc::c_int>(fd_ptr.into(), 2)?;

        fds[0] = unsafe {
            c::process_registerCompatDescriptor(
                sys.process,
                CompatDescriptor::into_raw(Box::new(CompatDescriptor::New(reader_desc))),
            )
        };
        fds[1] = unsafe {
            c::process_registerCompatDescriptor(
                sys.process,
                CompatDescriptor::into_raw(Box::new(CompatDescriptor::New(writer_desc))),
            )
        };

        debug!("Created pipe reader fd {} and writer fd {}", fds[0], fds[1]);
        Ok(0.into())
    })
}

mod export {
    use super::*;

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_close(
        sys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        assert!(!sys.is_null() && !args.is_null());
        close(unsafe { &mut *sys }, unsafe { &*args }).into()
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_dup(
        sys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        assert!(!sys.is_null() && !args.is_null());
        dup(unsafe { &mut *sys }, unsafe { &*args }).into()
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_read(
        sys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        assert!(!sys.is_null() && !args.is_null());
        read(unsafe { &mut *sys }, unsafe { &*args }).into()
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_pread64(
        sys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        assert!(!sys.is_null() && !args.is_null());
        pread64(unsafe { &mut *sys }, unsafe { &*args }).into()
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_write(
        sys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        assert!(!sys.is_null() && !args.is_null());
        write(unsafe { &mut *sys }, unsafe { &*args }).into()
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_pwrite64(
        sys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        assert!(!sys.is_null() && !args.is_null());
        pwrite64(unsafe { &mut *sys }, unsafe { &*args }).into()
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_pipe(
        sys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        assert!(!sys.is_null() && !args.is_null());
        pipe(unsafe { &mut *sys }, unsafe { &*args }).into()
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_pipe2(
        sys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        assert!(!sys.is_null() && !args.is_null());
        pipe2(unsafe { &mut *sys }, unsafe { &*args }).into()
    }
}
