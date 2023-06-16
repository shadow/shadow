use std::sync::Arc;

use linux_api::errno::Errno;
use linux_api::fcntl::DescriptorFlags;
use shadow_shim_helper_rs::simulation_time::SimulationTime;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;
use syscall_logger::log_syscall;

use crate::core::worker::Worker;
use crate::cshadow as c;
use crate::host::descriptor::epoll::Epoll;
use crate::host::descriptor::{CompatFile, Descriptor, File, FileState, OpenFile};
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall_types::SyscallError;
use crate::host::thread::Thread;
use crate::utility::callback_queue::CallbackQueue;

impl SyscallHandler {
    #[log_syscall(/* rv */ std::ffi::c_int, /* size */ std::ffi::c_int)]
    pub fn epoll_create(
        ctx: &mut SyscallContext,
        size: std::ffi::c_int,
    ) -> Result<std::ffi::c_int, SyscallError> {
        // epoll_create(2): "Since Linux 2.6.8, the size argument is ignored, but must be greater
        // than zero"
        if size <= 0 {
            return Err(Errno::EINVAL.into());
        }

        Self::epoll_create_helper(ctx, 0)
    }

    #[log_syscall(/* rv */ std::ffi::c_int, /* flags */ std::ffi::c_int)]
    pub fn epoll_create1(
        ctx: &mut SyscallContext,
        flags: std::ffi::c_int,
    ) -> Result<std::ffi::c_int, SyscallError> {
        Self::epoll_create_helper(ctx, flags)
    }

    fn epoll_create_helper(
        ctx: &mut SyscallContext,
        flags: std::ffi::c_int,
    ) -> Result<std::ffi::c_int, SyscallError> {
        // only one flag is supported
        if ![0, libc::EPOLL_CLOEXEC].contains(&flags) {
            return Err(Errno::EINVAL.into());
        }

        let mut descriptor_flags = DescriptorFlags::empty();

        if flags & libc::EPOLL_CLOEXEC != 0 {
            descriptor_flags.insert(DescriptorFlags::FD_CLOEXEC);
        }

        let epoll = Epoll::new();

        let mut desc = Descriptor::new(CompatFile::New(OpenFile::new(File::Epoll(epoll))));
        desc.set_flags(descriptor_flags);

        let fd = ctx
            .objs
            .thread
            .descriptor_table_borrow_mut(ctx.objs.host)
            .register_descriptor(desc)
            .or(Err(Errno::ENFILE))?;

        log::trace!("Created epoll fd {}", fd);

        Ok(fd.val().try_into().unwrap())
    }

    #[log_syscall(/* rv */ std::ffi::c_int, /* epfd */ std::ffi::c_int, /* op */ std::ffi::c_int,
                  /* fd */ std::ffi::c_int, /* event */ *const std::ffi::c_void)]
    pub fn epoll_ctl(
        ctx: &mut SyscallContext,
        epfd: std::ffi::c_int,
        op: std::ffi::c_int,
        fd: std::ffi::c_int,
        event_ptr: ForeignPtr<libc::epoll_event>,
    ) -> Result<std::ffi::c_int, SyscallError> {
        // return EINVAL if fd is the same as epfd
        if epfd == fd {
            return Err(Errno::EINVAL.into());
        }

        // get the descriptor, or return early if it doesn't exist
        let desc_table = ctx.objs.thread.descriptor_table_borrow(ctx.objs.host);
        let epoll_desc = Self::get_descriptor(&desc_table, epfd)?;

        let CompatFile::New(epoll) = epoll_desc.file() else {
            return Err(Errno::EINVAL.into());
        };

        let File::Epoll(epoll) = epoll.inner_file() else {
            return Err(Errno::EINVAL.into());
        };

        let target_desc = Self::get_descriptor(&desc_table, fd)?;
        /*
        let target = target_desc.file().clone();

        // make sure the child is not closed only if it's a legacy file
        if let CompatFile::Legacy(file) = &target {
            if unsafe { c::legacyfile_getStatus(file.ptr()) } & c::_Status_STATUS_FILE_CLOSED != 0 {
                log::debug!("Child {fd} of epoll {epfd} is closed");
                return Err(Errno::EINVAL.into());
            }
        };
        */

        if let CompatFile::Legacy(file) = target_desc.file() {
            // epoll doesn't support regular files
            if unsafe { c::legacyfile_getType(file.ptr()) } == c::_LegacyFileType_DT_FILE {
                return Err(Errno::EPERM.into());
            }
        };

        // note: we don't support legacy C files, but the only `LegacyFile` in shadow currently is
        // `RegularFile`, which isn't supported by epoll anyway
        let CompatFile::New(target) = target_desc.file() else {
            warn_once_then_trace!(
                "(LOG_ONCE) Attempted to add a legacy file to an \
                epoll file, which shadow doesn't support"
            );
            return Err(Errno::EINVAL.into());
        };

        let target = target.inner_file().clone();

        let mem = ctx.objs.process.memory_borrow();
        let event = mem.read(event_ptr)?;

        log::trace!("Calling epoll_ctl on epoll {epfd} with child {fd}");

        crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
            CallbackQueue::queue_and_run(|cb_queue| {
                epoll.borrow_mut().ctl(op, fd, target, event, cb_queue)
            })
        })
    }

    #[log_syscall(/* rv */ std::ffi::c_int, /* epfd */ std::ffi::c_int,
                  /* events */ *const std::ffi::c_void, /* max_events */ std::ffi::c_int,
                  /* timeout */ std::ffi::c_int)]
    pub fn epoll_wait(
        ctx: &mut SyscallContext,
        epfd: std::ffi::c_int,
        events_ptr: ForeignPtr<libc::epoll_event>,
        max_events: std::ffi::c_int,
        timeout: std::ffi::c_int,
    ) -> Result<std::ffi::c_int, SyscallError> {
        // epoll_wait(2): "Specifying a timeout of -1 causes epoll_wait() to block indefinitely"
        let timeout = (timeout >= 0).then_some(timeout);

        let timeout = if let Some(timeout) = timeout {
            // a non-negative c_int should always convert to a u64
            let timeout = timeout.try_into().unwrap();
            let timeout = SimulationTime::try_from_millis(timeout).ok_or(Errno::EINVAL)?;
            Some(timeout)
        } else {
            None
        };

        Self::epoll_wait_helper(ctx, epfd, events_ptr, max_events, timeout, None)
    }

    #[log_syscall(/* rv */ std::ffi::c_int, /* epfd */ std::ffi::c_int,
                  /* events */ *const std::ffi::c_void, /* max_events */ std::ffi::c_int,
                  /* timeout */ std::ffi::c_int, /* sigmask */ *const std::ffi::c_void)]
    pub fn epoll_pwait(
        ctx: &mut SyscallContext,
        epfd: std::ffi::c_int,
        events_ptr: ForeignPtr<libc::epoll_event>,
        max_events: std::ffi::c_int,
        timeout: std::ffi::c_int,
        sigmask_ptr: ForeignPtr<linux_api::signal::sigset_t>,
    ) -> Result<std::ffi::c_int, SyscallError> {
        // epoll_wait(2): "The sigmask argument may be specified as NULL, in which case
        // epoll_pwait() is equivalent to epoll_wait()"
        let sigmask = if sigmask_ptr.is_null() {
            None
        } else {
            Some(ctx.objs.process.memory_borrow().read(sigmask_ptr)?)
        };

        // epoll_wait(2): "Specifying a timeout of -1 causes epoll_wait() to block indefinitely"
        let timeout = (timeout >= 0).then_some(timeout);

        let timeout = if let Some(timeout) = timeout {
            // a non-negative c_int should always convert to a u64
            let timeout = timeout.try_into().unwrap();
            let timeout = SimulationTime::try_from_millis(timeout).ok_or(Errno::EINVAL)?;
            Some(timeout)
        } else {
            None
        };

        Self::epoll_wait_helper(ctx, epfd, events_ptr, max_events, timeout, sigmask)
    }

    #[log_syscall(/* rv */ std::ffi::c_int, /* epfd */ std::ffi::c_int,
                  /* events */ *const std::ffi::c_void, /* max_events */ std::ffi::c_int,
                  /* timeout */ *const std::ffi::c_void, /* sigmask */ *const std::ffi::c_void)]
    pub fn epoll_pwait2(
        ctx: &mut SyscallContext,
        epfd: std::ffi::c_int,
        events_ptr: ForeignPtr<libc::epoll_event>,
        max_events: std::ffi::c_int,
        timeout_ptr: ForeignPtr<linux_api::time::timespec>,
        sigmask_ptr: ForeignPtr<linux_api::signal::sigset_t>,
    ) -> Result<std::ffi::c_int, SyscallError> {
        let timeout;
        let sigmask;

        {
            let mem = ctx.objs.process.memory_borrow();

            // epoll_wait(2): "The sigmask argument may be specified as NULL, in which case
            // epoll_pwait() is equivalent to epoll_wait()"
            if sigmask_ptr.is_null() {
                sigmask = None;
            } else {
                sigmask = Some(mem.read(sigmask_ptr)?);
            }

            // epoll_wait(2): "If timeout is NULL, then epoll_pwait2() can block indefinitely"
            if timeout_ptr.is_null() {
                timeout = None;
            } else {
                timeout = Some(mem.read(timeout_ptr)?);
            }
        }

        let timeout = timeout
            .map(TryInto::try_into)
            .transpose()
            .or(Err(Errno::EINVAL))?;

        Self::epoll_wait_helper(ctx, epfd, events_ptr, max_events, timeout, sigmask)
    }

    fn epoll_wait_helper(
        ctx: &mut SyscallContext,
        epfd: std::ffi::c_int,
        events_ptr: ForeignPtr<libc::epoll_event>,
        max_events: std::ffi::c_int,
        timeout: Option<SimulationTime>,
        sigmask: Option<linux_api::signal::sigset_t>,
    ) -> Result<std::ffi::c_int, SyscallError> {
        if max_events <= 0 {
            log::trace!("Epoll maxevents {max_events} is not greater than 0");
            return Err(Errno::EINVAL.into());
        }

        let max_events: u32 = max_events.try_into().unwrap();

        // TODO: support the signal mask
        if sigmask.is_some() {
            warn_once_then_trace!(
                "Epoll pwait called with non-null sigmask, \
                which is not yet supported by shadow; returning EINVAL"
            );
            return Err(Errno::EINVAL.into());
        }

        // get the descriptor, or return early if it doesn't exist
        let desc_table = ctx.objs.thread.descriptor_table_borrow(ctx.objs.host);
        let epoll_desc = Self::get_descriptor(&desc_table, epfd)?;

        let CompatFile::New(epoll) = epoll_desc.file() else {
            return Err(Errno::EINVAL.into());
        };

        let File::Epoll(epoll) = epoll.inner_file() else {
            return Err(Errno::EINVAL.into());
        };

        // figure out how many events we actually have
        let num_events_ready = epoll.borrow().num_events_ready();
        log::trace!("Epoll {epfd} says {num_events_ready} events are ready");

        // if no events are ready, our behavior depends on timeout
        if num_events_ready == 0 {
            // return immediately if timeout is 0
            if let Some(timeout) = timeout {
                if timeout.is_zero() {
                    log::trace!("No events are ready on epoll {epfd} and the timeout is 0");
                    return Ok(0);
                }
            }

            // return immediately if we were already blocked for a while and still have no events
            if timeout_expired(ctx.objs.thread) {
                log::trace!("No events are ready on epoll {epfd} and the timeout expired");
                return Ok(0);
            }

            // if there's a signal pending, this syscall will be interrupted
            if ctx.objs.thread.unblocked_signal_pending(
                ctx.objs.process,
                &ctx.objs.host.shim_shmem_lock_borrow().unwrap(),
            ) {
                return Err(SyscallError::new_interrupted(false));
            }

            // convert timeout to an EmulatedTime
            let Ok(timeout) = timeout
                .map(|x| Worker::current_time().unwrap().checked_add(x).ok_or(()))
                .transpose()
            else {
                log::trace!("Epoll wait with invalid timeout {timeout:?} (too large)");
                return Err(Errno::EINVAL.into());
            };

            log::trace!("No events are ready on epoll {epfd} and we need to block");

            // block on epoll status; an epoll descriptor is readable when it has events
            let mut rv = SyscallError::new_blocked(
                File::Epoll(Arc::clone(epoll)),
                FileState::READABLE,
                /* restartable= */ false,
            );

            // set timeout, if provided
            rv.blocked_condition().unwrap().set_timeout(timeout);

            return Err(rv);
        }

        let mut mem = ctx.objs.process.memory_borrow_mut();

        // we have events, so get them
        let rv = epoll.borrow().get_events(events_ptr, max_events, &mut mem);
        rv
    }
}

fn timeout_expired(thread: &Thread) -> bool {
    let cond = thread.syscall_condition().unwrap();
    let timeout = cond.timeout();

    let Some(timeout) = timeout else {
        // there is no timeout
        return false;
    };

    Worker::current_time().unwrap() >= timeout
}
