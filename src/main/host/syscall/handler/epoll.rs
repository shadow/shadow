use std::ops::DerefMut;
use std::sync::Arc;

use linux_api::epoll::{EpollCreateFlags, EpollCtlOp, EpollEvents};
use linux_api::errno::Errno;
use linux_api::fcntl::DescriptorFlags;
use shadow_shim_helper_rs::simulation_time::SimulationTime;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::core::worker::Worker;
use crate::cshadow;
use crate::host::descriptor::descriptor_table::DescriptorHandle;
use crate::host::descriptor::epoll::Epoll;
use crate::host::descriptor::{CompatFile, Descriptor, File, FileState, OpenFile};
use crate::host::memory_manager::MemoryManager;
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::types::{ForeignArrayPtr, SyscallError};
use crate::utility::callback_queue::CallbackQueue;

impl SyscallHandler {
    log_syscall!(
        epoll_create,
        /* rv */ std::ffi::c_int,
        /* size */ std::ffi::c_int,
    );
    pub fn epoll_create(
        ctx: &mut SyscallContext,
        size: std::ffi::c_int,
    ) -> Result<DescriptorHandle, Errno> {
        // epoll_create(2): "Since Linux 2.6.8, the size argument is ignored, but must be greater
        // than zero"
        if size <= 0 {
            return Err(Errno::EINVAL);
        }

        Self::epoll_create_helper(ctx, 0)
    }

    log_syscall!(
        epoll_create1,
        /* rv */ std::ffi::c_int,
        /* flags */ std::ffi::c_int,
    );
    pub fn epoll_create1(
        ctx: &mut SyscallContext,
        flags: std::ffi::c_int,
    ) -> Result<DescriptorHandle, Errno> {
        Self::epoll_create_helper(ctx, flags)
    }

    fn epoll_create_helper(
        ctx: &mut SyscallContext,
        flags: std::ffi::c_int,
    ) -> Result<DescriptorHandle, Errno> {
        // See here for the order that the input args are checked in Linux:
        // https://github.com/torvalds/linux/blob/2cf0f715623872823a72e451243bbf555d10d032/fs/eventpoll.c#L2030
        let Some(flags) = EpollCreateFlags::from_bits(flags) else {
            log::debug!("Invalid epoll_create flags: {flags}");
            return Err(Errno::EINVAL);
        };

        let mut desc_flags = DescriptorFlags::empty();

        if flags.contains(EpollCreateFlags::EPOLL_CLOEXEC) {
            desc_flags.insert(DescriptorFlags::FD_CLOEXEC);
        }

        let epoll = Epoll::new();
        let mut desc = Descriptor::new(CompatFile::New(OpenFile::new(File::Epoll(epoll))));
        desc.set_flags(desc_flags);

        let fd = ctx
            .objs
            .thread
            .descriptor_table_borrow_mut(ctx.objs.host)
            .register_descriptor(desc)
            .or(Err(Errno::ENFILE))?;

        log::trace!("Created epoll fd {fd}");

        Ok(fd)
    }

    log_syscall!(
        epoll_ctl,
        /* rv */ std::ffi::c_int,
        /* epfd */ std::ffi::c_int,
        /* op */ std::ffi::c_int,
        /* fd */ std::ffi::c_int,
        /* event */ *const std::ffi::c_void,
    );
    pub fn epoll_ctl(
        ctx: &mut SyscallContext,
        epfd: std::ffi::c_int,
        op: std::ffi::c_int,
        fd: std::ffi::c_int,
        event_ptr: ForeignPtr<linux_api::epoll::epoll_event>,
    ) -> Result<(), Errno> {
        // See here for the order that the input args are checked in Linux:
        // https://github.com/torvalds/linux/blob/2cf0f715623872823a72e451243bbf555d10d032/fs/eventpoll.c#L2111

        // We'll need to look up descriptors.
        let desc_table = ctx.objs.thread.descriptor_table_borrow(ctx.objs.host);

        // Get the epoll descriptor, or return early if it doesn't exist.
        let (epoll, epoll_canon_handle) = {
            let desc = Self::get_descriptor(&desc_table, epfd)?;

            let CompatFile::New(epoll) = desc.file() else {
                return Err(Errno::EINVAL);
            };

            let epoll_canon_handle = epoll.inner_file().canonical_handle();

            let File::Epoll(epoll) = epoll.inner_file() else {
                return Err(Errno::EINVAL);
            };

            (epoll, epoll_canon_handle)
        };

        // Get the target descriptor, or return errors as appropriate.
        let target = {
            let desc = Self::get_descriptor(&desc_table, fd)?;

            // Our epoll implementation only supports adding new Rust descriptor types.
            // However, the only legacy type remaining in Shadow is a regular file, and
            // epoll_ctl(2) states that EPERM should be returned for regular files and
            // other files that don't support epolling.
            match desc.file() {
                CompatFile::New(file) => file.inner_file().clone(),
                CompatFile::Legacy(file) => {
                    let file_type = unsafe { cshadow::legacyfile_getType(file.ptr()) };
                    if file_type == cshadow::_LegacyFileType_DT_FILE {
                        // Epoll doesn't support regular files.
                        return Err(Errno::EPERM);
                    } else {
                        // Our implementation doesn't support other legacy types.
                        // We don't think we have such types remaining, but warn anyway.
                        warn_once_then_trace!(
                            "Attempted to add a legacy file to an epoll file, which \
                            shadow doesn't support"
                        );
                        return Err(Errno::EINVAL);
                    }
                }
            }
        };

        // An epoll instance is not allowed to monitor itself.
        if epoll_canon_handle == target.canonical_handle() {
            return Err(Errno::EINVAL);
        }

        // Extract the operation.
        let Ok(op) = EpollCtlOp::try_from(op) else {
            log::debug!("Invalid epoll op: {op}");
            return Err(Errno::EINVAL);
        };

        // Extract the events and data.
        let (events, data) = if op == EpollCtlOp::EPOLL_CTL_DEL {
            // epoll_ctl(2): Since Linux 2.6.9, the event pointer is ignored and can be specified as
            // NULL when using EPOLL_CTL_DEL.
            (EpollEvents::empty(), 0)
        } else {
            let mem = ctx.objs.process.memory_borrow();
            let ev = mem.read(event_ptr)?;

            let Some(mut events) = EpollEvents::from_bits(ev.events) else {
                // Braces are needed around `ev.events` for alignment (see rustc --explain E0793).
                log::debug!("Invalid epoll_ctl events: {}", { ev.events });
                return Err(Errno::EINVAL);
            };

            // epoll_ctl(2): epoll always reports for EPOLLERR and EPOLLHUP
            events.insert(EpollEvents::EPOLLERR | EpollEvents::EPOLLHUP);

            (events, ev.data)
        };

        log::trace!("Calling epoll_ctl on epoll {epfd} with child {fd}");

        crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
            CallbackQueue::queue_and_run(|cb_queue| {
                let weak_epoll = Arc::downgrade(epoll);
                epoll
                    .borrow_mut()
                    .ctl(op, fd, target, events, data, weak_epoll, cb_queue)
            })
        })?;
        Ok(())
    }

    log_syscall!(
        epoll_wait,
        /* rv */ std::ffi::c_int,
        /* epfd */ std::ffi::c_int,
        /* events */ *const std::ffi::c_void,
        /* max_events */ std::ffi::c_int,
        /* timeout */ std::ffi::c_int,
    );
    pub fn epoll_wait(
        ctx: &mut SyscallContext,
        epfd: std::ffi::c_int,
        events_ptr: ForeignPtr<linux_api::epoll::epoll_event>,
        max_events: std::ffi::c_int,
        timeout: std::ffi::c_int,
    ) -> Result<std::ffi::c_int, SyscallError> {
        // Note that timeout is given in milliseconds.
        let timeout = timeout_arg_to_maybe_simtime(timeout)?;
        Self::epoll_wait_helper(ctx, epfd, events_ptr, max_events, timeout, None)
    }

    log_syscall!(
        epoll_pwait,
        /* rv */ std::ffi::c_int,
        /* epfd */ std::ffi::c_int,
        /* events */ *const std::ffi::c_void,
        /* max_events */ std::ffi::c_int,
        /* timeout */ std::ffi::c_int,
        /* sigmask */ *const std::ffi::c_void,
        /* sigsetsize */ linux_api::posix_types::kernel_size_t,
    );
    pub fn epoll_pwait(
        ctx: &mut SyscallContext,
        epfd: std::ffi::c_int,
        events_ptr: ForeignPtr<linux_api::epoll::epoll_event>,
        max_events: std::ffi::c_int,
        timeout: std::ffi::c_int,
        sigmask_ptr: ForeignPtr<linux_api::signal::sigset_t>,
        _sigsetsize: linux_api::posix_types::kernel_size_t,
    ) -> Result<std::ffi::c_int, SyscallError> {
        // epoll_wait(2): "The sigmask argument may be specified as NULL, in which case
        // epoll_pwait() is equivalent to epoll_wait()"
        let sigmask = if sigmask_ptr.is_null() {
            None
        } else {
            Some(ctx.objs.process.memory_borrow().read(sigmask_ptr)?)
        };

        // Note that timeout is given in milliseconds.
        let timeout = timeout_arg_to_maybe_simtime(timeout)?;
        Self::epoll_wait_helper(ctx, epfd, events_ptr, max_events, timeout, sigmask)
    }

    log_syscall!(
        epoll_pwait2,
        /* rv */ std::ffi::c_int,
        /* epfd */ std::ffi::c_int,
        /* events */ *const std::ffi::c_void,
        /* max_events */ std::ffi::c_int,
        /* timeout */ *const std::ffi::c_void,
        /* sigmask */ *const std::ffi::c_void,
        /* sigsetsize */ linux_api::posix_types::kernel_size_t,
    );
    pub fn epoll_pwait2(
        ctx: &mut SyscallContext,
        epfd: std::ffi::c_int,
        events_ptr: ForeignPtr<linux_api::epoll::epoll_event>,
        max_events: std::ffi::c_int,
        timeout_ptr: ForeignPtr<linux_api::time::timespec>,
        sigmask_ptr: ForeignPtr<linux_api::signal::sigset_t>,
        _sigsetsize: linux_api::posix_types::kernel_size_t,
    ) -> Result<std::ffi::c_int, SyscallError> {
        let (sigmask, timeout) = {
            let mem = ctx.objs.process.memory_borrow();

            // epoll_wait(2): "The sigmask argument may be specified as NULL, in which case
            // epoll_pwait() is equivalent to epoll_wait()"
            let sigmask = if sigmask_ptr.is_null() {
                None
            } else {
                Some(mem.read(sigmask_ptr)?)
            };

            // epoll_wait(2): "If timeout is NULL, then epoll_pwait2() can block indefinitely"
            let timeout = if timeout_ptr.is_null() {
                None
            } else {
                let tspec = mem.read(timeout_ptr)?;
                let sim_time = SimulationTime::try_from(tspec).map_err(|_| Errno::EINVAL)?;
                Some(sim_time)
            };

            (sigmask, timeout)
        };

        Self::epoll_wait_helper(ctx, epfd, events_ptr, max_events, timeout, sigmask)
    }

    fn epoll_wait_helper(
        ctx: &mut SyscallContext,
        epfd: std::ffi::c_int,
        events_ptr: ForeignPtr<linux_api::epoll::epoll_event>,
        max_events: std::ffi::c_int,
        timeout: Option<SimulationTime>,
        sigmask: Option<linux_api::signal::sigset_t>,
    ) -> Result<std::ffi::c_int, SyscallError> {
        // Linux enforces a range for max_events.
        let max_events = {
            let upper_bound = epoll_max_events_upper_bound();

            if max_events <= 0 || max_events > upper_bound {
                log::trace!(
                    "Epoll maxevents {max_events} is not greater than 0 \
                            and less than {upper_bound}"
                );
                return Err(Errno::EINVAL.into());
            }

            u32::try_from(max_events).unwrap()
        };

        // TODO: support the signal mask
        if sigmask.is_some() {
            warn_once_then_trace!(
                "Epoll pwait called with non-null sigmask, \
                which is not yet supported by shadow; returning EINVAL"
            );
            return Err(Errno::EINVAL.into());
        }

        // Get the descriptor, or return early if it doesn't exist.
        let desc_table = ctx.objs.thread.descriptor_table_borrow(ctx.objs.host);
        let epoll = {
            let desc = Self::get_descriptor(&desc_table, epfd)?;

            let CompatFile::New(epoll) = desc.file() else {
                return Err(Errno::EINVAL.into());
            };

            let File::Epoll(epoll) = epoll.inner_file() else {
                return Err(Errno::EINVAL.into());
            };

            epoll
        };

        if epoll.borrow().has_ready_events() {
            log::trace!("Epoll {epfd} has ready events");

            // We must not return an error after collecting events from epoll, otherwise the epoll
            // state will become inconsitent with the view of events from the managed process.
            // Thus, we explicitly check that we have a valid location to return the events before
            // we collect them from epoll.
            if events_ptr.is_null() {
                return Err(Errno::EFAULT.into());
            }

            // After we collect the events here, failing to write them out to the events_ptr
            // ForeignPointer below will leave our event state inconsistent with the managed
            // process's understanding of the available events.
            let ready = CallbackQueue::queue_and_run(|cb_queue| {
                epoll
                    .borrow_mut()
                    .collect_ready_events(cb_queue, max_events)
            });
            let n_ready = ready.len();
            if n_ready > max_events as usize {
                panic!("Epoll should not return more than {max_events} events");
            }

            // Write the events out to the managed process memory.
            let mut mem = ctx.objs.process.memory_borrow_mut();
            write_events_to_ptr(&mut mem, ready, events_ptr)?;

            // Return the number of events we are reporting.
            log::trace!("Epoll {epfd} returning {n_ready} events");
            return Ok(n_ready.try_into().unwrap());
        }

        // Our behavior depends on the value of timeout.
        // Return immediately if timeout is 0.
        if let Some(timeout) = timeout {
            if timeout.is_zero() {
                log::trace!("No events are ready on epoll {epfd} and the timeout is 0");
                return Ok(0);
            }
        }

        // Return immediately if we were already blocked for a while and still have no events.
        // Condition will only exist after a wakeup.
        if let Some(cond) = ctx.objs.thread.syscall_condition() {
            if let Some(abs_timeout) = cond.timeout() {
                if Worker::current_time().unwrap() >= abs_timeout {
                    log::trace!("No events are ready on epoll {epfd} and the timeout expired");
                    return Ok(0);
                }
            }
        }

        // If there's a signal pending, this syscall will be interrupted.
        if ctx.objs.thread.unblocked_signal_pending(
            ctx.objs.process,
            &ctx.objs.host.shim_shmem_lock_borrow().unwrap(),
        ) {
            return Err(SyscallError::new_interrupted(false));
        }

        // Convert timeout to an EmulatedTime.
        let Ok(abs_timeout_opt) = timeout
            .map(|x| Worker::current_time().unwrap().checked_add(x).ok_or(()))
            .transpose()
        else {
            log::trace!("Epoll wait with invalid timeout {timeout:?} (too large)");
            return Err(Errno::EINVAL.into());
        };

        log::trace!("No events are ready on epoll {epfd} and we need to block");

        // Block on epoll state; an epoll descriptor is readable when it has events.
        let mut rv = SyscallError::new_blocked_on_file(
            File::Epoll(Arc::clone(epoll)),
            FileState::READABLE,
            /* restartable= */ false,
        );

        // Set timeout, if provided.
        if abs_timeout_opt.is_some() {
            rv.blocked_condition().unwrap().set_timeout(abs_timeout_opt);
        }

        Err(rv)
    }
}

fn timeout_arg_to_maybe_simtime(
    timeout_ms: std::ffi::c_int,
) -> Result<Option<SimulationTime>, Errno> {
    // epoll_wait(2): "Specifying a timeout of -1 causes epoll_wait() to block indefinitely"
    let timeout_ms = (timeout_ms >= 0).then_some(timeout_ms);

    if let Some(timeout_ms) = timeout_ms {
        // a non-negative c_int should always convert to a u64
        let timeout_ms = timeout_ms.try_into().unwrap();
        let timeout = SimulationTime::try_from_millis(timeout_ms).ok_or(Errno::EINVAL)?;
        Ok(Some(timeout))
    } else {
        Ok(None)
    }
}

/// There is a maximum number of events that can be specified in Linux:
/// https://github.com/torvalds/linux/blob/2cf0f715623872823a72e451243bbf555d10d032/fs/eventpoll.c#L2291
///
/// The maximum is defined as:
///   `#define EP_MAX_EVENTS (INT_MAX / sizeof(struct epoll_event))`
/// https://github.com/torvalds/linux/blob/2cf0f715623872823a72e451243bbf555d10d032/fs/eventpoll.c#L95
///
/// This function performs the above computation as Linux does.
fn epoll_max_events_upper_bound() -> i32 {
    let ep_max_events = i32::MAX;
    let ep_ev_size: i32 = std::mem::size_of::<linux_api::epoll::epoll_event>()
        .try_into()
        .unwrap_or(i32::MAX);
    ep_max_events.saturating_div(ep_ev_size)
}

fn write_events_to_ptr(
    mem: &mut MemoryManager,
    ready: Vec<(EpollEvents, u64)>,
    events_ptr: ForeignPtr<linux_api::epoll::epoll_event>,
) -> Result<(), Errno> {
    let events_ptr = ForeignArrayPtr::new(events_ptr, ready.len());
    let mut mem_ref = mem.memory_ref_mut(events_ptr)?;

    for ((ev, data), plugin_ev) in ready.iter().zip(mem_ref.deref_mut().iter_mut()) {
        plugin_ev.events = ev.bits();
        plugin_ev.data = *data;
    }

    mem_ref.flush()?;

    Ok(())
}
