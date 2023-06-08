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

    // <https://github.com/torvalds/linux/tree/v6.3/arch/x86/kernel/sys_x86_64.c#L86>
    // ```
    // SYSCALL_DEFINE6(mmap, unsigned long, addr, unsigned long, len,
    //                 unsigned long, prot, unsigned long, flags,
    //                 unsigned long, fd, unsigned long, off)
    // ```
    #[log_syscall(/* rv */ *const std::ffi::c_void, /* addr */ *const std::ffi::c_void,
                  /* length */ usize, /* prot */ linux_api::mman::ProtFlags,
                  /* flags */ linux_api::mman::MapFlags, /* fd */ std::ffi::c_ulong,
                  /* offset */ std::ffi::c_ulong)]
    pub fn mmap(
        ctx: &mut SyscallContext,
        _addr: std::ffi::c_ulong,
        _len: std::ffi::c_ulong,
        _prot: std::ffi::c_ulong,
        _flags: std::ffi::c_ulong,
        _fd: std::ffi::c_ulong,
        _offset: std::ffi::c_ulong,
    ) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_mmap, ctx)
    }

    // <https://github.com/torvalds/linux/tree/v6.3/mm/mremap.c#L895>
    // ```
    // SYSCALL_DEFINE5(mremap, unsigned long, addr, unsigned long, old_len,
    //                 unsigned long, new_len, unsigned long, flags,
    //                 unsigned long, new_addr)
    // ```
    #[log_syscall(/* rv */ *const std::ffi::c_void, /* old_address */ *const std::ffi::c_void,
                  /* old_size */ std::ffi::c_ulong, /* new_size */ std::ffi::c_ulong,
                  /* flags */ linux_api::mman::MRemapFlags, /* new_address */ *const std::ffi::c_void)]
    pub fn mremap(
        ctx: &mut SyscallContext,
        _old_addr: std::ffi::c_ulong,
        _old_size: std::ffi::c_ulong,
        _new_size: std::ffi::c_ulong,
        _flags: std::ffi::c_ulong,
        _new_addr: std::ffi::c_ulong,
    ) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_mremap, ctx)
    }

    // <https://github.com/torvalds/linux/tree/v6.3/mm/mmap.c#L2786>
    // ```
    // SYSCALL_DEFINE2(munmap, unsigned long, addr, size_t, len)
    // ```
    #[log_syscall(/* rv */ std::ffi::c_int, /* addr */ *const std::ffi::c_void, /* length */ usize)]
    pub fn munmap(
        ctx: &mut SyscallContext,
        _addr: std::ffi::c_ulong,
        _len: usize,
    ) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_munmap, ctx)
    }

    // <https://github.com/torvalds/linux/tree/v6.3/mm/mprotect.c#L849>
    // ```
    // SYSCALL_DEFINE3(mprotect, unsigned long, start, size_t, len, unsigned long, prot)
    // ```
    #[log_syscall(/* rv */ std::ffi::c_int, /* addr */ *const std::ffi::c_void, /* len */ usize,
                  /* prot */ linux_api::mman::ProtFlags)]
    pub fn mprotect(
        ctx: &mut SyscallContext,
        _addr: std::ffi::c_ulong,
        _len: usize,
        _prot: std::ffi::c_ulong,
    ) -> SyscallResult {
        Self::legacy_syscall(cshadow::syscallhandler_mprotect, ctx)
    }
}
