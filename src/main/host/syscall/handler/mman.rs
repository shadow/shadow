use syscall_logger::log_syscall;

use crate::cshadow;
use crate::host::context::ThreadContext;
use crate::host::syscall::handler::SyscallHandler;
use crate::host::syscall_types::{SysCallArgs, SyscallResult};

impl SyscallHandler {
    #[log_syscall(/* rv */ libc::c_int, /* addr */ *const libc::c_void)]
    pub fn brk(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        SyscallResult::from(unsafe {
            cshadow::syscallhandler_brk(
                ctx.thread.csyscallhandler(),
                args as *const cshadow::SysCallArgs,
            )
        })
    }

    #[log_syscall(/* rv */ *const libc::c_void, /* addr */ *const libc::c_void,
                  /* length */ libc::size_t, /* prot */ nix::sys::mman::ProtFlags,
                  /* flags */ nix::sys::mman::MapFlags, /* fd */ libc::c_int,
                  /* offset */ libc::off_t)]
    pub fn mmap(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        SyscallResult::from(unsafe {
            cshadow::syscallhandler_mmap(
                ctx.thread.csyscallhandler(),
                args as *const cshadow::SysCallArgs,
            )
        })
    }

    #[log_syscall(/* rv */ *const libc::c_void, /* old_address */ *const libc::c_void,
                  /* old_size */ libc::size_t, /* new_size */ libc::size_t,
                  /* flags */ nix::sys::mman::MRemapFlags, /* new_address */ *const libc::c_void)]
    pub fn mremap(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        SyscallResult::from(unsafe {
            cshadow::syscallhandler_mremap(
                ctx.thread.csyscallhandler(),
                args as *const cshadow::SysCallArgs,
            )
        })
    }

    #[log_syscall(/* rv */ libc::c_int, /* addr */ *const libc::c_void, /* length */ libc::size_t)]
    pub fn munmap(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        SyscallResult::from(unsafe {
            cshadow::syscallhandler_munmap(
                ctx.thread.csyscallhandler(),
                args as *const cshadow::SysCallArgs,
            )
        })
    }

    #[log_syscall(/* rv */ libc::c_int, /* addr */ *const libc::c_void, /* len */ libc::size_t,
                  /* prot */ libc::c_int)]
    pub fn mprotect(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        SyscallResult::from(unsafe {
            cshadow::syscallhandler_mprotect(
                ctx.thread.csyscallhandler(),
                args as *const cshadow::SysCallArgs,
            )
        })
    }
}
