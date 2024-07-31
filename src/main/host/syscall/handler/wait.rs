use std::cmp;
use std::ffi::c_int;

use linux_api::errno::Errno;
use linux_api::posix_types::kernel_pid_t;
use linux_api::resource::rusage;
use linux_api::signal::{siginfo_t, Signal};
use linux_api::wait::{WaitFlags, WaitId};
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::host::process::{ExitStatus, Process, ProcessId};
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::types::SyscallError;

enum WaitTarget {
    Pid(ProcessId),
    PidFd(#[allow(dead_code)] c_int),
    Pgid(ProcessId),
    Any,
}

impl WaitTarget {
    pub fn matches(&self, process: &Process) -> bool {
        match self {
            WaitTarget::Pid(pid) => process.id() == *pid,
            WaitTarget::PidFd(_) => unimplemented!(),
            WaitTarget::Pgid(pgid) => process.group_id() == *pgid,
            WaitTarget::Any => true,
        }
    }

    pub fn from_waitpid_pid(current_process: &Process, pid: kernel_pid_t) -> Self {
        // From `waitpid(2)`:
        // The value of pid can be:
        // < -1  meaning wait for any child process whose process group ID is
        //       equal to the absolute value of pid.
        // -1    meaning wait for any child process.
        // 0     meaning  wait  for any child process whose process group ID is
        //       equal to that of the calling process at the time of the call to
        //       waitpid().
        // > 0   meaning wait for the child whose process ID is equal to the
        //       value of pid.
        match pid.cmp(&0) {
            cmp::Ordering::Less => {
                if pid == -1 {
                    WaitTarget::Any
                } else {
                    WaitTarget::Pgid((-pid).try_into().unwrap())
                }
            }
            cmp::Ordering::Equal => WaitTarget::Pgid(current_process.group_id()),
            cmp::Ordering::Greater => WaitTarget::Pid(pid.try_into().unwrap()),
        }
    }

    pub fn from_waitid(
        current_process: &Process,
        wait_id: WaitId,
        pid: kernel_pid_t,
    ) -> Option<Self> {
        match wait_id {
            WaitId::P_ALL => Some(WaitTarget::Any),
            WaitId::P_PID => ProcessId::try_from(pid).ok().map(WaitTarget::Pid),
            WaitId::P_PGID => {
                let pgid = if pid == 0 {
                    Some(current_process.group_id())
                } else {
                    ProcessId::try_from(pid).ok()
                };
                pgid.map(WaitTarget::Pgid)
            }
            WaitId::P_PIDFD => Some(WaitTarget::PidFd(pid)),
        }
    }
}

impl SyscallHandler {
    fn wait_internal(
        ctx: &mut SyscallContext,
        target: WaitTarget,
        status_ptr: ForeignPtr<c_int>,
        infop: ForeignPtr<siginfo_t>,
        options: WaitFlags,
        usage: ForeignPtr<rusage>,
    ) -> Result<kernel_pid_t, SyscallError> {
        let processes = ctx.objs.host.processes_borrow();
        let matching_children = processes.iter().filter(|(_pid, process)| {
            let process = process.borrow(ctx.objs.host.root());
            if process.parent_id() != ctx.objs.process.id() || !target.matches(&process) {
                return false;
            }
            if options.contains(WaitFlags::__WNOTHREAD) {
                // TODO: track parent thread and check it here.
                warn_once_then_debug!("__WNOTHREAD unimplemented; ignoring.");
            }
            let is_clone_child = process.exit_signal() != Some(Signal::SIGCHLD);
            if options.contains(WaitFlags::__WALL) {
                true
            } else if options.contains(WaitFlags::__WCLONE) {
                is_clone_child
            } else {
                !is_clone_child
            }
        });
        let mut matching_children = matching_children.peekable();
        if matching_children.peek().is_none() {
            // `waitpid(2)`:
            // ECHILD: The process specified by pid (waitpid()) or idtype and id
            // (waitid()) does not exist or is not  a  child  of  the calling
            // process.
            return Err(Errno::ECHILD.into());
        }

        if !options.contains(WaitFlags::WEXITED) {
            warn_once_then_debug!("Waiting only for child events that currently never happen under Shadow: {options:?}");
            // The other events that can be waited for (WUNTRACED, WSTOPPED,
            // WCONTINUED) currently can't happen under Shadow.
            // TODO: If and when those things *can* happen, check for them here.
            return if options.contains(WaitFlags::WNOHANG) {
                Ok(0)
            } else {
                Err(SyscallError::new_blocked_on_child(
                    /* restartable */ true,
                ))
            };
        }

        let mut matching_child_zombies = matching_children.filter(|(_pid, process)| {
            let process = process.borrow(ctx.objs.host.root());
            let zombie = process.borrow_as_zombie();
            zombie.is_some()
        });
        let Some((matching_child_zombie_pid, matching_child_zombie)) =
            matching_child_zombies.next()
        else {
            // There are matching children, but none are zombies yet.
            return if options.contains(WaitFlags::WNOHANG) {
                Ok(0)
            } else {
                // FIXME: save `target` in SyscallCondition and reuse, in case
                // the target was specified as 0 => "current process group id"
                // and the process group changes in the meantime.
                Err(SyscallError::new_blocked_on_child(
                    /* restartable */ true,
                ))
            };
        };

        let zombie_process = matching_child_zombie.borrow(ctx.objs.host.root());
        let zombie = zombie_process.borrow_as_zombie().unwrap();
        let mut memory = ctx.objs.process.memory_borrow_mut();

        if !status_ptr.is_null() {
            let status = match zombie.exit_status() {
                ExitStatus::Normal(i) => i << 8,
                ExitStatus::Signaled(s) => {
                    // This should be `| 0x80` if the process dumped core, but since
                    // this depends on the system config we never set this flag.
                    i32::from(s)
                }
                ExitStatus::StoppedByShadow => unreachable!(),
            };
            memory.write(status_ptr, &status)?;
        }
        if !infop.is_null() {
            let info = zombie.exit_siginfo(Signal::SIGCHLD);
            memory.write(infop, &info)?;
        }
        if !usage.is_null() {
            memory.write(usage, &ctx.objs.process.rusage())?;
        }

        let matching_child_zombie_pid: ProcessId = *matching_child_zombie_pid;
        // Drop our borrow of the process list so that we can reap without a runtime borrow error.
        drop(memory);
        drop(zombie);
        drop(zombie_process);
        drop(processes);

        if !options.contains(WaitFlags::WNOWAIT) {
            let zombie_process = ctx
                .objs
                .host
                .process_remove(matching_child_zombie_pid)
                .unwrap();
            zombie_process.explicit_drop_recursive(ctx.objs.host.root(), ctx.objs.host);
        }

        Ok(matching_child_zombie_pid.into())
    }

    log_syscall!(
        wait4,
        /* rv */ kernel_pid_t,
        /* pid */ kernel_pid_t,
        /* status */ *const c_int,
        /* options */ c_int,
        /* usage */ *const std::ffi::c_void,
    );
    pub fn wait4(
        ctx: &mut SyscallContext,
        pid: kernel_pid_t,
        status: ForeignPtr<c_int>,
        options: c_int,
        usage: ForeignPtr<rusage>,
    ) -> Result<kernel_pid_t, SyscallError> {
        let Some(mut wait_flags) = WaitFlags::from_bits(options) else {
            return Err(Errno::EINVAL.into());
        };

        let allowed_flags = WaitFlags::WNOHANG
            | WaitFlags::WUNTRACED
            | WaitFlags::WCONTINUED
            | WaitFlags::__WCLONE
            | WaitFlags::__WALL
            | WaitFlags::__WNOTHREAD;
        let unexpected_flags = wait_flags.difference(allowed_flags);
        if !unexpected_flags.is_empty() {
            // These flags aren't permitted according to the `wait(2)`. We could
            // support them here, but conservatively disallow.
            log::debug!("Unexpected flags: {unexpected_flags:?}");
            return Err(Errno::EINVAL.into());
        }

        // WEXITED is implicit for this syscall.
        wait_flags |= WaitFlags::WEXITED;

        let target = WaitTarget::from_waitpid_pid(ctx.objs.process, pid);
        Self::wait_internal(ctx, target, status, ForeignPtr::null(), wait_flags, usage)
    }

    log_syscall!(
        waitid,
        /* rv */ kernel_pid_t,
        /* which */ c_int,
        /* upid */ kernel_pid_t,
        /* infop */ *const std::ffi::c_void,
        /* options */ c_int,
        /* uru */ *const std::ffi::c_void,
    );
    pub fn waitid(
        ctx: &mut SyscallContext,
        which: c_int,
        upid: kernel_pid_t,
        infop: ForeignPtr<siginfo_t>,
        options: c_int,
        uru: ForeignPtr<rusage>,
    ) -> Result<(), SyscallError> {
        let wait_flags = WaitFlags::from_bits_retain(options);
        let wait_id = WaitId::try_from(which).map_err(|_| Errno::EINVAL)?;
        let Some(target) = WaitTarget::from_waitid(ctx.objs.process, wait_id, upid) else {
            // We can get here if e.g. the ID was P_PID, but the pid was
            // negative so couldn't be converted to a ProcessId. Afaict from the man page,
            // this would simply result in no child matching the target, hence `ECHILD`.
            log::debug!("Invalid `which`+`upid` combination: {wait_id:?}:{upid}");
            return Err(Errno::ECHILD.into());
        };

        Self::wait_internal(ctx, target, ForeignPtr::null(), infop, wait_flags, uru).map(|_| ())
    }
}
