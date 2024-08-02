use linux_api::errno::Errno;
use linux_api::fcntl::DescriptorFlags;
use linux_api::socket::Shutdown;
use log::*;
use nix::sys::socket::SockFlag;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::host::descriptor::descriptor_table::DescriptorHandle;
use crate::host::descriptor::socket::inet::legacy_tcp::LegacyTcpSocket;
use crate::host::descriptor::socket::inet::tcp::TcpSocket;
use crate::host::descriptor::socket::inet::udp::UdpSocket;
use crate::host::descriptor::socket::inet::InetSocket;
use crate::host::descriptor::socket::netlink::{NetlinkFamily, NetlinkSocket, NetlinkSocketType};
use crate::host::descriptor::socket::unix::{UnixSocket, UnixSocketType};
use crate::host::descriptor::socket::{RecvmsgArgs, RecvmsgReturn, SendmsgArgs, Socket};
use crate::host::descriptor::{CompatFile, Descriptor, File, FileState, FileStatus, OpenFile};
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::io::{self, IoVec};
use crate::host::syscall::type_formatting::{SyscallBufferArg, SyscallSockAddrArg};
use crate::host::syscall::types::ForeignArrayPtr;
use crate::host::syscall::types::SyscallError;
use crate::utility::callback_queue::CallbackQueue;
use crate::utility::sockaddr::SockaddrStorage;

impl SyscallHandler {
    log_syscall!(
        socket,
        /* rv */ std::ffi::c_int,
        /* domain */ linux_api::socket::AddressFamily,
        /* type */ std::ffi::c_int,
        /* protocol */ std::ffi::c_int,
    );
    pub fn socket(
        ctx: &mut SyscallContext,
        domain: std::ffi::c_int,
        socket_type: std::ffi::c_int,
        protocol: std::ffi::c_int,
    ) -> Result<DescriptorHandle, Errno> {
        // remove any flags from the socket type
        let flags = socket_type & (libc::SOCK_NONBLOCK | libc::SOCK_CLOEXEC);
        let socket_type = socket_type & !flags;

        let mut file_flags = FileStatus::empty();
        let mut descriptor_flags = DescriptorFlags::empty();

        if flags & libc::SOCK_NONBLOCK != 0 {
            file_flags.insert(FileStatus::NONBLOCK);
        }

        if flags & libc::SOCK_CLOEXEC != 0 {
            descriptor_flags.insert(DescriptorFlags::FD_CLOEXEC);
        }

        let socket = match domain {
            libc::AF_UNIX => {
                let socket_type = match UnixSocketType::try_from(socket_type) {
                    Ok(x) => x,
                    Err(e) => {
                        warn!("{}", e);
                        return Err(Errno::EPROTONOSUPPORT);
                    }
                };

                // unix sockets don't support any protocols
                if protocol != 0 {
                    warn!(
                        "Unsupported socket protocol {}, we only support default protocol 0",
                        protocol
                    );
                    return Err(Errno::EPROTONOSUPPORT);
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
                        log::debug!("Unsupported inet stream socket protocol {protocol}");
                        return Err(Errno::EPROTONOSUPPORT);
                    }

                    if ctx.objs.host.params.use_new_tcp {
                        Socket::Inet(InetSocket::Tcp(TcpSocket::new(file_flags)))
                    } else {
                        Socket::Inet(InetSocket::LegacyTcp(LegacyTcpSocket::new(
                            file_flags,
                            ctx.objs.host,
                        )))
                    }
                }
                libc::SOCK_DGRAM => {
                    if protocol != 0 && protocol != libc::IPPROTO_UDP {
                        log::debug!("Unsupported inet dgram socket protocol {protocol}");
                        return Err(Errno::EPROTONOSUPPORT);
                    }
                    let send_buf_size = ctx.objs.host.params.init_sock_send_buf_size;
                    let recv_buf_size = ctx.objs.host.params.init_sock_recv_buf_size;
                    Socket::Inet(InetSocket::Udp(UdpSocket::new(
                        file_flags,
                        send_buf_size.try_into().unwrap(),
                        recv_buf_size.try_into().unwrap(),
                    )))
                }
                _ => return Err(Errno::ESOCKTNOSUPPORT),
            },
            libc::AF_NETLINK => {
                let socket_type = match NetlinkSocketType::try_from(socket_type) {
                    Ok(x) => x,
                    Err(e) => {
                        warn!("{}", e);
                        return Err(Errno::EPROTONOSUPPORT);
                    }
                };
                let family = match NetlinkFamily::try_from(protocol) {
                    Ok(x) => x,
                    Err(e) => {
                        warn!("{}", e);
                        return Err(Errno::EPROTONOSUPPORT);
                    }
                };
                Socket::Netlink(NetlinkSocket::new(file_flags, socket_type, family))
            }
            _ => return Err(Errno::EAFNOSUPPORT),
        };

        let mut desc = Descriptor::new(CompatFile::New(OpenFile::new(File::Socket(socket))));
        desc.set_flags(descriptor_flags);

        let fd = ctx
            .objs
            .thread
            .descriptor_table_borrow_mut(ctx.objs.host)
            .register_descriptor(desc)
            .or(Err(Errno::ENFILE))?;

        log::trace!("Created socket fd {fd}");

        Ok(fd)
    }

    log_syscall!(
        bind,
        /* rv */ std::ffi::c_int,
        /* sockfd */ std::ffi::c_int,
        /* addr */ SyscallSockAddrArg</* addrlen */ 2>,
        /* addrlen */ libc::socklen_t,
    );
    pub fn bind(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
        addr_ptr: ForeignPtr<u8>,
        addr_len: libc::socklen_t,
    ) -> Result<(), SyscallError> {
        let file = {
            // get the descriptor, or return early if it doesn't exist
            let desc_table = ctx.objs.thread.descriptor_table_borrow(ctx.objs.host);
            let desc = Self::get_descriptor(&desc_table, fd)?;

            let CompatFile::New(file) = desc.file() else {
                // we don't have any C socket objects
                return Err(Errno::ENOTSOCK.into());
            };

            file.inner_file().clone()
        };

        let File::Socket(ref socket) = file else {
            return Err(Errno::ENOTSOCK.into());
        };

        let addr = io::read_sockaddr(&ctx.objs.process.memory_borrow(), addr_ptr, addr_len)?;

        log::trace!("Attempting to bind fd {} to {:?}", fd, addr);

        let mut rng = ctx.objs.host.random_mut();
        let net_ns = ctx.objs.host.network_namespace_borrow();
        Socket::bind(socket, addr.as_ref(), &net_ns, &mut *rng)
    }

    log_syscall!(
        sendto,
        /* rv */ libc::ssize_t,
        /* sockfd */ std::ffi::c_int,
        /* buf */ SyscallBufferArg</* len */ 2>,
        /* len */ libc::size_t,
        /* flags */ nix::sys::socket::MsgFlags,
        /* dest_addr */ SyscallSockAddrArg</* addrlen */ 5>,
        /* addrlen */ libc::socklen_t,
    );
    pub fn sendto(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
        buf_ptr: ForeignPtr<u8>,
        buf_len: libc::size_t,
        flags: std::ffi::c_int,
        addr_ptr: ForeignPtr<u8>,
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
                let desc_table = ctx.objs.thread.descriptor_table_borrow(ctx.objs.host);
                let CompatFile::New(file) = Self::get_descriptor(&desc_table, fd)?.file() else {
                    // we don't have any C socket objects
                    return Err(Errno::ENOTSOCK.into());
                };
                file.clone()
            }
        };

        let File::Socket(ref socket) = file.inner_file() else {
            return Err(Errno::ENOTSOCK.into());
        };

        let mut mem = ctx.objs.process.memory_borrow_mut();
        let mut rng = ctx.objs.host.random_mut();
        let net_ns = ctx.objs.host.network_namespace_borrow();

        let addr = io::read_sockaddr(&mem, addr_ptr, addr_len)?;

        log::trace!("Attempting to send {} bytes to {:?}", buf_len, addr);

        let iov = IoVec {
            base: buf_ptr,
            len: buf_len,
        };

        let args = SendmsgArgs {
            addr,
            iovs: &[iov],
            control_ptr: ForeignArrayPtr::new(ForeignPtr::null(), 0),
            flags,
        };

        // call the socket's sendmsg(), and run any resulting events
        let mut result = crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
            CallbackQueue::queue_and_run(|cb_queue| {
                Socket::sendmsg(socket, args, &mut mem, &net_ns, &mut *rng, cb_queue)
            })
        });

        // if the syscall will block, keep the file open until the syscall restarts
        if let Some(err) = result.as_mut().err() {
            if let Some(cond) = err.blocked_condition() {
                cond.set_active_file(file);
            }
        }

        let bytes_sent = result?;
        Ok(bytes_sent)
    }

    log_syscall!(
        sendmsg,
        /* rv */ libc::ssize_t,
        /* sockfd */ std::ffi::c_int,
        /* msg */ *const libc::msghdr,
        /* flags */ nix::sys::socket::MsgFlags,
    );
    pub fn sendmsg(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
        msg_ptr: ForeignPtr<libc::msghdr>,
        flags: std::ffi::c_int,
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
                let desc_table = ctx.objs.thread.descriptor_table_borrow(ctx.objs.host);
                match Self::get_descriptor(&desc_table, fd)?.file() {
                    CompatFile::New(file) => file.clone(),
                    CompatFile::Legacy(_file) => {
                        return Err(Errno::ENOTSOCK.into());
                    }
                }
            }
        };

        let File::Socket(ref socket) = file.inner_file() else {
            return Err(Errno::ENOTSOCK.into());
        };

        let mut mem = ctx.objs.process.memory_borrow_mut();
        let mut rng = ctx.objs.host.random_mut();
        let net_ns = ctx.objs.host.network_namespace_borrow();

        let msg = io::read_msghdr(&mem, msg_ptr)?;

        let args = SendmsgArgs {
            addr: io::read_sockaddr(&mem, msg.name, msg.name_len)?,
            iovs: &msg.iovs,
            control_ptr: ForeignArrayPtr::new(msg.control, msg.control_len),
            // note: "the msg_flags field is ignored" for sendmsg; see send(2)
            flags,
        };

        // call the socket's sendmsg(), and run any resulting events
        let mut result = crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
            CallbackQueue::queue_and_run(|cb_queue| {
                Socket::sendmsg(socket, args, &mut mem, &net_ns, &mut *rng, cb_queue)
            })
        });

        // if the syscall will block, keep the file open until the syscall restarts
        if let Some(err) = result.as_mut().err() {
            if let Some(cond) = err.blocked_condition() {
                cond.set_active_file(file);
            }
        }

        let bytes_written = result?;
        Ok(bytes_written)
    }

    log_syscall!(
        recvfrom,
        /* rv */ libc::ssize_t,
        /* sockfd */ std::ffi::c_int,
        /* buf */ *const std::ffi::c_void,
        /* len */ libc::size_t,
        /* flags */ nix::sys::socket::MsgFlags,
        /* src_addr */ *const libc::sockaddr,
        /* addrlen */ *const libc::socklen_t,
    );
    pub fn recvfrom(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
        buf_ptr: ForeignPtr<u8>,
        buf_len: libc::size_t,
        flags: std::ffi::c_int,
        addr_ptr: ForeignPtr<u8>,
        addr_len_ptr: ForeignPtr<libc::socklen_t>,
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
                let desc_table = ctx.objs.thread.descriptor_table_borrow(ctx.objs.host);
                let CompatFile::New(file) = Self::get_descriptor(&desc_table, fd)?.file() else {
                    // we don't have any C socket objects
                    return Err(Errno::ENOTSOCK.into());
                };
                file.clone()
            }
        };

        let File::Socket(ref socket) = file.inner_file() else {
            return Err(Errno::ENOTSOCK.into());
        };

        let mut mem = ctx.objs.process.memory_borrow_mut();

        log::trace!("Attempting to recv {} bytes", buf_len);

        let iov = IoVec {
            base: buf_ptr,
            len: buf_len,
        };

        let args = RecvmsgArgs {
            iovs: &[iov],
            control_ptr: ForeignArrayPtr::new(ForeignPtr::null(), 0),
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
            return_val,
            addr: from_addr,
            ..
        } = result?;

        if !addr_ptr.is_null() {
            io::write_sockaddr_and_len(&mut mem, from_addr.as_ref(), addr_ptr, addr_len_ptr)?;
        }

        Ok(return_val)
    }

    log_syscall!(
        recvmsg,
        /* rv */ libc::ssize_t,
        /* sockfd */ std::ffi::c_int,
        /* msg */ *const libc::msghdr,
        /* flags */ nix::sys::socket::MsgFlags,
    );
    pub fn recvmsg(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
        msg_ptr: ForeignPtr<libc::msghdr>,
        flags: std::ffi::c_int,
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
                let desc_table = ctx.objs.thread.descriptor_table_borrow(ctx.objs.host);
                match Self::get_descriptor(&desc_table, fd)?.file() {
                    CompatFile::New(file) => file.clone(),
                    CompatFile::Legacy(_file) => {
                        return Err(Errno::ENOTSOCK.into());
                    }
                }
            }
        };

        let File::Socket(ref socket) = file.inner_file() else {
            return Err(Errno::ENOTSOCK.into());
        };

        let mut mem = ctx.objs.process.memory_borrow_mut();

        let mut msg = io::read_msghdr(&mem, msg_ptr)?;

        let args = RecvmsgArgs {
            iovs: &msg.iovs,
            control_ptr: ForeignArrayPtr::new(msg.control, msg.control_len),
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
                msg.name_len = io::write_sockaddr(&mut mem, from_addr, msg.name, msg.name_len)?;
            } else {
                msg.name_len = 0;
            }
        }

        // update the control len and flags in msg
        msg.control_len = result.control_len;
        msg.flags = result.msg_flags;

        // write msg back to the plugin
        io::update_msghdr(&mut mem, msg_ptr, msg)?;

        Ok(result.return_val)
    }

    log_syscall!(
        getsockname,
        /* rv */ std::ffi::c_int,
        /* sockfd */ std::ffi::c_int,
        /* addr */ *const libc::sockaddr,
        /* addrlen */ *const libc::socklen_t,
    );
    pub fn getsockname(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
        addr_ptr: ForeignPtr<u8>,
        addr_len_ptr: ForeignPtr<libc::socklen_t>,
    ) -> Result<(), Errno> {
        let addr_to_write: Option<SockaddrStorage> = {
            // get the descriptor, or return early if it doesn't exist
            let desc_table = ctx.objs.thread.descriptor_table_borrow(ctx.objs.host);
            let desc = Self::get_descriptor(&desc_table, fd)?;

            let CompatFile::New(file) = desc.file() else {
                // we don't have any C socket objects
                return Err(Errno::ENOTSOCK);
            };

            let File::Socket(socket) = file.inner_file() else {
                return Err(Errno::ENOTSOCK);
            };

            // linux will return an EFAULT before other errors
            if addr_ptr.is_null() || addr_len_ptr.is_null() {
                return Err(Errno::EFAULT);
            }

            let socket = socket.borrow();
            socket.getsockname()?
        };

        debug!("Returning socket address of {:?}", addr_to_write);
        io::write_sockaddr_and_len(
            &mut ctx.objs.process.memory_borrow_mut(),
            addr_to_write.as_ref(),
            addr_ptr,
            addr_len_ptr,
        )?;

        Ok(())
    }

    log_syscall!(
        getpeername,
        /* rv */ std::ffi::c_int,
        /* sockfd */ std::ffi::c_int,
        /* addr */ *const libc::sockaddr,
        /* addrlen */ *const libc::socklen_t,
    );
    pub fn getpeername(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
        addr_ptr: ForeignPtr<u8>,
        addr_len_ptr: ForeignPtr<libc::socklen_t>,
    ) -> Result<(), Errno> {
        let addr_to_write = {
            // get the descriptor, or return early if it doesn't exist
            let desc_table = ctx.objs.thread.descriptor_table_borrow(ctx.objs.host);
            let desc = Self::get_descriptor(&desc_table, fd)?;

            let CompatFile::New(file) = desc.file() else {
                // we don't have any C socket objects
                return Err(Errno::ENOTSOCK);
            };

            let File::Socket(socket) = file.inner_file() else {
                return Err(Errno::ENOTSOCK);
            };

            // linux will return an EFAULT before other errors like ENOTCONN
            if addr_ptr.is_null() || addr_len_ptr.is_null() {
                return Err(Errno::EFAULT);
            }

            // this is a clippy false-positive
            #[allow(clippy::let_and_return)]
            let addr_to_write = socket.borrow().getpeername()?;
            addr_to_write
        };

        debug!("Returning peer address of {:?}", addr_to_write);
        io::write_sockaddr_and_len(
            &mut ctx.objs.process.memory_borrow_mut(),
            addr_to_write.as_ref(),
            addr_ptr,
            addr_len_ptr,
        )?;

        Ok(())
    }

    log_syscall!(
        listen,
        /* rv */ std::ffi::c_int,
        /* sockfd */ std::ffi::c_int,
        /* backlog */ std::ffi::c_int,
    );
    pub fn listen(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
        backlog: std::ffi::c_int,
    ) -> Result<(), Errno> {
        // get the descriptor, or return early if it doesn't exist
        let desc_table = ctx.objs.thread.descriptor_table_borrow(ctx.objs.host);
        let desc = Self::get_descriptor(&desc_table, fd)?;

        let CompatFile::New(file) = desc.file() else {
            // we don't have any C socket objects
            return Err(Errno::ENOTSOCK);
        };

        let File::Socket(socket) = file.inner_file() else {
            return Err(Errno::ENOTSOCK);
        };

        let mut rng = ctx.objs.host.random_mut();
        let net_ns = ctx.objs.host.network_namespace_borrow();

        crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
            CallbackQueue::queue_and_run(|cb_queue| {
                Socket::listen(socket, backlog, &net_ns, &mut *rng, cb_queue)
            })
        })?;

        Ok(())
    }

    log_syscall!(
        accept,
        /* rv */ std::ffi::c_int,
        /* sockfd */ std::ffi::c_int,
        /* addr */ *const libc::sockaddr,
        /* addrlen */ *const libc::socklen_t,
    );
    pub fn accept(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
        addr_ptr: ForeignPtr<u8>,
        addr_len_ptr: ForeignPtr<libc::socklen_t>,
    ) -> Result<DescriptorHandle, SyscallError> {
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
                let desc_table = ctx.objs.thread.descriptor_table_borrow(ctx.objs.host);
                let CompatFile::New(file) = Self::get_descriptor(&desc_table, fd)?.file() else {
                    // we don't have any C socket objects
                    return Err(Errno::ENOTSOCK.into());
                };
                file.clone()
            }
        };

        let mut result = Self::accept_helper(ctx, file.inner_file(), addr_ptr, addr_len_ptr, 0);

        // if the syscall will block, keep the file open until the syscall restarts
        if let Some(err) = result.as_mut().err() {
            if let Some(cond) = err.blocked_condition() {
                cond.set_active_file(file);
            }
        }

        result
    }

    log_syscall!(
        accept4,
        /* rv */ std::ffi::c_int,
        /* sockfd */ std::ffi::c_int,
        /* addr */ *const libc::sockaddr,
        /* addrlen */ *const libc::socklen_t,
        /* flags */ std::ffi::c_int,
    );
    pub fn accept4(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
        addr_ptr: ForeignPtr<u8>,
        addr_len_ptr: ForeignPtr<libc::socklen_t>,
        flags: std::ffi::c_int,
    ) -> Result<DescriptorHandle, SyscallError> {
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
                let desc_table = ctx.objs.thread.descriptor_table_borrow(ctx.objs.host);
                let CompatFile::New(file) = Self::get_descriptor(&desc_table, fd)?.file() else {
                    // we don't have any C socket objects
                    return Err(Errno::ENOTSOCK.into());
                };
                file.clone()
            }
        };

        let mut result = Self::accept_helper(ctx, file.inner_file(), addr_ptr, addr_len_ptr, flags);

        // if the syscall will block, keep the file open until the syscall restarts
        if let Some(err) = result.as_mut().err() {
            if let Some(cond) = err.blocked_condition() {
                cond.set_active_file(file);
            }
        }

        result
    }

    fn accept_helper(
        ctx: &mut SyscallContext,
        file: &File,
        addr_ptr: ForeignPtr<u8>,
        addr_len_ptr: ForeignPtr<libc::socklen_t>,
        flags: std::ffi::c_int,
    ) -> Result<DescriptorHandle, SyscallError> {
        let File::Socket(ref socket) = file else {
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

        let mut rng = ctx.objs.host.random_mut();
        let net_ns = ctx.objs.host.network_namespace_borrow();

        let result = crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
            CallbackQueue::queue_and_run(|cb_queue| {
                socket.borrow_mut().accept(&net_ns, &mut *rng, cb_queue)
            })
        });

        let file_status = socket.borrow().status();

        // if the syscall would block and it's a blocking descriptor
        if result.as_ref().err() == Some(&Errno::EWOULDBLOCK.into())
            && !file_status.contains(FileStatus::NONBLOCK)
        {
            return Err(SyscallError::new_blocked_on_file(
                file.clone(),
                FileState::READABLE,
                socket.borrow().supports_sa_restart(),
            ));
        }

        let new_socket = result?;

        let from_addr = {
            let File::Socket(new_socket) = new_socket.inner_file() else {
                panic!("Accepted file should be a socket");
            };
            new_socket.borrow().getpeername().unwrap()
        };

        if !addr_ptr.is_null() {
            io::write_sockaddr_and_len(
                &mut ctx.objs.process.memory_borrow_mut(),
                from_addr.as_ref(),
                addr_ptr,
                addr_len_ptr,
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
            new_desc.set_flags(DescriptorFlags::FD_CLOEXEC);
        }

        Ok(ctx
            .objs
            .thread
            .descriptor_table_borrow_mut(ctx.objs.host)
            .register_descriptor(new_desc)
            .or(Err(Errno::ENFILE))?)
    }

    log_syscall!(
        connect,
        /* rv */ std::ffi::c_int,
        /* sockfd */ std::ffi::c_int,
        /* addr */ SyscallSockAddrArg</* addrlen */ 2>,
        /* addrlen */ libc::socklen_t,
    );
    pub fn connect(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
        addr_ptr: ForeignPtr<u8>,
        addr_len: libc::socklen_t,
    ) -> Result<(), SyscallError> {
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
                let desc_table = ctx.objs.thread.descriptor_table_borrow(ctx.objs.host);
                let CompatFile::New(file) = Self::get_descriptor(&desc_table, fd)?.file() else {
                    // we don't have any C socket objects
                    return Err(Errno::ENOTSOCK.into());
                };
                file.clone()
            }
        };

        let File::Socket(socket) = file.inner_file() else {
            return Err(Errno::ENOTSOCK.into());
        };

        let addr = io::read_sockaddr(&ctx.objs.process.memory_borrow(), addr_ptr, addr_len)?
            .ok_or(Errno::EFAULT)?;

        let mut rng = ctx.objs.host.random_mut();
        let net_ns = ctx.objs.host.network_namespace_borrow();

        let mut result = crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
            CallbackQueue::queue_and_run(|cb_queue| {
                Socket::connect(socket, &addr, &net_ns, &mut *rng, cb_queue)
            })
        });

        // if the syscall will block, keep the file open until the syscall restarts
        if let Some(err) = result.as_mut().err() {
            if let Some(cond) = err.blocked_condition() {
                cond.set_active_file(file);
            }
        }

        result?;

        Ok(())
    }

    log_syscall!(
        shutdown,
        /* rv */ std::ffi::c_int,
        /* sockfd */ std::ffi::c_int,
        /* how */ std::ffi::c_uint,
    );
    pub fn shutdown(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
        how: std::ffi::c_uint,
    ) -> Result<(), SyscallError> {
        // get the descriptor, or return early if it doesn't exist
        let desc_table = ctx.objs.thread.descriptor_table_borrow(ctx.objs.host);
        let desc = Self::get_descriptor(&desc_table, fd)?;

        let CompatFile::New(file) = desc.file() else {
            // we don't have any C socket objects
            return Err(Errno::ENOTSOCK.into());
        };

        let how = Shutdown::try_from(how).or(Err(Errno::EINVAL))?;

        let File::Socket(socket) = file.inner_file() else {
            return Err(Errno::ENOTSOCK.into());
        };

        crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
            CallbackQueue::queue_and_run(|cb_queue| socket.borrow_mut().shutdown(how, cb_queue))
        })?;

        Ok(())
    }

    log_syscall!(
        socketpair,
        /* rv */ std::ffi::c_int,
        /* domain */ linux_api::socket::AddressFamily,
        /* type */ std::ffi::c_int,
        /* protocol */ std::ffi::c_int,
        /* sv */ [std::ffi::c_int; 2],
    );
    pub fn socketpair(
        ctx: &mut SyscallContext,
        domain: std::ffi::c_int,
        socket_type: std::ffi::c_int,
        protocol: std::ffi::c_int,
        fd_ptr: ForeignPtr<[std::ffi::c_int; 2]>,
    ) -> Result<(), SyscallError> {
        // remove any flags from the socket type
        let flags = socket_type & (libc::SOCK_NONBLOCK | libc::SOCK_CLOEXEC);
        let socket_type = socket_type & !flags;

        // only AF_UNIX (AF_LOCAL) is supported on Linux (and technically AF_TIPC)
        if domain != libc::AF_UNIX {
            warn!("Domain {domain} is not supported for socketpair()");
            return Err(Errno::EOPNOTSUPP.into());
        }

        let socket_type = match UnixSocketType::try_from(socket_type) {
            Ok(x) => x,
            Err(e) => {
                warn!("Not a unix socket type: {e}");
                return Err(Errno::EPROTONOSUPPORT.into());
            }
        };

        // unix sockets don't support any protocols
        if protocol != 0 {
            warn!("Unsupported socket protocol {protocol}, we only support default protocol 0");
            return Err(Errno::EPROTONOSUPPORT.into());
        }

        let mut file_flags = FileStatus::empty();
        let mut descriptor_flags = DescriptorFlags::empty();

        if flags & libc::SOCK_NONBLOCK != 0 {
            file_flags.insert(FileStatus::NONBLOCK);
        }

        if flags & libc::SOCK_CLOEXEC != 0 {
            descriptor_flags.insert(DescriptorFlags::FD_CLOEXEC);
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
        let mut dt = ctx.objs.thread.descriptor_table_borrow_mut(ctx.objs.host);
        // unwrap here since the error handling would be messy (need to deregister) and we shouldn't
        // ever need to worry about this in practice
        let fd_1 = dt.register_descriptor(desc_1).unwrap();
        let fd_2 = dt.register_descriptor(desc_2).unwrap();

        // try to write them to the caller
        let fds = [i32::from(fd_1), i32::from(fd_2)];
        let write_res = ctx.objs.process.memory_borrow_mut().write(fd_ptr, &fds);

        // clean up in case of error
        match write_res {
            Ok(_) => Ok(()),
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

    log_syscall!(
        getsockopt,
        /* rv */ std::ffi::c_int,
        /* sockfd */ std::ffi::c_int,
        /* level */ std::ffi::c_int,
        /* optname */ std::ffi::c_int,
        /* optval */ *const std::ffi::c_void,
        /* optlen */ *const libc::socklen_t,
    );
    pub fn getsockopt(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
        level: std::ffi::c_int,
        optname: std::ffi::c_int,
        optval_ptr: ForeignPtr<()>,
        optlen_ptr: ForeignPtr<libc::socklen_t>,
    ) -> Result<(), SyscallError> {
        // get the descriptor, or return early if it doesn't exist
        let desc_table = ctx.objs.thread.descriptor_table_borrow(ctx.objs.host);
        let desc = Self::get_descriptor(&desc_table, fd)?;

        let CompatFile::New(file) = desc.file() else {
            // we don't have any C socket objects
            return Err(Errno::ENOTSOCK.into());
        };

        let File::Socket(socket) = file.inner_file() else {
            return Err(Errno::ENOTSOCK.into());
        };

        let mut mem = ctx.objs.process.memory_borrow_mut();

        // get the provided optlen
        let optlen = mem.read(optlen_ptr)?;

        let mut optlen_new = crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
            CallbackQueue::queue_and_run(|cb_queue| {
                socket
                    .borrow_mut()
                    .getsockopt(level, optname, optval_ptr, optlen, &mut mem, cb_queue)
            })
        })?;

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
        mem.write(optlen_ptr, &optlen_new)?;

        Ok(())
    }

    log_syscall!(
        setsockopt,
        /* rv */ std::ffi::c_int,
        /* sockfd */ std::ffi::c_int,
        /* level */ std::ffi::c_int,
        /* optname */ std::ffi::c_int,
        /* optval */ *const std::ffi::c_void,
        /* optlen */ libc::socklen_t,
    );
    pub fn setsockopt(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
        level: std::ffi::c_int,
        optname: std::ffi::c_int,
        optval_ptr: ForeignPtr<()>,
        optlen: libc::socklen_t,
    ) -> Result<(), SyscallError> {
        // get the descriptor, or return early if it doesn't exist
        let desc_table = ctx.objs.thread.descriptor_table_borrow(ctx.objs.host);
        let desc = Self::get_descriptor(&desc_table, fd)?;

        let CompatFile::New(file) = desc.file() else {
            // we don't have any C socket objects
            return Err(Errno::ENOTSOCK.into());
        };

        let File::Socket(socket) = file.inner_file() else {
            return Err(Errno::ENOTSOCK.into());
        };

        let mem = ctx.objs.process.memory_borrow();

        socket
            .borrow_mut()
            .setsockopt(level, optname, optval_ptr, optlen, &mem)?;

        Ok(())
    }
}
