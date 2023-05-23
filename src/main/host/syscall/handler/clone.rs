use linux_api::sched::LinuxCloneFlags;
use log::{debug, trace, warn};
use nix::errno::Errno;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;
use syscall_logger::log_syscall;

use crate::host::syscall_types::SyscallError;

use super::{SyscallContext, SyscallHandler};

impl SyscallHandler {
    fn clone_internal(
        ctx: &mut SyscallContext,
        flags_and_exit_signal: i32,
        child_stack: ForeignPtr<()>,
        ptid: ForeignPtr<libc::pid_t>,
        ctid: ForeignPtr<libc::pid_t>,
        newtls: u64,
    ) -> Result<libc::pid_t, SyscallError> {
        let raw_flags = flags_and_exit_signal as u32 & !0xff;
        let raw_exit_signal = flags_and_exit_signal as u32 & 0xff;

        let Some(flags) = LinuxCloneFlags::from_bits(raw_flags as u64) else {
            warn!("Couldn't parse clone flags: {raw_flags:?}");
            return Err(Errno::EINVAL.into());
        };

        // We use this for a consistency check to validate that we've inspected
        // and emulated all of the provided flags.
        let mut handled_flags = LinuxCloneFlags::empty();

        // The parameters that we'll pass to the native clone call.
        let mut native_flags = LinuxCloneFlags::empty();

        // We emulate the flags that would use these, so we always pass NULL to
        // the native call.
        let native_ctid = ForeignPtr::<libc::pid_t>::null();
        let native_ptid = ForeignPtr::<libc::pid_t>::null();

        // We use the managed-code provided stack.
        let native_child_stack = child_stack;

        // We use the managed-code provided newtls.
        let native_newtls = newtls;

        if raw_exit_signal != 0 {
            warn!("Exit signal is unimplemented");
            return Err(Errno::ENOTSUP.into());
        }
        // We use an i8 here because it needs to fit into the lowest 8 bits of
        // the flags parameter to the native clone call.
        let native_raw_exit_signal: i8 = 0;

        if flags.contains(LinuxCloneFlags::CLONE_THREAD) {
            // From clone(2):
            // > Since Linux 2.5.35, the flags mask must also include
            // > CLONE_SIGHAND if CLONE_THREAD is specified
            if !flags.contains(LinuxCloneFlags::CLONE_SIGHAND) {
                debug!("Missing CLONE_SIGHAND");
                return Err(Errno::EINVAL.into());
            }
            if !flags.contains(LinuxCloneFlags::CLONE_FILES) {
                // AFAICT from clone(2) this is legal, but we don't yet support
                // it in Shadow, since the file descriptor table is kept at the
                // Process level instead of the Thread level.
                warn!("CLONE_THREAD without CLONE_FILES not supported by shadow");
                return Err(Errno::ENOTSUP.into());
            }
            if !flags.contains(LinuxCloneFlags::CLONE_SETTLS) {
                // Legal in Linux, but the shim will be broken and behave unpredictably.
                warn!("CLONE_THREAD without CLONE_TLS not supported by shadow");
                return Err(Errno::ENOTSUP.into());
            }
            // The native clone call will:
            // - create a thread.
            native_flags.insert(LinuxCloneFlags::CLONE_THREAD);
            // - share signal handlers (mandatory anyway)
            native_flags.insert(LinuxCloneFlags::CLONE_SIGHAND);
            // - share file system info (mostly N/A for shadow, but conventional for threads)
            native_flags.insert(LinuxCloneFlags::CLONE_FS);
            // - share file descriptors
            native_flags.insert(LinuxCloneFlags::CLONE_FILES);
            // - share semaphores (mostly N/A for shadow, but conventional for threads)
            native_flags.insert(LinuxCloneFlags::CLONE_SYSVSEM);

            handled_flags.insert(LinuxCloneFlags::CLONE_THREAD);
        } else {
            warn!("Failing clone: we don't support creating a new process (e.g. fork) yet");
            return Err(Errno::ENOTSUP.into());
        }

        if flags.contains(LinuxCloneFlags::CLONE_SIGHAND) {
            // From clone(2):
            // > Since Linux 2.6.0, the flags mask must also include CLONE_VM if
            // > CLONE_SIGHAND is specified
            if !flags.contains(LinuxCloneFlags::CLONE_VM) {
                debug!("Missing CLONE_VM");
                return Err(Errno::EINVAL.into());
            }
            // Currently a no-op since threads always share signal handlers,
            // and we don't yet support non-CLONE_THREAD.
            handled_flags.insert(LinuxCloneFlags::CLONE_SIGHAND);
        }

        if flags.contains(LinuxCloneFlags::CLONE_FS) {
            // Currently a no-op since we don't support the related
            // metadata and syscalls that this affects (e.g. chroot).
            handled_flags.insert(LinuxCloneFlags::CLONE_FS);
        }

        if flags.contains(LinuxCloneFlags::CLONE_FILES) {
            if !flags.contains(LinuxCloneFlags::CLONE_THREAD) {
                // I *think* we'd just need to wrap the descriptor table
                // in e.g. a RootedRc, and clone the Rc in this case.
                warn!("Failing clone: we don't support fork with shared descriptor table");
                return Err(Errno::ENOTSUP.into());
            }
            handled_flags.insert(LinuxCloneFlags::CLONE_FILES);
        }

        if flags.contains(LinuxCloneFlags::CLONE_SETTLS) {
            native_flags.insert(LinuxCloneFlags::CLONE_SETTLS);
            handled_flags.insert(LinuxCloneFlags::CLONE_SETTLS);
        }

        if flags.contains(LinuxCloneFlags::CLONE_VM) {
            native_flags.insert(LinuxCloneFlags::CLONE_VM);
            handled_flags.insert(LinuxCloneFlags::CLONE_VM);
        }

        if flags.contains(LinuxCloneFlags::CLONE_SYSVSEM) {
            // Currently a no-op since we don't support sysv semaphores.
            handled_flags.insert(LinuxCloneFlags::CLONE_SYSVSEM);
        }

        // Handled after native clone
        let do_parent_settid = flags.contains(LinuxCloneFlags::CLONE_PARENT_SETTID);
        handled_flags.insert(LinuxCloneFlags::CLONE_PARENT_SETTID);

        // Handled after native clone
        let do_child_settid = flags.contains(LinuxCloneFlags::CLONE_CHILD_SETTID);
        handled_flags.insert(LinuxCloneFlags::CLONE_CHILD_SETTID);

        // Handled after native clone
        let do_child_cleartid = flags.contains(LinuxCloneFlags::CLONE_CHILD_CLEARTID);
        handled_flags.insert(LinuxCloneFlags::CLONE_CHILD_CLEARTID);

        let unhandled_flags = flags.difference(handled_flags);
        if !unhandled_flags.is_empty() {
            warn!("Unhandled clone flags: {unhandled_flags:?}");
            return Err(Errno::ENOTSUP.into());
        }

        let child_tid = {
            let (pctx, thread) = ctx.objs.split_thread();
            thread.handle_clone_syscall(
                &pctx,
                native_flags.bits() | (native_raw_exit_signal as u64),
                native_child_stack,
                native_ptid,
                native_ctid,
                native_newtls,
            )?
        };

        if do_parent_settid {
            ctx.objs
                .process
                .memory_borrow_mut()
                .write(ptid, &libc::pid_t::from(child_tid))?;
        }

        if do_child_settid {
            // FIXME: handle the case where child doesn't share virtual memory.
            assert!(flags.contains(LinuxCloneFlags::CLONE_VM));
            ctx.objs
                .process
                .memory_borrow_mut()
                .write(ctid, &libc::pid_t::from(child_tid))?;
        }

        if do_child_cleartid {
            let childrc = ctx.objs.process.thread_borrow(child_tid).unwrap();
            let child = childrc.borrow(ctx.objs.host.root());
            child.set_tid_address(ctid);
        }

        Ok(libc::pid_t::from(child_tid))
    }
    // Note that the syscall args are different than the libc wrapper.
    // See "C library/kernel differences" in clone(2).
    #[log_syscall(
        /* rv */libc::pid_t,
        /* flags */i32,
        /* child_stack */*const libc::c_void,
        /* ptid */*const libc::pid_t,
        /* ctid */*const libc::pid_t,
        /* newtls */*const libc::c_void)]
    pub fn clone(
        ctx: &mut SyscallContext,
        flags_and_exit_signal: i32,
        child_stack: ForeignPtr<()>,
        ptid: ForeignPtr<libc::pid_t>,
        ctid: ForeignPtr<libc::pid_t>,
        newtls: u64,
    ) -> Result<libc::pid_t, SyscallError> {
        Self::clone_internal(ctx, flags_and_exit_signal, child_stack, ptid, ctid, newtls)
    }

    #[log_syscall(
        /* rv */libc::pid_t,
        /* args*/*const libc::c_void,
        /* args_size*/usize)]
    pub fn clone3(
        ctx: &mut SyscallContext,
        args: ForeignPtr<libc::clone_args>,
        args_size: usize,
    ) -> Result<libc::pid_t, SyscallError> {
        if args_size != std::mem::size_of::<libc::clone_args>() {
            // TODO: allow smaller size, and be careful to only read
            // as much as the caller specified, and zero-fill the rest.
            return Err(Errno::EINVAL.into());
        }
        let args = ctx.objs.process.memory_borrow().read(args)?;
        trace!("clone3 args: {args:?}");
        let Ok(flags) = i32::try_from(args.flags) else {
            debug!("Couldn't safely truncate flags to 32 bits: {} ({:?})",
                   args.flags, LinuxCloneFlags::from_bits(args.flags));
            return Err(Errno::EINVAL.into());
        };
        if flags & 0xff != 0 {
            // We can't multiplex through `clone` in this case, since these bits
            // conflict with the exit signal. Currently this won't happen in practice
            // because there are no legal flags that use these bits. It's possible
            // that clone3-flags could be added here, but more likely they'll use
            // the higher bits first. (clone3 uses 64 bits vs clone's 32).
            debug!(
                "clone3 got a flag it can't pass through to clone: {} ({:?})",
                flags & 0xff,
                LinuxCloneFlags::from_bits(args.flags & 0xff)
            );
            return Err(Errno::EINVAL.into());
        }
        let Ok(exit_signal) = i32::try_from(args.exit_signal) else {
            // Couldn't truncate to the 32 bits allowed by `clone`, but
            // there also aren't any valid signal numbers that need that many bits.
            debug!("Bad signal number: {}", args.exit_signal);
            return Err(Errno::EINVAL.into());
        };
        if exit_signal & !0xff != 0 {
            // Couldn't fit into the 8 bits allowed for the signal number by `clone`,
            // but there also aren't any valid signal numbers that need that many bits.
            return Err(Errno::EINVAL.into());
        }
        Self::clone_internal(
            ctx,
            flags | exit_signal,
            ForeignPtr::<()>::from(args.stack + args.stack_size),
            ForeignPtr::<libc::pid_t>::from_raw_ptr(args.parent_tid as *mut libc::pid_t),
            ForeignPtr::<libc::pid_t>::from_raw_ptr(args.child_tid as *mut libc::pid_t),
            args.tls,
        )
    }

    #[log_syscall(/* rv */libc::pid_t)]
    pub fn gettid(ctx: &mut SyscallContext) -> Result<libc::pid_t, SyscallError> {
        Ok(libc::pid_t::from(ctx.objs.thread.id()))
    }
}
