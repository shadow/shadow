use log::warn;
use nix::errno::Errno;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;
use syscall_logger::log_syscall;

use crate::host::syscall_types::SyscallError;

use super::{SyscallContext, SyscallHandler};

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

    #[log_syscall(/* rv */libc::pid_t)]
    pub fn gettid(ctx: &mut SyscallContext) -> Result<libc::pid_t, SyscallError> {
        Ok(libc::pid_t::from(ctx.objs.thread.id()))
    }
}
