use std::borrow::Cow;

#[cfg(feature = "perf_timers")]
use std::time::Duration;

use linux_api::errno::Errno;
use linux_api::syscall::SyscallNum;
use shadow_shim_helper_rs::simulation_time::SimulationTime;
use shadow_shim_helper_rs::syscall_types::SyscallArgs;
use shadow_shim_helper_rs::syscall_types::SyscallReg;
use shadow_shim_helper_rs::util::SendPointer;
use shadow_shim_helper_rs::HostId;

use crate::core::worker::Worker;
use crate::cshadow as c;
use crate::host::context::ThreadContext;
use crate::host::descriptor::descriptor_table::{DescriptorHandle, DescriptorTable};
use crate::host::descriptor::Descriptor;
use crate::host::process::ProcessId;
use crate::host::syscall::formatter::log_syscall_simple;
use crate::host::syscall::is_shadow_syscall;
use crate::host::syscall::types::SyscallReturn;
use crate::host::syscall::types::{SyscallError, SyscallResult};
use crate::host::thread::ThreadId;
use crate::utility::counter::Counter;

#[cfg(feature = "perf_timers")]
use crate::utility::perf_timer::PerfTimer;

mod clone;
mod epoll;
mod eventfd;
mod fcntl;
mod file;
mod fileat;
mod futex;
mod ioctl;
mod mman;
mod poll;
mod prctl;
mod random;
mod resource;
mod sched;
mod select;
mod shadow;
mod signal;
mod socket;
mod sysinfo;
mod time;
mod timerfd;
mod uio;
mod unistd;
mod wait;

type LegacySyscallFn =
    unsafe extern "C-unwind" fn(*mut SyscallHandler, *const SyscallArgs) -> SyscallReturn;

// Will eventually contain syscall handler state once migrated from the c handler
pub struct SyscallHandler {
    /// The host that this `SyscallHandler` belongs to. Intended to be used for logging.
    host_id: HostId,
    /// The process that this `SyscallHandler` belongs to. Intended to be used for logging.
    process_id: ProcessId,
    /// The thread that this `SyscallHandler` belongs to. Intended to be used for logging.
    thread_id: ThreadId,
    /// The total number of syscalls that we have handled.
    num_syscalls: u64,
    /// A counter for individual syscalls.
    syscall_counter: Option<Counter>,
    /// If we are currently blocking a specific syscall, i.e., waiting for a socket to be
    /// readable/writable or waiting for a timeout, the syscall number of that function is stored
    /// here. Will be `None` if a syscall is not currently blocked.
    blocked_syscall: Option<SyscallNum>,
    /// In some cases the syscall handler completes, but we block the caller anyway to move time
    /// forward. This stores the result of the completed syscall, to be returned when the caller
    /// resumes.
    pending_result: Option<SyscallResult>,
    /// We use this epoll to service syscalls that need to block on the status of multiple
    /// descriptors, like poll.
    epoll: SendPointer<c::Epoll>,
    /// The cumulative time consumed while handling the current syscall. This includes the time from
    /// previous calls that ended up blocking.
    #[cfg(feature = "perf_timers")]
    perf_duration_current: Duration,
    /// The total time elapsed while handling all syscalls.
    #[cfg(feature = "perf_timers")]
    perf_duration_total: Duration,
}

impl SyscallHandler {
    pub fn new(
        host_id: HostId,
        process_id: ProcessId,
        thread_id: ThreadId,
        count_syscalls: bool,
    ) -> SyscallHandler {
        SyscallHandler {
            host_id,
            process_id,
            thread_id,
            num_syscalls: 0,
            syscall_counter: count_syscalls.then(Counter::new),
            blocked_syscall: None,
            pending_result: None,
            epoll: unsafe { SendPointer::new(c::epoll_new()) },
            #[cfg(feature = "perf_timers")]
            perf_duration_current: Duration::ZERO,
            #[cfg(feature = "perf_timers")]
            perf_duration_total: Duration::ZERO,
        }
    }

    pub fn syscall(&mut self, ctx: &ThreadContext, args: &SyscallArgs) -> SyscallResult {
        // it wouldn't make sense if we were given a different host, process, and thread
        assert_eq!(ctx.host.id(), self.host_id);
        assert_eq!(ctx.process.id(), self.process_id);
        assert_eq!(ctx.thread.id(), self.thread_id);

        let syscall = SyscallNum::new(args.number.try_into().unwrap());
        let syscall_name = syscall.to_str().unwrap_or("unknown-syscall");

        // make sure that we either don't have a blocked syscall, or if we blocked a syscall, then
        // that same syscall should be executed again when it becomes unblocked
        if let Some(blocked_syscall) = self.blocked_syscall {
            if blocked_syscall != syscall {
                panic!("We blocked syscall {blocked_syscall} but syscall {syscall} is unexpectedly being invoked");
            }
        }

        // were we previously blocked on this same syscall?
        let was_blocked = self.blocked_syscall.is_some();

        if let Some(pending_result) = self.pending_result.take() {
            // The syscall was already completed, but we delayed the response to yield the CPU.
            // Return that response now.
            log::trace!("Returning delayed result");
            assert!(!matches!(pending_result, Err(SyscallError::Blocked(_))));

            self.blocked_syscall = None;
            self.pending_result = None;

            return pending_result;
        }

        log::trace!(
            "SYSCALL_HANDLER_PRE: {} ({}){} — ({}, tid={})",
            syscall_name,
            args.number,
            if was_blocked {
                " (previously BLOCKed)"
            } else {
                ""
            },
            &*ctx.process.name(),
            ctx.thread.id(),
        );

        // Count the frequency of each syscall, but only on the initial call. This avoids double
        // counting in the case where the initial call blocked at first, but then later became
        // unblocked and is now being handled again here.
        if let Some(syscall_counter) = self.syscall_counter.as_mut() {
            if !was_blocked {
                syscall_counter.add_one(syscall_name);
            }
        }

        #[cfg(feature = "perf_timers")]
        let timer = PerfTimer::new();

        let mut rv = self.run_handler(ctx, args);

        #[cfg(feature = "perf_timers")]
        {
            // add the cumulative elapsed seconds
            self.perf_duration_current += timer.elapsed();

            log::debug!(
                "Handling syscall {} ({}) took cumulative {} ms",
                syscall_name,
                args.number,
                self.perf_duration_current.as_millis(),
            );
        }

        if !matches!(rv, Err(SyscallError::Blocked(_))) {
            // the syscall completed, count it and the cumulative time to complete it
            self.num_syscalls += 1;

            #[cfg(feature = "perf_timers")]
            {
                self.perf_duration_total += self.perf_duration_current;
                self.perf_duration_current = Duration::ZERO;
            }
        }

        if log::log_enabled!(log::Level::Trace) {
            let rv_formatted = match &rv {
                Ok(reg) => format!("{}", i64::from(*reg)),
                Err(SyscallError::Failed(failed)) => {
                    let errno = failed.errno;
                    format!("{} ({errno})", errno.to_negated_i64())
                }
                Err(SyscallError::Native) => "<native>".to_string(),
                Err(SyscallError::Blocked(_)) => "<blocked>".to_string(),
            };

            log::trace!(
                "SYSCALL_HANDLER_POST: {} ({}) result {}{} — ({}, tid={})",
                syscall_name,
                args.number,
                if was_blocked { "BLOCK -> " } else { "" },
                rv_formatted,
                &*ctx.process.name(),
                ctx.thread.id(),
            );
        }

        // If the syscall would be blocked, but there's a signal pending, fail with
        // EINTR instead. The shim-side code will run the signal handlers and then
        // either return the EINTR or restart the syscall (See SA_RESTART in
        // signal(7)).
        //
        // We do this check *after* (not before) trying the syscall so that we don't
        // "interrupt" a syscall that wouldn't have blocked in the first place, or
        // that can return a "partial" result when interrupted. e.g. consider the
        // sequence:
        //
        // * Thread is blocked on reading a file descriptor.
        // * The read becomes ready and the thread is scheduled to run.
        // * The thread receives an unblocked signal.
        // * The thread runs again.
        //
        // In this scenario, the `read` call should be allowed to complete successfully.
        // from signal(7):  "If an I/O call on a slow device has already transferred
        // some data by the time it is interrupted by a signal handler, then the
        // call will return a success  status  (normally,  the  number of bytes
        // transferred)."

        if let Err(SyscallError::Blocked(ref blocked)) = rv {
            // the syscall wants to block, but is there a signal pending?
            let is_unblocked_signal_pending = ctx
                .thread
                .unblocked_signal_pending(ctx.process, &ctx.host.shim_shmem_lock_borrow().unwrap());

            if is_unblocked_signal_pending {
                // return EINTR instead
                rv = Err(SyscallError::new_interrupted(blocked.restartable));
            }
        }

        // we only use unsafe borrows from C code, and we should have only called into C syscall
        // handlers through `Self::legacy_syscall` which should have already flushed the pointers,
        // but we may as well do it again here just to be safe
        if rv.is_err() {
            // the syscall didn't complete successfully; don't write back pointers
            log::trace!(
                "Syscall didn't complete successfully; discarding plugin ptrs without writing back"
            );
            ctx.process.free_unsafe_borrows_noflush();
        } else {
            ctx.process
                .free_unsafe_borrows_flush()
                .expect("flushing syscall ptrs");
        }

        if ctx.host.shim_shmem().model_unblocked_syscall_latency
            && ctx.process.is_running()
            && !matches!(rv, Err(SyscallError::Blocked(_)))
        {
            let max_unapplied_cpu_latency = ctx.host.shim_shmem().max_unapplied_cpu_latency;

            // increment unblocked syscall latency, but only for non-shadow-syscalls, since the
            // latter are part of Shadow's internal plumbing; they shouldn't necessarily "consume"
            // time
            if !is_shadow_syscall(syscall) {
                ctx.host
                    .shim_shmem_lock_borrow_mut()
                    .unwrap()
                    .unapplied_cpu_latency += ctx.host.shim_shmem().unblocked_syscall_latency;
            }

            let unapplied_cpu_latency = ctx
                .host
                .shim_shmem_lock_borrow()
                .unwrap()
                .unapplied_cpu_latency;

            log::trace!(
                "Unapplied CPU latency amt={}ns max={}ns",
                unapplied_cpu_latency.as_nanos(),
                max_unapplied_cpu_latency.as_nanos()
            );

            if unapplied_cpu_latency > max_unapplied_cpu_latency {
                let new_time = Worker::current_time().unwrap() + unapplied_cpu_latency;
                let max_time = Worker::max_event_runahead_time(ctx.host);

                if new_time <= max_time {
                    log::trace!("Reached unblocked syscall limit; Incrementing time");

                    ctx.host
                        .shim_shmem_lock_borrow_mut()
                        .unwrap()
                        .unapplied_cpu_latency = SimulationTime::ZERO;
                    Worker::set_current_time(new_time);
                } else {
                    log::trace!("Reached unblocked syscall limit; Yielding");

                    // block instead, but save the result so that we can return it later instead of
                    // re-executing the syscall
                    assert!(self.pending_result.is_none());
                    self.pending_result = Some(rv);
                    rv = Err(SyscallError::new_blocked_until(new_time, false));
                }
            }
        }

        if matches!(rv, Err(SyscallError::Blocked(_))) {
            // we are blocking: store the syscall number so we know to expect the same syscall again
            // when it unblocks
            self.blocked_syscall = Some(syscall);
        } else {
            self.blocked_syscall = None;
        }

        rv
    }

    #[allow(non_upper_case_globals)]
    fn run_handler(&mut self, ctx: &ThreadContext, args: &SyscallArgs) -> SyscallResult {
        const NR_shadow_yield: SyscallNum = SyscallNum::new(c::ShadowSyscallNum_SYS_shadow_yield);
        const NR_shadow_init_memory_manager: SyscallNum =
            SyscallNum::new(c::ShadowSyscallNum_SYS_shadow_init_memory_manager);
        const NR_shadow_hostname_to_addr_ipv4: SyscallNum =
            SyscallNum::new(c::ShadowSyscallNum_SYS_shadow_hostname_to_addr_ipv4);

        let mut ctx = SyscallContext {
            objs: ctx,
            args,
            handler: self,
        };

        let syscall = SyscallNum::new(ctx.args.number.try_into().unwrap());
        let syscall_name = syscall.to_str().unwrap_or("unknown-syscall");

        macro_rules! handle {
            ($f:ident) => {{
                SyscallHandlerFn::call(Self::$f, &mut ctx)
            }};
        }

        match syscall {
            // SHADOW-HANDLED SYSCALLS
            //
            SyscallNum::NR_accept => handle!(accept),
            SyscallNum::NR_accept4 => handle!(accept4),
            SyscallNum::NR_alarm => handle!(alarm),
            SyscallNum::NR_bind => handle!(bind),
            SyscallNum::NR_brk => handle!(brk),
            SyscallNum::NR_capget => handle!(capget),
            SyscallNum::NR_capset => handle!(capset),
            SyscallNum::NR_clock_getres => handle!(clock_getres),
            SyscallNum::NR_clock_nanosleep => handle!(clock_nanosleep),
            SyscallNum::NR_clone => handle!(clone),
            SyscallNum::NR_clone3 => handle!(clone3),
            SyscallNum::NR_close => handle!(close),
            SyscallNum::NR_connect => handle!(connect),
            SyscallNum::NR_creat => handle!(creat),
            SyscallNum::NR_dup => handle!(dup),
            SyscallNum::NR_dup2 => handle!(dup2),
            SyscallNum::NR_dup3 => handle!(dup3),
            SyscallNum::NR_epoll_create => handle!(epoll_create),
            SyscallNum::NR_epoll_create1 => handle!(epoll_create1),
            SyscallNum::NR_epoll_ctl => handle!(epoll_ctl),
            SyscallNum::NR_epoll_pwait => handle!(epoll_pwait),
            SyscallNum::NR_epoll_pwait2 => handle!(epoll_pwait2),
            SyscallNum::NR_epoll_wait => handle!(epoll_wait),
            SyscallNum::NR_eventfd => handle!(eventfd),
            SyscallNum::NR_eventfd2 => handle!(eventfd2),
            SyscallNum::NR_execve => handle!(execve),
            SyscallNum::NR_execveat => handle!(execveat),
            SyscallNum::NR_exit_group => handle!(exit_group),
            SyscallNum::NR_faccessat => handle!(faccessat),
            SyscallNum::NR_fadvise64 => handle!(fadvise64),
            SyscallNum::NR_fallocate => handle!(fallocate),
            SyscallNum::NR_fchmod => handle!(fchmod),
            SyscallNum::NR_fchmodat => handle!(fchmodat),
            SyscallNum::NR_fchown => handle!(fchown),
            SyscallNum::NR_fchownat => handle!(fchownat),
            SyscallNum::NR_fcntl => handle!(fcntl),
            SyscallNum::NR_fdatasync => handle!(fdatasync),
            SyscallNum::NR_fgetxattr => handle!(fgetxattr),
            SyscallNum::NR_flistxattr => handle!(flistxattr),
            SyscallNum::NR_flock => handle!(flock),
            SyscallNum::NR_fork => handle!(fork),
            SyscallNum::NR_fremovexattr => handle!(fremovexattr),
            SyscallNum::NR_fsetxattr => handle!(fsetxattr),
            SyscallNum::NR_fstat => handle!(fstat),
            SyscallNum::NR_fstatfs => handle!(fstatfs),
            SyscallNum::NR_fsync => handle!(fsync),
            SyscallNum::NR_ftruncate => handle!(ftruncate),
            SyscallNum::NR_futex => handle!(futex),
            SyscallNum::NR_futimesat => handle!(futimesat),
            SyscallNum::NR_get_robust_list => handle!(get_robust_list),
            SyscallNum::NR_getdents => handle!(getdents),
            SyscallNum::NR_getdents64 => handle!(getdents64),
            SyscallNum::NR_getitimer => handle!(getitimer),
            SyscallNum::NR_getpeername => handle!(getpeername),
            SyscallNum::NR_getpgid => handle!(getpgid),
            SyscallNum::NR_getpgrp => handle!(getpgrp),
            SyscallNum::NR_getpid => handle!(getpid),
            SyscallNum::NR_getppid => handle!(getppid),
            SyscallNum::NR_getrandom => handle!(getrandom),
            SyscallNum::NR_getsid => handle!(getsid),
            SyscallNum::NR_getsockname => handle!(getsockname),
            SyscallNum::NR_getsockopt => handle!(getsockopt),
            SyscallNum::NR_gettid => handle!(gettid),
            SyscallNum::NR_ioctl => handle!(ioctl),
            SyscallNum::NR_kill => handle!(kill),
            SyscallNum::NR_linkat => handle!(linkat),
            SyscallNum::NR_listen => handle!(listen),
            SyscallNum::NR_lseek => handle!(lseek),
            SyscallNum::NR_mkdirat => handle!(mkdirat),
            SyscallNum::NR_mknodat => handle!(mknodat),
            SyscallNum::NR_mmap => handle!(mmap),
            SyscallNum::NR_mprotect => handle!(mprotect),
            SyscallNum::NR_mremap => handle!(mremap),
            SyscallNum::NR_munmap => handle!(munmap),
            SyscallNum::NR_nanosleep => handle!(nanosleep),
            SyscallNum::NR_newfstatat => handle!(newfstatat),
            SyscallNum::NR_open => handle!(open),
            SyscallNum::NR_openat => handle!(openat),
            SyscallNum::NR_pipe => handle!(pipe),
            SyscallNum::NR_pipe2 => handle!(pipe2),
            SyscallNum::NR_poll => handle!(poll),
            SyscallNum::NR_ppoll => handle!(ppoll),
            SyscallNum::NR_prctl => handle!(prctl),
            SyscallNum::NR_pread64 => handle!(pread64),
            SyscallNum::NR_preadv => handle!(preadv),
            SyscallNum::NR_preadv2 => handle!(preadv2),
            SyscallNum::NR_prlimit64 => handle!(prlimit64),
            SyscallNum::NR_pselect6 => handle!(pselect6),
            SyscallNum::NR_pwrite64 => handle!(pwrite64),
            SyscallNum::NR_pwritev => handle!(pwritev),
            SyscallNum::NR_pwritev2 => handle!(pwritev2),
            SyscallNum::NR_read => handle!(read),
            SyscallNum::NR_readahead => handle!(readahead),
            SyscallNum::NR_readlinkat => handle!(readlinkat),
            SyscallNum::NR_readv => handle!(readv),
            SyscallNum::NR_recvfrom => handle!(recvfrom),
            SyscallNum::NR_recvmsg => handle!(recvmsg),
            SyscallNum::NR_renameat => handle!(renameat),
            SyscallNum::NR_renameat2 => handle!(renameat2),
            SyscallNum::NR_rseq => handle!(rseq),
            SyscallNum::NR_rt_sigaction => handle!(rt_sigaction),
            SyscallNum::NR_rt_sigprocmask => handle!(rt_sigprocmask),
            SyscallNum::NR_sched_getaffinity => handle!(sched_getaffinity),
            SyscallNum::NR_sched_setaffinity => handle!(sched_setaffinity),
            SyscallNum::NR_select => handle!(select),
            SyscallNum::NR_sendmsg => handle!(sendmsg),
            SyscallNum::NR_sendto => handle!(sendto),
            SyscallNum::NR_set_robust_list => handle!(set_robust_list),
            SyscallNum::NR_set_tid_address => handle!(set_tid_address),
            SyscallNum::NR_setitimer => handle!(setitimer),
            SyscallNum::NR_setpgid => handle!(setpgid),
            SyscallNum::NR_setsid => handle!(setsid),
            SyscallNum::NR_setsockopt => handle!(setsockopt),
            SyscallNum::NR_shutdown => handle!(shutdown),
            SyscallNum::NR_sigaltstack => handle!(sigaltstack),
            SyscallNum::NR_socket => handle!(socket),
            SyscallNum::NR_socketpair => handle!(socketpair),
            SyscallNum::NR_statx => handle!(statx),
            SyscallNum::NR_symlinkat => handle!(symlinkat),
            SyscallNum::NR_sync_file_range => handle!(sync_file_range),
            SyscallNum::NR_syncfs => handle!(syncfs),
            SyscallNum::NR_sysinfo => handle!(sysinfo),
            SyscallNum::NR_tgkill => handle!(tgkill),
            SyscallNum::NR_timerfd_create => handle!(timerfd_create),
            SyscallNum::NR_timerfd_gettime => handle!(timerfd_gettime),
            SyscallNum::NR_timerfd_settime => handle!(timerfd_settime),
            SyscallNum::NR_tkill => handle!(tkill),
            SyscallNum::NR_uname => handle!(uname),
            SyscallNum::NR_unlinkat => handle!(unlinkat),
            SyscallNum::NR_utimensat => handle!(utimensat),
            SyscallNum::NR_vfork => handle!(vfork),
            SyscallNum::NR_waitid => handle!(waitid),
            SyscallNum::NR_wait4 => handle!(wait4),
            SyscallNum::NR_write => handle!(write),
            SyscallNum::NR_writev => handle!(writev),
            //
            // CUSTOM SHADOW-SPECIFIC SYSCALLS
            //
            NR_shadow_hostname_to_addr_ipv4 => handle!(shadow_hostname_to_addr_ipv4),
            NR_shadow_init_memory_manager => handle!(shadow_init_memory_manager),
            NR_shadow_yield => handle!(shadow_yield),
            //
            // SHIM-ONLY SYSCALLS
            //
            SyscallNum::NR_clock_gettime
            | SyscallNum::NR_gettimeofday
            | SyscallNum::NR_sched_yield
            | SyscallNum::NR_time => {
                panic!(
                    "Syscall {} ({}) should have been handled in the shim",
                    syscall_name, ctx.args.number,
                )
            }
            //
            // NATIVE LINUX-HANDLED SYSCALLS
            //
            SyscallNum::NR_access
            | SyscallNum::NR_arch_prctl
            | SyscallNum::NR_chmod
            | SyscallNum::NR_chown
            | SyscallNum::NR_exit
            | SyscallNum::NR_getcwd
            | SyscallNum::NR_geteuid
            | SyscallNum::NR_getegid
            | SyscallNum::NR_getgid
            | SyscallNum::NR_getgroups
            | SyscallNum::NR_getresgid
            | SyscallNum::NR_getresuid
            | SyscallNum::NR_getrlimit
            | SyscallNum::NR_getuid
            | SyscallNum::NR_getxattr
            | SyscallNum::NR_lchown
            | SyscallNum::NR_lgetxattr
            | SyscallNum::NR_link
            | SyscallNum::NR_listxattr
            | SyscallNum::NR_llistxattr
            | SyscallNum::NR_lremovexattr
            | SyscallNum::NR_lsetxattr
            | SyscallNum::NR_lstat
            | SyscallNum::NR_madvise
            | SyscallNum::NR_mkdir
            | SyscallNum::NR_mknod
            | SyscallNum::NR_readlink
            | SyscallNum::NR_removexattr
            | SyscallNum::NR_rename
            | SyscallNum::NR_rmdir
            | SyscallNum::NR_rt_sigreturn
            | SyscallNum::NR_setfsgid
            | SyscallNum::NR_setfsuid
            | SyscallNum::NR_setgid
            | SyscallNum::NR_setregid
            | SyscallNum::NR_setresgid
            | SyscallNum::NR_setresuid
            | SyscallNum::NR_setreuid
            | SyscallNum::NR_setrlimit
            | SyscallNum::NR_setuid
            | SyscallNum::NR_setxattr
            | SyscallNum::NR_stat
            | SyscallNum::NR_statfs
            | SyscallNum::NR_symlink
            | SyscallNum::NR_truncate
            | SyscallNum::NR_unlink
            | SyscallNum::NR_utime
            | SyscallNum::NR_utimes => {
                log::trace!("Native syscall {} ({})", syscall_name, ctx.args.number);

                let rv = Err(SyscallError::Native);

                log_syscall_simple(
                    ctx.objs.process,
                    ctx.objs.process.strace_logging_options(),
                    ctx.objs.thread.id(),
                    syscall_name,
                    "...",
                    &rv,
                )
                .unwrap();

                rv
            }
            //
            // UNSUPPORTED SYSCALL
            //
            _ => {
                log_once_per_value_at_level!(
                    syscall,
                    SyscallNum, log::Level::Warn, log::Level::Debug,
                    "Detected unsupported syscall {} ({}) called from thread {} in process {} on host {}",
                    syscall_name,
                    ctx.args.number,
                    ctx.objs.thread.id(),
                    &*ctx.objs.process.plugin_name(),
                    ctx.objs.host.name(),
                );

                let rv = Err(Errno::ENOSYS.into());

                let (syscall_name, syscall_args) = match syscall.to_str() {
                    // log it in the form "poll(...)"
                    Some(syscall_name) => (syscall_name, Cow::Borrowed("...")),
                    // log it in the form "syscall(X, ...)"
                    None => ("syscall", Cow::Owned(format!("{}, ...", ctx.args.number))),
                };

                log_syscall_simple(
                    ctx.objs.process,
                    ctx.objs.process.strace_logging_options(),
                    ctx.objs.thread.id(),
                    syscall_name,
                    &syscall_args,
                    &rv,
                )
                .unwrap();

                rv
            }
        }
    }

    /// Did the last syscall result in `SyscallError::Blocked`? If called from a syscall handler and
    /// `is_blocked()` returns `true`, then the current syscall is the same syscall that previously
    /// blocked. For example, if currently running the `connect` syscall handler and `is_blocked()`
    /// is `true`, then the previous syscall handler that ran was also `connect` and it returned
    /// `SyscallError::Blocked`.
    pub fn is_blocked(&self) -> bool {
        self.blocked_syscall.is_some()
    }

    /// Internal helper that returns the `Descriptor` for the fd if it exists, otherwise returns
    /// EBADF.
    fn get_descriptor(
        descriptor_table: &DescriptorTable,
        fd: impl TryInto<DescriptorHandle>,
    ) -> Result<&Descriptor, linux_api::errno::Errno> {
        // check that fd is within bounds
        let fd = fd.try_into().or(Err(linux_api::errno::Errno::EBADF))?;

        match descriptor_table.get(fd) {
            Some(desc) => Ok(desc),
            None => Err(linux_api::errno::Errno::EBADF),
        }
    }

    /// Internal helper that returns the `Descriptor` for the fd if it exists, otherwise returns
    /// EBADF.
    fn get_descriptor_mut(
        descriptor_table: &mut DescriptorTable,
        fd: impl TryInto<DescriptorHandle>,
    ) -> Result<&mut Descriptor, linux_api::errno::Errno> {
        // check that fd is within bounds
        let fd = fd.try_into().or(Err(linux_api::errno::Errno::EBADF))?;

        match descriptor_table.get_mut(fd) {
            Some(desc) => Ok(desc),
            None => Err(linux_api::errno::Errno::EBADF),
        }
    }

    /// Run a legacy C syscall handler.
    fn legacy_syscall(syscall: LegacySyscallFn, ctx: &mut SyscallContext) -> SyscallResult {
        let rv: SyscallResult =
            unsafe { syscall(ctx.handler, std::ptr::from_ref(ctx.args)) }.into();

        // we need to flush pointers here so that the syscall formatter can reliably borrow process
        // memory without an incompatible borrow
        if rv.is_err() {
            // the syscall didn't complete successfully; don't write back pointers
            log::trace!("Syscall didn't complete successfully; discarding plugin ptrs without writing back.");
            ctx.objs.process.free_unsafe_borrows_noflush();
        } else {
            ctx.objs
                .process
                .free_unsafe_borrows_flush()
                .expect("flushing syscall ptrs");
        }

        rv
    }
}

impl std::ops::Drop for SyscallHandler {
    fn drop(&mut self) {
        #[cfg(feature = "perf_timers")]
        log::debug!(
            "Handled {} syscalls in {} seconds",
            self.num_syscalls,
            self.perf_duration_total.as_secs()
        );
        #[cfg(not(feature = "perf_timers"))]
        log::debug!("Handled {} syscalls", self.num_syscalls);

        if let Some(syscall_counter) = self.syscall_counter.as_mut() {
            // log the plugin thread specific counts
            log::debug!(
                "Thread {} syscall counts: {}",
                self.thread_id,
                syscall_counter,
            );

            // add up the counts at the worker level
            Worker::add_syscall_counts(syscall_counter);
        }

        unsafe { c::legacyfile_unref(self.epoll.ptr() as *mut std::ffi::c_void) };
    }
}

pub struct SyscallContext<'a, 'b> {
    pub objs: &'a ThreadContext<'b>,
    pub args: &'a SyscallArgs,
    pub handler: &'a mut SyscallHandler,
}

pub trait SyscallHandlerFn<T> {
    fn call(self, ctx: &mut SyscallContext) -> SyscallResult;
}

impl<F, T0> SyscallHandlerFn<()> for F
where
    F: Fn(&mut SyscallContext) -> Result<T0, SyscallError>,
    T0: Into<SyscallReg>,
{
    fn call(self, ctx: &mut SyscallContext) -> SyscallResult {
        self(ctx).map(Into::into)
    }
}

impl<F, T0, T1> SyscallHandlerFn<(T1,)> for F
where
    F: Fn(&mut SyscallContext, T1) -> Result<T0, SyscallError>,
    T0: Into<SyscallReg>,
    T1: From<SyscallReg>,
{
    fn call(self, ctx: &mut SyscallContext) -> SyscallResult {
        self(ctx, ctx.args.get(0).into()).map(Into::into)
    }
}

impl<F, T0, T1, T2> SyscallHandlerFn<(T1, T2)> for F
where
    F: Fn(&mut SyscallContext, T1, T2) -> Result<T0, SyscallError>,
    T0: Into<SyscallReg>,
    T1: From<SyscallReg>,
    T2: From<SyscallReg>,
{
    fn call(self, ctx: &mut SyscallContext) -> SyscallResult {
        self(ctx, ctx.args.get(0).into(), ctx.args.get(1).into()).map(Into::into)
    }
}

impl<F, T0, T1, T2, T3> SyscallHandlerFn<(T1, T2, T3)> for F
where
    F: Fn(&mut SyscallContext, T1, T2, T3) -> Result<T0, SyscallError>,
    T0: Into<SyscallReg>,
    T1: From<SyscallReg>,
    T2: From<SyscallReg>,
    T3: From<SyscallReg>,
{
    fn call(self, ctx: &mut SyscallContext) -> SyscallResult {
        self(
            ctx,
            ctx.args.get(0).into(),
            ctx.args.get(1).into(),
            ctx.args.get(2).into(),
        )
        .map(Into::into)
    }
}

impl<F, T0, T1, T2, T3, T4> SyscallHandlerFn<(T1, T2, T3, T4)> for F
where
    F: Fn(&mut SyscallContext, T1, T2, T3, T4) -> Result<T0, SyscallError>,
    T0: Into<SyscallReg>,
    T1: From<SyscallReg>,
    T2: From<SyscallReg>,
    T3: From<SyscallReg>,
    T4: From<SyscallReg>,
{
    fn call(self, ctx: &mut SyscallContext) -> SyscallResult {
        self(
            ctx,
            ctx.args.get(0).into(),
            ctx.args.get(1).into(),
            ctx.args.get(2).into(),
            ctx.args.get(3).into(),
        )
        .map(Into::into)
    }
}

impl<F, T0, T1, T2, T3, T4, T5> SyscallHandlerFn<(T1, T2, T3, T4, T5)> for F
where
    F: Fn(&mut SyscallContext, T1, T2, T3, T4, T5) -> Result<T0, SyscallError>,
    T0: Into<SyscallReg>,
    T1: From<SyscallReg>,
    T2: From<SyscallReg>,
    T3: From<SyscallReg>,
    T4: From<SyscallReg>,
    T5: From<SyscallReg>,
{
    fn call(self, ctx: &mut SyscallContext) -> SyscallResult {
        self(
            ctx,
            ctx.args.get(0).into(),
            ctx.args.get(1).into(),
            ctx.args.get(2).into(),
            ctx.args.get(3).into(),
            ctx.args.get(4).into(),
        )
        .map(Into::into)
    }
}

impl<F, T0, T1, T2, T3, T4, T5, T6> SyscallHandlerFn<(T1, T2, T3, T4, T5, T6)> for F
where
    F: Fn(&mut SyscallContext, T1, T2, T3, T4, T5, T6) -> Result<T0, SyscallError>,
    T0: Into<SyscallReg>,
    T1: From<SyscallReg>,
    T2: From<SyscallReg>,
    T3: From<SyscallReg>,
    T4: From<SyscallReg>,
    T5: From<SyscallReg>,
    T6: From<SyscallReg>,
{
    fn call(self, ctx: &mut SyscallContext) -> SyscallResult {
        self(
            ctx,
            ctx.args.get(0).into(),
            ctx.args.get(1).into(),
            ctx.args.get(2).into(),
            ctx.args.get(3).into(),
            ctx.args.get(4).into(),
            ctx.args.get(5).into(),
        )
        .map(Into::into)
    }
}

mod export {
    use crate::host::host::Host;
    use crate::host::process::Process;
    use crate::host::thread::Thread;

    use super::*;

    /// Returns a pointer to the current running host. The returned pointer is invalidated the next
    /// time the worker switches hosts. Rust syscall handlers should get the host from the
    /// [`SyscallContext`] instead.
    #[no_mangle]
    pub extern "C-unwind" fn rustsyscallhandler_getHost(sys: *const SyscallHandler) -> *const Host {
        let sys = unsafe { sys.as_ref() }.unwrap();
        Worker::with_active_host(|h| {
            assert_eq!(h.id(), sys.host_id);
            std::ptr::from_ref(h)
        })
        .unwrap()
    }

    /// Returns a pointer to the current running process. The returned pointer is invalidated the
    /// next time the worker switches processes. Rust syscall handlers should get the process from
    /// the [`SyscallContext`] instead.
    #[no_mangle]
    pub extern "C-unwind" fn rustsyscallhandler_getProcess(
        sys: *const SyscallHandler,
    ) -> *const Process {
        let sys = unsafe { sys.as_ref() }.unwrap();
        Worker::with_active_process(|p| {
            assert_eq!(p.id(), sys.process_id);
            std::ptr::from_ref(p)
        })
        .unwrap()
    }

    /// Returns a pointer to the current running thread. The returned pointer is invalidated the
    /// next time the worker switches threads. Rust syscall handlers should get the thread from the
    /// [`SyscallContext`] instead.
    #[no_mangle]
    pub extern "C-unwind" fn rustsyscallhandler_getThread(
        sys: *const SyscallHandler,
    ) -> *const Thread {
        let sys = unsafe { sys.as_ref() }.unwrap();
        Worker::with_active_thread(|t| {
            assert_eq!(t.id(), sys.thread_id);
            std::ptr::from_ref(t)
        })
        .unwrap()
    }

    #[no_mangle]
    pub extern "C-unwind" fn rustsyscallhandler_wasBlocked(sys: *const SyscallHandler) -> bool {
        let sys = unsafe { sys.as_ref() }.unwrap();
        sys.is_blocked()
    }

    #[no_mangle]
    pub extern "C-unwind" fn rustsyscallhandler_didListenTimeoutExpire(
        sys: *const SyscallHandler,
    ) -> bool {
        let sys = unsafe { sys.as_ref() }.unwrap();

        // will be `None` if the syscall condition doesn't exist or there's no timeout
        let timeout = Worker::with_active_thread(|t| {
            assert_eq!(t.id(), sys.thread_id);
            t.syscall_condition().and_then(|x| x.timeout())
        })
        .unwrap();

        // true if there is a timeout and it's before or at the current time
        timeout
            .map(|timeout| Worker::current_time().unwrap() >= timeout)
            .unwrap_or(false)
    }

    #[no_mangle]
    pub extern "C-unwind" fn rustsyscallhandler_getEpoll(
        sys: *const SyscallHandler,
    ) -> *mut c::Epoll {
        let sys = unsafe { sys.as_ref() }.unwrap();
        sys.epoll.ptr()
    }
}
