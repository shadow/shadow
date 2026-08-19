use linux_api::errno::Errno;
use linux_api::posix_types::kernel_key_t;
use linux_api::shm::{ShmctlCmd, shmid64_ds};
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::types::ForeignArrayPtr;

impl SyscallHandler {
    log_syscall!(
        shmget,
        /* rv */ std::ffi::c_int,
        /* key */ kernel_key_t,
        /* size */ libc::size_t,
        /* shmflg */ std::ffi::c_int,
    );
    pub fn shmget(
        ctx: &mut SyscallContext,
        key: kernel_key_t,
        size: libc::size_t,
        shmflg: std::ffi::c_int,
    ) -> Result<std::ffi::c_int, Errno> {
        ctx.objs
            .host
            .sysv_shm_borrow_mut()
            .shmget(ctx.objs.process.id(), key, size, shmflg)
    }

    log_syscall!(
        shmat,
        /* rv */ *const std::ffi::c_void,
        /* shmid */ std::ffi::c_int,
        /* shmaddr */ *const std::ffi::c_void,
        /* shmflg */ std::ffi::c_int,
    );
    pub fn shmat(
        ctx: &mut SyscallContext,
        shmid: std::ffi::c_int,
        shmaddr: ForeignPtr<u8>,
        shmflg: std::ffi::c_int,
    ) -> Result<ForeignPtr<u8>, Errno> {
        ctx.objs
            .host
            .sysv_shm_borrow_mut()
            .shmat(ctx.objs, shmid, shmaddr, shmflg)
    }

    log_syscall!(
        shmdt,
        /* rv */ std::ffi::c_int,
        /* shmaddr */ *const std::ffi::c_void,
    );
    pub fn shmdt(ctx: &mut SyscallContext, shmaddr: ForeignPtr<u8>) -> Result<(), Errno> {
        ctx.objs.host.sysv_shm_borrow_mut().shmdt(ctx.objs, shmaddr)
    }

    log_syscall!(
        shmctl,
        /* rv */ std::ffi::c_int,
        /* shmid */ std::ffi::c_int,
        /* cmd */ std::ffi::c_int,
        /* buf */ *const std::ffi::c_void,
    );
    pub fn shmctl(
        ctx: &mut SyscallContext,
        shmid: std::ffi::c_int,
        cmd: std::ffi::c_int,
        buf: ForeignPtr<shmid64_ds>,
    ) -> Result<(), Errno> {
        let command = ShmctlCmd::try_from(cmd & 0xff).map_err(|_| Errno::ENOSYS)?;

        match command {
            ShmctlCmd::IPC_STAT => {
                if buf.is_null() {
                    return Err(Errno::EFAULT);
                }
                let ds = ctx.objs.host.sysv_shm_borrow().shmctl_ipc_stat(shmid)?;
                // Use MaybeUninit<u8> so ABI padding doesn't require shmid64_ds to be Pod.
                let ds_bytes = unsafe {
                    std::slice::from_raw_parts(
                        std::ptr::from_ref(&ds).cast::<std::mem::MaybeUninit<u8>>(),
                        std::mem::size_of::<shmid64_ds>(),
                    )
                };
                ctx.objs.process.memory_borrow_mut().copy_to_ptr(
                    ForeignArrayPtr::new(buf.cast::<std::mem::MaybeUninit<u8>>(), ds_bytes.len()),
                    ds_bytes,
                )
            }
            ShmctlCmd::IPC_RMID => ctx.objs.host.sysv_shm_borrow_mut().shmctl_ipc_rmid(shmid),
            _ => Err(Errno::ENOSYS),
        }
    }
}
