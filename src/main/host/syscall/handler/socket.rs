use std::mem::MaybeUninit;

use crate::cshadow as c;
use crate::host::context::ThreadContext;
use crate::host::descriptor::socket::unix::{UnixSocket, UnixSocketType};
use crate::host::descriptor::socket::Socket;
use crate::host::descriptor::{
    CompatFile, Descriptor, DescriptorFlags, File, FileState, FileStatus, OpenFile,
};
use crate::host::memory_manager::MemoryManager;
use crate::host::syscall::handler::SyscallHandler;
use crate::host::syscall::Trigger;
use crate::host::syscall_condition::SysCallCondition;
use crate::host::syscall_types::{Blocked, PluginPtr, SysCallArgs, TypedPluginPtr};
use crate::host::syscall_types::{SyscallError, SyscallResult};
use crate::utility::callback_queue::CallbackQueue;
use crate::utility::sockaddr::SockaddrStorage;

use log::*;
use nix::errno::Errno;
use nix::sys::socket::{MsgFlags, SockFlag};

use syscall_logger::log_syscall;

impl SyscallHandler {
    #[log_syscall(/* rv */ libc::c_int, /* domain */ nix::sys::socket::AddressFamily,
                  /* type */ libc::c_int, /* protocol */ libc::c_int)]
    pub fn socket(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let domain = libc::c_int::from(args.get(0));
        let socket_type = libc::c_int::from(args.get(1));
        let protocol = libc::c_int::from(args.get(2));

        // remove any flags from the socket type
        let flags = socket_type & (libc::SOCK_NONBLOCK | libc::SOCK_CLOEXEC);
        let socket_type = socket_type & !flags;

        // if it's not a unix socket, use the C syscall handler instead
        if domain != libc::AF_UNIX {
            return unsafe {
                c::syscallhandler_socket(
                    ctx.thread.csyscallhandler(),
                    args as *const c::SysCallArgs,
                )
                .into()
            };
        }

        let mut file_flags = FileStatus::empty();
        let mut descriptor_flags = DescriptorFlags::empty();

        if flags & libc::SOCK_NONBLOCK != 0 {
            file_flags.insert(FileStatus::NONBLOCK);
        }

        if flags & libc::SOCK_CLOEXEC != 0 {
            descriptor_flags.insert(DescriptorFlags::CLOEXEC);
        }

        let socket = match domain {
            libc::AF_UNIX => {
                let socket_type = match UnixSocketType::try_from(socket_type) {
                    Ok(x) => x,
                    Err(e) => {
                        warn!("{}", e);
                        return Err(Errno::EPROTONOSUPPORT.into());
                    }
                };

                // unix sockets don't support any protocols
                if protocol != 0 {
                    warn!(
                        "Unsupported socket protocol {}, we only support default protocol 0",
                        protocol
                    );
                    return Err(Errno::EPROTONOSUPPORT.into());
                }

                Socket::Unix(UnixSocket::new(
                    file_flags,
                    socket_type,
                    ctx.host.abstract_unix_namespace(),
                ))
            }
            _ => return Err(Errno::EAFNOSUPPORT.into()),
        };

        let mut desc = Descriptor::new(CompatFile::New(OpenFile::new(File::Socket(socket))));
        desc.set_flags(descriptor_flags);

        let fd = ctx.process.register_descriptor(desc);

        debug!("Created socket fd {}", fd);

        Ok(fd.into())
    }

    #[log_syscall(/* rv */ libc::c_int, /* sockfd */ libc::c_int, /* addr */ *const libc::sockaddr,
                  /* addrlen */ libc::socklen_t)]
    pub fn bind(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let fd: libc::c_int = args.get(0).into();
        let addr_ptr: PluginPtr = args.get(1).into();
        let addr_len: libc::socklen_t = args.get(2).into();

        // get the descriptor, or return early if it doesn't exist
        let desc = Self::get_descriptor(ctx.process, fd)?;

        let file = match desc.file() {
            CompatFile::New(file) => file,
            // if it's a legacy file, use the C syscall handler instead
            CompatFile::Legacy(_) => {
                return unsafe {
                    c::syscallhandler_bind(
                        ctx.thread.csyscallhandler(),
                        args as *const c::SysCallArgs,
                    )
                    .into()
                }
            }
        };

        let file = file.inner_file().clone();

        let File::Socket(ref socket) = file else {
            return Err(Errno::ENOTSOCK.into());
        };

        let addr = read_sockaddr(ctx.process.memory(), addr_ptr, addr_len)?;

        debug!("Attempting to bind fd {} to {:?}", fd, addr);

        let mut rng = ctx.host.random_mut();
        Socket::bind(socket, addr.as_ref(), &mut *rng)
    }

    #[log_syscall(/* rv */ libc::ssize_t, /* sockfd */ libc::c_int, /* buf */ *const libc::c_char,
                  /* len */ libc::size_t, /* flags */ nix::sys::socket::MsgFlags,
                  /* dest_addr */ *const libc::sockaddr, /* addrlen */ libc::socklen_t)]
    pub fn sendto(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let fd: libc::c_int = args.get(0).into();
        let buf_ptr: PluginPtr = args.get(1).into();
        let buf_len: libc::size_t = args.get(2).into();
        let flags: libc::c_int = args.get(3).into();
        let addr_ptr: PluginPtr = args.get(4).into();
        let addr_len: libc::socklen_t = args.get(5).into();

        // if we were previously blocked, get the active file from the last syscall handler
        // invocation since it may no longer exist in the descriptor table
        let file = ctx
            .thread
            .syscall_condition()
            // if this was for a C descriptor, then there won't be an active file object
            .map(|x| x.active_file().cloned())
            .flatten();

        let file = match file {
            // we were previously blocked, so re-use the file from the previous syscall invocation
            Some(x) => x,
            // get the file from the descriptor table, or return early if it doesn't exist
            None => match Self::get_descriptor(ctx.process, fd)?.file() {
                CompatFile::New(file) => file.clone(),
                // if it's a legacy file, use the C syscall handler instead
                CompatFile::Legacy(_) => {
                    return unsafe {
                        c::syscallhandler_sendto(
                            ctx.thread.csyscallhandler(),
                            args as *const SysCallArgs,
                        )
                        .into()
                    };
                }
            },
        };

        self.sendto_helper(ctx, file, buf_ptr, buf_len, flags, addr_ptr, addr_len)
    }

    pub fn sendto_helper(
        &self,
        ctx: &mut ThreadContext,
        open_file: OpenFile,
        buf_ptr: PluginPtr,
        buf_len: libc::size_t,
        flags: libc::c_int,
        addr_ptr: PluginPtr,
        addr_len: libc::socklen_t,
    ) -> SyscallResult {
        let File::Socket(ref socket) = open_file.inner_file() else {
            return Err(Errno::ENOTSOCK.into());
        };

        // get the send flags
        let flags = match MsgFlags::from_bits(flags) {
            Some(x) => x,
            None => {
                // linux doesn't return an error if there are unexpected flags
                warn!("Invalid sendto flags: {}", flags);
                MsgFlags::from_bits_truncate(flags)
            }
        };

        // MSG_NOSIGNAL is currently a no-op, since we haven't implemented the behavior
        // it's meant to disable.
        // TODO: Once we've implemented generating a SIGPIPE when the peer on a
        // stream-oriented socket has closed the connection, MSG_NOSIGNAL should
        // disable it.
        let supported_flags = MsgFlags::MSG_DONTWAIT | MsgFlags::MSG_NOSIGNAL;
        if flags.intersects(!supported_flags) {
            warn!("Unsupported sendto flags: {:?}", flags);
            return Err(Errno::EOPNOTSUPP.into());
        }

        let addr = read_sockaddr(ctx.process.memory(), addr_ptr, addr_len)?;

        debug!("Attempting to send {} bytes to {:?}", buf_len, addr);

        let file_status = socket.borrow().get_status();

        // call the socket's sendto(), and run any resulting events
        let result = CallbackQueue::queue_and_run(|cb_queue| {
            socket.borrow_mut().sendto(
                ctx.process
                    .memory()
                    .reader(TypedPluginPtr::new::<u8>(buf_ptr, buf_len)),
                addr,
                cb_queue,
            )
        });

        // if the syscall would block, it's a blocking descriptor, and the `MSG_DONTWAIT` flag is not set
        if result == Err(Errno::EWOULDBLOCK.into())
            && !file_status.contains(FileStatus::NONBLOCK)
            && !flags.contains(MsgFlags::MSG_DONTWAIT)
        {
            let trigger = Trigger::from_file(open_file.inner_file().clone(), FileState::WRITABLE);
            let mut cond = SysCallCondition::new(trigger);
            let supports_sa_restart = socket.borrow().supports_sa_restart();
            cond.set_active_file(open_file);

            return Err(SyscallError::Blocked(Blocked {
                condition: cond,
                restartable: supports_sa_restart,
            }));
        };

        result
    }

    #[log_syscall(/* rv */ libc::ssize_t, /* sockfd */ libc::c_int, /* buf */ *const libc::c_void,
                  /* len */ libc::size_t, /* flags */ nix::sys::socket::MsgFlags,
                  /* src_addr */ *const libc::sockaddr, /* addrlen */ *const libc::socklen_t)]
    pub fn recvfrom(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let fd: libc::c_int = args.get(0).into();
        let buf_ptr: PluginPtr = args.get(1).into();
        let buf_len: libc::size_t = args.get(2).into();
        let flags: libc::c_int = args.get(3).into();
        let addr_ptr: PluginPtr = args.get(4).into();
        let addr_len_ptr: PluginPtr = args.get(5).into();

        // if we were previously blocked, get the active file from the last syscall handler
        // invocation since it may no longer exist in the descriptor table
        let file = ctx
            .thread
            .syscall_condition()
            // if this was for a C descriptor, then there won't be an active file object
            .map(|x| x.active_file().cloned())
            .flatten();

        let file = match file {
            // we were previously blocked, so re-use the file from the previous syscall invocation
            Some(x) => x,
            // get the file from the descriptor table, or return early if it doesn't exist
            None => match Self::get_descriptor(ctx.process, fd)?.file() {
                CompatFile::New(file) => file.clone(),
                // if it's a legacy file, use the C syscall handler instead
                CompatFile::Legacy(_) => {
                    return unsafe {
                        c::syscallhandler_recvfrom(
                            ctx.thread.csyscallhandler(),
                            args as *const SysCallArgs,
                        )
                        .into()
                    };
                }
            },
        };

        self.recvfrom_helper(ctx, file, buf_ptr, buf_len, flags, addr_ptr, addr_len_ptr)
    }

    pub fn recvfrom_helper(
        &self,
        ctx: &mut ThreadContext,
        open_file: OpenFile,
        buf_ptr: PluginPtr,
        buf_len: libc::size_t,
        flags: libc::c_int,
        addr_ptr: PluginPtr,
        addr_len_ptr: PluginPtr,
    ) -> SyscallResult {
        let File::Socket(ref socket) = open_file.inner_file() else {
            return Err(Errno::ENOTSOCK.into());
        };

        // get the recv flags
        let flags = match MsgFlags::from_bits(flags) {
            Some(x) => x,
            None => {
                // linux doesn't return an error if there are unexpected flags
                warn!("Invalid recvfrom flags: {}", flags);
                MsgFlags::from_bits_truncate(flags)
            }
        };

        let supported_flags = MsgFlags::MSG_DONTWAIT;
        if flags.intersects(!supported_flags) {
            warn!("Unsupported recvfrom flags: {:?}", flags);
            return Err(Errno::EOPNOTSUPP.into());
        }

        debug!("Attempting to recv {} bytes", buf_len);

        let file_status = socket.borrow().get_status();

        // call the socket's recvfrom(), and run any resulting events
        let result = CallbackQueue::queue_and_run(|cb_queue| {
            socket.borrow_mut().recvfrom(
                ctx.process
                    .memory_mut()
                    .writer(TypedPluginPtr::new::<u8>(buf_ptr, buf_len)),
                cb_queue,
            )
        });

        // if the syscall would block, it's a blocking descriptor, and the `MSG_DONTWAIT` flag is not set
        if matches!(result, Err(ref err) if err == &Errno::EWOULDBLOCK.into())
            && !file_status.contains(FileStatus::NONBLOCK)
            && !flags.contains(MsgFlags::MSG_DONTWAIT)
        {
            let trigger = Trigger::from_file(open_file.inner_file().clone(), FileState::READABLE);
            let mut cond = SysCallCondition::new(trigger);
            let supports_sa_restart = socket.borrow().supports_sa_restart();
            cond.set_active_file(open_file);

            return Err(SyscallError::Blocked(Blocked {
                condition: cond,
                restartable: supports_sa_restart,
            }));
        };

        let (result, from_addr) = result?;

        if !addr_ptr.is_null() {
            write_sockaddr(
                ctx.process.memory_mut(),
                from_addr.as_ref(),
                addr_ptr,
                TypedPluginPtr::new::<libc::socklen_t>(addr_len_ptr, 1),
            )?;
        }

        Ok(result)
    }

    #[log_syscall(/* rv */ libc::c_int, /* sockfd */ libc::c_int, /* addr */ *const libc::sockaddr,
                  /* addrlen */ *const libc::socklen_t)]
    pub fn getsockname(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let fd: libc::c_int = args.get(0).into();
        let addr_ptr: PluginPtr = args.get(1).into();
        let addr_len_ptr: TypedPluginPtr<libc::socklen_t> =
            TypedPluginPtr::new::<libc::socklen_t>(args.get(2).into(), 1);

        // get the descriptor, or return early if it doesn't exist
        let desc = Self::get_descriptor(ctx.process, fd)?;

        let file = match desc.file() {
            CompatFile::New(file) => file,
            // if it's a legacy file, use the C syscall handler instead
            CompatFile::Legacy(_) => {
                return unsafe {
                    c::syscallhandler_getsockname(
                        ctx.thread.csyscallhandler(),
                        args as *const c::SysCallArgs,
                    )
                    .into()
                }
            }
        };

        let File::Socket(socket) = file.inner_file() else {
            return Err(Errno::ENOTSOCK.into());
        };

        // linux will return an EFAULT before other errors
        if addr_ptr.is_null() || addr_len_ptr.is_null() {
            return Err(Errno::EFAULT.into());
        }

        let addr_to_write = socket.borrow().getsockname()?;

        debug!("Returning socket address of {:?}", addr_to_write);
        write_sockaddr(
            ctx.process.memory_mut(),
            addr_to_write.as_ref(),
            addr_ptr,
            addr_len_ptr,
        )?;

        Ok(0.into())
    }

    #[log_syscall(/* rv */ libc::c_int, /* sockfd */ libc::c_int, /* addr */ *const libc::sockaddr,
                  /* addrlen */ *const libc::socklen_t)]
    pub fn getpeername(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let fd: libc::c_int = args.get(0).into();
        let addr_ptr: PluginPtr = args.get(1).into();
        let addr_len_ptr: TypedPluginPtr<libc::socklen_t> =
            TypedPluginPtr::new::<libc::socklen_t>(args.get(2).into(), 1);

        // get the descriptor, or return early if it doesn't exist
        let desc = Self::get_descriptor(ctx.process, fd)?;

        let file = match desc.file() {
            CompatFile::New(file) => file,
            // if it's a legacy file, use the C syscall handler instead
            CompatFile::Legacy(_) => {
                return unsafe {
                    c::syscallhandler_getpeername(
                        ctx.thread.csyscallhandler(),
                        args as *const c::SysCallArgs,
                    )
                    .into()
                }
            }
        };

        let File::Socket(socket) = file.inner_file() else {
            return Err(Errno::ENOTSOCK.into());
        };

        // linux will return an EFAULT before other errors like ENOTCONN
        if addr_ptr.is_null() || addr_len_ptr.is_null() {
            return Err(Errno::EFAULT.into());
        }

        let addr_to_write = socket.borrow().getpeername()?;

        debug!("Returning peer address of {:?}", addr_to_write);
        write_sockaddr(
            ctx.process.memory_mut(),
            addr_to_write.as_ref(),
            addr_ptr,
            addr_len_ptr,
        )?;

        Ok(0.into())
    }

    #[log_syscall(/* rv */ libc::c_int, /* sockfd */ libc::c_int, /* backlog */ libc::c_int)]
    pub fn listen(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let fd: libc::c_int = args.get(0).into();
        let backlog: libc::c_int = args.get(1).into();

        // get the descriptor, or return early if it doesn't exist
        let desc = Self::get_descriptor(ctx.process, fd)?;

        let file = match desc.file() {
            CompatFile::New(file) => file,
            // if it's a legacy file, use the C syscall handler instead
            CompatFile::Legacy(_) => {
                return unsafe {
                    c::syscallhandler_listen(
                        ctx.thread.csyscallhandler(),
                        args as *const c::SysCallArgs,
                    )
                    .into()
                }
            }
        };

        let File::Socket(socket) = file.inner_file() else {
            return Err(Errno::ENOTSOCK.into());
        };

        CallbackQueue::queue_and_run(|cb_queue| socket.borrow_mut().listen(backlog, cb_queue))?;

        Ok(0.into())
    }

    #[log_syscall(/* rv */ libc::c_int, /* sockfd */ libc::c_int, /* addr */ *const libc::sockaddr,
                  /* addrlen */ *const libc::socklen_t)]
    pub fn accept(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let fd: libc::c_int = args.get(0).into();
        let addr_ptr: PluginPtr = args.get(1).into();
        let addr_len_ptr: PluginPtr = args.get(2).into();

        // if we were previously blocked, get the active file from the last syscall handler
        // invocation since it may no longer exist in the descriptor table
        let file = ctx
            .thread
            .syscall_condition()
            // if this was for a C descriptor, then there won't be an active file object
            .map(|x| x.active_file().cloned())
            .flatten();

        let file = match file {
            // we were previously blocked, so re-use the file from the previous syscall invocation
            Some(x) => x,
            // get the file from the descriptor table, or return early if it doesn't exist
            None => match Self::get_descriptor(ctx.process, fd)?.file() {
                CompatFile::New(file) => file.clone(),
                // if it's a legacy file, use the C syscall handler instead
                CompatFile::Legacy(_) => {
                    return unsafe {
                        c::syscallhandler_accept(
                            ctx.thread.csyscallhandler(),
                            args as *const SysCallArgs,
                        )
                        .into()
                    };
                }
            },
        };

        self.accept_helper(ctx, file, addr_ptr, addr_len_ptr, 0)
    }

    #[log_syscall(/* rv */ libc::c_int, /* sockfd */ libc::c_int, /* addr */ *const libc::sockaddr,
                  /* addrlen */ *const libc::socklen_t, /* flags */ libc::c_int)]
    pub fn accept4(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let fd: libc::c_int = args.get(0).into();
        let addr_ptr: PluginPtr = args.get(1).into();
        let addr_len_ptr: PluginPtr = args.get(2).into();
        let flags: libc::c_int = args.get(3).into();

        // if we were previously blocked, get the active file from the last syscall handler
        // invocation since it may no longer exist in the descriptor table
        let file = ctx
            .thread
            .syscall_condition()
            // if this was for a C descriptor, then there won't be an active file object
            .map(|x| x.active_file().cloned())
            .flatten();

        let file = match file {
            // we were previously blocked, so re-use the file from the previous syscall invocation
            Some(x) => x,
            // get the file from the descriptor table, or return early if it doesn't exist
            None => match Self::get_descriptor(ctx.process, fd)?.file() {
                CompatFile::New(file) => file.clone(),
                // if it's a legacy file, use the C syscall handler instead
                CompatFile::Legacy(_) => {
                    return unsafe {
                        c::syscallhandler_accept4(
                            ctx.thread.csyscallhandler(),
                            args as *const SysCallArgs,
                        )
                        .into()
                    };
                }
            },
        };

        self.accept_helper(ctx, file, addr_ptr, addr_len_ptr, flags)
    }

    fn accept_helper(
        &self,
        ctx: &mut ThreadContext,
        open_file: OpenFile,
        addr_ptr: PluginPtr,
        addr_len_ptr: PluginPtr,
        flags: libc::c_int,
    ) -> SyscallResult {
        let File::Socket(ref socket) = open_file.inner_file() else {
            return Err(Errno::ENOTSOCK.into());
        };

        // get the accept flags
        let flags = match SockFlag::from_bits(flags) {
            Some(x) => x,
            None => {
                // linux doesn't return an error if there are unexpected flags
                warn!("Invalid recvfrom flags: {}", flags);
                SockFlag::from_bits_truncate(flags)
            }
        };

        let result = CallbackQueue::queue_and_run(|cb_queue| socket.borrow_mut().accept(cb_queue));

        let file_status = socket.borrow().get_status();

        // if the syscall would block and it's a blocking descriptor
        if result.as_ref().err() == Some(&Errno::EWOULDBLOCK.into())
            && !file_status.contains(FileStatus::NONBLOCK)
        {
            let trigger = Trigger::from_file(open_file.inner_file().clone(), FileState::READABLE);
            let mut cond = SysCallCondition::new(trigger);
            let supports_sa_restart = socket.borrow().supports_sa_restart();
            cond.set_active_file(open_file);

            return Err(SyscallError::Blocked(Blocked {
                condition: cond,
                restartable: supports_sa_restart,
            }));
        }

        // must not drop the new socket without closing
        let new_socket = result?;

        let from_addr = new_socket.borrow().getpeername().unwrap();

        if !addr_ptr.is_null() {
            if let Err(e) = write_sockaddr(
                ctx.process.memory_mut(),
                from_addr.as_ref(),
                addr_ptr,
                TypedPluginPtr::new::<libc::socklen_t>(addr_len_ptr, 1),
            ) {
                CallbackQueue::queue_and_run(|cb_queue| new_socket.borrow_mut().close(cb_queue))
                    .unwrap();
                return Err(e);
            }
        }

        if flags.contains(SockFlag::SOCK_NONBLOCK) {
            new_socket.borrow_mut().set_status(FileStatus::NONBLOCK);
        }

        let mut new_desc =
            Descriptor::new(CompatFile::New(OpenFile::new(File::Socket(new_socket))));

        if flags.contains(SockFlag::SOCK_CLOEXEC) {
            new_desc.set_flags(DescriptorFlags::CLOEXEC);
        }

        let new_fd = ctx.process.register_descriptor(new_desc);

        Ok(new_fd.into())
    }

    #[log_syscall(/* rv */ libc::c_int, /* sockfd */ libc::c_int, /* addr */ *const libc::sockaddr,
                  /* addrlen */ libc::socklen_t)]
    pub fn connect(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let fd: libc::c_int = args.get(0).into();
        let addr_ptr: PluginPtr = args.get(1).into();
        let addr_len: libc::socklen_t = args.get(2).into();

        // if we were previously blocked, get the active file from the last syscall handler
        // invocation since it may no longer exist in the descriptor table
        let file = ctx
            .thread
            .syscall_condition()
            // if this was for a C descriptor, then there won't be an active file object
            .map(|x| x.active_file().cloned())
            .flatten();

        let file = match file {
            // we were previously blocked, so re-use the file from the previous syscall invocation
            Some(x) => x,
            // get the file from the descriptor table, or return early if it doesn't exist
            None => match Self::get_descriptor(ctx.process, fd)?.file() {
                CompatFile::New(file) => file.clone(),
                // if it's a legacy file, use the C syscall handler instead
                CompatFile::Legacy(_) => {
                    return unsafe {
                        c::syscallhandler_connect(
                            ctx.thread.csyscallhandler(),
                            args as *const SysCallArgs,
                        )
                        .into()
                    };
                }
            },
        };

        let File::Socket(socket) = file.inner_file() else {
            return Err(Errno::ENOTSOCK.into());
        };

        let addr = read_sockaddr(ctx.process.memory(), addr_ptr, addr_len)?.ok_or(Errno::EINVAL)?;

        let mut rv =
            CallbackQueue::queue_and_run(|cb_queue| Socket::connect(socket, &addr, cb_queue));

        // if we will block
        if let Err(SyscallError::Blocked(ref mut blocked)) = rv {
            // make sure the file does not close before the blocking syscall completes
            blocked.condition.set_active_file(file);
        }

        rv?;

        Ok(0.into())
    }

    #[log_syscall(/* rv */ libc::c_int, /* sockfd */ libc::c_int, /* how */ libc::c_int)]
    pub fn shutdown(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let fd: libc::c_int = args.get(0).into();

        // get the descriptor, or return early if it doesn't exist
        let desc = Self::get_descriptor(ctx.process, fd)?;

        let file = match desc.file() {
            CompatFile::New(file) => file,
            // if it's a legacy file, use the C syscall handler instead
            CompatFile::Legacy(_) => {
                return unsafe {
                    c::syscallhandler_shutdown(
                        ctx.thread.csyscallhandler(),
                        args as *const c::SysCallArgs,
                    )
                    .into()
                }
            }
        };

        let File::Socket(socket) = file.inner_file() else {
            return Err(Errno::ENOTSOCK.into());
        };

        // TODO: support rust sockets
        log::warn!(
            "shutdown() syscall not yet supported for fd {} of type {:?}; Returning ENOSYS",
            fd,
            socket,
        );
        Err(Errno::ENOSYS.into())
    }

    #[log_syscall(/* rv */ libc::c_int, /* domain */ nix::sys::socket::AddressFamily,
                  /* type */ libc::c_int, /* protocol */ libc::c_int, /* sv */ [libc::c_int; 2])]
    pub fn socketpair(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let domain: libc::c_int = args.get(0).into();
        let socket_type: libc::c_int = args.get(1).into();
        let protocol: libc::c_int = args.get(2).into();
        let fd_ptr: PluginPtr = args.get(3).into();

        // remove any flags from the socket type
        let flags = socket_type & (libc::SOCK_NONBLOCK | libc::SOCK_CLOEXEC);
        let socket_type = socket_type & !flags;

        // only AF_UNIX (AF_LOCAL) is supported on Linux (and technically AF_TIPC)
        if domain != libc::AF_UNIX {
            warn!("Domain {} is not supported for socketpair()", domain);
            return Err(Errno::EOPNOTSUPP.into());
        }

        let socket_type = match UnixSocketType::try_from(socket_type) {
            Ok(x) => x,
            Err(e) => {
                warn!("{}", e);
                return Err(Errno::EPROTONOSUPPORT.into());
            }
        };

        // unix sockets don't support any protocols
        if protocol != 0 {
            warn!(
                "Unsupported socket protocol {}, we only support default protocol 0",
                protocol
            );
            return Err(Errno::EPROTONOSUPPORT.into());
        }

        let mut file_flags = FileStatus::empty();
        let mut descriptor_flags = DescriptorFlags::empty();

        if flags & libc::SOCK_NONBLOCK != 0 {
            file_flags.insert(FileStatus::NONBLOCK);
        }

        if flags & libc::SOCK_CLOEXEC != 0 {
            descriptor_flags.insert(DescriptorFlags::CLOEXEC);
        }

        let (socket_1, socket_2) = CallbackQueue::queue_and_run(|cb_queue| {
            UnixSocket::pair(
                file_flags,
                socket_type,
                ctx.host.abstract_unix_namespace(),
                cb_queue,
            )
        });

        // file descriptors for the sockets
        let mut desc_1 = Descriptor::new(CompatFile::New(OpenFile::new(File::Socket(
            Socket::Unix(socket_1),
        ))));
        let mut desc_2 = Descriptor::new(CompatFile::New(OpenFile::new(File::Socket(
            Socket::Unix(socket_2),
        ))));

        // set the file descriptor flags
        desc_1.set_flags(descriptor_flags);
        desc_2.set_flags(descriptor_flags);

        // register the file descriptors
        let fd_1 = ctx.process.register_descriptor(desc_1);
        let fd_2 = ctx.process.register_descriptor(desc_2);

        // try to write them to the caller
        let fds = [i32::try_from(fd_1).unwrap(), i32::try_from(fd_2).unwrap()];
        let write_res = ctx
            .process
            .memory_mut()
            .copy_to_ptr(TypedPluginPtr::new::<libc::c_int>(fd_ptr.into(), 2), &fds);

        // clean up in case of error
        match write_res {
            Ok(_) => Ok(0.into()),
            Err(e) => {
                CallbackQueue::queue_and_run(|cb_queue| {
                    // ignore any errors when closing
                    ctx.process
                        .deregister_descriptor(fd_1)
                        .unwrap()
                        .close(ctx.host, cb_queue);
                    ctx.process
                        .deregister_descriptor(fd_2)
                        .unwrap()
                        .close(ctx.host, cb_queue);
                });
                Err(e.into())
            }
        }
    }

    #[log_syscall(/* rv */ libc::c_int, /* sockfd */ libc::c_int, /* level */ libc::c_int,
                  /* optname */ libc::c_int, /* optval */ *const libc::c_void,
                  /* optlen */ *const libc::socklen_t)]
    pub fn getsockopt(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let fd: libc::c_int = args.get(0).into();

        // get the descriptor, or return early if it doesn't exist
        let desc = Self::get_descriptor(ctx.process, fd)?;

        let file = match desc.file() {
            CompatFile::New(file) => file,
            // if it's a legacy file, use the C syscall handler instead
            CompatFile::Legacy(_) => {
                return unsafe {
                    c::syscallhandler_getsockopt(
                        ctx.thread.csyscallhandler(),
                        args as *const c::SysCallArgs,
                    )
                    .into()
                }
            }
        };

        let File::Socket(socket) = file.inner_file() else {
            return Err(Errno::ENOTSOCK.into());
        };

        // TODO: support rust sockets
        log::warn!(
            "getsockopt() syscall not yet supported for fd {} of type {:?}; Returning ENOSYS",
            fd,
            socket,
        );
        Err(Errno::ENOSYS.into())
    }

    #[log_syscall(/* rv */ libc::c_int, /* sockfd */ libc::c_int, /* level */ libc::c_int,
                  /* optname */ libc::c_int, /* optval */ *const libc::c_void,
                  /* optlen */ libc::socklen_t)]
    pub fn setsockopt(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let fd: libc::c_int = args.get(0).into();

        // get the descriptor, or return early if it doesn't exist
        let desc = Self::get_descriptor(ctx.process, fd)?;

        let file = match desc.file() {
            CompatFile::New(file) => file,
            // if it's a legacy file, use the C syscall handler instead
            CompatFile::Legacy(_) => {
                return unsafe {
                    c::syscallhandler_setsockopt(
                        ctx.thread.csyscallhandler(),
                        args as *const c::SysCallArgs,
                    )
                    .into()
                }
            }
        };

        let File::Socket(socket) = file.inner_file() else {
            return Err(Errno::ENOTSOCK.into());
        };

        // TODO: support rust sockets
        log::warn!(
            "setsockopt() syscall not yet supported for fd {} of type {:?}; Returning ENOSYS",
            fd,
            socket,
        );
        Err(Errno::ENOSYS.into())
    }
}

fn write_sockaddr(
    mem: &mut MemoryManager,
    addr: Option<&SockaddrStorage>,
    plugin_addr: PluginPtr,
    plugin_addr_len: TypedPluginPtr<libc::socklen_t>,
) -> Result<(), SyscallError> {
    let addr = match addr {
        Some(x) => x,
        None => {
            mem.copy_to_ptr(plugin_addr_len, &[0])?;
            return Ok(());
        }
    };

    let from_addr_slice = addr.as_slice();
    let from_len: u32 = from_addr_slice.len().try_into().unwrap();

    // get the provided address buffer length, and overwrite it with the real address length
    let plugin_addr_len = {
        let mut plugin_addr_len = mem.memory_ref_mut(plugin_addr_len)?;
        let plugin_addr_len_value = plugin_addr_len.get_mut(0).unwrap();

        // keep a copy before we change it
        let plugin_addr_len_copy = *plugin_addr_len_value;

        *plugin_addr_len_value = from_len;

        plugin_addr_len.flush()?;
        plugin_addr_len_copy
    };

    // return early if the address length is 0
    if plugin_addr_len == 0 {
        return Ok(());
    }

    // the minimum of the given address buffer length and the real address length
    let len_to_copy = std::cmp::min(from_len, plugin_addr_len).try_into().unwrap();

    let plugin_addr = TypedPluginPtr::new::<MaybeUninit<u8>>(plugin_addr, len_to_copy);
    mem.copy_to_ptr(plugin_addr, &from_addr_slice[..len_to_copy])?;

    Ok(())
}

fn read_sockaddr(
    mem: &MemoryManager,
    addr_ptr: PluginPtr,
    addr_len: libc::socklen_t,
) -> Result<Option<SockaddrStorage>, SyscallError> {
    if addr_ptr.is_null() {
        return Ok(None);
    }

    let addr_len_usize: usize = addr_len.try_into().unwrap();

    // this won't have the correct alignment, but that's fine since `SockaddrStorage::from_bytes()`
    // doesn't require alignment
    let mut addr_buf = [MaybeUninit::new(0u8); std::mem::size_of::<libc::sockaddr_storage>()];

    // make sure we will not lose data when we copy
    if addr_len_usize > std::mem::size_of_val(&addr_buf) {
        warn!(
            "Shadow does not support the address length {}, which is larger than {}",
            addr_len,
            std::mem::size_of_val(&addr_buf),
        );
        return Err(Errno::EINVAL.into());
    }

    let addr_buf = &mut addr_buf[..addr_len_usize];

    mem.copy_from_ptr(
        addr_buf,
        TypedPluginPtr::new::<MaybeUninit<u8>>(addr_ptr, addr_len_usize),
    )?;

    let addr = unsafe { SockaddrStorage::from_bytes(&addr_buf).ok_or(Errno::EINVAL)? };

    Ok(Some(addr))
}
