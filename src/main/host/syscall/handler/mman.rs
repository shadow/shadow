use linux_api::posix_types::kernel_off_t;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;
use syscall_logger::log_syscall;

use crate::cshadow;
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall_types::SyscallResult;

impl SyscallHandler {
    #[log_syscall(/* rv */ std::ffi::c_int, /* addr */ *const std::ffi::c_void)]
    pub fn brk(ctx: &mut SyscallContext, _addr: ForeignPtr<()>) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_brk, ctx)
    }

    #[log_syscall(/* rv */ *const std::ffi::c_void, /* addr */ *const std::ffi::c_void,
                  /* length */ usize, /* prot */ nix::sys::mman::ProtFlags,
                  /* flags */ nix::sys::mman::MapFlags, /* fd */ std::ffi::c_int,
                  /* offset */ kernel_off_t)]
    pub fn mmap(
        ctx: &mut SyscallContext,
        _addr: ForeignPtr<()>,
        _len: usize,
        _prot: std::ffi::c_int,
        _flags: std::ffi::c_int,
        _fd: std::ffi::c_int,
        _offset: kernel_off_t,
    ) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_mmap, ctx)
    }

    #[log_syscall(/* rv */ *const std::ffi::c_void, /* old_address */ *const std::ffi::c_void,
                  /* old_size */ usize, /* new_size */ usize,
                  /* flags */ nix::sys::mman::MRemapFlags, /* new_address */ *const std::ffi::c_void)]
    pub fn mremap(
        ctx: &mut SyscallContext,
        _old_addr: ForeignPtr<()>,
        _old_size: usize,
        _new_size: usize,
        _flags: std::ffi::c_int,
        _new_addr: ForeignPtr<()>,
    ) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_mremap, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int, /* addr */ *const std::ffi::c_void, /* length */ usize)]
    pub fn munmap(ctx: &mut SyscallContext, _addr: ForeignPtr<()>, _len: usize) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_munmap, ctx)
    }

    #[log_syscall(/* rv */ std::ffi::c_int, /* addr */ *const std::ffi::c_void, /* len */ usize,
                  /* prot */ std::ffi::c_int)]
    pub fn mprotect(
        ctx: &mut SyscallContext,
        _addr: ForeignPtr<()>,
        _len: usize,
        _prot: std::ffi::c_int,
    ) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_mprotect, ctx)
    }
}
