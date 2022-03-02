use crate::cshadow as c;
use crate::host::context::ThreadContext;
use crate::host::descriptor::socket::unix::{UnixSocketFile, UnixSocketType};
use crate::host::descriptor::socket::SocketFile;
use crate::host::descriptor::{
    CompatDescriptor, Descriptor, DescriptorFlags, FileMode, FileState, FileStatus, PosixFile,
};
use crate::host::process::Process;
use crate::host::syscall::handler::SyscallHandler;
use crate::host::syscall::Trigger;
use crate::host::syscall_condition::SysCallCondition;
use crate::host::syscall_types::{PluginPtr, SysCallArgs, TypedPluginPtr};
use crate::host::syscall_types::{SyscallError, SyscallResult};
use crate::utility::event_queue::EventQueue;

use std::sync::Arc;

use log::*;
use nix::errno::Errno;
use nix::sys::socket::MsgFlags;

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

                SocketFile::Unix(UnixSocketFile::new(
                    FileMode::READ | FileMode::WRITE,
                    file_flags,
                    socket_type,
                    ctx.host.abstract_unix_namespace(),
                ))
            }
            _ => return Err(Errno::EAFNOSUPPORT.into()),
        };

        let mut desc = Descriptor::new(PosixFile::Socket(socket));
        desc.set_flags(descriptor_flags);

        let fd = ctx.process.register_descriptor(CompatDescriptor::New(desc));

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
        let desc = match self.get_descriptor(ctx.process, fd)? {
            CompatDescriptor::New(desc) => desc,
            // if it's a legacy descriptor, use the C syscall handler instead
            CompatDescriptor::Legacy(_) => {
                return unsafe {
                    c::syscallhandler_bind(
                        ctx.thread.csyscallhandler(),
                        args as *const c::SysCallArgs,
                    )
                    .into()
                }
            }
        };

        let posix_file = desc.get_file().clone();

        // get the socket for the descriptor
        let socket = match posix_file {
            PosixFile::Socket(ref x) => x,
            _ => return Err(Errno::ENOTSOCK.into()),
        };

        let addr = self.read_sockaddr(ctx.process, addr_ptr, addr_len)?;

        debug!("Attempting to bind fd {} to {:?}", fd, addr);

        SocketFile::bind(socket, addr.as_ref(), ctx.host.random())
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

        // get the descriptor, or return early if it doesn't exist
        let desc = match self.get_descriptor(ctx.process, fd)? {
            CompatDescriptor::New(desc) => desc,
            // if it's a legacy descriptor, use the C syscall handler instead
            CompatDescriptor::Legacy(_) => {
                return unsafe {
                    c::syscallhandler_sendto(
                        ctx.thread.csyscallhandler(),
                        args as *const c::SysCallArgs,
                    )
                    .into()
                }
            }
        };

        let posix_file = desc.get_file().clone();

        // get the socket for the descriptor
        let socket = match posix_file {
            PosixFile::Socket(ref x) => x,
            _ => return Err(Errno::ENOTSOCK.into()),
        };

        self.sendto_helper(ctx, socket, buf_ptr, buf_len, flags, addr_ptr, addr_len)
    }

    pub fn sendto_helper(
        &self,
        ctx: &mut ThreadContext,
        socket: &SocketFile,
        buf_ptr: PluginPtr,
        buf_len: libc::size_t,
        flags: libc::c_int,
        addr_ptr: PluginPtr,
        addr_len: libc::socklen_t,
    ) -> SyscallResult {
        // get the send flags
        let flags = match MsgFlags::from_bits(flags) {
            Some(x) => x,
            None => {
                // linux doesn't return an error if there are unexpected flags
                warn!("Invalid sendto flags: {}", flags);
                MsgFlags::from_bits_truncate(flags)
            }
        };

        let supported_flags = MsgFlags::MSG_DONTWAIT;
        if flags.intersects(!supported_flags) {
            warn!("Unsupported sendto flags: {:?}", flags);
            return Err(Errno::EOPNOTSUPP.into());
        }

        let addr = self.read_sockaddr(ctx.process, addr_ptr, addr_len)?;

        debug!("Attempting to send {} bytes to {:?}", buf_len, addr);

        let file_status = socket.borrow().get_status();

        // call the socket's sendto(), and run any resulting events
        let result = EventQueue::queue_and_run(|event_queue| {
            socket.borrow_mut().sendto(
                ctx.process
                    .memory()
                    .reader(TypedPluginPtr::new::<u8>(buf_ptr, buf_len)),
                addr,
                event_queue,
            )
        });

        // if the syscall would block, it's a blocking descriptor, and the `MSG_DONTWAIT` flag is not set
        if result == Err(Errno::EWOULDBLOCK.into())
            && !file_status.contains(FileStatus::NONBLOCK)
            && !flags.contains(MsgFlags::MSG_DONTWAIT)
        {
            let trigger =
                Trigger::from_posix_file(PosixFile::Socket(socket.clone()), FileState::WRITABLE);

            return Err(SyscallError::Cond(SysCallCondition::new(trigger)));
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

        // get the descriptor, or return early if it doesn't exist
        let desc = match self.get_descriptor(ctx.process, fd)? {
            CompatDescriptor::New(desc) => desc,
            // if it's a legacy descriptor, use the C syscall handler instead
            CompatDescriptor::Legacy(_) => {
                return unsafe {
                    c::syscallhandler_recvfrom(
                        ctx.thread.csyscallhandler(),
                        args as *const c::SysCallArgs,
                    )
                    .into()
                }
            }
        };

        let posix_file = desc.get_file().clone();

        // get the socket for the descriptor
        let socket = match posix_file {
            PosixFile::Socket(ref x) => x,
            _ => return Err(Errno::ENOTSOCK.into()),
        };

        self.recvfrom_helper(ctx, socket, buf_ptr, buf_len, flags, addr_ptr, addr_len_ptr)
    }

    pub fn recvfrom_helper(
        &self,
        ctx: &mut ThreadContext,
        socket: &SocketFile,
        buf_ptr: PluginPtr,
        buf_len: libc::size_t,
        flags: libc::c_int,
        addr_ptr: PluginPtr,
        addr_len_ptr: PluginPtr,
    ) -> SyscallResult {
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
        let result = EventQueue::queue_and_run(|event_queue| {
            socket.borrow_mut().recvfrom(
                ctx.process
                    .memory_mut()
                    .writer(TypedPluginPtr::new::<u8>(buf_ptr, buf_len)),
                event_queue,
            )
        });

        // if the syscall would block, it's a blocking descriptor, and the `MSG_DONTWAIT` flag is not set
        if result == Err(Errno::EWOULDBLOCK.into())
            && !file_status.contains(FileStatus::NONBLOCK)
            && !flags.contains(MsgFlags::MSG_DONTWAIT)
        {
            let trigger =
                Trigger::from_posix_file(PosixFile::Socket(socket.clone()), FileState::READABLE);

            return Err(SyscallError::Cond(SysCallCondition::new(trigger)));
        };

        let (result, from_addr) = result?;

        if !addr_ptr.is_null() {
            self.write_sockaddr(
                ctx.process,
                from_addr,
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
        let desc = match self.get_descriptor(ctx.process, fd)? {
            CompatDescriptor::New(desc) => desc,
            // if it's a legacy descriptor, use the C syscall handler instead
            CompatDescriptor::Legacy(_) => {
                return unsafe {
                    c::syscallhandler_getsockname(
                        ctx.thread.csyscallhandler(),
                        args as *const c::SysCallArgs,
                    )
                    .into()
                }
            }
        };

        // get the socket for the descriptor
        let socket = match desc.get_file() {
            PosixFile::Socket(x) => x,
            _ => return Err(Errno::ENOTSOCK.into()),
        };

        let addr_to_write = match socket.borrow().get_bound_address() {
            Some(x) => x,
            None => self.empty_sockaddr(socket.borrow().address_family()),
        };

        debug!("Returning socket address of {}", addr_to_write);
        self.write_sockaddr(ctx.process, Some(addr_to_write), addr_ptr, addr_len_ptr)?;

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
        let desc = match self.get_descriptor(ctx.process, fd)? {
            CompatDescriptor::New(desc) => desc,
            // if it's a legacy descriptor, use the C syscall handler instead
            CompatDescriptor::Legacy(_) => {
                return unsafe {
                    c::syscallhandler_getpeername(
                        ctx.thread.csyscallhandler(),
                        args as *const c::SysCallArgs,
                    )
                    .into()
                }
            }
        };

        // get the socket for the descriptor
        let socket = match desc.get_file() {
            PosixFile::Socket(x) => x,
            _ => return Err(Errno::ENOTSOCK.into()),
        };

        let peer_addr = socket.borrow().get_peer_address();
        if let Some(addr_to_write) = peer_addr {
            debug!("Returning peer address of {}", addr_to_write);
            self.write_sockaddr(ctx.process, Some(addr_to_write), addr_ptr, addr_len_ptr)?;
        } else {
            return Err(Errno::ENOTCONN.into());
        }

        Ok(0.into())
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

        let socket_1 = UnixSocketFile::new(
            FileMode::READ | FileMode::WRITE,
            file_flags,
            socket_type,
            ctx.host.abstract_unix_namespace(),
        );
        let socket_2 = UnixSocketFile::new(
            FileMode::READ | FileMode::WRITE,
            file_flags,
            socket_type,
            ctx.host.abstract_unix_namespace(),
        );

        // link the sockets together
        EventQueue::queue_and_run(|event_queue| {
            let unnamed_sock_addr = self.empty_sockaddr(nix::sys::socket::AddressFamily::Unix);
            let unnamed_sock_addr = if let nix::sys::socket::SockAddr::Unix(x) = unnamed_sock_addr {
                x
            } else {
                panic!("Unexpected socket address type");
            };

            UnixSocketFile::connect(
                &socket_1,
                unnamed_sock_addr,
                Arc::clone(socket_2.borrow().recv_buffer()),
                event_queue,
            );
            UnixSocketFile::connect(
                &socket_2,
                unnamed_sock_addr,
                Arc::clone(socket_1.borrow().recv_buffer()),
                event_queue,
            );

            socket_1
                .borrow_mut()
                .set_bound_address(Some(unnamed_sock_addr))
                .unwrap();
            socket_2
                .borrow_mut()
                .set_bound_address(Some(unnamed_sock_addr))
                .unwrap();
        });

        // file descriptors for the sockets
        let mut desc_1 = Descriptor::new(PosixFile::Socket(SocketFile::Unix(socket_1)));
        let mut desc_2 = Descriptor::new(PosixFile::Socket(SocketFile::Unix(socket_2)));

        // set the file descriptor flags
        desc_1.set_flags(descriptor_flags);
        desc_2.set_flags(descriptor_flags);

        // register the file descriptors
        let fd_1 = ctx
            .process
            .register_descriptor(CompatDescriptor::New(desc_1));
        let fd_2 = ctx
            .process
            .register_descriptor(CompatDescriptor::New(desc_2));

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
                EventQueue::queue_and_run(|event_queue| {
                    // ignore any errors when closing
                    ctx.process
                        .deregister_descriptor(fd_1)
                        .unwrap()
                        .close(ctx.host.chost(), event_queue);
                    ctx.process
                        .deregister_descriptor(fd_2)
                        .unwrap()
                        .close(ctx.host.chost(), event_queue);
                });
                Err(e.into())
            }
        }
    }

    /// Returns a nix socket address object where only the family is set.
    fn empty_sockaddr(
        &self,
        family: nix::sys::socket::AddressFamily,
    ) -> nix::sys::socket::SockAddr {
        let family = family as libc::sa_family_t;
        let mut addr: nix::sys::socket::sockaddr_storage = unsafe { std::mem::zeroed() };
        addr.ss_family = family;
        // the size of ss_family will be 2 bytes on linux
        nix::sys::socket::sockaddr_storage_to_addr(&addr, 2).unwrap()
    }

    /// Copy the socket address to the plugin. Will return an error if either the address or address
    /// length pointers are NULL. The plugin's address length will be updated to store the size of the
    /// socket address, even if greater than the provided buffer size. If the address is `None`, the
    /// plugin's address length will be set to 0.
    fn write_sockaddr(
        &self,
        process: &mut Process,
        addr: Option<nix::sys::socket::SockAddr>,
        plugin_addr: PluginPtr,
        plugin_addr_len: TypedPluginPtr<libc::socklen_t>,
    ) -> Result<(), SyscallError> {
        let addr = match addr {
            Some(x) => x,
            None => {
                process.memory_mut().copy_to_ptr(plugin_addr_len, &[0])?;
                return Ok(());
            }
        };

        let (from_addr, from_len) = addr.as_ffi_pair();

        // make sure that we have a real libc sockaddr before converting to a u8 pointer
        let from_addr: &libc::sockaddr = from_addr;
        let from_addr_slice = unsafe {
            std::slice::from_raw_parts(
                from_addr as *const _ as *const u8,
                from_len.try_into().unwrap(),
            )
        };

        // get the provided address buffer length, and overwrite it with the real address length
        let plugin_addr_len = {
            let mut plugin_addr_len = process.memory_mut().memory_ref_mut(plugin_addr_len)?;
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

        let plugin_addr = TypedPluginPtr::new::<u8>(plugin_addr, len_to_copy);
        process
            .memory_mut()
            .copy_to_ptr(plugin_addr, &from_addr_slice[..len_to_copy])?;

        Ok(())
    }

    /// Reads a sockaddr pointer from the plugin. Returns `None` if the pointer is null, otherwise
    /// returns a nix `SockAddr`. The address length must be at most the size of
    /// [`nix::sys::socket::sockaddr_storage`].
    fn read_sockaddr(
        &self,
        process: &mut Process,
        addr_ptr: PluginPtr,
        addr_len: libc::socklen_t,
    ) -> Result<Option<nix::sys::socket::SockAddr>, SyscallError> {
        if addr_ptr.is_null() {
            return Ok(None);
        }

        let addr_len: usize = addr_len.try_into().unwrap();

        let mut addr: nix::sys::socket::sockaddr_storage = unsafe { std::mem::zeroed() };

        // make sure we will not lose data when we copy
        if addr_len > std::mem::size_of_val(&addr) {
            warn!(
                "Shadow does not support the address length {}, which is larger than {}",
                addr_len,
                std::mem::size_of_val(&addr),
            );
            return Err(Errno::EINVAL.into());
        }

        // make sure that we have at least the address family
        if addr_len < 2 {
            return Err(Errno::EINVAL.into());
        }

        // limit the scope of the unsafe slice
        {
            // safety: do not make any other references to addr within this scope
            let slice = unsafe {
                std::slice::from_raw_parts_mut(
                    &mut addr as *mut _ as *mut u8,
                    std::mem::size_of_val(&addr),
                )
            };

            process.memory().copy_from_ptr(
                &mut slice[..addr_len],
                TypedPluginPtr::new::<u8>(addr_ptr, addr_len),
            )?;
        }

        Ok(Some(nix::sys::socket::sockaddr_storage_to_addr(
            &addr, addr_len,
        )?))
    }
}
