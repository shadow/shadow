use crate::cshadow as c;
use crate::host::context::ThreadContext;
use crate::host::descriptor::pipe;
use crate::host::descriptor::shared_buf::SharedBuf;
use crate::host::descriptor::{
    CompatDescriptor, Descriptor, DescriptorFlags, FileMode, FileState, FileStatus, PosixFile,
};
use crate::host::syscall::handler::SyscallHandler;
use crate::host::syscall::Trigger;
use crate::host::syscall_condition::SysCallCondition;
use crate::host::syscall_types::{PluginPtr, SysCallArgs, TypedPluginPtr};
use crate::host::syscall_types::{SyscallError, SyscallResult};
use crate::utility::event_queue::EventQueue;

use std::sync::Arc;

use atomic_refcell::AtomicRefCell;
use log::*;
use nix::errno::Errno;

use syscall_logger::log_syscall;

impl SyscallHandler {
    #[log_syscall(/* rv */ libc::c_int, /* fd */ libc::c_int)]
    pub fn close(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
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

    #[log_syscall(/* rv */ libc::c_int, /* oldfd */ libc::c_int)]
    pub fn dup(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let fd = libc::c_int::from(args.get(0));

        // get the descriptor, or return early if it doesn't exist
        let desc = match self.get_descriptor(ctx.process, fd)? {
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

    #[log_syscall(/* rv */ libc::c_int, /* oldfd */ libc::c_int, /* newfd */ libc::c_int)]
    pub fn dup2(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let old_fd = libc::c_int::from(args.get(0));
        let new_fd = libc::c_int::from(args.get(1));

        // get the descriptor, or return early if it doesn't exist
        let desc = match self.get_descriptor(ctx.process, old_fd)? {
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
            EventQueue::queue_and_run(|event_queue| {
                replaced_desc.close(ctx.host.chost(), event_queue)
            });
        }

        // return the new fd
        Ok(libc::c_int::try_from(new_fd).unwrap().into())
    }

    #[log_syscall(/* rv */ libc::c_int, /* oldfd */ libc::c_int, /* newfd */ libc::c_int,
                  /* flags */ nix::fcntl::OFlag)]
    pub fn dup3(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let old_fd = libc::c_int::from(args.get(0));
        let new_fd = libc::c_int::from(args.get(1));
        let flags = libc::c_int::from(args.get(2));

        // get the descriptor, or return early if it doesn't exist
        let desc = match self.get_descriptor(ctx.process, old_fd)? {
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
            EventQueue::queue_and_run(|event_queue| {
                replaced_desc.close(ctx.host.chost(), event_queue)
            });
        }

        // return the new fd
        Ok(libc::c_int::try_from(new_fd).unwrap().into())
    }

    #[log_syscall(/* rv */ libc::ssize_t, /* fd */ libc::c_int, /* buf */ *const libc::c_void,
                  /* count */ libc::size_t)]
    pub fn read(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let fd = libc::c_int::from(args.get(0));
        let buf_ptr = PluginPtr::from(args.get(1));
        let buf_size = libc::size_t::from(args.get(2));
        let offset = 0;

        // get the descriptor, or return early if it doesn't exist
        match self.get_descriptor(ctx.process, fd)? {
            CompatDescriptor::New(desc) => {
                let file = desc.get_file().clone();
                self.read_helper(ctx, fd, &file, buf_ptr, buf_size, offset)
            }
            // if it's a legacy descriptor, use the C syscall handler instead
            CompatDescriptor::Legacy(_) => unsafe {
                c::syscallhandler_read(ctx.thread.csyscallhandler(), args as *const SysCallArgs)
                    .into()
            },
        }
    }

    #[log_syscall(/* rv */ libc::ssize_t, /* fd */ libc::c_int, /* buf */ *const libc::c_void,
                  /* count */ libc::size_t, /* offset */ libc::off_t)]
    pub fn pread64(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let fd = libc::c_int::from(args.get(0));
        let buf_ptr = PluginPtr::from(args.get(1));
        let buf_size = libc::size_t::from(args.get(2));
        let offset = libc::off_t::from(args.get(3));

        // get the descriptor, or return early if it doesn't exist
        match self.get_descriptor(ctx.process, fd)? {
            CompatDescriptor::New(desc) => {
                let file = desc.get_file().clone();
                self.read_helper(ctx, fd, &file, buf_ptr, buf_size, offset)
            }
            // if it's a legacy descriptor, use the C syscall handler instead
            CompatDescriptor::Legacy(_) => unsafe {
                c::syscallhandler_pread64(
                    ctx.thread.csyscallhandler(),
                    args as *const c::SysCallArgs,
                )
                .into()
            },
        }
    }

    fn read_helper(
        &self,
        ctx: &mut ThreadContext,
        _fd: libc::c_int,
        posix_file: &PosixFile,
        buf_ptr: PluginPtr,
        buf_size: libc::size_t,
        offset: libc::off_t,
    ) -> SyscallResult {
        // if it's a socket, call recvfrom() instead
        if let PosixFile::Socket(ref socket) = posix_file {
            if offset != 0 {
                // sockets don't support offsets
                return Err(Errno::ESPIPE.into());
            }
            return self.recvfrom_helper(
                ctx,
                socket,
                buf_ptr,
                buf_size,
                0,
                PluginPtr::null(),
                PluginPtr::null(),
            );
        }

        let file_status = posix_file.borrow().get_status();

        let result =
            // call the file's read(), and run any resulting events
            EventQueue::queue_and_run(|event_queue| {
                posix_file.borrow_mut().read(
                    ctx.process.memory_mut().writer(TypedPluginPtr::new::<u8>(buf_ptr, buf_size)),
                    offset,
                    event_queue,
                )
            });

        // if the syscall would block and it's a blocking descriptor
        if result == Err(Errno::EWOULDBLOCK.into()) && !file_status.contains(FileStatus::NONBLOCK) {
            let trigger = Trigger::from_posix_file(posix_file.clone(), FileState::READABLE);

            return Err(SyscallError::Cond(SysCallCondition::new(trigger)));
        }

        result
    }

    #[log_syscall(/* rv */ libc::ssize_t, /* fd */ libc::c_int, /* buf */ *const libc::c_char,
                  /* count */ libc::size_t)]
    pub fn write(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let fd = libc::c_int::from(args.get(0));
        let buf_ptr = PluginPtr::from(args.get(1));
        let buf_size = libc::size_t::from(args.get(2));
        let offset = 0;

        // get the descriptor, or return early if it doesn't exist
        match self.get_descriptor(ctx.process, fd)? {
            CompatDescriptor::New(desc) => {
                let file = desc.get_file().clone();
                self.write_helper(ctx, fd, &file, buf_ptr, buf_size, offset)
            }
            // if it's a legacy descriptor, use the C syscall handler instead
            CompatDescriptor::Legacy(_) => unsafe {
                c::syscallhandler_write(ctx.thread.csyscallhandler(), args as *const c::SysCallArgs)
                    .into()
            },
        }
    }

    #[log_syscall(/* rv */ libc::ssize_t, /* fd */ libc::c_int, /* buf */ *const libc::c_char,
                  /* count */ libc::size_t, /* offset */ libc::off_t)]
    pub fn pwrite64(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let fd = libc::c_int::from(args.get(0));
        let buf_ptr = PluginPtr::from(args.get(1));
        let buf_size = libc::size_t::from(args.get(2));
        let offset = libc::off_t::from(args.get(3));

        // get the descriptor, or return early if it doesn't exist
        match self.get_descriptor(ctx.process, fd)? {
            CompatDescriptor::New(desc) => {
                let file = desc.get_file().clone();
                self.write_helper(ctx, fd, &file, buf_ptr, buf_size, offset)
            }
            // if it's a legacy descriptor, use the C syscall handler instead
            CompatDescriptor::Legacy(_) => unsafe {
                c::syscallhandler_pwrite64(ctx.thread.csyscallhandler(), args as *const SysCallArgs)
                    .into()
            },
        }
    }

    fn write_helper(
        &self,
        ctx: &mut ThreadContext,
        _fd: libc::c_int,
        posix_file: &PosixFile,
        buf_ptr: PluginPtr,
        buf_size: libc::size_t,
        offset: libc::off_t,
    ) -> SyscallResult {
        // if it's a socket, call recvfrom() instead
        if let PosixFile::Socket(ref socket) = posix_file {
            if offset != 0 {
                // sockets don't support offsets
                return Err(Errno::ESPIPE.into());
            }
            return self.sendto_helper(ctx, socket, buf_ptr, buf_size, 0, PluginPtr::null(), 0);
        }

        let file_status = posix_file.borrow().get_status();

        let result =
            // call the file's write(), and run any resulting events
            EventQueue::queue_and_run(|event_queue| {
                posix_file.borrow_mut().write(
                    ctx.process.memory().reader(TypedPluginPtr::new::<u8>(buf_ptr, buf_size)),
                    offset,
                    event_queue,
                )
            });

        // if the syscall would block and it's a blocking descriptor
        if result == Err(Errno::EWOULDBLOCK.into()) && !file_status.contains(FileStatus::NONBLOCK) {
            let trigger = Trigger::from_posix_file(posix_file.clone(), FileState::WRITABLE);

            return Err(SyscallError::Cond(SysCallCondition::new(trigger)));
        };

        result
    }

    #[log_syscall(/* rv */ libc::c_int, /* pipefd */ [libc::c_int; 2])]
    pub fn pipe(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let fd_ptr: PluginPtr = args.args[0].into();

        self.pipe_helper(ctx, fd_ptr, 0)
    }

    #[log_syscall(/* rv */ libc::c_int, /* pipefd */ [libc::c_int; 2], /* flags */ nix::fcntl::OFlag)]
    pub fn pipe2(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let fd_ptr: PluginPtr = args.args[0].into();
        let flags = unsafe { args.args[1].as_u64 } as libc::c_int;

        self.pipe_helper(ctx, fd_ptr, flags)
    }

    fn pipe_helper(&self, ctx: &mut ThreadContext, fd_ptr: PluginPtr, flags: i32) -> SyscallResult {
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

        if flags & libc::O_DIRECT != 0 {
            file_flags.insert(FileStatus::DIRECT);
            remaining_flags &= !libc::O_DIRECT;
        }

        if flags & libc::O_CLOEXEC != 0 {
            descriptor_flags.insert(DescriptorFlags::CLOEXEC);
            remaining_flags &= !libc::O_CLOEXEC;
        }

        // the user requested flags that we don't support
        if remaining_flags != 0 {
            warn!("Ignoring pipe flags");
        }

        // reference-counted buffer for the pipe
        let buffer = SharedBuf::new(c::CONFIG_PIPE_BUFFER_SIZE.try_into().unwrap());
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
        let read_fd = ctx
            .process
            .register_descriptor(CompatDescriptor::New(reader_desc));
        let write_fd = ctx
            .process
            .register_descriptor(CompatDescriptor::New(writer_desc));

        // try to write them to the caller
        let fds = [
            i32::try_from(read_fd).unwrap(),
            i32::try_from(write_fd).unwrap(),
        ];
        let write_res = ctx
            .process
            .memory_mut()
            .copy_to_ptr(TypedPluginPtr::new::<libc::c_int>(fd_ptr.into(), 2), &fds);

        // clean up in case of error
        match write_res {
            Ok(_) => Ok(0.into()),
            Err(e) => {
                EventQueue::queue_and_run(|event_queue| {
                    // ignore any errors when closing
                    ctx.process
                        .deregister_descriptor(read_fd)
                        .unwrap()
                        .close(ctx.host.chost(), event_queue);
                    ctx.process
                        .deregister_descriptor(write_fd)
                        .unwrap()
                        .close(ctx.host.chost(), event_queue);
                });
                Err(e.into())
            }
        }
    }
}
