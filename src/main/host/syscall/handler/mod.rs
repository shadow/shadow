use crate::cshadow as c;
use crate::host::context::{ThreadContext, ThreadContextObjs};
use crate::host::descriptor::CompatDescriptor;
use crate::host::process::Process;
use crate::host::syscall_types::SysCallArgs;
use crate::host::syscall_types::{SyscallError, SyscallResult};

use nix::errno::Errno;

mod eventfd;
mod fcntl;
mod ioctl;
mod random;
mod socket;
mod sysinfo;
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
            libc::SYS_bind => self.bind(ctx, args),
            libc::SYS_close => self.close(ctx, args),
            libc::SYS_dup => self.dup(ctx, args),
            libc::SYS_dup2 => self.dup2(ctx, args),
            libc::SYS_dup3 => self.dup3(ctx, args),
            libc::SYS_eventfd => self.eventfd(ctx, args),
            libc::SYS_eventfd2 => self.eventfd2(ctx, args),
            libc::SYS_fcntl => self.fcntl(ctx, args),
            libc::SYS_getpeername => self.getpeername(ctx, args),
            libc::SYS_getrandom => self.getrandom(ctx, args),
            libc::SYS_getsockname => self.getsockname(ctx, args),
            libc::SYS_ioctl => self.ioctl(ctx, args),
            libc::SYS_pipe => self.pipe(ctx, args),
            libc::SYS_pipe2 => self.pipe2(ctx, args),
            libc::SYS_pread64 => self.pread64(ctx, args),
            libc::SYS_pwrite64 => self.pwrite64(ctx, args),
            libc::SYS_read => self.read(ctx, args),
            libc::SYS_recvfrom => self.recvfrom(ctx, args),
            libc::SYS_sendto => self.sendto(ctx, args),
            libc::SYS_socket => self.socket(ctx, args),
            libc::SYS_socketpair => self.socketpair(ctx, args),
            libc::SYS_sysinfo => self.sysinfo(ctx, args),
            libc::SYS_write => self.write(ctx, args),
            _ => Err(SyscallError::from(Errno::ENOSYS)),
        }
    }

    /// Internal helper that returns the `CompatDescriptor` for the fd if it
    /// exists, otherwise returns EBADF.
    fn get_descriptor(
        process: &Process,
        fd: impl TryInto<u32>,
    ) -> Result<&CompatDescriptor, nix::errno::Errno> {
        // check that fd is within bounds
        let fd: u32 = fd.try_into().map_err(|_| nix::errno::Errno::EBADF)?;

        match process.get_descriptor(fd) {
            Some(desc) => Ok(desc),
            None => Err(nix::errno::Errno::EBADF),
        }
    }

    /// Internal helper that returns the `CompatDescriptor` for the fd if it
    /// exists, otherwise returns EBADF.
    fn get_descriptor_mut(
        process: &mut Process,
        fd: impl TryInto<u32>,
    ) -> Result<&mut CompatDescriptor, nix::errno::Errno> {
        // check that fd is within bounds
        let fd: u32 = fd.try_into().map_err(|_| nix::errno::Errno::EBADF)?;

        match process.get_descriptor_mut(fd) {
            Some(desc) => Ok(desc),
            None => Err(nix::errno::Errno::EBADF),
        }
    }
}

mod export {
    use crate::utility::notnull::notnull_mut_debug;

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
        unsafe {
            Box::from_raw(handler_ptr);
        }
    }

    #[no_mangle]
    pub extern "C" fn rustsyscallhandler_syscall(
        sys: *mut SyscallHandler,
        csys: *mut c::SysCallHandler,
        args: *const c::SysCallArgs,
    ) -> c::SysCallReturn {
        assert!(!sys.is_null());
        let sys = unsafe { &mut *sys };
        let mut objs = unsafe { ThreadContextObjs::from_syscallhandler(notnull_mut_debug(csys)) };
        sys.syscall(&mut objs.borrow(), unsafe { args.as_ref().unwrap() })
            .into()
    }
}
