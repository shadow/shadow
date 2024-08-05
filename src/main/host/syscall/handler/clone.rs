use linux_api::capability::{user_cap_data, user_cap_header, LINUX_CAPABILITY_VERSION_3};
use linux_api::errno::Errno;
use linux_api::posix_types::kernel_pid_t;
use linux_api::sched::CloneFlags;
use linux_api::signal::Signal;
use log::{debug, trace, warn};
use shadow_shim_helper_rs::explicit_drop::ExplicitDropper;
use shadow_shim_helper_rs::rootedcell::rc::RootedRc;
use shadow_shim_helper_rs::rootedcell::refcell::RootedRefCell;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::host::descriptor::descriptor_table::DescriptorTable;
use crate::host::process::ProcessId;
use crate::host::thread::Thread;

use super::{SyscallContext, SyscallHandler};

impl SyscallHandler {
    fn clone_internal(
        ctx: &mut SyscallContext,
        flags: CloneFlags,
        exit_signal: Option<Signal>,
        child_stack: ForeignPtr<()>,
        ptid: ForeignPtr<kernel_pid_t>,
        ctid: ForeignPtr<kernel_pid_t>,
        newtls: u64,
    ) -> Result<kernel_pid_t, Errno> {
        // We use this for a consistency check to validate that we've inspected
        // and emulated all of the provided flags.
        let mut handled_flags = CloneFlags::empty();

        // The parameters that we'll pass to the native clone call.
        let mut native_flags = CloneFlags::empty();

        // We emulate the flags that would use these, so we always pass NULL to
        // the native call.
        let native_ctid = ForeignPtr::<kernel_pid_t>::null();
        let native_ptid = ForeignPtr::<kernel_pid_t>::null();

        // We use the managed-code provided stack.
        let native_child_stack = child_stack;

        // We use the managed-code provided newtls.
        let native_newtls = newtls;

        if flags.contains(CloneFlags::CLONE_THREAD) {
            // From clone(2):
            // > Since Linux 2.5.35, the flags mask must also include
            // > CLONE_SIGHAND if CLONE_THREAD is specified
            if !flags.contains(CloneFlags::CLONE_SIGHAND) {
                debug!("Missing CLONE_SIGHAND");
                return Err(Errno::EINVAL);
            }
            if !flags.contains(CloneFlags::CLONE_SETTLS) {
                // Legal in Linux, but the shim will be broken and behave unpredictably.
                warn!("CLONE_THREAD without CLONE_TLS not supported by shadow");
                return Err(Errno::ENOTSUP);
            }
            if exit_signal.is_some() {
                warn!("Exit signal is unimplemented");
                return Err(Errno::ENOTSUP);
            }
            // The native clone call will:
            // - create a thread.
            native_flags.insert(CloneFlags::CLONE_THREAD);
            // - share signal handlers (mandatory anyway)
            native_flags.insert(CloneFlags::CLONE_SIGHAND);
            // - share file system info (mostly N/A for shadow, but conventional for threads)
            native_flags.insert(CloneFlags::CLONE_FS);
            // - share file descriptors
            native_flags.insert(CloneFlags::CLONE_FILES);
            // - share semaphores (mostly N/A for shadow, but conventional for threads)
            native_flags.insert(CloneFlags::CLONE_SYSVSEM);

            handled_flags.insert(CloneFlags::CLONE_THREAD);
        } else {
            if ctx.objs.process.memory_borrow().has_mapper() {
                warn!("Fork with memory mapper unimplemented");
                return Err(Errno::ENOTSUP);
            }
            // Make shadow the parent process
            native_flags.insert(CloneFlags::CLONE_PARENT);
        }

        if flags.contains(CloneFlags::CLONE_SIGHAND) {
            // From clone(2):
            // > Since Linux 2.6.0, the flags mask must also include CLONE_VM if
            // > CLONE_SIGHAND is specified
            if !flags.contains(CloneFlags::CLONE_VM) {
                debug!("Missing CLONE_VM");
                return Err(Errno::EINVAL);
            }
            // Currently a no-op since threads always share signal handlers,
            // and we don't yet support non-CLONE_THREAD.
            handled_flags.insert(CloneFlags::CLONE_SIGHAND);
        }

        if flags.contains(CloneFlags::CLONE_FS) {
            // Currently a no-op since we don't support the related
            // metadata and syscalls that this affects (e.g. chroot).
            handled_flags.insert(CloneFlags::CLONE_FS);
        }

        let desc_table = if flags.contains(CloneFlags::CLONE_FILES) {
            // Child gets a reference to the same table.
            RootedRc::clone(ctx.objs.thread.descriptor_table(), ctx.objs.host.root())
        } else {
            // Child gets a *copy* of the table.
            let root = ctx.objs.host.root();
            let table: DescriptorTable = ctx
                .objs
                .thread
                .descriptor_table_borrow(ctx.objs.host)
                .clone();
            RootedRc::new(root, RootedRefCell::new(root, table))
        };
        let desc_table = ExplicitDropper::new(desc_table, |desc_table| {
            desc_table.explicit_drop_recursive(ctx.objs.host.root(), ctx.objs.host);
        });
        handled_flags.insert(CloneFlags::CLONE_FILES);

        if flags.contains(CloneFlags::CLONE_SETTLS) {
            native_flags.insert(CloneFlags::CLONE_SETTLS);
            handled_flags.insert(CloneFlags::CLONE_SETTLS);
        }

        if flags.contains(CloneFlags::CLONE_VFORK) {
            // *Typically* `CLONE_VFORK|CLONE_VM` is used as a "faster fork", and
            // ignoring it will still work as intended.
            //
            // In principle this might not be true if the managed program
            // actually uses the shared memory with the parent process as a
            // "feature" and e.g. writes to non-scratch memory, expecting the
            // parent process to see those writes when it resumes.
            warn_once_then_debug!(
                "Ignoring CLONE_VFORK (and CLONE_VM if set). In *typical* usage this won't \
                result in incorrect behavior."
            );
            handled_flags.insert(CloneFlags::CLONE_VFORK);
        }

        if flags.contains(CloneFlags::CLONE_VM) {
            if flags.contains(CloneFlags::CLONE_THREAD) {
                native_flags.insert(CloneFlags::CLONE_VM);
            } else if flags.contains(CloneFlags::CLONE_VFORK) {
                // We already handled (warned) about this above.
            } else {
                // Haven't seen this in practice.
                //
                // Unclear that it'd be safe to ignore. Lack of CLONE_VFORK
                // (which normally pauses the parent until the child exec's or
                // exits) implies that this that the child may exist for more
                // than a brief window before exec'ing.
                warn!("CLONE_VM without CLONE_THREAD and without CLONE_VFORK unsupported");
                return Err(Errno::ENOTSUP);
            }
            handled_flags.insert(CloneFlags::CLONE_VM);
        }

        if flags.contains(CloneFlags::CLONE_SYSVSEM) {
            // Currently a no-op since we don't support sysv semaphores.
            handled_flags.insert(CloneFlags::CLONE_SYSVSEM);
        }

        // Handled after native clone
        let do_parent_settid = flags.contains(CloneFlags::CLONE_PARENT_SETTID);
        handled_flags.insert(CloneFlags::CLONE_PARENT_SETTID);

        // Handled after native clone
        let do_child_settid = flags.contains(CloneFlags::CLONE_CHILD_SETTID);
        handled_flags.insert(CloneFlags::CLONE_CHILD_SETTID);

        // Handled after native clone
        let do_child_cleartid = flags.contains(CloneFlags::CLONE_CHILD_CLEARTID);
        handled_flags.insert(CloneFlags::CLONE_CHILD_CLEARTID);

        let do_copy_sighandlers = if flags.contains(CloneFlags::CLONE_CLEAR_SIGHAND) {
            // clone(2): Specifying this flag together with CLONE_SIGHAND is
            // nonsensical and disallowed.
            if flags.contains(CloneFlags::CLONE_SIGHAND) {
                return Err(Errno::EINVAL);
            }
            false
        } else {
            // We only need to copy if they're not shared.
            !flags.contains(CloneFlags::CLONE_SIGHAND)
        };
        handled_flags.insert(CloneFlags::CLONE_CLEAR_SIGHAND);

        if flags.contains(CloneFlags::CLONE_PARENT) {
            // Handled in `new_forked_process` when creating a new process.
            // No-op when not creating a new process.
            handled_flags.insert(CloneFlags::CLONE_PARENT);
        }

        let unhandled_flags = flags.difference(handled_flags);
        if !unhandled_flags.is_empty() {
            warn!("Unhandled clone flags: {unhandled_flags:?}");
            return Err(Errno::ENOTSUP);
        }

        let child_mthread = ctx.objs.thread.mthread().native_clone(
            ctx.objs,
            native_flags,
            native_child_stack,
            native_ptid,
            native_ctid,
            native_newtls,
        )?;

        let child_tid = ctx.objs.host.get_new_thread_id();
        let child_pid = if flags.contains(CloneFlags::CLONE_THREAD) {
            ctx.objs.process.id()
        } else {
            ProcessId::from(child_tid)
        };

        let child_thread = Thread::wrap_mthread(
            ctx.objs.host,
            child_mthread,
            desc_table.into_value(),
            child_pid,
            child_tid,
        )?;

        let childrc = ExplicitDropper::new(
            RootedRc::new(
                ctx.objs.host.root(),
                RootedRefCell::new(ctx.objs.host.root(), child_thread),
            ),
            |childrc| {
                childrc.explicit_drop_recursive(ctx.objs.host.root(), ctx.objs.host);
            },
        );

        let child_process_rc;
        let child_process_borrow;
        let child_process;
        if flags.contains(CloneFlags::CLONE_THREAD) {
            child_process_borrow = None;
            child_process = ctx.objs.process;
            ctx.objs
                .process
                .add_thread(ctx.objs.host, childrc.into_value());
        } else {
            let process = ctx
                .objs
                .process
                .borrow_as_runnable()
                .unwrap()
                .new_forked_process(ctx.objs.host, flags, exit_signal, childrc.into_value());
            child_process_rc = Some(ExplicitDropper::new(
                process.clone(ctx.objs.host.root()),
                |x| {
                    x.explicit_drop_recursive(ctx.objs.host.root(), ctx.objs.host);
                },
            ));
            child_process_borrow = Some(
                child_process_rc
                    .as_ref()
                    .unwrap()
                    .borrow(ctx.objs.host.root()),
            );
            child_process = child_process_borrow.as_ref().unwrap();
            ctx.objs
                .host
                .add_and_schedule_forked_process(ctx.objs.host, process);
        }

        if do_parent_settid {
            ctx.objs
                .process
                .memory_borrow_mut()
                .write(ptid, &kernel_pid_t::from(child_tid))?;
        }

        if do_child_settid {
            // Set the child thread id in the child's memory.
            child_process
                .memory_borrow_mut()
                .write(ctid, &kernel_pid_t::from(child_tid))?;
        }

        if do_child_cleartid {
            let childrc = child_process.thread_borrow(child_tid).unwrap();
            let child = childrc.borrow(ctx.objs.host.root());
            child.set_tid_address(ctid);
        }

        if do_copy_sighandlers {
            let shmem_lock = ctx.objs.host.shim_shmem_lock_borrow_mut().unwrap();

            let parent_shmem = ctx.objs.process.shmem();
            let parent_shmem_prot = parent_shmem.protected.borrow(&shmem_lock.root);

            let child_shmem = child_process_borrow.as_ref().unwrap().shmem();
            let mut child_shmem_prot = child_shmem.protected.borrow_mut(&shmem_lock.root);
            // Safety: pointers in the parent are valid in the child.
            unsafe { child_shmem_prot.clone_signal_actions(&parent_shmem_prot) };
        }

        Ok(kernel_pid_t::from(child_tid))
    }

    // Note that the syscall args are different than the libc wrapper.
    // See "C library/kernel differences" in clone(2).
    log_syscall!(
        clone,
        /* rv */ kernel_pid_t,
        /* flags */ CloneFlags,
        /* child_stack */ *const std::ffi::c_void,
        /* ptid */ *const kernel_pid_t,
        /* ctid */ *const kernel_pid_t,
        /* newtls */ *const std::ffi::c_void,
    );
    pub fn clone(
        ctx: &mut SyscallContext,
        flags_and_exit_signal: i32,
        child_stack: ForeignPtr<()>,
        ptid: ForeignPtr<kernel_pid_t>,
        ctid: ForeignPtr<kernel_pid_t>,
        newtls: u64,
    ) -> Result<kernel_pid_t, Errno> {
        let raw_flags = flags_and_exit_signal as u32 & !0xff;
        let raw_exit_signal = (flags_and_exit_signal as u32 & 0xff) as i32;

        let Some(flags) = CloneFlags::from_bits(raw_flags as u64) else {
            debug!("Couldn't parse clone flags: {raw_flags:x}");
            return Err(Errno::EINVAL);
        };

        let exit_signal = if raw_exit_signal == 0 {
            None
        } else {
            let Ok(exit_signal) = Signal::try_from(raw_exit_signal) else {
                debug!("Bad exit signal: {raw_exit_signal:?}");
                return Err(Errno::EINVAL);
            };
            Some(exit_signal)
        };

        Self::clone_internal(ctx, flags, exit_signal, child_stack, ptid, ctid, newtls)
    }

    log_syscall!(
        clone3,
        /* rv */ kernel_pid_t,
        /* args*/ *const linux_api::sched::clone_args,
        /* args_size*/ usize,
    );
    pub fn clone3(
        ctx: &mut SyscallContext,
        args: ForeignPtr<linux_api::sched::clone_args>,
        args_size: usize,
    ) -> Result<kernel_pid_t, Errno> {
        if args_size != std::mem::size_of::<linux_api::sched::clone_args>() {
            // TODO: allow smaller size, and be careful to only read
            // as much as the caller specified, and zero-fill the rest.
            return Err(Errno::EINVAL);
        }
        let args = ctx.objs.process.memory_borrow().read(args)?;
        trace!("clone3 args: {args:?}");
        let Some(flags) = CloneFlags::from_bits(args.flags) else {
            debug!("Couldn't parse clone flags: {:x}", args.flags);
            return Err(Errno::EINVAL);
        };
        let exit_signal = if args.exit_signal == 0 {
            None
        } else {
            let Ok(exit_signal) = Signal::try_from(args.exit_signal as i32) else {
                debug!("Bad signal number: {}", args.exit_signal);
                return Err(Errno::EINVAL);
            };
            Some(exit_signal)
        };
        Self::clone_internal(
            ctx,
            flags,
            exit_signal,
            ForeignPtr::<()>::from(args.stack + args.stack_size),
            ForeignPtr::<kernel_pid_t>::from_raw_ptr(args.parent_tid as *mut kernel_pid_t),
            ForeignPtr::<kernel_pid_t>::from_raw_ptr(args.child_tid as *mut kernel_pid_t),
            args.tls,
        )
    }

    log_syscall!(fork, /* rv */ kernel_pid_t);
    pub fn fork(ctx: &mut SyscallContext) -> Result<kernel_pid_t, Errno> {
        // This should be the correct call to `clone_internal`, but `clone_internal`
        // will currently return an error.
        Self::clone_internal(
            ctx,
            CloneFlags::empty(),
            Some(Signal::SIGCHLD),
            ForeignPtr::<()>::null(),
            ForeignPtr::<kernel_pid_t>::null(),
            ForeignPtr::<kernel_pid_t>::null(),
            0,
        )
    }

    log_syscall!(vfork, /* rv */ kernel_pid_t);
    pub fn vfork(ctx: &mut SyscallContext) -> Result<kernel_pid_t, Errno> {
        // This should be the correct call to `clone_internal`, but `clone_internal`
        // will currently return an error.
        Self::clone_internal(
            ctx,
            CloneFlags::CLONE_VFORK | CloneFlags::CLONE_VM,
            Some(Signal::SIGCHLD),
            ForeignPtr::<()>::null(),
            ForeignPtr::<kernel_pid_t>::null(),
            ForeignPtr::<kernel_pid_t>::null(),
            0,
        )
    }

    log_syscall!(gettid, /* rv */ kernel_pid_t);
    pub fn gettid(ctx: &mut SyscallContext) -> Result<kernel_pid_t, Errno> {
        Ok(kernel_pid_t::from(ctx.objs.thread.id()))
    }

    log_syscall!(
        capget,
        /* rv */ std::ffi::c_int,
        /* hdrp */ *const std::ffi::c_void,
        /* datap */ *const std::ffi::c_void,
    );
    pub fn capget(
        ctx: &mut SyscallContext,
        hdrp: ForeignPtr<user_cap_header>,
        datap: ForeignPtr<[user_cap_data; 2]>,
    ) -> Result<(), Errno> {
        // If the version is not 3, we return the error
        let hdrp = ctx.objs.process.memory_borrow().read(hdrp)?;
        if hdrp.version != LINUX_CAPABILITY_VERSION_3 {
            warn_once_then_debug!(
                "The version of Linux capabilities is not supported ({})",
                hdrp.version
            );
            return Err(Errno::EINVAL);
        }

        if !datap.is_null() {
            // Since we don't provide any capability to the managed plugin, we return zeroes to both
            // datap[0] and datap[1]
            let empty = user_cap_data {
                effective: 0,
                permitted: 0,
                inheritable: 0,
            };
            ctx.objs
                .process
                .memory_borrow_mut()
                .write(datap, &[empty, empty])?;
        }
        Ok(())
    }

    log_syscall!(
        capset,
        /* rv */ std::ffi::c_int,
        /* hdrp */ *const std::ffi::c_void,
        /* datap */ *const std::ffi::c_void,
    );
    pub fn capset(
        ctx: &mut SyscallContext,
        hdrp: ForeignPtr<user_cap_header>,
        datap: ForeignPtr<[user_cap_data; 2]>,
    ) -> Result<(), Errno> {
        // If the version is not 3, we return the error
        let hdrp = ctx.objs.process.memory_borrow().read(hdrp)?;
        if hdrp.version != LINUX_CAPABILITY_VERSION_3 {
            warn_once_then_debug!(
                "The version of Linux capabilities is not supported ({})",
                hdrp.version
            );
            return Err(Errno::EINVAL);
        }

        let datap: [_; 2] = ctx.objs.process.memory_borrow().read(datap)?;
        for data in &datap {
            // We don't allow the plugin to set any capability
            if data.effective != 0 || data.permitted != 0 || data.inheritable != 0 {
                warn_once_then_debug!("Setting Linux capabilities is not supported");
                return Err(Errno::EINVAL);
            }
        }
        Ok(())
    }
}
