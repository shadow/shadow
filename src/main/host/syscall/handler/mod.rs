use crate::cshadow as c;
use crate::host::context::{ThreadContext, ThreadContextObjs};
use crate::host::descriptor::Descriptor;
use crate::host::process::Process;
use crate::host::syscall_types::SysCallArgs;
use crate::host::syscall_types::SyscallResult;

use nix::errno::Errno;

mod eventfd;
mod fcntl;
mod file;
mod ioctl;
mod random;
mod sched;
mod socket;
mod sysinfo;
mod time;
mod unistd;

pub struct SyscallHandler {
    // Will eventually contain syscall handler state once migrated from the c handler
}

impl SyscallHandler {
    pub fn new() -> SyscallHandler {
        SyscallHandler {}
    }

    pub fn syscall(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        match args.number {
            libc::SYS_accept => self.accept(ctx, args),
            libc::SYS_accept4 => self.accept4(ctx, args),
            libc::SYS_bind => self.bind(ctx, args),
            libc::SYS_close => self.close(ctx, args),
            libc::SYS_connect => self.connect(ctx, args),
            libc::SYS_dup => self.dup(ctx, args),
            libc::SYS_dup2 => self.dup2(ctx, args),
            libc::SYS_dup3 => self.dup3(ctx, args),
            libc::SYS_eventfd => self.eventfd(ctx, args),
            libc::SYS_eventfd2 => self.eventfd2(ctx, args),
            libc::SYS_fcntl => self.fcntl(ctx, args),
            libc::SYS_getitimer => self.getitimer(ctx, args),
            libc::SYS_getpeername => self.getpeername(ctx, args),
            libc::SYS_getrandom => self.getrandom(ctx, args),
            libc::SYS_getsockname => self.getsockname(ctx, args),
            libc::SYS_getsockopt => self.getsockopt(ctx, args),
            libc::SYS_ioctl => self.ioctl(ctx, args),
            libc::SYS_listen => self.listen(ctx, args),
            libc::SYS_open => self.open(ctx, args),
            libc::SYS_openat => self.openat(ctx, args),
            libc::SYS_pipe => self.pipe(ctx, args),
            libc::SYS_pipe2 => self.pipe2(ctx, args),
            libc::SYS_pread64 => self.pread64(ctx, args),
            libc::SYS_pwrite64 => self.pwrite64(ctx, args),
            libc::SYS_rseq => self.rseq(ctx, args),
            libc::SYS_read => self.read(ctx, args),
            libc::SYS_recvfrom => self.recvfrom(ctx, args),
            libc::SYS_sched_yield => self.sched_yield(ctx, args),
            libc::SYS_sendto => self.sendto(ctx, args),
            libc::SYS_setitimer => self.setitimer(ctx, args),
            libc::SYS_setsockopt => self.setsockopt(ctx, args),
            libc::SYS_shutdown => self.shutdown(ctx, args),
            libc::SYS_socket => self.socket(ctx, args),
            libc::SYS_socketpair => self.socketpair(ctx, args),
            libc::SYS_sysinfo => self.sysinfo(ctx, args),
            libc::SYS_write => self.write(ctx, args),
            _ => {
                // if we added a HANDLE_RUST() macro for this syscall in
                // 'syscallhandler_make_syscall()' but didn't add an entry here, we should get a
                // warning
                log::warn!("Rust syscall {} is not mapped", args.number);
                Err(Errno::ENOSYS.into())
            }
        }
    }

    /// Internal helper that returns the `Descriptor` for the fd if it exists, otherwise returns
    /// EBADF.
    fn get_descriptor(
        process: &Process,
        fd: impl TryInto<u32>,
    ) -> Result<&Descriptor, nix::errno::Errno> {
        // check that fd is within bounds
        let fd: u32 = fd.try_into().map_err(|_| nix::errno::Errno::EBADF)?;

        match process.get_descriptor(fd) {
            Some(desc) => Ok(desc),
            None => Err(nix::errno::Errno::EBADF),
        }
    }

    /// Internal helper that returns the `Descriptor` for the fd if it exists, otherwise returns
    /// EBADF.
    fn get_descriptor_mut(
        process: &mut Process,
        fd: impl TryInto<u32>,
    ) -> Result<&mut Descriptor, nix::errno::Errno> {
        // check that fd is within bounds
        let fd: u32 = fd.try_into().map_err(|_| nix::errno::Errno::EBADF)?;

        match process.get_descriptor_mut(fd) {
            Some(desc) => Ok(desc),
            None => Err(nix::errno::Errno::EBADF),
        }
    }
}

mod export {
    use crate::{core::worker::Worker, utility::notnull::notnull_mut_debug};

    use super::*;

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_new() -> *mut SyscallHandler {
        Box::into_raw(Box::new(SyscallHandler::new()))
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_free(handler_ptr: *mut SyscallHandler) {
        if handler_ptr.is_null() {
            return;
        }
        unsafe { Box::from_raw(handler_ptr) };
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_syscall(
        sys: *mut SyscallHandler,
        csys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        assert!(!sys.is_null());
        let sys = unsafe { &mut *sys };
        Worker::with_active_host(|host| {
            let mut objs =
                unsafe { ThreadContextObjs::from_syscallhandler(host, notnull_mut_debug(csys)) };
            sys.syscall(&mut objs.borrow(), unsafe { args.as_ref().unwrap() })
                .into()
        })
        .unwrap()
    }
}
