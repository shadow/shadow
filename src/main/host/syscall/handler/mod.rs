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
mod ioctl;
mod mman;
mod prctl;
mod random;
mod resource;
mod sched;
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

    pub fn syscall(&self, mut ctx: SyscallContext) -> SyscallResult {
        match ctx.args.number {
            libc::SYS_accept => SyscallHandlerFn::call(Self::accept, &mut ctx),
            libc::SYS_accept4 => SyscallHandlerFn::call(Self::accept4, &mut ctx),
            libc::SYS_bind => SyscallHandlerFn::call(Self::bind, &mut ctx),
            libc::SYS_brk => SyscallHandlerFn::call(Self::brk, &mut ctx),
            libc::SYS_clock_getres => SyscallHandlerFn::call(Self::clock_getres, &mut ctx),
            libc::SYS_clock_nanosleep => SyscallHandlerFn::call(Self::clock_nanosleep, &mut ctx),
            libc::SYS_clone => SyscallHandlerFn::call(Self::clone, &mut ctx),
            libc::SYS_clone3 => SyscallHandlerFn::call(Self::clone3, &mut ctx),
            libc::SYS_close => SyscallHandlerFn::call(Self::close, &mut ctx),
            libc::SYS_connect => SyscallHandlerFn::call(Self::connect, &mut ctx),
            libc::SYS_dup => SyscallHandlerFn::call(Self::dup, &mut ctx),
            libc::SYS_dup2 => SyscallHandlerFn::call(Self::dup2, &mut ctx),
            libc::SYS_dup3 => SyscallHandlerFn::call(Self::dup3, &mut ctx),
            libc::SYS_epoll_create => SyscallHandlerFn::call(Self::epoll_create, &mut ctx),
            libc::SYS_epoll_create1 => SyscallHandlerFn::call(Self::epoll_create1, &mut ctx),
            libc::SYS_epoll_ctl => SyscallHandlerFn::call(Self::epoll_ctl, &mut ctx),
            libc::SYS_epoll_pwait => SyscallHandlerFn::call(Self::epoll_pwait, &mut ctx),
            libc::SYS_epoll_pwait2 => SyscallHandlerFn::call(Self::epoll_pwait2, &mut ctx),
            libc::SYS_epoll_wait => SyscallHandlerFn::call(Self::epoll_wait, &mut ctx),
            libc::SYS_eventfd => SyscallHandlerFn::call(Self::eventfd, &mut ctx),
            libc::SYS_eventfd2 => SyscallHandlerFn::call(Self::eventfd2, &mut ctx),
            libc::SYS_execve => SyscallHandlerFn::call(Self::execve, &mut ctx),
            libc::SYS_execveat => SyscallHandlerFn::call(Self::execveat, &mut ctx),
            libc::SYS_exit_group => SyscallHandlerFn::call(Self::exit_group, &mut ctx),
            libc::SYS_fcntl => SyscallHandlerFn::call(Self::fcntl, &mut ctx),
            libc::SYS_fork => SyscallHandlerFn::call(Self::fork, &mut ctx),
            libc::SYS_getitimer => SyscallHandlerFn::call(Self::getitimer, &mut ctx),
            libc::SYS_getpeername => SyscallHandlerFn::call(Self::getpeername, &mut ctx),
            libc::SYS_getpgid => SyscallHandlerFn::call(Self::getpgid, &mut ctx),
            libc::SYS_getpgrp => SyscallHandlerFn::call(Self::getpgrp, &mut ctx),
            libc::SYS_getpid => SyscallHandlerFn::call(Self::getpid, &mut ctx),
            libc::SYS_getppid => SyscallHandlerFn::call(Self::getppid, &mut ctx),
            libc::SYS_getrandom => SyscallHandlerFn::call(Self::getrandom, &mut ctx),
            libc::SYS_getsid => SyscallHandlerFn::call(Self::getsid, &mut ctx),
            libc::SYS_getsockname => SyscallHandlerFn::call(Self::getsockname, &mut ctx),
            libc::SYS_getsockopt => SyscallHandlerFn::call(Self::getsockopt, &mut ctx),
            libc::SYS_gettid => SyscallHandlerFn::call(Self::gettid, &mut ctx),
            libc::SYS_ioctl => SyscallHandlerFn::call(Self::ioctl, &mut ctx),
            libc::SYS_listen => SyscallHandlerFn::call(Self::listen, &mut ctx),
            libc::SYS_mmap => SyscallHandlerFn::call(Self::mmap, &mut ctx),
            libc::SYS_mprotect => SyscallHandlerFn::call(Self::mprotect, &mut ctx),
            libc::SYS_mremap => SyscallHandlerFn::call(Self::mremap, &mut ctx),
            libc::SYS_munmap => SyscallHandlerFn::call(Self::munmap, &mut ctx),
            libc::SYS_nanosleep => SyscallHandlerFn::call(Self::nanosleep, &mut ctx),
            libc::SYS_open => SyscallHandlerFn::call(Self::open, &mut ctx),
            libc::SYS_openat => SyscallHandlerFn::call(Self::openat, &mut ctx),
            libc::SYS_pipe => SyscallHandlerFn::call(Self::pipe, &mut ctx),
            libc::SYS_pipe2 => SyscallHandlerFn::call(Self::pipe2, &mut ctx),
            libc::SYS_prctl => SyscallHandlerFn::call(Self::prctl, &mut ctx),
            libc::SYS_pread64 => SyscallHandlerFn::call(Self::pread64, &mut ctx),
            libc::SYS_preadv => SyscallHandlerFn::call(Self::preadv, &mut ctx),
            libc::SYS_preadv2 => SyscallHandlerFn::call(Self::preadv2, &mut ctx),
            libc::SYS_prlimit64 => SyscallHandlerFn::call(Self::prlimit64, &mut ctx),
            libc::SYS_pwrite64 => SyscallHandlerFn::call(Self::pwrite64, &mut ctx),
            libc::SYS_pwritev => SyscallHandlerFn::call(Self::pwritev, &mut ctx),
            libc::SYS_pwritev2 => SyscallHandlerFn::call(Self::pwritev2, &mut ctx),
            libc::SYS_rseq => SyscallHandlerFn::call(Self::rseq, &mut ctx),
            libc::SYS_read => SyscallHandlerFn::call(Self::read, &mut ctx),
            libc::SYS_readv => SyscallHandlerFn::call(Self::readv, &mut ctx),
            libc::SYS_recvfrom => SyscallHandlerFn::call(Self::recvfrom, &mut ctx),
            libc::SYS_recvmsg => SyscallHandlerFn::call(Self::recvmsg, &mut ctx),
            libc::SYS_sched_getaffinity => {
                SyscallHandlerFn::call(Self::sched_getaffinity, &mut ctx)
            }
            libc::SYS_sched_setaffinity => {
                SyscallHandlerFn::call(Self::sched_setaffinity, &mut ctx)
            }
            libc::SYS_sched_yield => SyscallHandlerFn::call(Self::sched_yield, &mut ctx),
            libc::SYS_sendmsg => SyscallHandlerFn::call(Self::sendmsg, &mut ctx),
            libc::SYS_sendto => SyscallHandlerFn::call(Self::sendto, &mut ctx),
            libc::SYS_setitimer => SyscallHandlerFn::call(Self::setitimer, &mut ctx),
            libc::SYS_setpgid => SyscallHandlerFn::call(Self::setpgid, &mut ctx),
            libc::SYS_setsid => SyscallHandlerFn::call(Self::setsid, &mut ctx),
            libc::SYS_setsockopt => SyscallHandlerFn::call(Self::setsockopt, &mut ctx),
            libc::SYS_shutdown => SyscallHandlerFn::call(Self::shutdown, &mut ctx),
            libc::SYS_socket => SyscallHandlerFn::call(Self::socket, &mut ctx),
            libc::SYS_socketpair => SyscallHandlerFn::call(Self::socketpair, &mut ctx),
            libc::SYS_sysinfo => SyscallHandlerFn::call(Self::sysinfo, &mut ctx),
            libc::SYS_timerfd_create => SyscallHandlerFn::call(Self::timerfd_create, &mut ctx),
            libc::SYS_timerfd_gettime => SyscallHandlerFn::call(Self::timerfd_gettime, &mut ctx),
            libc::SYS_timerfd_settime => SyscallHandlerFn::call(Self::timerfd_settime, &mut ctx),
            libc::SYS_vfork => SyscallHandlerFn::call(Self::vfork, &mut ctx),
            libc::SYS_waitid => SyscallHandlerFn::call(Self::waitid, &mut ctx),
            libc::SYS_wait4 => SyscallHandlerFn::call(Self::wait4, &mut ctx),
            libc::SYS_write => SyscallHandlerFn::call(Self::write, &mut ctx),
            libc::SYS_writev => SyscallHandlerFn::call(Self::writev, &mut ctx),
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
