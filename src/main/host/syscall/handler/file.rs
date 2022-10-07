use syscall_logger::log_syscall;

use crate::cshadow;
use crate::host::context::ThreadContext;
use crate::host::syscall::handler::SyscallHandler;
use crate::host::syscall_types::{SysCallArgs, SyscallResult};

impl SyscallHandler {
    #[log_syscall(/* rv */ libc::c_int, /* pathname */ *const libc::c_char,
                  /* flags */ nix::fcntl::OFlag, /* mode */ nix::sys::stat::Mode)]
    pub fn open(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        SyscallResult::from(unsafe {
            cshadow::syscallhandler_open(
                ctx.thread.csyscallhandler(),
                args as *const cshadow::SysCallArgs,
            )
        })
    }

    #[log_syscall(/* rv */ libc::c_int, /* dirfd */ libc::c_int, /* pathname */ *const libc::c_char,
                  /* flags */ nix::fcntl::OFlag, /* mode */ nix::sys::stat::Mode)]
    pub fn openat(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        SyscallResult::from(unsafe {
            cshadow::syscallhandler_openat(
                ctx.thread.csyscallhandler(),
                args as *const cshadow::SysCallArgs,
            )
        })
    }
}
