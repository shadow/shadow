use linux_api::errno::Errno;
use shadow_shim_helper_rs::syscall_types::SysCallArgs;
use shadow_shim_helper_rs::syscall_types::SysCallReg;

use crate::cshadow as c;
use crate::host::context::{ThreadContext, ThreadContextObjs};
use crate::host::descriptor::descriptor_table::{DescriptorHandle, DescriptorTable};
use crate::host::descriptor::Descriptor;
use crate::host::syscall_types::SyscallReturn;
use crate::host::syscall_types::{SyscallError, SyscallResult};

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
    unsafe extern "C-unwind" fn(*mut c::SysCallHandler, *const SysCallArgs) -> SyscallReturn;

pub struct SyscallHandler {
    // Will eventually contain syscall handler state once migrated from the c handler
}

impl SyscallHandler {
    #[allow(clippy::new_without_default)]
    pub fn new() -> SyscallHandler {
        SyscallHandler {}
    }

    #[allow(non_upper_case_globals)]
    pub fn syscall(&self, mut ctx: SyscallContext) -> SyscallResult {
        const SYS_shadow_yield: i64 = c::ShadowSyscallNum_SYS_shadow_yield as i64;
        const SYS_shadow_init_memory_manager: i64 =
            c::ShadowSyscallNum_SYS_shadow_init_memory_manager as i64;
        const SYS_shadow_hostname_to_addr_ipv4: i64 =
            c::ShadowSyscallNum_SYS_shadow_hostname_to_addr_ipv4 as i64;

        macro_rules! handle {
            ($f:ident) => {{
                SyscallHandlerFn::call(Self::$f, &mut ctx)
            }};
        }

        match ctx.args.number {
            SYS_shadow_hostname_to_addr_ipv4 => handle!(shadow_hostname_to_addr_ipv4),
            SYS_shadow_init_memory_manager => handle!(shadow_init_memory_manager),
            SYS_shadow_yield => handle!(shadow_yield),
            libc::SYS_accept => handle!(accept),
            libc::SYS_accept4 => handle!(accept4),
            libc::SYS_bind => handle!(bind),
            libc::SYS_brk => handle!(brk),
            libc::SYS_clock_getres => handle!(clock_getres),
            libc::SYS_clock_nanosleep => handle!(clock_nanosleep),
            libc::SYS_clone => handle!(clone),
            libc::SYS_clone3 => handle!(clone3),
            libc::SYS_close => handle!(close),
            libc::SYS_connect => handle!(connect),
            libc::SYS_creat => handle!(creat),
            libc::SYS_dup => handle!(dup),
            libc::SYS_dup2 => handle!(dup2),
            libc::SYS_dup3 => handle!(dup3),
            libc::SYS_epoll_create => handle!(epoll_create),
            libc::SYS_epoll_create1 => handle!(epoll_create1),
            libc::SYS_epoll_ctl => handle!(epoll_ctl),
            libc::SYS_epoll_pwait => handle!(epoll_pwait),
            libc::SYS_epoll_pwait2 => handle!(epoll_pwait2),
            libc::SYS_epoll_wait => handle!(epoll_wait),
            libc::SYS_eventfd => handle!(eventfd),
            libc::SYS_eventfd2 => handle!(eventfd2),
            libc::SYS_execve => handle!(execve),
            libc::SYS_execveat => handle!(execveat),
            libc::SYS_exit_group => handle!(exit_group),
            libc::SYS_faccessat => handle!(faccessat),
            libc::SYS_fadvise64 => handle!(fadvise64),
            libc::SYS_fallocate => handle!(fallocate),
            libc::SYS_fchmod => handle!(fchmod),
            libc::SYS_fchmodat => handle!(fchmodat),
            libc::SYS_fchown => handle!(fchown),
            libc::SYS_fchownat => handle!(fchownat),
            libc::SYS_fcntl => handle!(fcntl),
            libc::SYS_fdatasync => handle!(fdatasync),
            libc::SYS_fgetxattr => handle!(fgetxattr),
            libc::SYS_flistxattr => handle!(flistxattr),
            libc::SYS_flock => handle!(flock),
            libc::SYS_fork => handle!(fork),
            libc::SYS_fremovexattr => handle!(fremovexattr),
            libc::SYS_fsetxattr => handle!(fsetxattr),
            libc::SYS_fstat => handle!(fstat),
            libc::SYS_fstatfs => handle!(fstatfs),
            libc::SYS_fsync => handle!(fsync),
            libc::SYS_ftruncate => handle!(ftruncate),
            libc::SYS_futex => handle!(futex),
            libc::SYS_futimesat => handle!(futimesat),
            libc::SYS_get_robust_list => handle!(get_robust_list),
            libc::SYS_getdents => handle!(getdents),
            libc::SYS_getdents64 => handle!(getdents64),
            libc::SYS_getitimer => handle!(getitimer),
            libc::SYS_getpeername => handle!(getpeername),
            libc::SYS_getpgid => handle!(getpgid),
            libc::SYS_getpgrp => handle!(getpgrp),
            libc::SYS_getpid => handle!(getpid),
            libc::SYS_getppid => handle!(getppid),
            libc::SYS_getrandom => handle!(getrandom),
            libc::SYS_getsid => handle!(getsid),
            libc::SYS_getsockname => handle!(getsockname),
            libc::SYS_getsockopt => handle!(getsockopt),
            libc::SYS_gettid => handle!(gettid),
            libc::SYS_ioctl => handle!(ioctl),
            libc::SYS_kill => handle!(kill),
            libc::SYS_linkat => handle!(linkat),
            libc::SYS_listen => handle!(listen),
            libc::SYS_lseek => handle!(lseek),
            libc::SYS_mkdirat => handle!(mkdirat),
            libc::SYS_mknodat => handle!(mknodat),
            libc::SYS_mmap => handle!(mmap),
            libc::SYS_mprotect => handle!(mprotect),
            libc::SYS_mremap => handle!(mremap),
            libc::SYS_munmap => handle!(munmap),
            libc::SYS_nanosleep => handle!(nanosleep),
            libc::SYS_newfstatat => handle!(newfstatat),
            libc::SYS_open => handle!(open),
            libc::SYS_openat => handle!(openat),
            libc::SYS_pipe => handle!(pipe),
            libc::SYS_pipe2 => handle!(pipe2),
            libc::SYS_poll => handle!(poll),
            libc::SYS_ppoll => handle!(ppoll),
            libc::SYS_prctl => handle!(prctl),
            libc::SYS_pread64 => handle!(pread64),
            libc::SYS_preadv => handle!(preadv),
            libc::SYS_preadv2 => handle!(preadv2),
            libc::SYS_prlimit64 => handle!(prlimit64),
            libc::SYS_pselect6 => handle!(pselect6),
            libc::SYS_pwrite64 => handle!(pwrite64),
            libc::SYS_pwritev => handle!(pwritev),
            libc::SYS_pwritev2 => handle!(pwritev2),
            libc::SYS_read => handle!(read),
            libc::SYS_readahead => handle!(readahead),
            libc::SYS_readlinkat => handle!(readlinkat),
            libc::SYS_readv => handle!(readv),
            libc::SYS_recvfrom => handle!(recvfrom),
            libc::SYS_recvmsg => handle!(recvmsg),
            libc::SYS_renameat => handle!(renameat),
            libc::SYS_renameat2 => handle!(renameat2),
            libc::SYS_rseq => handle!(rseq),
            libc::SYS_rt_sigaction => handle!(rt_sigaction),
            libc::SYS_rt_sigprocmask => handle!(rt_sigprocmask),
            libc::SYS_sched_getaffinity => handle!(sched_getaffinity),
            libc::SYS_sched_setaffinity => handle!(sched_setaffinity),
            libc::SYS_sched_yield => handle!(sched_yield),
            libc::SYS_select => handle!(select),
            libc::SYS_sendmsg => handle!(sendmsg),
            libc::SYS_sendto => handle!(sendto),
            libc::SYS_set_robust_list => handle!(set_robust_list),
            libc::SYS_set_tid_address => handle!(set_tid_address),
            libc::SYS_setitimer => handle!(setitimer),
            libc::SYS_setpgid => handle!(setpgid),
            libc::SYS_setsid => handle!(setsid),
            libc::SYS_setsockopt => handle!(setsockopt),
            libc::SYS_shutdown => handle!(shutdown),
            libc::SYS_sigaltstack => handle!(sigaltstack),
            libc::SYS_socket => handle!(socket),
            libc::SYS_socketpair => handle!(socketpair),
            libc::SYS_statx => handle!(statx),
            libc::SYS_symlinkat => handle!(symlinkat),
            libc::SYS_sync_file_range => handle!(sync_file_range),
            libc::SYS_syncfs => handle!(syncfs),
            libc::SYS_sysinfo => handle!(sysinfo),
            libc::SYS_tgkill => handle!(tgkill),
            libc::SYS_timerfd_create => handle!(timerfd_create),
            libc::SYS_timerfd_gettime => handle!(timerfd_gettime),
            libc::SYS_timerfd_settime => handle!(timerfd_settime),
            libc::SYS_tkill => handle!(tkill),
            libc::SYS_uname => handle!(uname),
            libc::SYS_unlinkat => handle!(unlinkat),
            libc::SYS_utimensat => handle!(utimensat),
            libc::SYS_vfork => handle!(vfork),
            libc::SYS_waitid => handle!(waitid),
            libc::SYS_wait4 => handle!(wait4),
            libc::SYS_write => handle!(write),
            libc::SYS_writev => handle!(writev),
            _ => {
                // if we added a HANDLE_RUST() macro for this syscall in
                // 'syscallhandler_make_syscall()' but didn't add an entry here, we should get a
                // warning
                log::warn!("Rust syscall {} is not mapped", ctx.args.number);
                Err(Errno::ENOSYS.into())
            }
        }
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
        unsafe { syscall(ctx.objs.thread.csyscallhandler(), ctx.args as *const _) }.into()
    }
}

pub struct SyscallContext<'a, 'b> {
    pub objs: &'a mut ThreadContext<'b>,
    pub args: &'a SysCallArgs,
}

pub trait SyscallHandlerFn<T> {
    fn call(self, ctx: &mut SyscallContext) -> SyscallResult;
}

impl<F, T0> SyscallHandlerFn<()> for F
where
    F: Fn(&mut SyscallContext) -> Result<T0, SyscallError>,
    T0: Into<SysCallReg>,
{
    fn call(self, ctx: &mut SyscallContext) -> SyscallResult {
        self(ctx).map(Into::into)
    }
}

impl<F, T0, T1> SyscallHandlerFn<(T1,)> for F
where
    F: Fn(&mut SyscallContext, T1) -> Result<T0, SyscallError>,
    T0: Into<SysCallReg>,
    T1: From<SysCallReg>,
{
    fn call(self, ctx: &mut SyscallContext) -> SyscallResult {
        self(ctx, ctx.args.get(0).into()).map(Into::into)
    }
}

impl<F, T0, T1, T2> SyscallHandlerFn<(T1, T2)> for F
where
    F: Fn(&mut SyscallContext, T1, T2) -> Result<T0, SyscallError>,
    T0: Into<SysCallReg>,
    T1: From<SysCallReg>,
    T2: From<SysCallReg>,
{
    fn call(self, ctx: &mut SyscallContext) -> SyscallResult {
        self(ctx, ctx.args.get(0).into(), ctx.args.get(1).into()).map(Into::into)
    }
}

impl<F, T0, T1, T2, T3> SyscallHandlerFn<(T1, T2, T3)> for F
where
    F: Fn(&mut SyscallContext, T1, T2, T3) -> Result<T0, SyscallError>,
    T0: Into<SysCallReg>,
    T1: From<SysCallReg>,
    T2: From<SysCallReg>,
    T3: From<SysCallReg>,
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
    T0: Into<SysCallReg>,
    T1: From<SysCallReg>,
    T2: From<SysCallReg>,
    T3: From<SysCallReg>,
    T4: From<SysCallReg>,
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
    T0: Into<SysCallReg>,
    T1: From<SysCallReg>,
    T2: From<SysCallReg>,
    T3: From<SysCallReg>,
    T4: From<SysCallReg>,
    T5: From<SysCallReg>,
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
    T0: Into<SysCallReg>,
    T1: From<SysCallReg>,
    T2: From<SysCallReg>,
    T3: From<SysCallReg>,
    T4: From<SysCallReg>,
    T5: From<SysCallReg>,
    T6: From<SysCallReg>,
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
    use shadow_shim_helper_rs::notnull::*;

    use super::*;
    use crate::core::worker::Worker;

    #[no_mangle]
    pub extern "C-unwind" fn rustsyscallhandler_new() -> *mut SyscallHandler {
        Box::into_raw(Box::new(SyscallHandler::new()))
    }

    #[no_mangle]
    pub extern "C-unwind" fn rustsyscallhandler_free(handler_ptr: *mut SyscallHandler) {
        if handler_ptr.is_null() {
            return;
        }
        drop(unsafe { Box::from_raw(handler_ptr) });
    }

    #[no_mangle]
    pub extern "C-unwind" fn rustsyscallhandler_syscall(
        sys: *mut SyscallHandler,
        csys: *mut c::SysCallHandler,
        args: *const SysCallArgs,
    ) -> SyscallReturn {
        assert!(!sys.is_null());
        let sys = unsafe { &mut *sys };
        Worker::with_active_host(|host| {
            let mut objs =
                unsafe { ThreadContextObjs::from_syscallhandler(host, notnull_mut_debug(csys)) };
            objs.with_ctx(|ctx| {
                let ctx = SyscallContext {
                    objs: ctx,
                    args: unsafe { args.as_ref().unwrap() },
                };
                sys.syscall(ctx).into()
            })
        })
        .unwrap()
    }
}
