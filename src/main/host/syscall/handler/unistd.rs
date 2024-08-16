use std::ffi::{CStr, CString};
use std::os::unix::ffi::OsStringExt;
use std::sync::Arc;

use atomic_refcell::AtomicRefCell;
use linux_api::errno::Errno;
use linux_api::fcntl::{DescriptorFlags, OFlag};
use linux_api::posix_types::{kernel_off_t, kernel_pid_t};
use log::*;
use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::rootedcell::refcell::RootedRefCell;
use shadow_shim_helper_rs::simulation_time::SimulationTime;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::core::work::task::TaskRef;
use crate::core::worker::Worker;
use crate::cshadow as c;
use crate::host::descriptor::descriptor_table::DescriptorHandle;
use crate::host::descriptor::pipe;
use crate::host::descriptor::shared_buf::SharedBuf;
use crate::host::descriptor::{CompatFile, Descriptor, File, FileMode, FileStatus, OpenFile};
use crate::host::process::{Process, ProcessId};
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::io::{read_cstring_vec, IoVec};
use crate::host::syscall::type_formatting::{SyscallBufferArg, SyscallStringArg};
use crate::host::syscall::types::{ForeignArrayPtr, SyscallError};
use crate::utility::callback_queue::CallbackQueue;
use crate::utility::u8_to_i8_slice;

impl SyscallHandler {
    log_syscall!(
        close,
        /* rv */ std::ffi::c_int,
        /* fd */ std::ffi::c_int,
    );
    pub fn close(ctx: &mut SyscallContext, fd: std::ffi::c_int) -> Result<(), SyscallError> {
        trace!("Trying to close fd {}", fd);

        let fd = fd.try_into().or(Err(linux_api::errno::Errno::EBADF))?;

        // according to "man 2 close", in Linux any errors that may occur will happen after the fd is
        // released, so we should always deregister the descriptor even if there's an error while
        // closing
        let desc = ctx
            .objs
            .thread
            .descriptor_table_borrow_mut(ctx.objs.host)
            .deregister_descriptor(fd)
            .ok_or(linux_api::errno::Errno::EBADF)?;

        // if there are still valid descriptors to the open file, close() will do nothing
        // and return None
        crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
            CallbackQueue::queue_and_run(|cb_queue| desc.close(ctx.objs.host, cb_queue))
                .unwrap_or(Ok(()))
        })
    }

    log_syscall!(
        dup,
        /* rv */ std::ffi::c_int,
        /* oldfd */ std::ffi::c_int,
    );
    pub fn dup(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
    ) -> Result<DescriptorHandle, SyscallError> {
        // get the descriptor, or return early if it doesn't exist
        let mut desc_table = ctx.objs.thread.descriptor_table_borrow_mut(ctx.objs.host);
        let desc = Self::get_descriptor(&desc_table, fd)?;

        // duplicate the descriptor
        let new_desc = desc.dup(DescriptorFlags::empty());

        Ok(desc_table
            .register_descriptor(new_desc)
            .or(Err(Errno::ENFILE))?)
    }

    log_syscall!(
        dup2,
        /* rv */ std::ffi::c_int,
        /* oldfd */ std::ffi::c_int,
        /* newfd */ std::ffi::c_int,
    );
    pub fn dup2(
        ctx: &mut SyscallContext,
        old_fd: std::ffi::c_int,
        new_fd: std::ffi::c_int,
    ) -> Result<DescriptorHandle, SyscallError> {
        let old_fd = DescriptorHandle::try_from(old_fd).or(Err(Errno::EBADF))?;
        let new_fd = DescriptorHandle::try_from(new_fd).or(Err(Errno::EBADF))?;

        // get the descriptor, or return early if it doesn't exist
        let mut desc_table = ctx.objs.thread.descriptor_table_borrow_mut(ctx.objs.host);
        let desc = Self::get_descriptor(&desc_table, old_fd)?;

        // from 'man 2 dup2': "If oldfd is a valid file descriptor, and newfd has the same
        // value as oldfd, then dup2() does nothing, and returns newfd"
        if old_fd == new_fd {
            return Ok(new_fd);
        }

        // duplicate the descriptor
        let new_desc = desc.dup(DescriptorFlags::empty());
        let replaced_desc = desc_table.register_descriptor_with_fd(new_desc, new_fd);

        // close the replaced descriptor
        if let Some(replaced_desc) = replaced_desc {
            // from 'man 2 dup2': "If newfd was open, any errors that would have been reported at
            // close(2) time are lost"
            crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
                CallbackQueue::queue_and_run(|cb_queue| {
                    replaced_desc.close(ctx.objs.host, cb_queue)
                })
            });
        }

        // return the new fd
        Ok(new_fd)
    }

    log_syscall!(
        dup3,
        /* rv */ std::ffi::c_int,
        /* oldfd */ std::ffi::c_int,
        /* newfd */ std::ffi::c_int,
        /* flags */ linux_api::fcntl::OFlag,
    );
    pub fn dup3(
        ctx: &mut SyscallContext,
        old_fd: std::ffi::c_int,
        new_fd: std::ffi::c_int,
        flags: std::ffi::c_int,
    ) -> Result<DescriptorHandle, SyscallError> {
        // get the descriptor, or return early if it doesn't exist
        let mut desc_table = ctx.objs.thread.descriptor_table_borrow_mut(ctx.objs.host);
        let desc = Self::get_descriptor(&desc_table, old_fd)?;

        // from 'man 2 dup3': "If oldfd equals newfd, then dup3() fails with the error EINVAL"
        if old_fd == new_fd {
            return Err(linux_api::errno::Errno::EINVAL.into());
        }

        let new_fd = new_fd.try_into().or(Err(linux_api::errno::Errno::EBADF))?;

        let Some(flags) = OFlag::from_bits(flags) else {
            debug!("Invalid flags: {flags}");
            return Err(linux_api::errno::Errno::EINVAL.into());
        };

        let mut descriptor_flags = DescriptorFlags::empty();

        // dup3 only supports the O_CLOEXEC flag
        for flag in flags {
            match flag {
                OFlag::O_CLOEXEC => descriptor_flags.insert(DescriptorFlags::FD_CLOEXEC),
                x if x == OFlag::empty() => {
                    // The "empty" flag is always present. Ignore.
                }
                _ => {
                    debug!("Invalid flags for dup3: {flags:?}");
                    return Err(linux_api::errno::Errno::EINVAL.into());
                }
            }
        }

        // duplicate the descriptor
        let new_desc = desc.dup(descriptor_flags);
        let replaced_desc = desc_table.register_descriptor_with_fd(new_desc, new_fd);

        // close the replaced descriptor
        if let Some(replaced_desc) = replaced_desc {
            // from 'man 2 dup3': "If newfd was open, any errors that would have been reported at
            // close(2) time are lost"
            crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
                CallbackQueue::queue_and_run(|cb_queue| {
                    replaced_desc.close(ctx.objs.host, cb_queue)
                })
            });
        }

        // return the new fd
        Ok(new_fd)
    }

    log_syscall!(
        read,
        /* rv */ isize,
        /* fd */ std::ffi::c_int,
        /* buf */ *const std::ffi::c_void,
        /* count */ usize,
    );
    pub fn read(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
        buf_ptr: ForeignPtr<u8>,
        buf_size: usize,
    ) -> Result<isize, SyscallError> {
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
                        return Self::legacy_syscall(c::syscallhandler_read, ctx);
                    }
                }
            }
        };

        let mut result = Self::read_helper(ctx, file.inner_file(), buf_ptr, buf_size, None);

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
        pread64,
        /* rv */ isize,
        /* fd */ std::ffi::c_int,
        /* buf */ *const std::ffi::c_void,
        /* count */ usize,
        /* offset */ kernel_off_t,
    );
    pub fn pread64(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
        buf_ptr: ForeignPtr<u8>,
        buf_size: usize,
        offset: kernel_off_t,
    ) -> Result<isize, SyscallError> {
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
                        return Self::legacy_syscall(c::syscallhandler_pread64, ctx);
                    }
                }
            }
        };

        let mut result = Self::read_helper(ctx, file.inner_file(), buf_ptr, buf_size, Some(offset));

        // if the syscall will block, keep the file open until the syscall restarts
        if let Some(err) = result.as_mut().err() {
            if let Some(cond) = err.blocked_condition() {
                cond.set_active_file(file);
            }
        }

        let bytes_read = result?;
        Ok(bytes_read)
    }

    fn read_helper(
        ctx: &mut SyscallContext,
        file: &File,
        buf_ptr: ForeignPtr<u8>,
        buf_size: usize,
        offset: Option<kernel_off_t>,
    ) -> Result<isize, SyscallError> {
        let iov = IoVec {
            base: buf_ptr,
            len: buf_size,
        };
        Self::readv_helper(ctx, file, &[iov], offset, 0)
    }

    log_syscall!(
        write,
        /* rv */ isize,
        /* fd */ std::ffi::c_int,
        /* buf */ SyscallBufferArg</* count */ 2>,
        /* count */ usize,
    );
    pub fn write(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
        buf_ptr: ForeignPtr<u8>,
        buf_size: usize,
    ) -> Result<isize, SyscallError> {
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
                        return Self::legacy_syscall(c::syscallhandler_write, ctx);
                    }
                }
            }
        };

        let mut result = Self::write_helper(ctx, file.inner_file(), buf_ptr, buf_size, None);

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
        pwrite64,
        /* rv */ isize,
        /* fd */ std::ffi::c_int,
        /* buf */ SyscallBufferArg</* count */ 2>,
        /* count */ usize,
        /* offset */ kernel_off_t,
    );
    pub fn pwrite64(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_int,
        buf_ptr: ForeignPtr<u8>,
        buf_size: usize,
        offset: kernel_off_t,
    ) -> Result<isize, SyscallError> {
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
                        return Self::legacy_syscall(c::syscallhandler_pwrite64, ctx);
                    }
                }
            }
        };

        let mut result =
            Self::write_helper(ctx, file.inner_file(), buf_ptr, buf_size, Some(offset));

        // if the syscall will block, keep the file open until the syscall restarts
        if let Some(err) = result.as_mut().err() {
            if let Some(cond) = err.blocked_condition() {
                cond.set_active_file(file);
            }
        }

        let bytes_written = result?;
        Ok(bytes_written)
    }

    fn write_helper(
        ctx: &mut SyscallContext,
        file: &File,
        buf_ptr: ForeignPtr<u8>,
        buf_size: usize,
        offset: Option<kernel_off_t>,
    ) -> Result<isize, SyscallError> {
        let iov = IoVec {
            base: buf_ptr,
            len: buf_size,
        };
        Self::writev_helper(ctx, file, &[iov], offset, 0)
    }

    log_syscall!(
        pipe,
        /* rv */ std::ffi::c_int,
        /* pipefd */ [std::ffi::c_int; 2],
    );
    pub fn pipe(
        ctx: &mut SyscallContext,
        fd_ptr: ForeignPtr<[std::ffi::c_int; 2]>,
    ) -> Result<(), SyscallError> {
        Self::pipe_helper(ctx, fd_ptr, 0)
    }

    log_syscall!(
        pipe2,
        /* rv */ std::ffi::c_int,
        /* pipefd */ [std::ffi::c_int; 2],
        /* flags */ linux_api::fcntl::OFlag,
    );
    pub fn pipe2(
        ctx: &mut SyscallContext,
        fd_ptr: ForeignPtr<[std::ffi::c_int; 2]>,
        flags: std::ffi::c_int,
    ) -> Result<(), SyscallError> {
        Self::pipe_helper(ctx, fd_ptr, flags)
    }

    fn pipe_helper(
        ctx: &mut SyscallContext,
        fd_ptr: ForeignPtr<[std::ffi::c_int; 2]>,
        flags: i32,
    ) -> Result<(), SyscallError> {
        // make sure they didn't pass a NULL pointer
        if fd_ptr.is_null() {
            return Err(linux_api::errno::Errno::EFAULT.into());
        }

        let Some(flags) = OFlag::from_bits(flags) else {
            debug!("Invalid flags: {flags}");
            return Err(Errno::EINVAL.into());
        };

        let mut file_flags = FileStatus::empty();
        let mut descriptor_flags = DescriptorFlags::empty();

        for flag in flags.iter() {
            match flag {
                OFlag::O_NONBLOCK => file_flags.insert(FileStatus::NONBLOCK),
                OFlag::O_DIRECT => file_flags.insert(FileStatus::DIRECT),
                OFlag::O_CLOEXEC => descriptor_flags.insert(DescriptorFlags::FD_CLOEXEC),
                x if x == OFlag::empty() => {
                    // The "empty" flag is always present. Ignore.
                }
                unhandled => {
                    // TODO: return an error and change this to `warn_once_then_debug`?
                    warn!("Ignoring pipe flag {unhandled:?}");
                }
            }
        }

        // reference-counted buffer for the pipe
        let buffer = SharedBuf::new(c::CONFIG_PIPE_BUFFER_SIZE.try_into().unwrap());
        let buffer = Arc::new(AtomicRefCell::new(buffer));

        // reference-counted file object for read end of the pipe
        let reader = pipe::Pipe::new(FileMode::READ, file_flags);
        let reader = Arc::new(AtomicRefCell::new(reader));

        // reference-counted file object for write end of the pipe
        let writer = pipe::Pipe::new(FileMode::WRITE, file_flags);
        let writer = Arc::new(AtomicRefCell::new(writer));

        // set the file objects to listen for events on the buffer
        CallbackQueue::queue_and_run(|cb_queue| {
            pipe::Pipe::connect_to_buffer(&reader, Arc::clone(&buffer), cb_queue);
            pipe::Pipe::connect_to_buffer(&writer, Arc::clone(&buffer), cb_queue);
        });

        // file descriptors for the read and write file objects
        let mut reader_desc = Descriptor::new(CompatFile::New(OpenFile::new(File::Pipe(reader))));
        let mut writer_desc = Descriptor::new(CompatFile::New(OpenFile::new(File::Pipe(writer))));

        // set the file descriptor flags
        reader_desc.set_flags(descriptor_flags);
        writer_desc.set_flags(descriptor_flags);

        // register the file descriptors
        let mut dt = ctx.objs.thread.descriptor_table_borrow_mut(ctx.objs.host);
        // unwrap here since the error handling would be messy (need to deregister) and we shouldn't
        // ever need to worry about this in practice
        let read_fd = dt.register_descriptor(reader_desc).unwrap();
        let write_fd = dt.register_descriptor(writer_desc).unwrap();

        // try to write them to the caller
        let fds = [i32::from(read_fd), i32::from(write_fd)];
        let write_res = ctx.objs.process.memory_borrow_mut().write(fd_ptr, &fds);

        // clean up in case of error
        match write_res {
            Ok(_) => Ok(()),
            Err(e) => {
                CallbackQueue::queue_and_run(|cb_queue| {
                    // ignore any errors when closing
                    dt.deregister_descriptor(read_fd)
                        .unwrap()
                        .close(ctx.objs.host, cb_queue);
                    dt.deregister_descriptor(write_fd)
                        .unwrap()
                        .close(ctx.objs.host, cb_queue);
                });
                Err(e.into())
            }
        }
    }

    log_syscall!(getpid, /* rv */ linux_api::posix_types::kernel_pid_t);
    pub fn getpid(ctx: &mut SyscallContext) -> Result<kernel_pid_t, SyscallError> {
        Ok(ctx.objs.process.id().into())
    }

    log_syscall!(getppid, /* rv */ linux_api::posix_types::kernel_pid_t);
    pub fn getppid(ctx: &mut SyscallContext) -> Result<kernel_pid_t, SyscallError> {
        Ok(ctx.objs.process.parent_id().into())
    }

    log_syscall!(getpgrp, /* rv */ kernel_pid_t);
    pub fn getpgrp(ctx: &mut SyscallContext) -> Result<kernel_pid_t, SyscallError> {
        Ok(ctx.objs.process.group_id().into())
    }

    log_syscall!(
        getpgid,
        /* rv */ kernel_pid_t,
        /* pid*/ kernel_pid_t,
    );
    pub fn getpgid(
        ctx: &mut SyscallContext,
        pid: kernel_pid_t,
    ) -> Result<kernel_pid_t, SyscallError> {
        if pid == 0 || pid == kernel_pid_t::from(ctx.objs.process.id()) {
            return Ok(ctx.objs.process.group_id().into());
        }
        let pid = ProcessId::try_from(pid).map_err(|_| Errno::EINVAL)?;
        let Some(process) = ctx.objs.host.process_borrow(pid) else {
            return Err(Errno::ESRCH.into());
        };
        let process = process.borrow(ctx.objs.host.root());
        Ok(process.group_id().into())
    }

    log_syscall!(
        setpgid,
        /* rv */ std::ffi::c_int,
        /* pid */ kernel_pid_t,
        /* pgid */ kernel_pid_t,
    );
    pub fn setpgid(
        ctx: &mut SyscallContext,
        pid: kernel_pid_t,
        pgid: kernel_pid_t,
    ) -> Result<(), SyscallError> {
        let _processrc_borrow;
        let _process_borrow;
        let process: &Process;
        if pid == 0 || pid == kernel_pid_t::from(ctx.objs.process.id()) {
            _processrc_borrow = None;
            _process_borrow = None;
            process = ctx.objs.process;
        } else {
            let pid = ProcessId::try_from(pid).map_err(|_| Errno::EINVAL)?;
            let Some(pbrc) = ctx.objs.host.process_borrow(pid) else {
                return Err(Errno::ESRCH.into());
            };
            _processrc_borrow = Some(pbrc);
            _process_borrow = Some(
                _processrc_borrow
                    .as_ref()
                    .unwrap()
                    .borrow(ctx.objs.host.root()),
            );
            process = _process_borrow.as_ref().unwrap();
        }
        let pgid = if pgid == 0 {
            None
        } else {
            Some(ProcessId::try_from(pgid).map_err(|_| Errno::EINVAL)?)
        };
        if process.id() != ctx.objs.process.id() && process.parent_id() != ctx.objs.process.id() {
            // `setpgid(2)`: pid is not the calling process and not a child  of
            // the calling process.
            return Err(Errno::ESRCH.into());
        }
        if let Some(pgid) = pgid {
            if ctx.objs.host.process_session_id_of_group_id(pgid) != Some(process.session_id()) {
                // An attempt was made to move a process into a process group in
                // a different session
                return Err(Errno::EPERM.into());
            }
        }
        if process.session_id() != ctx.objs.process.session_id() {
            // `setpgid(2)`: ... or to change the process  group  ID of one of
            // the children of the calling process and the child was in a
            // different session
            return Err(Errno::EPERM.into());
        }
        if process.session_id() == process.id() {
            // `setpgid(2)`: ... or to change the process group ID of a session leader
            return Err(Errno::EPERM.into());
        }
        // TODO: Keep track of whether a process has performed an `execve`.
        // `setpgid(2): EACCES: An attempt was made to change the process group
        // ID of one of the children of the calling process and the child had
        // already performed an execve(2).
        if let Some(pgid) = pgid {
            if ctx.objs.host.process_session_id_of_group_id(pgid) != Some(process.session_id()) {
                // `setpgid(2)`: An attempt was made to move a process into a
                // process group in a different session
                return Err(Errno::EPERM.into());
            }
            process.set_group_id(pgid);
        } else {
            // `setpgid(2)`: If pgid is zero, then the PGID of the process
            // specified by pid is made the same as its process ID.
            process.set_group_id(process.id());
        }
        Ok(())
    }

    log_syscall!(
        getsid,
        /* rv */ kernel_pid_t,
        /* pid */ kernel_pid_t,
    );
    pub fn getsid(
        ctx: &mut SyscallContext,
        pid: kernel_pid_t,
    ) -> Result<kernel_pid_t, SyscallError> {
        if pid == 0 {
            return Ok(ctx.objs.process.session_id().into());
        }
        let Ok(pid) = ProcessId::try_from(pid) else {
            return Err(Errno::EINVAL.into());
        };
        let Some(processrc) = ctx.objs.host.process_borrow(pid) else {
            return Err(Errno::ESRCH.into());
        };
        let process = processrc.borrow(ctx.objs.host.root());
        // No need to check that process is in the same session:
        //
        // `getsid(2)`: A process with process ID pid exists, but it is not in
        // the same session as the calling process, and the implementation
        // considers this an error... **Linux does not return EPERM**.

        Ok(process.session_id().into())
    }

    log_syscall!(setsid, /* rv */ kernel_pid_t);
    pub fn setsid(ctx: &mut SyscallContext) -> Result<kernel_pid_t, SyscallError> {
        let pid = ctx.objs.process.id();
        if ctx.objs.host.process_session_id_of_group_id(pid).is_some() {
            // `setsid(2)`: The process group ID of any process equals the PID
            // of the calling process.  Thus, in particular, setsid() fails if
            // the calling process is already a process group leader.
            return Err(Errno::EPERM.into());
        }

        // `setsid(2)`: The calling process is the leader of the new session
        // (i.e., its session ID is made the same as its process ID).
        ctx.objs.process.set_session_id(pid);

        // `setsid(2)`: The calling  process  also  becomes  the  process group
        // leader of a new process group in the session (i.e., its process group
        // ID is made the same as its process ID).
        ctx.objs.process.set_group_id(pid);

        Ok(pid.into())
    }

    fn execve_common(
        ctx: &mut SyscallContext,
        base_dir: &CStr,
        path: &CStr,
        argv_ptr_ptr: ForeignPtr<ForeignPtr<std::ffi::c_char>>,
        envv_ptr_ptr: ForeignPtr<ForeignPtr<std::ffi::c_char>>,
        _flags: std::ffi::c_int,
    ) -> Result<(), SyscallError> {
        if path.is_empty() {
            // execve(2): The file pathname or a script or ELF interpreter does not exist.
            return Err(Errno::ENOENT.into());
        }

        let path_bytes_with_nul = path.to_bytes_with_nul();

        let _abs_path_storage: Option<CString>;
        let abs_path: &CStr;
        if path_bytes_with_nul[0] != b'/' {
            let base_dir_bytes = base_dir.to_bytes();

            // Maybe TODO: this could be done in place without allocating
            // and with less copying (but more fiddly and error-prone).
            let mut tmp = Vec::with_capacity(
                base_dir_bytes.len() + path_bytes_with_nul.len() + /*separator*/1,
            );
            tmp.extend(base_dir_bytes);
            tmp.push(b'/');
            tmp.extend(path_bytes_with_nul);

            _abs_path_storage = Some(CString::from_vec_with_nul(tmp).unwrap());
            abs_path = _abs_path_storage.as_ref().unwrap();
        } else {
            _abs_path_storage = None;
            abs_path = path;
        }

        // TODO: canonicalize? On one hand that would improve caching behavior
        // in `verify_plugin_path`; OTOH it does some redundant work with
        // `verify_plugin_path`. Ideal solution is probably to split up
        // `verify_plugin_path` a bit.

        // `execve(2)`: Most UNIX implementations impose some limit on the
        // total size of the command-line  argument  (argv)  and
        // environment  (envp) strings that may be passed to a new program.
        // POSIX.1 allows an implementation to advertise this limit using
        // the ARG_MAX constant

        let argv;
        let envv;
        {
            let mem = ctx.objs.process.memory_borrow();
            argv = read_cstring_vec(&mem, argv_ptr_ptr)?;
            envv = read_cstring_vec(&mem, envv_ptr_ptr)?;
        }

        let mthread = ctx
            .objs
            .process
            .borrow_as_runnable()
            .unwrap()
            .spawn_mthread_for_exec(ctx.objs.host, abs_path, argv, envv)?;

        // If we get this far, then we should be able to ultimately succeed.
        // We need a mutable reference to the Process to update it, though, which we can't
        // get from here since it's already borrowed immutably.
        //
        // So, we return a "blocking" result from this syscall handler, and
        // schedule an event to update the `Process` and resume execution.
        //
        // It's possible that other events may affect the `Process` before this one runs.
        // We try to handle this gracefully; e.g. if the `Process` has exited before this
        // event runs, we kill and drop the exec'd `ManagedThread` and carry on.
        //
        // TODO: There may be other interactions that aren't handled correctly.
        // e.g. if the exec'ing thread ends up handling a signal in the meantime.
        // * We could add a new state "`Execing`" to `Process`, and force any
        // such events to decide how to deal with it. e.g. signal delivery
        // events could reschedule themselves to run after the exec has
        // completed. This seems a bit heavy-weight, though.
        // * We could add more interior mutability s.t. we don't need mutable
        // references to the Thread and Process in order to do the necessary
        // updates. This is a fair bit of extra interior mutability to add
        // though, and has a side-effect of further complicating read-accesses
        // to items that are read-mostly.
        // * We could arrange for syscall handlers to get or be able to get
        // mutable references to the Thread and Process, so that we can complete
        // the updates synchronously here. This is currently blocked by the
        // usage of `worker_getCurrentProcess` and `worker_getCurrentThread`,
        // which will panic with incompatible borrow errors if those are
        // borrowed mutably.  There aren't many references left to those though,
        // maybe we can eliminate them.
        {
            let pid = ctx.objs.process.id();
            let tid = ctx.objs.thread.id();

            // Tasks are currently required to be `Sync` and to implement `Fn`, not just `FnOnce`.
            // Since `mthread` isn't `Sync`, we need to wrap it in a `RootedRefCell`.
            // Since we need to consume it, we need to also wrap it in an
            // `Option` and fail at runtime if this actually gets executed
            // multiple times.
            // TODO: Split TaskRef into another type that only requires `FnOnce` and `Send`.
            let mthread = RootedRefCell::new(ctx.objs.host.root(), Some(mthread));
            ctx.objs.host.schedule_task_with_delay(
                TaskRef::new(move |host| {
                    // Take the `mthread` out of the captured wrapper.
                    // This task shouldn't run multiple times, so this should be
                    // infallible.
                    let mthread = mthread.borrow_mut(host.root()).take().unwrap();
                    // The exec'ing thread's ID is changed to match the pid, since it's
                    // the new thread-group-leader.
                    let new_tglid = {
                        let Some(processrc) = host.process_borrow(pid) else {
                            // Can happen if another event runs before this one
                            // and causes the Process to exit (e.g. exit_group
                            // called from anothe Thread).
                            log::debug!("Process {pid:?} disappeared before exec could complete");
                            mthread.kill_and_drop();
                            return;
                        };
                        Worker::set_active_process(&processrc);
                        let mut process = processrc.borrow_mut(host.root());
                        process.update_for_exec(host, tid, mthread);
                        Worker::clear_active_process();
                        process.thread_group_leader_id()
                    };
                    host.resume(pid, new_tglid);
                }),
                SimulationTime::ZERO,
            );
        }

        Err(SyscallError::new_blocked_until(EmulatedTime::MAX, false))
    }

    log_syscall!(
        execve,
        /* rv */ i32,
        /* pathname */ SyscallStringArg,
        /* argv */ *const std::ffi::c_void,
        /* envp */ *const std::ffi::c_void,
    );
    pub fn execve(
        ctx: &mut SyscallContext,
        pathname: ForeignPtr<std::ffi::c_char>,
        argv: ForeignPtr<ForeignPtr<std::ffi::c_char>>,
        envp: ForeignPtr<ForeignPtr<std::ffi::c_char>>,
    ) -> Result<i64, SyscallError> {
        let mut path_buf = [0u8; linux_api::limits::PATH_MAX];
        let path_buf_capacity = path_buf.len();
        let path = ctx.objs.process.memory_borrow().copy_str_from_ptr(
            &mut path_buf,
            ForeignArrayPtr::new(pathname.cast::<u8>(), path_buf_capacity),
        )?;

        Self::execve_common(
            ctx,
            &ctx.objs.process.current_working_dir(),
            path,
            argv,
            envp,
            0,
        )
        .map(|_| 0)
    }

    log_syscall!(
        execveat,
        /* rv */ i32,
        /* dirfd */ std::ffi::c_int,
        /* pathname */ SyscallStringArg,
        /* argv */ *const std::ffi::c_void,
        /* envp */ *const std::ffi::c_void,
        /* flags */ std::ffi::c_int,
    );
    pub fn execveat(
        _ctx: &mut SyscallContext,
        _dirfd: std::ffi::c_int,
        _pathname: ForeignPtr<std::ffi::c_char>,
        _argv: ForeignPtr<ForeignPtr<std::ffi::c_char>>,
        _envp: ForeignPtr<ForeignPtr<std::ffi::c_char>>,
        _flags: std::ffi::c_int,
    ) -> Result<i64, SyscallError> {
        // TODO: Implement resolution of the path to the executable,
        // and then call `execve_common` with that.
        Err(Errno::ENOSYS.into())
    }

    log_syscall!(
        exit_group,
        /* rv */ std::ffi::c_int,
        /* error_code */ std::ffi::c_int,
    );
    pub fn exit_group(
        _ctx: &mut SyscallContext,
        error_code: std::ffi::c_int,
    ) -> Result<(), SyscallError> {
        log::trace!("Exit group with exit code {error_code}");
        Err(SyscallError::Native)
    }

    log_syscall!(
        set_tid_address,
        /* rv */ linux_api::posix_types::kernel_pid_t,
        /* tidptr */ *const std::ffi::c_int,
    );
    pub fn set_tid_address(
        ctx: &mut SyscallContext,
        tid_ptr: ForeignPtr<std::ffi::c_int>,
    ) -> Result<kernel_pid_t, SyscallError> {
        ctx.objs
            .thread
            .set_tid_address(tid_ptr.cast::<libc::pid_t>());
        Ok(ctx.objs.thread.id().into())
    }

    log_syscall!(
        uname,
        /* rv */ std::ffi::c_int,
        /* name */ *const std::ffi::c_void,
    );
    pub fn uname(
        ctx: &mut SyscallContext,
        name_ptr: ForeignPtr<linux_api::utsname::new_utsname>,
    ) -> Result<(), SyscallError> {
        // NOTE: On linux x86-64, `SYS_uname` corresponds with `__NR_uname` which calls
        // `sys_newuname` and not `sys_uname`. The correct mapping is:
        //
        // - __NR_oldolduname -> sys_olduname
        // - __NR_olduname -> sys_uname
        // - __NR_uname -> sys_newuname
        //
        // Some online resources such as the chromium syscall table are incorrect.

        let mut name: linux_api::utsname::new_utsname = shadow_pod::zeroed();

        let nodename = u8_to_i8_slice(ctx.objs.host.info().name.as_bytes());

        let sysname = u8_to_i8_slice(&b"shadowsys"[..]);
        let release = u8_to_i8_slice(&b"shadowrelease"[..]);
        let version = u8_to_i8_slice(&b"shadowversion"[..]);
        let machine = u8_to_i8_slice(&b"shadowmachine"[..]);

        name.sysname[..sysname.len()].copy_from_slice(sysname);
        name.nodename[..nodename.len()].copy_from_slice(nodename);
        name.release[..release.len()].copy_from_slice(release);
        name.version[..version.len()].copy_from_slice(version);
        name.machine[..machine.len()].copy_from_slice(machine);

        ctx.objs
            .process
            .memory_borrow_mut()
            .write(name_ptr, &name)?;

        Ok(())
    }

    log_syscall!(
        chdir,
        /* rv */ std::ffi::c_int,
        /* path */ SyscallStringArg,
    );
    pub fn chdir(
        ctx: &mut SyscallContext,
        path: ForeignPtr<std::ffi::c_char>,
    ) -> Result<(), SyscallError> {
        // The native working directory must match the emulated one
        // <https://github.com/shadow/shadow/issues/2960>. First execute the
        // native chdir, propagating any failures.
        let (process, thread) = ctx.objs.split_thread();
        thread.native_chdir(&process, path)?;

        // Update our internal copy of the cwd.
        //
        // We could try to work it out ourselves based on the previous cwd and
        // the path we were passed, but this seems a bit tricky and error-prone.
        //
        // We could have the managed thread execute a native `getcwd`, but we'd
        // also need to have it allocate and free memory to use with it, making
        // this a bit complex and high overhead.
        //
        // Instead we use the proc file system. `/proc/<pid>/cwd` should be a
        // symbolic link to the actual working dir we just set.
        let procpath = format!("/proc/{}/cwd", thread.native_tid().as_raw_nonzero().get());
        let newcwd = std::fs::read_link(&procpath)
            .unwrap_or_else(|e| panic!("Couldn't find new cwd {procpath}: {e:?}"));
        let mut newcwd = newcwd.into_os_string().into_vec();
        newcwd.push(0);
        let newcwd = CString::from_vec_with_nul(newcwd).unwrap();
        process.process.set_current_working_dir(newcwd);
        Ok(())
    }
}
