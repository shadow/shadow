use crate::cshadow as c;
use crate::host::descriptor::socket::inet::legacy_tcp::LegacyTcpSocket;
use crate::host::descriptor::socket::inet::InetSocket;
use crate::host::descriptor::socket::unix::{UnixSocket, UnixSocketType};
use crate::host::descriptor::socket::{RecvmsgArgs, RecvmsgReturn, SendmsgArgs, Socket};
use crate::host::descriptor::{
    CompatFile, Descriptor, DescriptorFlags, File, FileState, FileStatus, OpenFile,
};
use crate::host::syscall::handler::{
    read_sockaddr, write_sockaddr, write_sockaddr_and_len, SyscallContext, SyscallHandler,
};
use crate::host::syscall::io::{
    self, read_mmsghdrs, read_msghdr, write_mmsghdrs, write_msghdr, IoVec, MmsgHdr, MsgHdr,
};
use crate::host::syscall::type_formatting::{SyscallBufferArg, SyscallSockAddrArg};
use crate::host::syscall::Trigger;
use crate::host::syscall_condition::SysCallCondition;
use crate::host::syscall_types::{Blocked, PluginPtr, TypedPluginPtr};
use crate::host::syscall_types::{SyscallError, SyscallResult};
use crate::utility::callback_queue::CallbackQueue;
use crate::utility::sockaddr::SockaddrStorage;

use log::*;
use nix::errno::Errno;
use nix::sys::socket::{MsgFlags, Shutdown, SockFlag};

use syscall_logger::log_syscall;

impl SyscallHandler {
    #[log_syscall(/* rv */ libc::c_int, /* domain */ nix::sys::socket::AddressFamily,
                  /* type */ libc::c_int, /* protocol */ libc::c_int)]
    pub fn socket(
        ctx: &mut SyscallContext,
        domain: libc::c_int,
        socket_type: libc::c_int,
        protocol: libc::c_int,
    ) -> SyscallResult {
        // remove any flags from the socket type
        let flags = socket_type & (libc::SOCK_NONBLOCK | libc::SOCK_CLOEXEC);
        let socket_type = socket_type & !flags;

        // if it's not a unix socket or tcp socket, use the C syscall handler instead
        if domain != libc::AF_UNIX && (domain != libc::AF_INET || socket_type != libc::SOCK_STREAM)
        {
            return Self::legacy_syscall(c::syscallhandler_socket, ctx);
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
                    &ctx.objs.host.abstract_unix_namespace(),
                ))
            }
            libc::AF_INET => match socket_type {
                libc::SOCK_STREAM => {
                    if protocol != 0 && protocol != libc::IPPROTO_TCP {
                        warn!("Unsupported inet stream socket protocol {protocol}");
                        return Err(Errno::EPROTONOSUPPORT.into());
                    }
                    Socket::Inet(InetSocket::LegacyTcp(LegacyTcpSocket::new(
                        file_flags,
                        ctx.objs.host,
                    )))
                }
                _ => panic!("Should have called the C syscall handler"),
            },
            _ => return Err(Errno::EAFNOSUPPORT.into()),
        };

        let mut desc = Descriptor::new(CompatFile::New(OpenFile::new(File::Socket(socket))));
        desc.set_flags(descriptor_flags);

        let fd = ctx
            .objs
            .process
            .descriptor_table_borrow_mut()
            .register_descriptor(desc)
            .or(Err(Errno::ENFILE))?;

        log::trace!("Created socket fd {}", fd);

        Ok(fd.val().into())
    }

    #[log_syscall(/* rv */ libc::c_int, /* sockfd */ libc::c_int,
                  /* addr */ SyscallSockAddrArg</* addrlen */ 2>, /* addrlen */ libc::socklen_t)]
    pub fn bind(
        ctx: &mut SyscallContext,
        fd: libc::c_int,
        addr_ptr: PluginPtr,
        addr_len: libc::socklen_t,
    ) -> SyscallResult {
        let file = {
            // get the descriptor, or return early if it doesn't exist
            let desc_table = ctx.objs.process.descriptor_table_borrow();
            let desc = Self::get_descriptor(&desc_table, fd)?;

            let file = match desc.file() {
                CompatFile::New(file) => file,
                // if it's a legacy file, use the C syscall handler instead
                CompatFile::Legacy(_) => {
                    drop(desc_table);
                    return Self::legacy_syscall(c::syscallhandler_bind, ctx);
                }
            };

            file.inner_file().clone()
        };

        let File::Socket(ref socket) = file else {
            return Err(Errno::ENOTSOCK.into());
        };

        let addr = read_sockaddr(&ctx.objs.process.memory_borrow(), addr_ptr, addr_len)?;

        log::trace!("Attempting to bind fd {} to {:?}", fd, addr);

        let mut rng = ctx.objs.host.random_mut();
        let net_ns = ctx.objs.host.network_namespace_borrow();
        Socket::bind(socket, addr.as_ref(), &net_ns, &mut *rng)
    }

    #[log_syscall(/* rv */ libc::ssize_t, /* sockfd */ libc::c_int,
                  /* buf */ SyscallBufferArg</* len */ 2>, /* len */ libc::size_t,
                  /* flags */ nix::sys::socket::MsgFlags,
                  /* dest_addr */ SyscallSockAddrArg</* addrlen */ 5>,
                  /* addrlen */ libc::socklen_t)]
    pub fn sendto(
        ctx: &mut SyscallContext,
        fd: libc::c_int,
        buf_ptr: PluginPtr,
        buf_len: libc::size_t,
        flags: libc::c_int,
        addr_ptr: PluginPtr,
        addr_len: libc::socklen_t,
    ) -> Result<libc::ssize_t, SyscallError> {
        // if we were previously blocked, get the active file from the last syscall handler
        // invocation since it may no longer exist in the descriptor table
        let file = ctx
            .objs
            .thread
            .syscall_condition()
            // if this was for a C descriptor, then there won't be an active file object
            .and_then(|x| x.active_file().cloned());

        let file = match file {
            // we were previously blocked, so re-use the file from the previous syscall invocation
            Some(x) => x,
            // get the file from the descriptor table, or return early if it doesn't exist
            None => {
                let desc_table = ctx.objs.process.descriptor_table_borrow();
                match Self::get_descriptor(&desc_table, fd)?.file() {
                    CompatFile::New(file) => file.clone(),
                    // if it's a legacy file, use the C syscall handler instead
                    CompatFile::Legacy(_) => {
                        drop(desc_table);
                        return Self::legacy_syscall(c::syscallhandler_sendto, ctx).map(Into::into);
                    }
                }
            }
        };

        let File::Socket(ref socket) = file.inner_file() else {
            return Err(Errno::ENOTSOCK.into());
        };

        //if let Socket::Inet(InetSocket::LegacyTcp(_)) = socket {
        //    return Self::legacy_syscall(c::syscallhandler_sendto, ctx).map(Into::into);
        //}

        let mut mem = ctx.objs.process.memory_borrow_mut();

        let iov = IoVec {
            base: buf_ptr,
            len: buf_len,
        };

        let args = SendmsgArgs {
            addr: read_sockaddr(&mem, addr_ptr, addr_len)?,
            iovs: &[iov],
            control_ptr: TypedPluginPtr::new::<u8>(PluginPtr::null(), 0),
            flags,
        };

        // call the socket's sendmsg(), and run any resulting events
        let mut result = crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
            CallbackQueue::queue_and_run(|cb_queue| {
                Socket::sendmsg(socket, args, &mut mem, cb_queue)
            })
        });

        // if the syscall will block, keep the file open until the syscall restarts
        if let Some(err) = result.as_mut().err() {
            if let Some(cond) = err.blocked_condition() {
                cond.set_active_file(file);
            }
        }

        let bytes_written = result?;

        Ok(bytes_written.try_into().unwrap())

        /*
        let iov = IoVec {
            base: buf_ptr,
            len: buf_len,
        };

        let msg = MsgHdr {
            name: addr_ptr,
            name_len: addr_len,
            iovs: Vec::from([iov]),
            control: PluginPtr::null(),
            control_len: 0,
            flags,
        };

        let mut result = Self::sendmsg_helper(ctx, socket, msg);

        // if the syscall will block, keep the file open until the syscall restarts
        if let Some(err) = result.as_mut().err() {
            if let Some(cond) = err.blocked_condition() {
                cond.set_active_file(file);
            }
        }

        let (bytes_written, _msg) = result?;

        Ok(bytes_written.into())
        */
    }

    #[log_syscall(/* rv */ libc::ssize_t, /* sockfd */ libc::c_int, /* msg */ *const libc::msghdr,
                  /* flags */ nix::sys::socket::MsgFlags)]
    pub fn sendmsg(
        ctx: &mut SyscallContext,
        fd: libc::c_int,
        msg_ptr: PluginPtr,
        flags: libc::c_int,
    ) -> Result<libc::ssize_t, SyscallError> {
        // if we were previously blocked, get the active file from the last syscall handler
        // invocation since it may no longer exist in the descriptor table
        let file = ctx
            .objs
            .thread
            .syscall_condition()
            // if this was for a C descriptor, then there won't be an active file object
            .and_then(|x| x.active_file().cloned());

        let file = match file {
            // we were previously blocked, so re-use the file from the previous syscall invocation
            Some(x) => x,
            // get the file from the descriptor table, or return early if it doesn't exist
            None => {
                let desc_table = ctx.objs.process.descriptor_table_borrow();
                match Self::get_descriptor(&desc_table, fd)?.file() {
                    CompatFile::New(file) => file.clone(),
                    CompatFile::Legacy(file) => {
                        let file_type = unsafe { c::legacyfile_getType(file.ptr()) };
                        if file_type == c::_LegacyFileType_DT_UDPSOCKET {
                            return Err(Errno::ENOSYS.into());
                        }
                        return Err(Errno::ENOTSOCK.into());
                    }
                }
            }
        };

        let File::Socket(ref socket) = file.inner_file() else {
            return Err(Errno::ENOTSOCK.into());
        };

        //if let Socket::Inet(InetSocket::LegacyTcp(_)) = socket {
        //    return Err(Errno::ENOSYS.into());
        //}

        let mut mem = ctx.objs.process.memory_borrow_mut();

        let msg = read_msghdr(&mem, msg_ptr)?;

        let args = SendmsgArgs {
            addr: read_sockaddr(&mem, msg.name, msg.name_len)?,
            iovs: &msg.iovs,
            control_ptr: TypedPluginPtr::new::<u8>(msg.control, msg.control_len),
            // note: "the msg_flags field is ignored" for sendmsg; see send(2)
            // TODO: for sendmmsg, the MSG_EOR flag is used: https://sourceware.org/bugzilla/show_bug.cgi?id=23037
            flags,
        };

        // call the socket's sendmsg(), and run any resulting events
        let mut result = crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
            CallbackQueue::queue_and_run(|cb_queue| {
                Socket::sendmsg(socket, args, &mut mem, cb_queue)
            })
        });

        // if the syscall will block, keep the file open until the syscall restarts
        if let Some(err) = result.as_mut().err() {
            if let Some(cond) = err.blocked_condition() {
                cond.set_active_file(file);
            }
        }

        let bytes_written = result?;

        Ok(bytes_written.try_into().unwrap())
    }

    #[log_syscall(/* rv */ libc::c_int, /* sockfd */ libc::c_int, /* msgvec */ *const libc::mmsghdr,
                  /* vlen */ libc::c_uint, /* flags */ nix::sys::socket::MsgFlags)]
    pub fn sendmmsg(
        ctx: &mut SyscallContext,
        fd: libc::c_int,
        mmsg_ptr: PluginPtr,
        mmsg_count: libc::c_uint,
        flags: libc::c_int,
    ) -> Result<libc::c_int, SyscallError> {
        // if we were previously blocked, get the active file from the last syscall handler
        // invocation since it may no longer exist in the descriptor table
        let file = ctx
            .objs
            .thread
            .syscall_condition()
            // if this was for a C descriptor, then there won't be an active file object
            .and_then(|x| x.active_file().cloned());

        let file = match file {
            // we were previously blocked, so re-use the file from the previous syscall invocation
            Some(x) => x,
            // get the file from the descriptor table, or return early if it doesn't exist
            None => {
                let desc_table = ctx.objs.process.descriptor_table_borrow();
                match Self::get_descriptor(&desc_table, fd)?.file() {
                    CompatFile::New(file) => file.clone(),
                    CompatFile::Legacy(file) => {
                        let file_type = unsafe { c::legacyfile_getType(file.ptr()) };
                        if file_type == c::_LegacyFileType_DT_UDPSOCKET {
                            return Err(Errno::ENOSYS.into());
                        }
                        return Err(Errno::ENOTSOCK.into());
                    }
                }
            }
        };

        let File::Socket(ref socket) = file.inner_file() else {
            return Err(Errno::ENOTSOCK.into());
        };

        //if let Socket::Inet(InetSocket::LegacyTcp(_)) = socket {
        //    return Err(Errno::ENOSYS.into());
        //}

        if mmsg_count > libc::UIO_MAXIOV.try_into().unwrap() {
            // TODO: test this
            return Err(Errno::EINVAL.into());
        }

        let mut mem = ctx.objs.process.memory_borrow_mut();

        let mut mmsgs = read_mmsghdrs(&mem, mmsg_ptr, mmsg_count.try_into().unwrap())?;
        let mut num_msgs = 0;

        // TODO: the blocking behaviour isn't correct: "A blocking sendmmsg() call blocks until vlen
        // messages have been sent."

        // run in a closure so that a `?` doesn't return from the syscall handler
        let result: Result<_, SyscallError> = (|| {
            for mmsg in &mut mmsgs {
                let args = SendmsgArgs {
                    addr: read_sockaddr(&mem, mmsg.hdr.name, mmsg.hdr.name_len)?,
                    iovs: &mmsg.hdr.iovs,
                    control_ptr: TypedPluginPtr::new::<u8>(mmsg.hdr.control, mmsg.hdr.control_len),
                    // note: "the msg_flags field is ignored" for sendmsg; see send(2)
                    // TODO: for sendmmsg, the MSG_EOR flag is used: https://sourceware.org/bugzilla/show_bug.cgi?id=23037
                    flags: flags | (mmsg.hdr.flags & libc::MSG_EOR),
                };

                // call the socket's sendmsg(), and run any resulting events
                let bytes_written =
                    crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
                        CallbackQueue::queue_and_run(|cb_queue| {
                            Socket::sendmsg(socket, args, &mut mem, cb_queue)
                        })
                    })?;

                mmsg.len = bytes_written.try_into().unwrap();
                num_msgs += 1;
            }

            Ok(())
        })();

        assert!(num_msgs as u32 <= mmsg_count);
        write_mmsghdrs(&mut mem, mmsg_ptr, &mmsgs[..num_msgs.try_into().unwrap()])?;

        if let Err(mut err) = result {
            // sendmmsg(2): "If an error occurs after at least one message has been sent, the call
            // succeeds, and returns the number of messages sent. The error code is lost."
            if num_msgs == 0 {
                // if the syscall will block, keep the file open until the syscall restarts
                if let Some(cond) = err.blocked_condition() {
                    cond.set_active_file(file);
                }

                return Err(err);
            }
        }

        Ok(num_msgs)
    }

    /*
    pub fn sendmsg_helper(
        ctx: &mut SyscallContext,
        socket: &Socket,
        addr: Option<SockaddrStorage>,
        iovs: &[IoVec],
        control_ptr: TypedPluginPtr<u8>,
        flags: libc::c_int,
    ) -> Result<(libc::ssize_t, MsgHdr), SyscallError> {
        let mut mem = ctx.objs.process.memory_borrow_mut();

        // call the socket's sendmsg(), and run any resulting events
        let bytes_written = CallbackQueue::queue_and_run(|cb_queue| {
            Socket::sendmsg(socket, &mut msgs, &mut mem, cb_queue)
        })?;



        /*
        let mmsg = MmsgHdr {
            hdr: msg,
            // this should get filled in by sendmmsg
            len: 0,
        };

        let (num_msgs, mut mmsgs) = Self::sendmmsg_helper(ctx, socket, Vec::from([mmsg]))?;
        let mmsg = mmsgs.pop().unwrap();

        // we only gave it one message, so it can't have sent more
        assert!(num_msgs <= 1);

        // if it sent 0 messages, then the length should still be 0
        assert!(num_msgs != 0 || mmsg.len == 0);

        return Ok((mmsg.len.try_into().unwrap(), mmsg.hdr));
        */
    }

    pub fn sendmmsg_helper(
        ctx: &mut SyscallContext,
        socket: &Socket,
        mut msgs: Vec<MmsgHdr>,
    ) -> Result<(libc::c_int, Vec<MmsgHdr>), SyscallError> {
        let mut mem = ctx.objs.process.memory_borrow_mut();

        // call the socket's sendmmsg(), and run any resulting events
        let num_msgs = CallbackQueue::queue_and_run(|cb_queue| {
            Socket::sendmmsg(socket, &mut msgs, &mut mem, cb_queue)
        })?;

        Ok((num_msgs, msgs))
    }
    */

    #[log_syscall(/* rv */ libc::ssize_t, /* sockfd */ libc::c_int, /* buf */ *const libc::c_void,
                  /* len */ libc::size_t, /* flags */ nix::sys::socket::MsgFlags,
                  /* src_addr */ *const libc::sockaddr, /* addrlen */ *const libc::socklen_t)]
    pub fn recvfrom(
        ctx: &mut SyscallContext,
        fd: libc::c_int,
        buf_ptr: PluginPtr,
        buf_len: libc::size_t,
        flags: libc::c_int,
        addr_ptr: PluginPtr,
        addr_len_ptr: PluginPtr,
    ) -> Result<libc::ssize_t, SyscallError> {
        // if we were previously blocked, get the active file from the last syscall handler
        // invocation since it may no longer exist in the descriptor table
        let file = ctx
            .objs
            .thread
            .syscall_condition()
            // if this was for a C descriptor, then there won't be an active file object
            .and_then(|x| x.active_file().cloned());

        let file = match file {
            // we were previously blocked, so re-use the file from the previous syscall invocation
            Some(x) => x,
            // get the file from the descriptor table, or return early if it doesn't exist
            None => {
                let desc_table = ctx.objs.process.descriptor_table_borrow();
                match Self::get_descriptor(&desc_table, fd)?.file() {
                    CompatFile::New(file) => file.clone(),
                    // if it's a legacy file, use the C syscall handler instead
                    CompatFile::Legacy(_) => {
                        drop(desc_table);
                        return Self::legacy_syscall(c::syscallhandler_recvfrom, ctx)
                            .map(Into::into);
                    }
                }
            }
        };

        let File::Socket(ref socket) = file.inner_file() else {
            return Err(Errno::ENOTSOCK.into());
        };

        //if let Socket::Inet(InetSocket::LegacyTcp(_)) = socket {
        //    return Self::legacy_syscall(c::syscallhandler_recvfrom, ctx).map(Into::into);
        //}

        let addr_len_ptr = TypedPluginPtr::new::<libc::socklen_t>(addr_len_ptr, 1);

        let mut mem = ctx.objs.process.memory_borrow_mut();

        let iov = IoVec {
            base: buf_ptr,
            len: buf_len,
        };

        let args = RecvmsgArgs {
            iovs: &[iov],
            control_ptr: TypedPluginPtr::new::<u8>(PluginPtr::null(), 0),
            flags,
        };

        // call the socket's recvmsg(), and run any resulting events
        let mut result = crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
            CallbackQueue::queue_and_run(|cb_queue| {
                Socket::recvmsg(socket, args, &mut mem, cb_queue)
            })
        });

        // if the syscall will block, keep the file open until the syscall restarts
        if let Some(err) = result.as_mut().err() {
            if let Some(cond) = err.blocked_condition() {
                cond.set_active_file(file);
            }
        }

        let RecvmsgReturn {
            bytes_read,
            addr: from_addr,
            ..
        } = result?;

        if !addr_ptr.is_null() {
            write_sockaddr_and_len(&mut mem, from_addr.as_ref(), addr_ptr, addr_len_ptr)?;
        }

        Ok(bytes_read.try_into().unwrap())

        /*
        let addr_len_ptr = TypedPluginPtr::new::<libc::socklen_t>(addr_len_ptr, 1);

        let iov = IoVec {
            base: buf_ptr,
            len: buf_len,
        };

        // TODO: should this addr len code be simplified?

        // only access the addrlen pointer if both addr and addrlen are non-null
        let use_addrlen = !addr_ptr.is_null() && !addr_len_ptr.is_null();

        // if non-null address but null address length
        if !addr_ptr.is_null() && addr_len_ptr.is_null() {
            return Err(Errno::EFAULT.into());
        }

        let orig_name_len = if use_addrlen {
            let mem = ctx.objs.process.memory_borrow_mut();
            mem.read_vals::<_, 1>(addr_len_ptr)?[0]
        } else {
            // addr_ptr must be null, so a 0 should be fine
            0
        };

        let msg = MsgHdr {
            name: addr_ptr,
            name_len: orig_name_len,
            iovs: Vec::from([iov]),
            control: PluginPtr::null(),
            control_len: 0,
            flags,
        };

        let mut result = Self::recvmsg_helper(ctx, socket, msg);

        // if the syscall will block, keep the file open until the syscall restarts
        if let Some(err) = result.as_mut().err() {
            if let Some(cond) = err.blocked_condition() {
                cond.set_active_file(file);
            }
        }

        let (bytes_read, msg) = result?;

        // write the size of the returned address to the plugin
        if use_addrlen {
            let mut mem = ctx.objs.process.memory_borrow_mut();
            mem.copy_to_ptr(addr_len_ptr, &[msg.name_len]).unwrap();
        }

        Ok(bytes_read.into())
        */
    }

    #[log_syscall(/* rv */ libc::ssize_t, /* sockfd */ libc::c_int, /* msg */ *const libc::msghdr,
                  /* flags */ nix::sys::socket::MsgFlags)]
    pub fn recvmsg(
        ctx: &mut SyscallContext,
        fd: libc::c_int,
        msg_ptr: PluginPtr,
        flags: libc::c_int,
    ) -> Result<libc::ssize_t, SyscallError> {
        // if we were previously blocked, get the active file from the last syscall handler
        // invocation since it may no longer exist in the descriptor table
        let file = ctx
            .objs
            .thread
            .syscall_condition()
            // if this was for a C descriptor, then there won't be an active file object
            .and_then(|x| x.active_file().cloned());

        let file = match file {
            // we were previously blocked, so re-use the file from the previous syscall invocation
            Some(x) => x,
            // get the file from the descriptor table, or return early if it doesn't exist
            None => {
                let desc_table = ctx.objs.process.descriptor_table_borrow();
                match Self::get_descriptor(&desc_table, fd)?.file() {
                    CompatFile::New(file) => file.clone(),
                    CompatFile::Legacy(file) => {
                        let file_type = unsafe { c::legacyfile_getType(file.ptr()) };
                        if file_type == c::_LegacyFileType_DT_UDPSOCKET {
                            return Err(Errno::ENOSYS.into());
                        }
                        return Err(Errno::ENOTSOCK.into());
                    }
                }
            }
        };

        let File::Socket(ref socket) = file.inner_file() else {
            return Err(Errno::ENOTSOCK.into());
        };

        //if let Socket::Inet(InetSocket::LegacyTcp(_)) = socket {
        //    return Err(Errno::ENOSYS.into());
        //}

        let mut mem = ctx.objs.process.memory_borrow_mut();

        let mut msg = read_msghdr(&mem, msg_ptr)?;

        let args = RecvmsgArgs {
            iovs: &msg.iovs,
            control_ptr: TypedPluginPtr::new::<u8>(msg.control, msg.control_len),
            flags,
        };

        // call the socket's recvmsg(), and run any resulting events
        let mut result = crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
            CallbackQueue::queue_and_run(|cb_queue| {
                Socket::recvmsg(socket, args, &mut mem, cb_queue)
            })
        });

        // if the syscall will block, keep the file open until the syscall restarts
        if let Some(err) = result.as_mut().err() {
            if let Some(cond) = err.blocked_condition() {
                cond.set_active_file(file);
            }
        }

        let result = result?;

        // write the socket address to the plugin and update the length in msg
        if !msg.name.is_null() {
            if let Some(from_addr) = result.addr.as_ref() {
                msg.name_len = write_sockaddr(&mut mem, from_addr, msg.name, msg.name_len)?;
            } else {
                msg.name_len = 0;
            }
        }

        // update the control len and flags in msg
        msg.control_len = result.control_len;
        msg.flags = result.msg_flags;

        // write msg back to the plugin
        write_msghdr(&mut mem, msg_ptr, msg)?;

        Ok(result.bytes_read.try_into().unwrap())
    }

    #[log_syscall(/* rv */ libc::c_int, /* sockfd */ libc::c_int, /* msgvec */ *const libc::mmsghdr,
                  /* vlen */ libc::c_uint, /* flags */ nix::sys::socket::MsgFlags,
                  /* timeout */ *const libc::timespec)]
    pub fn recvmmsg(
        ctx: &mut SyscallContext,
        fd: libc::c_int,
        mmsg_ptr: PluginPtr,
        mmsg_count: libc::c_uint,
        flags: libc::c_int,
        timeout_ptr: PluginPtr,
    ) -> Result<libc::c_int, SyscallError> {
        // if we were previously blocked, get the active file from the last syscall handler
        // invocation since it may no longer exist in the descriptor table
        let file = ctx
            .objs
            .thread
            .syscall_condition()
            // if this was for a C descriptor, then there won't be an active file object
            .and_then(|x| x.active_file().cloned());

        let file = match file {
            // we were previously blocked, so re-use the file from the previous syscall invocation
            Some(x) => x,
            // get the file from the descriptor table, or return early if it doesn't exist
            None => {
                let desc_table = ctx.objs.process.descriptor_table_borrow();
                match Self::get_descriptor(&desc_table, fd)?.file() {
                    CompatFile::New(file) => file.clone(),
                    CompatFile::Legacy(file) => {
                        let file_type = unsafe { c::legacyfile_getType(file.ptr()) };
                        if file_type == c::_LegacyFileType_DT_UDPSOCKET {
                            return Err(Errno::ENOSYS.into());
                        }
                        return Err(Errno::ENOTSOCK.into());
                    }
                }
            }
        };

        let File::Socket(ref socket) = file.inner_file() else {
            return Err(Errno::ENOTSOCK.into());
        };

        //if let Socket::Inet(InetSocket::LegacyTcp(_)) = socket {
        //    return Err(Errno::ENOSYS.into());
        //}

        if mmsg_count > libc::UIO_MAXIOV.try_into().unwrap() {
            // TODO: test this
            return Err(Errno::EINVAL.into());
        }

        let mut mem = ctx.objs.process.memory_borrow_mut();

        let timeout_ptr = TypedPluginPtr::new::<libc::timespec>(timeout_ptr, 1);
        let timeout = mem.read_vals::<_, 1>(timeout_ptr)?[0];

        let mut mmsgs = read_mmsghdrs(&mem, mmsg_ptr, mmsg_count.try_into().unwrap())?;
        let mut num_msgs = 0;

        // TODO: the blocking behaviour isn't correct: "A blocking recvmmsg() call blocks until vlen
        // messages have been received or until the timeout expires"

        // run in a closure so that a `?` doesn't return from the syscall handler
        let result: Result<_, SyscallError> = (|| {
            for mmsg in &mut mmsgs {
                let mut flags = flags;

                if num_msgs > 0 && (flags & libc::MSG_WAITFORONE) != 0 {
                    // recvmmsg(2): "Turns on MSG_DONTWAIT after the first message has been
                    // received"
                    flags |= libc::MSG_DONTWAIT;
                }

                let args = RecvmsgArgs {
                    iovs: &mmsg.hdr.iovs,
                    control_ptr: TypedPluginPtr::new::<u8>(mmsg.hdr.control, mmsg.hdr.control_len),
                    flags,
                };

                // call the socket's sendmsg(), and run any resulting events
                let result = crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
                    CallbackQueue::queue_and_run(|cb_queue| {
                        Socket::recvmsg(socket, args, &mut mem, cb_queue)
                    })
                })?;

                // write the socket address to the plugin and update the length in mmsg
                if !mmsg.hdr.name.is_null() {
                    // TODO: If this returns an error, the plugin will lose the message. Is this
                    // what Linux does? We should probably add a test for this. (This applies to the
                    // `write_mmsghdrs` below as well, and to the other recvfrom() and recvmsg()
                    // syscall handlers.)
                    if let Some(from_addr) = result.addr.as_ref() {
                        mmsg.hdr.name_len =
                            write_sockaddr(&mut mem, from_addr, mmsg.hdr.name, mmsg.hdr.name_len)?;
                    } else {
                        mmsg.hdr.name_len = 0;
                    }
                }

                // update the control len and flags in mmsg
                mmsg.hdr.control_len = result.control_len;
                mmsg.hdr.flags = result.msg_flags;

                mmsg.len = result.bytes_read.try_into().unwrap();
                num_msgs += 1;

                // TODO: check timeout value (we need to know when the syscall was first called)
            }

            Ok(())
        })();

        assert!(num_msgs as u32 <= mmsg_count);
        write_mmsghdrs(&mut mem, mmsg_ptr, &mmsgs[..num_msgs.try_into().unwrap()])?;

        if let Err(mut err) = result {
            // recvmmsg(2): "If an error occurs after at least one message has been received, the
            // call succeeds, and returns the number of messages received. The error code is
            // expected to be returned on a subsequent call to recvmmsg()."
            if num_msgs == 0 {
                // if the syscall will block, keep the file open until the syscall restarts
                if let Some(cond) = err.blocked_condition() {
                    cond.set_active_file(file);
                }

                return Err(err);
            }
            // TODO: save the error so we can return it in a subsequent call to recvmmsg()
        }

        Ok(num_msgs)
    }

    /*
    pub fn recvmsg_helper(
        ctx: &mut SyscallContext,
        socket: &Socket,
        msg: MsgHdr,
    ) -> Result<(libc::ssize_t, MsgHdr), SyscallError> {
        let mmsg = MmsgHdr {
            hdr: msg,
            // this should get filled in by recvmmsg
            len: 0,
        };

        let (num_msgs, mut mmsgs) = Self::recvmmsg_helper(ctx, socket, Vec::from([mmsg]))?;
        let mmsg = mmsgs.pop().unwrap();

        // we only gave it one message, so it can't have read more
        assert!(num_msgs <= 1);

        // if it read 0 messages, then the length should still be 0
        assert!(num_msgs != 0 || mmsg.len == 0);

        return Ok((mmsg.len.try_into().unwrap(), mmsg.hdr));
    }

    pub fn recvmmsg_helper(
        ctx: &mut SyscallContext,
        socket: &Socket,
        mut mmsgs: Vec<MmsgHdr>,
    ) -> Result<(libc::c_int, Vec<MmsgHdr>), SyscallError> {
        let mut mem = ctx.objs.process.memory_borrow_mut();

        // call the socket's recvmmsg(), and run any resulting events
        let num_msgs = CallbackQueue::queue_and_run(|cb_queue| {
            Socket::recvmmsg(socket, &mut mmsgs, &mut mem, cb_queue)
        })?;

        Ok((num_msgs, mmsgs))
    }
    */

    /*
    #[log_syscall(/* rv */ libc::ssize_t, /* sockfd */ libc::c_int,
                  /* buf */ SyscallBufferArg</* len */ 2>, /* len */ libc::size_t,
                  /* flags */ nix::sys::socket::MsgFlags,
                  /* dest_addr */ SyscallSockAddrArg</* addrlen */ 5>,
                  /* addrlen */ libc::socklen_t)]
    pub fn sendto(
        ctx: &mut SyscallContext,
        fd: libc::c_int,
        buf_ptr: PluginPtr,
        buf_len: libc::size_t,
        flags: libc::c_int,
        addr_ptr: PluginPtr,
        addr_len: libc::socklen_t,
    ) -> SyscallResult {
        // if we were previously blocked, get the active file from the last syscall handler
        // invocation since it may no longer exist in the descriptor table
        let file = ctx
            .objs
            .thread
            .syscall_condition()
            // if this was for a C descriptor, then there won't be an active file object
            .and_then(|x| x.active_file().cloned());

        let file = match file {
            // we were previously blocked, so re-use the file from the previous syscall invocation
            Some(x) => x,
            // get the file from the descriptor table, or return early if it doesn't exist
            None => {
                let desc_table = ctx.objs.process.descriptor_table_borrow();
                match Self::get_descriptor(&desc_table, fd)?.file() {
                    CompatFile::New(file) => file.clone(),
                    // if it's a legacy file, use the C syscall handler instead
                    CompatFile::Legacy(_) => {
                        drop(desc_table);
                        return Self::legacy_syscall(c::syscallhandler_sendto, ctx);
                    }
                }
            }
        };

        if let File::Socket(Socket::Inet(InetSocket::LegacyTcp(_))) = file.inner_file() {
            return Self::legacy_syscall(c::syscallhandler_sendto, ctx);
        }

        Self::sendto_helper(ctx, file, buf_ptr, buf_len, flags, addr_ptr, addr_len)
    }

    pub fn sendto_helper(
        ctx: &mut SyscallContext,
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

        let addr = read_sockaddr(&ctx.objs.process.memory_borrow(), addr_ptr, addr_len)?;

        debug!("Attempting to send {} bytes to {:?}", buf_len, addr);

        let file_status = socket.borrow().get_status();

        // call the socket's sendto(), and run any resulting events
        let result = CallbackQueue::queue_and_run(|cb_queue| {
            socket.borrow_mut().sendto(
                ctx.objs
                    .process
                    .memory_borrow()
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
    */

    /*
    #[log_syscall(/* rv */ libc::ssize_t, /* sockfd */ libc::c_int, /* buf */ *const libc::c_void,
                  /* len */ libc::size_t, /* flags */ nix::sys::socket::MsgFlags,
                  /* src_addr */ *const libc::sockaddr, /* addrlen */ *const libc::socklen_t)]
    pub fn recvfrom(
        ctx: &mut SyscallContext,
        fd: libc::c_int,
        buf_ptr: PluginPtr,
        buf_len: libc::size_t,
        flags: libc::c_int,
        addr_ptr: PluginPtr,
        addr_len_ptr: PluginPtr,
    ) -> SyscallResult {
        // if we were previously blocked, get the active file from the last syscall handler
        // invocation since it may no longer exist in the descriptor table
        let file = ctx
            .objs
            .thread
            .syscall_condition()
            // if this was for a C descriptor, then there won't be an active file object
            .and_then(|x| x.active_file().cloned());

        let file = match file {
            // we were previously blocked, so re-use the file from the previous syscall invocation
            Some(x) => x,
            // get the file from the descriptor table, or return early if it doesn't exist
            None => {
                let desc_table = ctx.objs.process.descriptor_table_borrow();
                match Self::get_descriptor(&desc_table, fd)?.file() {
                    CompatFile::New(file) => file.clone(),
                    // if it's a legacy file, use the C syscall handler instead
                    CompatFile::Legacy(_) => {
                        drop(desc_table);
                        return Self::legacy_syscall(c::syscallhandler_recvfrom, ctx);
                    }
                }
            }
        };

        if let File::Socket(Socket::Inet(InetSocket::LegacyTcp(_))) = file.inner_file() {
            return Self::legacy_syscall(c::syscallhandler_recvfrom, ctx);
        }

        Self::recvfrom_helper(ctx, file, buf_ptr, buf_len, flags, addr_ptr, addr_len_ptr)
    }

    pub fn recvfrom_helper(
        ctx: &mut SyscallContext,
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
                ctx.objs
                    .process
                    .memory_borrow_mut()
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
            write_sockaddr_and_len(
                &mut ctx.objs.process.memory_borrow_mut(),
                from_addr.as_ref(),
                addr_ptr,
                TypedPluginPtr::new::<libc::socklen_t>(addr_len_ptr, 1),
            )?;
        }

        Ok(result)
    }
    */

    #[log_syscall(/* rv */ libc::c_int, /* sockfd */ libc::c_int, /* addr */ *const libc::sockaddr,
                  /* addrlen */ *const libc::socklen_t)]
    pub fn getsockname(
        ctx: &mut SyscallContext,
        fd: libc::c_int,
        addr_ptr: PluginPtr,
        addr_len_ptr: PluginPtr,
    ) -> SyscallResult {
        let addr_len_ptr = TypedPluginPtr::new::<libc::socklen_t>(addr_len_ptr, 1);

        let addr_to_write: Option<SockaddrStorage> = {
            // get the descriptor, or return early if it doesn't exist
            let desc_table = ctx.objs.process.descriptor_table_borrow();
            let desc = Self::get_descriptor(&desc_table, fd)?;

            let file = match desc.file() {
                CompatFile::New(file) => file,
                // if it's a legacy file, use the C syscall handler instead
                CompatFile::Legacy(_) => {
                    drop(desc_table);
                    return Self::legacy_syscall(c::syscallhandler_getsockname, ctx);
                }
            };

            let File::Socket(socket) = file.inner_file() else {
                return Err(Errno::ENOTSOCK.into());
            };

            // linux will return an EFAULT before other errors
            if addr_ptr.is_null() || addr_len_ptr.is_null() {
                return Err(Errno::EFAULT.into());
            }

            let socket = socket.borrow();
            socket.getsockname()?
        };

        debug!("Returning socket address of {:?}", addr_to_write);
        write_sockaddr_and_len(
            &mut ctx.objs.process.memory_borrow_mut(),
            addr_to_write.as_ref(),
            addr_ptr,
            addr_len_ptr,
        )?;

        Ok(0.into())
    }

    #[log_syscall(/* rv */ libc::c_int, /* sockfd */ libc::c_int, /* addr */ *const libc::sockaddr,
                  /* addrlen */ *const libc::socklen_t)]
    pub fn getpeername(
        ctx: &mut SyscallContext,
        fd: libc::c_int,
        addr_ptr: PluginPtr,
        addr_len_ptr: PluginPtr,
    ) -> SyscallResult {
        let addr_len_ptr = TypedPluginPtr::new::<libc::socklen_t>(addr_len_ptr, 1);

        let addr_to_write = {
            // get the descriptor, or return early if it doesn't exist
            let desc_table = ctx.objs.process.descriptor_table_borrow();
            let desc = Self::get_descriptor(&desc_table, fd)?;

            let file = match desc.file() {
                CompatFile::New(file) => file,
                // if it's a legacy file, use the C syscall handler instead
                CompatFile::Legacy(_) => {
                    drop(desc_table);
                    return Self::legacy_syscall(c::syscallhandler_getpeername, ctx);
                }
            };

            let File::Socket(socket) = file.inner_file() else {
                return Err(Errno::ENOTSOCK.into());
            };

            // linux will return an EFAULT before other errors like ENOTCONN
            if addr_ptr.is_null() || addr_len_ptr.is_null() {
                return Err(Errno::EFAULT.into());
            }

            // this is a clippy false-positive
            #[allow(clippy::let_and_return)]
            let addr_to_write = socket.borrow().getpeername()?;
            addr_to_write
        };

        debug!("Returning peer address of {:?}", addr_to_write);
        write_sockaddr_and_len(
            &mut ctx.objs.process.memory_borrow_mut(),
            addr_to_write.as_ref(),
            addr_ptr,
            addr_len_ptr,
        )?;

        Ok(0.into())
    }

    #[log_syscall(/* rv */ libc::c_int, /* sockfd */ libc::c_int, /* backlog */ libc::c_int)]
    pub fn listen(
        ctx: &mut SyscallContext,
        fd: libc::c_int,
        backlog: libc::c_int,
    ) -> SyscallResult {
        // get the descriptor, or return early if it doesn't exist
        let desc_table = ctx.objs.process.descriptor_table_borrow();
        let desc = Self::get_descriptor(&desc_table, fd)?;

        let file = match desc.file() {
            CompatFile::New(file) => file,
            // if it's a legacy file, use the C syscall handler instead
            CompatFile::Legacy(_) => {
                drop(desc_table);
                return Self::legacy_syscall(c::syscallhandler_listen, ctx);
            }
        };

        let File::Socket(socket) = file.inner_file() else {
            drop(desc_table);
            return Err(Errno::ENOTSOCK.into());
        };

        let mut rng = ctx.objs.host.random_mut();
        let net_ns = ctx.objs.host.network_namespace_borrow();

        crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
            CallbackQueue::queue_and_run(|cb_queue| {
                Socket::listen(socket, backlog, &net_ns, &mut *rng, cb_queue)
            })
        })?;

        Ok(0.into())
    }

    #[log_syscall(/* rv */ libc::c_int, /* sockfd */ libc::c_int, /* addr */ *const libc::sockaddr,
                  /* addrlen */ *const libc::socklen_t)]
    pub fn accept(
        ctx: &mut SyscallContext,
        fd: libc::c_int,
        addr_ptr: PluginPtr,
        addr_len_ptr: PluginPtr,
    ) -> SyscallResult {
        // if we were previously blocked, get the active file from the last syscall handler
        // invocation since it may no longer exist in the descriptor table
        let file = ctx
            .objs
            .thread
            .syscall_condition()
            // if this was for a C descriptor, then there won't be an active file object
            .and_then(|x| x.active_file().cloned());

        let file = match file {
            // we were previously blocked, so re-use the file from the previous syscall invocation
            Some(x) => x,
            // get the file from the descriptor table, or return early if it doesn't exist
            None => {
                let desc_table = ctx.objs.process.descriptor_table_borrow();
                match Self::get_descriptor(&desc_table, fd)?.file() {
                    CompatFile::New(file) => file.clone(),
                    // if it's a legacy file, use the C syscall handler instead
                    CompatFile::Legacy(_) => {
                        drop(desc_table);
                        return Self::legacy_syscall(c::syscallhandler_accept, ctx);
                    }
                }
            }
        };

        Self::accept_helper(ctx, file, addr_ptr, addr_len_ptr, 0)
    }

    #[log_syscall(/* rv */ libc::c_int, /* sockfd */ libc::c_int, /* addr */ *const libc::sockaddr,
                  /* addrlen */ *const libc::socklen_t, /* flags */ libc::c_int)]
    pub fn accept4(
        ctx: &mut SyscallContext,
        fd: libc::c_int,
        addr_ptr: PluginPtr,
        addr_len_ptr: PluginPtr,
        flags: libc::c_int,
    ) -> SyscallResult {
        // if we were previously blocked, get the active file from the last syscall handler
        // invocation since it may no longer exist in the descriptor table
        let file = ctx
            .objs
            .thread
            .syscall_condition()
            // if this was for a C descriptor, then there won't be an active file object
            .and_then(|x| x.active_file().cloned());

        let file = match file {
            // we were previously blocked, so re-use the file from the previous syscall invocation
            Some(x) => x,
            // get the file from the descriptor table, or return early if it doesn't exist
            None => {
                let desc_table = ctx.objs.process.descriptor_table_borrow();
                match Self::get_descriptor(&desc_table, fd)?.file() {
                    CompatFile::New(file) => file.clone(),
                    // if it's a legacy file, use the C syscall handler instead
                    CompatFile::Legacy(_) => {
                        drop(desc_table);
                        return Self::legacy_syscall(c::syscallhandler_accept4, ctx);
                    }
                }
            }
        };

        Self::accept_helper(ctx, file, addr_ptr, addr_len_ptr, flags)
    }

    fn accept_helper(
        ctx: &mut SyscallContext,
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

        let result = crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
            CallbackQueue::queue_and_run(|cb_queue| socket.borrow_mut().accept(cb_queue))
        });

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

        let new_socket = result?;

        let from_addr = {
            let File::Socket(new_socket) = new_socket.inner_file() else {
                panic!("Accepted file should be a socket");
            };
            new_socket.borrow().getpeername().unwrap()
        };

        if !addr_ptr.is_null() {
            write_sockaddr_and_len(
                &mut ctx.objs.process.memory_borrow_mut(),
                from_addr.as_ref(),
                addr_ptr,
                TypedPluginPtr::new::<libc::socklen_t>(addr_len_ptr, 1),
            )?;
        }

        if flags.contains(SockFlag::SOCK_NONBLOCK) {
            new_socket
                .inner_file()
                .borrow_mut()
                .set_status(FileStatus::NONBLOCK);
        }

        let mut new_desc = Descriptor::new(CompatFile::New(new_socket));

        if flags.contains(SockFlag::SOCK_CLOEXEC) {
            new_desc.set_flags(DescriptorFlags::CLOEXEC);
        }

        let new_fd = ctx
            .objs
            .process
            .descriptor_table_borrow_mut()
            .register_descriptor(new_desc)
            .or(Err(Errno::ENFILE))?;

        Ok(new_fd.val().into())
    }

    #[log_syscall(/* rv */ libc::c_int, /* sockfd */ libc::c_int,
                  /* addr */ SyscallSockAddrArg</* addrlen */ 2>, /* addrlen */ libc::socklen_t)]
    pub fn connect(
        ctx: &mut SyscallContext,
        fd: libc::c_int,
        addr_ptr: PluginPtr,
        addr_len: libc::socklen_t,
    ) -> SyscallResult {
        // if we were previously blocked, get the active file from the last syscall handler
        // invocation since it may no longer exist in the descriptor table
        let file = ctx
            .objs
            .thread
            .syscall_condition()
            // if this was for a C descriptor, then there won't be an active file object
            .and_then(|x| x.active_file().cloned());

        let file = match file {
            // we were previously blocked, so re-use the file from the previous syscall invocation
            Some(x) => x,
            // get the file from the descriptor table, or return early if it doesn't exist
            None => {
                let desc_table = ctx.objs.process.descriptor_table_borrow();
                match Self::get_descriptor(&desc_table, fd)?.file() {
                    CompatFile::New(file) => file.clone(),
                    // if it's a legacy file, use the C syscall handler instead
                    CompatFile::Legacy(_) => {
                        drop(desc_table);
                        return Self::legacy_syscall(c::syscallhandler_connect, ctx);
                    }
                }
            }
        };

        let File::Socket(socket) = file.inner_file() else {
            return Err(Errno::ENOTSOCK.into());
        };

        let addr = read_sockaddr(&ctx.objs.process.memory_borrow(), addr_ptr, addr_len)?
            .ok_or(Errno::EFAULT)?;

        let mut rng = ctx.objs.host.random_mut();
        let net_ns = ctx.objs.host.network_namespace_borrow();

        let mut rv = crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
            CallbackQueue::queue_and_run(|cb_queue| {
                Socket::connect(socket, &addr, &net_ns, &mut *rng, cb_queue)
            })
        });

        // if we will block
        if let Err(SyscallError::Blocked(ref mut blocked)) = rv {
            // make sure the file does not close before the blocking syscall completes
            blocked.condition.set_active_file(file);
        }

        rv?;

        Ok(0.into())
    }

    #[log_syscall(/* rv */ libc::c_int, /* sockfd */ libc::c_int, /* how */ libc::c_int)]
    pub fn shutdown(ctx: &mut SyscallContext, fd: libc::c_int, how: libc::c_int) -> SyscallResult {
        // get the descriptor, or return early if it doesn't exist
        let desc_table = ctx.objs.process.descriptor_table_borrow();
        let desc = Self::get_descriptor(&desc_table, fd)?;

        let file = match desc.file() {
            CompatFile::New(file) => file,
            // if it's a legacy file, use the C syscall handler instead
            CompatFile::Legacy(_) => {
                drop(desc_table);
                return Self::legacy_syscall(c::syscallhandler_shutdown, ctx);
            }
        };

        let how = match how {
            libc::SHUT_RD => Shutdown::Read,
            libc::SHUT_WR => Shutdown::Write,
            libc::SHUT_RDWR => Shutdown::Both,
            _ => return Err(Errno::EINVAL.into()),
        };

        let File::Socket(socket) = file.inner_file() else {
            drop(desc_table);
            return Err(Errno::ENOTSOCK.into());
        };

        crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
            CallbackQueue::queue_and_run(|cb_queue| socket.borrow_mut().shutdown(how, cb_queue))
        })?;

        Ok(0.into())
    }

    #[log_syscall(/* rv */ libc::c_int, /* domain */ nix::sys::socket::AddressFamily,
                  /* type */ libc::c_int, /* protocol */ libc::c_int, /* sv */ [libc::c_int; 2])]
    pub fn socketpair(
        ctx: &mut SyscallContext,
        domain: libc::c_int,
        socket_type: libc::c_int,
        protocol: libc::c_int,
        fd_ptr: PluginPtr,
    ) -> SyscallResult {
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
                &ctx.objs.host.abstract_unix_namespace(),
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
        let mut dt = ctx.objs.process.descriptor_table_borrow_mut();
        // unwrap here since the error handling would be messy (need to deregister) and we shouldn't
        // ever need to worry about this in practice
        let fd_1 = dt.register_descriptor(desc_1).unwrap();
        let fd_2 = dt.register_descriptor(desc_2).unwrap();

        // try to write them to the caller
        let fds = [i32::from(fd_1), i32::from(fd_2)];
        let write_res = ctx
            .objs
            .process
            .memory_borrow_mut()
            .copy_to_ptr(TypedPluginPtr::new::<libc::c_int>(fd_ptr, 2), &fds);

        // clean up in case of error
        match write_res {
            Ok(_) => Ok(0.into()),
            Err(e) => {
                CallbackQueue::queue_and_run(|cb_queue| {
                    // ignore any errors when closing
                    dt.deregister_descriptor(fd_1)
                        .unwrap()
                        .close(ctx.objs.host, cb_queue);
                    dt.deregister_descriptor(fd_2)
                        .unwrap()
                        .close(ctx.objs.host, cb_queue);
                });
                Err(e.into())
            }
        }
    }

    #[log_syscall(/* rv */ libc::c_int, /* sockfd */ libc::c_int, /* level */ libc::c_int,
                  /* optname */ libc::c_int, /* optval */ *const libc::c_void,
                  /* optlen */ *const libc::socklen_t)]
    pub fn getsockopt(
        ctx: &mut SyscallContext,
        fd: libc::c_int,
        level: libc::c_int,
        optname: libc::c_int,
        optval_ptr: PluginPtr,
        optlen_ptr: PluginPtr,
    ) -> SyscallResult {
        // get the descriptor, or return early if it doesn't exist
        let desc_table = ctx.objs.process.descriptor_table_borrow();
        let desc = Self::get_descriptor(&desc_table, fd)?;

        let file = match desc.file() {
            CompatFile::New(file) => file,
            // if it's a legacy file, use the C syscall handler instead
            CompatFile::Legacy(_) => {
                drop(desc_table);
                return Self::legacy_syscall(c::syscallhandler_getsockopt, ctx);
            }
        };

        let File::Socket(socket) = file.inner_file() else {
            return Err(Errno::ENOTSOCK.into());
        };

        let mut mem = ctx.objs.process.memory_borrow_mut();

        // get the provided optlen
        let optlen_ptr = TypedPluginPtr::new::<libc::socklen_t>(optlen_ptr, 1);
        let optlen = mem.read_vals::<_, 1>(optlen_ptr)?[0];

        let mut optlen_new = socket
            .borrow()
            .getsockopt(level, optname, optval_ptr, optlen, &mut mem)?;

        if optlen_new > optlen {
            // this is probably a bug in the socket's getsockopt implementation
            log::warn!(
                "Attempting to return an optlen {} that's greater than the provided optlen {}",
                optlen_new,
                optlen
            );
            optlen_new = optlen;
        }

        // write the new optlen back to the plugin
        mem.copy_to_ptr(optlen_ptr, &[optlen_new])?;

        Ok(0.into())
    }

    #[log_syscall(/* rv */ libc::c_int, /* sockfd */ libc::c_int, /* level */ libc::c_int,
                  /* optname */ libc::c_int, /* optval */ *const libc::c_void,
                  /* optlen */ libc::socklen_t)]
    pub fn setsockopt(
        ctx: &mut SyscallContext,
        fd: libc::c_int,
        level: libc::c_int,
        optname: libc::c_int,
        optval_ptr: PluginPtr,
        optlen: libc::socklen_t,
    ) -> SyscallResult {
        // get the descriptor, or return early if it doesn't exist
        let desc_table = ctx.objs.process.descriptor_table_borrow();
        let desc = Self::get_descriptor(&desc_table, fd)?;

        let file = match desc.file() {
            CompatFile::New(file) => file,
            // if it's a legacy file, use the C syscall handler instead
            CompatFile::Legacy(_) => {
                drop(desc_table);
                return Self::legacy_syscall(c::syscallhandler_setsockopt, ctx);
            }
        };

        let File::Socket(socket) = file.inner_file() else {
            drop(desc_table);
            return Err(Errno::ENOTSOCK.into());
        };

        let mem = ctx.objs.process.memory_borrow();

        socket
            .borrow_mut()
            .setsockopt(level, optname, optval_ptr, optlen, &mem)?;

        Ok(0.into())
    }
}
