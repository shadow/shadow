use crate::cshadow as c;
use crate::host::descriptor::socket::inet::InetSocket;
use crate::host::descriptor::socket::{RecvmsgArgs, RecvmsgReturn, SendmsgArgs, Socket};
use crate::host::descriptor::{CompatFile, File, FileState, FileStatus};
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::io::{self, IoVec, MsgHdr};
use crate::host::syscall_types::{PluginPtr, SyscallError, TypedPluginPtr};
use crate::utility::callback_queue::CallbackQueue;

use nix::errno::Errno;

use syscall_logger::log_syscall;

impl SyscallHandler {
    #[log_syscall(/* rv */ libc::ssize_t, /* fd */ libc::c_int, /* iov */ *const libc::iovec,
                  /* iovcnt */ libc::c_int)]
    pub fn readv(
        ctx: &mut SyscallContext,
        fd: libc::c_int,
        iov_ptr: PluginPtr,
        iov_count: libc::c_int,
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
                        return Self::legacy_syscall(c::syscallhandler_readv, ctx).map(Into::into);
                    }
                }
            }
        };

        //if let File::Socket(Socket::Inet(InetSocket::LegacyTcp(_))) = file.inner_file() {
        //    return Self::legacy_syscall(c::syscallhandler_readv, ctx).map(Into::into);
        //}

        let iov_count = iov_count.try_into().or(Err(Errno::EINVAL))?;

        let iovs = {
            let mem = ctx.objs.process.memory_borrow_mut();
            io::read_iovecs(&mem, iov_ptr, iov_count)?
        };
        assert_eq!(iovs.len(), iov_count);

        let mut result = Self::readv_helper(ctx, file.inner_file(), iovs, None, 0);

        // if the syscall will block, keep the file open until the syscall restarts
        if let Some(err) = result.as_mut().err() {
            if let Some(cond) = err.blocked_condition() {
                cond.set_active_file(file);
            }
        }

        let bytes_read = result?;
        Ok(bytes_read.try_into().unwrap())
    }

    #[log_syscall(/* rv */ libc::ssize_t, /* fd */ libc::c_int, /* iov */ *const libc::iovec,
                  /* iovcnt */ libc::c_int, /* offset */ libc::off_t)]
    pub fn preadv(
        ctx: &mut SyscallContext,
        fd: libc::c_int,
        iov_ptr: PluginPtr,
        iov_count: libc::c_int,
        offset: libc::off_t,
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
                        return Self::legacy_syscall(c::syscallhandler_preadv, ctx).map(Into::into);
                    }
                }
            }
        };

        //if let File::Socket(Socket::Inet(InetSocket::LegacyTcp(_))) = file.inner_file() {
        //    return Self::legacy_syscall(c::syscallhandler_preadv, ctx).map(Into::into);
        //}

        let iov_count = iov_count.try_into().or(Err(Errno::EINVAL))?;

        let iovs = {
            let mem = ctx.objs.process.memory_borrow_mut();
            io::read_iovecs(&mem, iov_ptr, iov_count)?
        };
        assert_eq!(iovs.len(), iov_count);

        let mut result = Self::readv_helper(ctx, file.inner_file(), iovs, Some(offset), 0);

        // if the syscall will block, keep the file open until the syscall restarts
        if let Some(err) = result.as_mut().err() {
            if let Some(cond) = err.blocked_condition() {
                cond.set_active_file(file);
            }
        }

        let bytes_read = result?;
        Ok(bytes_read.try_into().unwrap())
    }

    #[log_syscall(/* rv */ libc::ssize_t, /* fd */ libc::c_int, /* iov */ *const libc::iovec,
                  /* iovcnt */ libc::c_int, /* offset */ libc::off_t, /* flags */ libc::c_int)]
    pub fn preadv2(
        ctx: &mut SyscallContext,
        fd: libc::c_int,
        iov_ptr: PluginPtr,
        iov_count: libc::c_int,
        offset: libc::off_t,
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
                    // if it's a legacy file, use the C syscall handler instead
                    CompatFile::Legacy(_) => {
                        drop(desc_table);
                        return Self::legacy_syscall(c::syscallhandler_preadv2, ctx)
                            .map(Into::into);
                    }
                }
            }
        };

        //if let File::Socket(Socket::Inet(InetSocket::LegacyTcp(_))) = file.inner_file() {
        //    return Self::legacy_syscall(c::syscallhandler_preadv2, ctx).map(Into::into);
        //}

        let iov_count = iov_count.try_into().or(Err(Errno::EINVAL))?;

        let iovs = {
            let mem = ctx.objs.process.memory_borrow_mut();
            io::read_iovecs(&mem, iov_ptr, iov_count)?
        };
        assert_eq!(iovs.len(), iov_count);

        // TODO: "Unlike preadv() and pwritev(), if the offset argument is -1, then the current file
        // offset is used and updated."

        let mut result = Self::readv_helper(ctx, file.inner_file(), iovs, Some(offset), flags);

        // if the syscall will block, keep the file open until the syscall restarts
        if let Some(err) = result.as_mut().err() {
            if let Some(cond) = err.blocked_condition() {
                cond.set_active_file(file);
            }
        }

        let bytes_read = result?;
        Ok(bytes_read.try_into().unwrap())
    }

    pub fn readv_helper(
        ctx: &mut SyscallContext,
        file: &File,
        iovs: Vec<IoVec>,
        offset: Option<libc::off_t>,
        flags: libc::c_int,
    ) -> Result<libc::ssize_t, SyscallError> {
        let mut mem = ctx.objs.process.memory_borrow_mut();

        // if it's a socket, call recvmsg_helper() instead
        if let File::Socket(ref socket) = file {
            if offset.is_some() {
                // sockets don't support offsets
                return Err(Errno::ESPIPE.into());
            }

            let args = RecvmsgArgs {
                iovs: &iovs,
                control_ptr: TypedPluginPtr::new::<u8>(PluginPtr::null(), 0),
                flags: 0,
            };

            // call the socket's recvmsg(), and run any resulting events
            let RecvmsgReturn { bytes_read, .. } =
                crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
                    CallbackQueue::queue_and_run(|cb_queue| {
                        Socket::recvmsg(socket, args, &mut mem, cb_queue)
                    })
                })?;

            return Ok(bytes_read);
        }

        let file_status = file.borrow().get_status();

        let mut result =
            // call the file's read(), and run any resulting events
            crate::utility::legacy_callback_queue::with_global_cb_queue(|| {CallbackQueue::queue_and_run(|cb_queue| {
                file.borrow_mut().readv(
                    &iovs,
                    offset,
                    flags,
                    &mut mem,
                    cb_queue,
                )
            })});

        // if the syscall would block and it's a blocking descriptor
        if result == Err(Errno::EWOULDBLOCK.into()) && !file_status.contains(FileStatus::NONBLOCK) {
            result = Err(SyscallError::new_blocked(
                file.clone(),
                FileState::READABLE,
                file.borrow().supports_sa_restart(),
            ));
        }

        result
    }

    #[log_syscall(/* rv */ libc::ssize_t, /* fd */ libc::c_int, /* iov */ *const libc::iovec,
                  /* iovcnt */ libc::c_int)]
    pub fn writev(
        ctx: &mut SyscallContext,
        fd: libc::c_int,
        iov_ptr: PluginPtr,
        iov_count: libc::c_int,
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
                        return Self::legacy_syscall(c::syscallhandler_writev, ctx).map(Into::into);
                    }
                }
            }
        };

        //if let File::Socket(Socket::Inet(InetSocket::LegacyTcp(_))) = file.inner_file() {
        //    return Self::legacy_syscall(c::syscallhandler_writev, ctx).map(Into::into);
        //}

        let iov_count = iov_count.try_into().or(Err(Errno::EINVAL))?;

        let iovs = {
            let mem = ctx.objs.process.memory_borrow_mut();
            io::read_iovecs(&mem, iov_ptr, iov_count)?
        };
        assert_eq!(iovs.len(), iov_count);

        let mut result = Self::writev_helper(ctx, file.inner_file(), iovs, None, 0);

        // if the syscall will block, keep the file open until the syscall restarts
        if let Some(err) = result.as_mut().err() {
            if let Some(cond) = err.blocked_condition() {
                cond.set_active_file(file);
            }
        }

        let bytes_written = result?;
        Ok(bytes_written.try_into().unwrap())
    }

    #[log_syscall(/* rv */ libc::ssize_t, /* fd */ libc::c_int, /* iov */ *const libc::iovec,
                  /* iovcnt */ libc::c_int, /* offset */ libc::off_t)]
    pub fn pwritev(
        ctx: &mut SyscallContext,
        fd: libc::c_int,
        iov_ptr: PluginPtr,
        iov_count: libc::c_int,
        offset: libc::off_t,
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
                        return Self::legacy_syscall(c::syscallhandler_pwritev, ctx)
                            .map(Into::into);
                    }
                }
            }
        };

        //if let File::Socket(Socket::Inet(InetSocket::LegacyTcp(_))) = file.inner_file() {
        //    return Self::legacy_syscall(c::syscallhandler_pwritev, ctx).map(Into::into);
        //}

        let iov_count = iov_count.try_into().or(Err(Errno::EINVAL))?;

        let iovs = {
            let mem = ctx.objs.process.memory_borrow_mut();
            io::read_iovecs(&mem, iov_ptr, iov_count)?
        };
        assert_eq!(iovs.len(), iov_count);

        let mut result = Self::writev_helper(ctx, file.inner_file(), iovs, Some(offset), 0);

        // if the syscall will block, keep the file open until the syscall restarts
        if let Some(err) = result.as_mut().err() {
            if let Some(cond) = err.blocked_condition() {
                cond.set_active_file(file);
            }
        }

        let bytes_read = result?;
        Ok(bytes_read.try_into().unwrap())
    }

    #[log_syscall(/* rv */ libc::ssize_t, /* fd */ libc::c_int, /* iov */ *const libc::iovec,
                  /* iovcnt */ libc::c_int, /* offset */ libc::off_t, /* flags */ libc::c_int)]
    pub fn pwritev2(
        ctx: &mut SyscallContext,
        fd: libc::c_int,
        iov_ptr: PluginPtr,
        iov_count: libc::c_int,
        offset: libc::off_t,
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
                    // if it's a legacy file, use the C syscall handler instead
                    CompatFile::Legacy(_) => {
                        drop(desc_table);
                        return Self::legacy_syscall(c::syscallhandler_pwritev2, ctx)
                            .map(Into::into);
                    }
                }
            }
        };

        //if let File::Socket(Socket::Inet(InetSocket::LegacyTcp(_))) = file.inner_file() {
        //    return Self::legacy_syscall(c::syscallhandler_pwritev2, ctx).map(Into::into);
        //}

        let iov_count = iov_count.try_into().or(Err(Errno::EINVAL))?;

        let iovs = {
            let mem = ctx.objs.process.memory_borrow_mut();
            io::read_iovecs(&mem, iov_ptr, iov_count)?
        };
        assert_eq!(iovs.len(), iov_count);

        // TODO: "Unlike preadv() and pwritev(), if the offset argument is -1, then the current file
        // offset is used and updated."

        let mut result = Self::writev_helper(ctx, file.inner_file(), iovs, Some(offset), flags);

        // if the syscall will block, keep the file open until the syscall restarts
        if let Some(err) = result.as_mut().err() {
            if let Some(cond) = err.blocked_condition() {
                cond.set_active_file(file);
            }
        }

        let bytes_read = result?;
        Ok(bytes_read.try_into().unwrap())
    }

    pub fn writev_helper(
        ctx: &mut SyscallContext,
        file: &File,
        iovs: Vec<IoVec>,
        offset: Option<libc::off_t>,
        flags: libc::c_int,
    ) -> Result<libc::ssize_t, SyscallError> {
        let mut mem = ctx.objs.process.memory_borrow_mut();

        // if it's a socket, call sendmsg_helper() instead
        if let File::Socket(ref socket) = file {
            if offset.is_some() {
                // sockets don't support offsets
                return Err(Errno::ESPIPE.into());
            }

            let args = SendmsgArgs {
                addr: None,
                iovs: &iovs,
                control_ptr: TypedPluginPtr::new::<u8>(PluginPtr::null(), 0),
                flags: 0,
            };

            // call the socket's sendmsg(), and run any resulting events
            let bytes_written =
                crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
                    CallbackQueue::queue_and_run(|cb_queue| {
                        Socket::sendmsg(socket, args, &mut mem, cb_queue)
                    })
                })?;

            return Ok(bytes_written);
        }

        let file_status = file.borrow().get_status();

        let mut result =
            // call the file's write(), and run any resulting events
            crate::utility::legacy_callback_queue::with_global_cb_queue(|| {CallbackQueue::queue_and_run(|cb_queue| {
                file.borrow_mut().writev(
                    &iovs,
                    offset,
                    flags,
                    &mut mem,
                    cb_queue,
                )
            })});

        // if the syscall would block and it's a blocking descriptor
        if result == Err(Errno::EWOULDBLOCK.into()) && !file_status.contains(FileStatus::NONBLOCK) {
            result = Err(SyscallError::new_blocked(
                file.clone(),
                FileState::WRITABLE,
                file.borrow().supports_sa_restart(),
            ));
        }

        result
    }
}
