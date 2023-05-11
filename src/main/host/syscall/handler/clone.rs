use log::{debug, trace, warn};
use nix::errno::Errno;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;
use syscall_logger::log_syscall;

use crate::host::syscall_types::SyscallError;

use super::{SyscallContext, SyscallHandler};

// We don't use nix::sched::CloneFlags here, because nix omits flags
// that it doesn't support. e.g. https://docs.rs/nix/0.26.2/src/nix/sched.rs.html#57
bitflags::bitflags! {
    // While `clone` is documented as taking an i32 parameter for flags,
    // in `clone3` its a u64. Promote to u64 throughout.
    #[derive(Copy, Clone, Debug)]
    pub struct CloneFlags: u64 {
        // Not exported by the libc crate.
        // Manually copied from sched.h.
        const CLONE_CLEAR_SIGHAND = 0x100000000;
        const CLONE_INTO_CGROUP = 0x200000000;
        const CLONE_NEWTIME = 0x00000080;

        const CLONE_VM = libc::CLONE_VM as u64;
        const CLONE_FS = libc::CLONE_FS as u64;
        const CLONE_FILES = libc::CLONE_FILES as u64;
        const CLONE_SIGHAND = libc::CLONE_SIGHAND as u64;
        const CLONE_PIDFD = libc::CLONE_PIDFD as u64;
        const CLONE_PTRACE = libc::CLONE_PTRACE as u64;
        const CLONE_VFORK = libc::CLONE_VFORK as u64;
        const CLONE_PARENT = libc::CLONE_PARENT as u64;
        const CLONE_THREAD = libc::CLONE_THREAD as u64;
        const CLONE_NEWNS = libc::CLONE_NEWNS as u64;
        const CLONE_SYSVSEM = libc::CLONE_SYSVSEM as u64;
        const CLONE_SETTLS = libc::CLONE_SETTLS as u64;
        const CLONE_PARENT_SETTID = libc::CLONE_PARENT_SETTID as u64;
        const CLONE_CHILD_CLEARTID = libc::CLONE_CHILD_CLEARTID as u64;
        const CLONE_DETACHED = libc::CLONE_DETACHED as u64;
        const CLONE_UNTRACED = libc::CLONE_UNTRACED as u64;
        const CLONE_CHILD_SETTID = libc::CLONE_CHILD_SETTID as u64;
        const CLONE_NEWCGROUP = libc::CLONE_NEWCGROUP as u64;
        const CLONE_NEWUTS = libc::CLONE_NEWUTS as u64;
        const CLONE_NEWIPC = libc::CLONE_NEWIPC as u64;
        const CLONE_NEWUSER = libc::CLONE_NEWUSER as u64;
        const CLONE_NEWPID = libc::CLONE_NEWPID as u64;
        const CLONE_NEWNET = libc::CLONE_NEWNET as u64;
        const CLONE_IO = libc::CLONE_IO as u64;
    }
}

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

        let Some(flags) = CloneFlags::from_bits(raw_flags as u64) else {
            warn!("Couldn't parse clone flags: {raw_flags:?}");
            return Err(Errno::EINVAL.into());
        };

        {
            let required_flags = CloneFlags::CLONE_VM
                | CloneFlags::CLONE_FS
                | CloneFlags::CLONE_FILES
                | CloneFlags::CLONE_SIGHAND
                | CloneFlags::CLONE_THREAD
                | CloneFlags::CLONE_SYSVSEM;
            let missing_flags = required_flags.difference(flags);
            if !missing_flags.is_empty() {
                warn!("Missing required clone flags: {missing_flags:?}");
                return Err(Errno::EINVAL.into());
            }
        }

        let child_tid = {
            let flags_to_emulate = CloneFlags::CLONE_PARENT_SETTID
                | CloneFlags::CLONE_CHILD_SETTID
                | CloneFlags::CLONE_CHILD_CLEARTID;
            let filtered_flags = flags.difference(flags_to_emulate);
            if raw_exit_signal != 0 {
                warn!("Exit signal is unimplemented");
                return Err(Errno::ENOTSUP.into());
            }
            let (pctx, thread) = ctx.objs.split_thread();
            thread.handle_clone_syscall(
                &pctx,
                filtered_flags.bits(),
                child_stack,
                ptid,
                ctid,
                newtls,
            )?
        };

        if flags.contains(CloneFlags::CLONE_PARENT_SETTID) {
            ctx.objs
                .process
                .memory_borrow_mut()
                .write(ptid, &libc::pid_t::from(child_tid))?;
        }

        if flags.contains(CloneFlags::CLONE_CHILD_SETTID) {
            ctx.objs
                .process
                .memory_borrow_mut()
                .write(ctid, &libc::pid_t::from(child_tid))?;
        }

        if flags.contains(CloneFlags::CLONE_CHILD_CLEARTID) {
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
                   args.flags, CloneFlags::from_bits(args.flags));
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
                CloneFlags::from_bits(args.flags & 0xff)
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
