use linux_api::errno::Errno;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::cshadow as c;
use crate::host::descriptor::socket::{RecvmsgArgs, RecvmsgReturn, SendmsgArgs, Socket};
use crate::host::descriptor::{CompatFile, File, FileState, FileStatus};
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::io::{self, IoVec};
use crate::host::syscall::types::{ForeignArrayPtr, SyscallError};
use crate::utility::callback_queue::CallbackQueue;

impl SyscallHandler {
    log_syscall!(
        readv,
        /* rv */ libc::ssize_t,
        /* fd */ std::ffi::c_int,
        /* iov */ *const libc::iovec,
        /* iovcnt */ std::ffi::c_int,
    );
    pub fn readv(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
        iov_ptr: ForeignPtr<libc::iovec>,
        iov_count: std::ffi::c_int,
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
                    // if it's a legacy file, use the C syscall handler instead
                    CompatFile::Legacy(_) => {
                        drop(desc_table);
                        return Self::legacy_syscall(c::syscallhandler_readv, ctx);
                    }
                }
            }
        };

        let iov_count = iov_count.try_into().or(Err(Errno::EINVAL))?;

        let iovs = {
            let mem = ctx.objs.process.memory_borrow_mut();
            io::read_iovecs(&mem, iov_ptr, iov_count)?
        };
        assert_eq!(iovs.len(), iov_count);

        let mut result = Self::readv_helper(ctx, file.inner_file(), &iovs, None, 0);

        // if the syscall will block, keep the file open until the syscall restarts
        if let Some(err) = result.as_mut().err() {
            if let Some(cond) = err.blocked_condition() {
                cond.set_active_file(file);
            }
        }

        let bytes_read = result?;
        Ok(bytes_read)
    }

    log_syscall!(
        preadv,
        /* rv */ libc::ssize_t,
        /* fd */ std::ffi::c_int,
        /* iov */ *const libc::iovec,
        /* iovcnt */ std::ffi::c_int,
        /* pos_l */ libc::c_ulong,
        /* pos_h */ libc::c_ulong,
    );
    pub fn preadv(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
        iov_ptr: ForeignPtr<libc::iovec>,
        iov_count: std::ffi::c_int,
        offset_l: libc::c_ulong,
        _offset_h: libc::c_ulong,
    ) -> Result<libc::ssize_t, SyscallError> {
        // on Linux x86-64, an `unsigned long` is 64 bits, so we can ignore `offset_h`
        static_assertions::assert_eq_size!(libc::c_ulong, libc::off_t);
        let offset = offset_l as libc::off_t;

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
                    // if it's a legacy file, use the C syscall handler instead
                    CompatFile::Legacy(_) => {
                        drop(desc_table);
                        return Self::legacy_syscall(c::syscallhandler_preadv, ctx);
                    }
                }
            }
        };

        // make sure the offset is not negative
        if offset < 0 {
            return Err(Errno::EINVAL.into());
        }

        let iov_count = iov_count.try_into().or(Err(Errno::EINVAL))?;

        let iovs = {
            let mem = ctx.objs.process.memory_borrow_mut();
            io::read_iovecs(&mem, iov_ptr, iov_count)?
        };
        assert_eq!(iovs.len(), iov_count);

        let mut result = Self::readv_helper(ctx, file.inner_file(), &iovs, Some(offset), 0);

        // if the syscall will block, keep the file open until the syscall restarts
        if let Some(err) = result.as_mut().err() {
            if let Some(cond) = err.blocked_condition() {
                cond.set_active_file(file);
            }
        }

        let bytes_read = result?;
        Ok(bytes_read)
    }

    log_syscall!(
        preadv2,
        /* rv */ libc::ssize_t,
        /* fd */ std::ffi::c_int,
        /* iov */ *const libc::iovec,
        /* iovcnt */ std::ffi::c_int,
        /* pos_l */ libc::c_ulong,
        /* pos_h */ libc::c_ulong,
        /* flags */ std::ffi::c_int,
    );
    pub fn preadv2(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
        iov_ptr: ForeignPtr<libc::iovec>,
        iov_count: std::ffi::c_int,
        offset_l: libc::c_ulong,
        _offset_h: libc::c_ulong,
        flags: std::ffi::c_int,
    ) -> Result<libc::ssize_t, SyscallError> {
        // on Linux x86-64, an `unsigned long` is 64 bits, so we can ignore `offset_h`
        static_assertions::assert_eq_size!(libc::c_ulong, libc::off_t);
        let offset = offset_l as libc::off_t;

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
                    // if it's a legacy file, use the C syscall handler instead
                    CompatFile::Legacy(_) => {
                        drop(desc_table);
                        return Self::legacy_syscall(c::syscallhandler_preadv2, ctx);
                    }
                }
            }
        };

        // readv(2): "Unlike preadv() and pwritev(), if the offset argument is -1, then the current
        // file offset is used and updated."
        let offset = (offset != -1).then_some(offset);

        // if the offset is set, make sure it's not negative
        if let Some(offset) = offset {
            if offset < 0 {
                return Err(Errno::EINVAL.into());
            }
        }

        let iov_count = iov_count.try_into().or(Err(Errno::EINVAL))?;

        let iovs = {
            let mem = ctx.objs.process.memory_borrow_mut();
            io::read_iovecs(&mem, iov_ptr, iov_count)?
        };
        assert_eq!(iovs.len(), iov_count);

        let mut result = Self::readv_helper(ctx, file.inner_file(), &iovs, offset, flags);

        // if the syscall will block, keep the file open until the syscall restarts
        if let Some(err) = result.as_mut().err() {
            if let Some(cond) = err.blocked_condition() {
                cond.set_active_file(file);
            }
        }

        let bytes_read = result?;
        Ok(bytes_read)
    }

    pub fn readv_helper(
        ctx: &mut SyscallContext,
        file: &File,
        iovs: &[IoVec],
        offset: Option<libc::off_t>,
        flags: std::ffi::c_int,
    ) -> Result<libc::ssize_t, SyscallError> {
        let mut mem = ctx.objs.process.memory_borrow_mut();

        // if it's a socket, call recvmsg_helper() instead
        if let File::Socket(ref socket) = file {
            if offset.is_some() {
                // sockets don't support offsets
                return Err(Errno::ESPIPE.into());
            }

            // experimentally, it seems that read() calls on sockets with 0-length buffers will
            // always return 0, even if there would otherwise be an EWOULDBOCK from a recv() call
            // (see the `test_zero_len_buf_read_and_recv` and `test_zero_len_msg_read_and_recv`
            // send/recv tests for examples)
            if iovs.iter().map(|x| x.len).sum::<usize>() == 0 {
                return Ok(0);
            }

            let args = RecvmsgArgs {
                iovs,
                control_ptr: ForeignArrayPtr::new(ForeignPtr::null(), 0),
                flags: 0,
            };

            // call the socket's recvmsg(), and run any resulting events
            let RecvmsgReturn { return_val, .. } =
                crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
                    CallbackQueue::queue_and_run(|cb_queue| {
                        Socket::recvmsg(socket, args, &mut mem, cb_queue)
                    })
                })?;

            return Ok(return_val);
        }

        let file_status = file.borrow().status();

        let result =
            // call the file's read(), and run any resulting events
            crate::utility::legacy_callback_queue::with_global_cb_queue(|| {CallbackQueue::queue_and_run(|cb_queue| {
                file.borrow_mut().readv(
                    iovs,
                    offset,
                    flags,
                    &mut mem,
                    cb_queue,
                )
            })});

        // if the syscall would block and it's a blocking descriptor
        if result == Err(Errno::EWOULDBLOCK.into()) && !file_status.contains(FileStatus::NONBLOCK) {
            return Err(SyscallError::new_blocked_on_file(
                file.clone(),
                FileState::READABLE,
                file.borrow().supports_sa_restart(),
            ));
        }

        result
    }

    log_syscall!(
        writev,
        /* rv */ libc::ssize_t,
        /* fd */ std::ffi::c_int,
        /* iov */ *const libc::iovec,
        /* iovcnt */ std::ffi::c_int,
    );
    pub fn writev(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
        iov_ptr: ForeignPtr<libc::iovec>,
        iov_count: std::ffi::c_int,
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
                    // if it's a legacy file, use the C syscall handler instead
                    CompatFile::Legacy(_) => {
                        drop(desc_table);
                        return Self::legacy_syscall(c::syscallhandler_writev, ctx);
                    }
                }
            }
        };

        let iov_count = iov_count.try_into().or(Err(Errno::EINVAL))?;

        let iovs = {
            let mem = ctx.objs.process.memory_borrow_mut();
            io::read_iovecs(&mem, iov_ptr, iov_count)?
        };
        assert_eq!(iovs.len(), iov_count);

        let mut result = Self::writev_helper(ctx, file.inner_file(), &iovs, None, 0);

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
        pwritev,
        /* rv */ libc::ssize_t,
        /* fd */ std::ffi::c_int,
        /* iov */ *const libc::iovec,
        /* iovcnt */ std::ffi::c_int,
        /* pos_l */ libc::c_ulong,
        /* pos_h */ libc::c_ulong,
    );
    pub fn pwritev(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
        iov_ptr: ForeignPtr<libc::iovec>,
        iov_count: std::ffi::c_int,
        offset_l: libc::c_ulong,
        _offset_h: libc::c_ulong,
    ) -> Result<libc::ssize_t, SyscallError> {
        // on Linux x86-64, an `unsigned long` is 64 bits, so we can ignore `offset_h`
        static_assertions::assert_eq_size!(libc::c_ulong, libc::off_t);
        let offset = offset_l as libc::off_t;

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
                    // if it's a legacy file, use the C syscall handler instead
                    CompatFile::Legacy(_) => {
                        drop(desc_table);
                        return Self::legacy_syscall(c::syscallhandler_pwritev, ctx);
                    }
                }
            }
        };

        // make sure the offset is not negative
        if offset < 0 {
            return Err(Errno::EINVAL.into());
        }

        let iov_count = iov_count.try_into().or(Err(Errno::EINVAL))?;

        let iovs = {
            let mem = ctx.objs.process.memory_borrow_mut();
            io::read_iovecs(&mem, iov_ptr, iov_count)?
        };
        assert_eq!(iovs.len(), iov_count);

        let mut result = Self::writev_helper(ctx, file.inner_file(), &iovs, Some(offset), 0);

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
        pwritev2,
        /* rv */ libc::ssize_t,
        /* fd */ std::ffi::c_int,
        /* iov */ *const libc::iovec,
        /* iovcnt */ std::ffi::c_int,
        /* pos_l */ libc::c_ulong,
        /* pos_h */ libc::c_ulong,
        /* flags */ std::ffi::c_int,
    );
    pub fn pwritev2(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
        iov_ptr: ForeignPtr<libc::iovec>,
        iov_count: std::ffi::c_int,
        offset_l: libc::c_ulong,
        _offset_h: libc::c_ulong,
        flags: std::ffi::c_int,
    ) -> Result<libc::ssize_t, SyscallError> {
        // on Linux x86-64, an `unsigned long` is 64 bits, so we can ignore `offset_h`
        static_assertions::assert_eq_size!(libc::c_ulong, libc::off_t);
        let offset = offset_l as libc::off_t;

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
                    // if it's a legacy file, use the C syscall handler instead
                    CompatFile::Legacy(_) => {
                        drop(desc_table);
                        return Self::legacy_syscall(c::syscallhandler_pwritev2, ctx);
                    }
                }
            }
        };

        // readv(2): "Unlike preadv() and pwritev(), if the offset argument is -1, then the current
        // file offset is used and updated."
        let offset = (offset != -1).then_some(offset);

        // if the offset is set, make sure it's not negative
        if let Some(offset) = offset {
            if offset < 0 {
                return Err(Errno::EINVAL.into());
            }
        }

        let iov_count = iov_count.try_into().or(Err(Errno::EINVAL))?;

        let iovs = {
            let mem = ctx.objs.process.memory_borrow_mut();
            io::read_iovecs(&mem, iov_ptr, iov_count)?
        };
        assert_eq!(iovs.len(), iov_count);

        let mut result = Self::writev_helper(ctx, file.inner_file(), &iovs, offset, flags);

        // if the syscall will block, keep the file open until the syscall restarts
        if let Some(err) = result.as_mut().err() {
            if let Some(cond) = err.blocked_condition() {
                cond.set_active_file(file);
            }
        }

        let bytes_written = result?;
        Ok(bytes_written)
    }

    pub fn writev_helper(
        ctx: &mut SyscallContext,
        file: &File,
        iovs: &[IoVec],
        offset: Option<libc::off_t>,
        flags: std::ffi::c_int,
    ) -> Result<libc::ssize_t, SyscallError> {
        let mut mem = ctx.objs.process.memory_borrow_mut();
        let mut rng = ctx.objs.host.random_mut();
        let net_ns = ctx.objs.host.network_namespace_borrow();

        // if it's a socket, call sendmsg_helper() instead
        if let File::Socket(ref socket) = file {
            if offset.is_some() {
                // sockets don't support offsets
                return Err(Errno::ESPIPE.into());
            }

            let args = SendmsgArgs {
                addr: None,
                iovs,
                control_ptr: ForeignArrayPtr::new(ForeignPtr::null(), 0),
                flags: 0,
            };

            // call the socket's sendmsg(), and run any resulting events
            let bytes_written =
                crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
                    CallbackQueue::queue_and_run(|cb_queue| {
                        Socket::sendmsg(socket, args, &mut mem, &net_ns, &mut *rng, cb_queue)
                    })
                })?;

            return Ok(bytes_written);
        }

        let file_status = file.borrow().status();

        let result =
            // call the file's write(), and run any resulting events
            crate::utility::legacy_callback_queue::with_global_cb_queue(|| {CallbackQueue::queue_and_run(|cb_queue| {
                file.borrow_mut().writev(
                    iovs,
                    offset,
                    flags,
                    &mut mem,
                    cb_queue,
                )
            })});

        // if the syscall would block and it's a blocking descriptor
        if result == Err(Errno::EWOULDBLOCK.into()) && !file_status.contains(FileStatus::NONBLOCK) {
            return Err(SyscallError::new_blocked_on_file(
                file.clone(),
                FileState::WRITABLE,
                file.borrow().supports_sa_restart(),
            ));
        }

        result
    }
}
