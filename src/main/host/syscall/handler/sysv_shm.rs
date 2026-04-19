use linux_api::errno::Errno;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::host::syscall::handler::{SyscallContext, SyscallHandler};

impl SyscallHandler {
    log_syscall!(
        shmget,
        /* rv */ std::ffi::c_int,
        /* key */ libc::key_t,
        /* size */ libc::size_t,
        /* shmflg */ std::ffi::c_int,
    );
    pub fn shmget(
        ctx: &mut SyscallContext,
        key: libc::key_t,
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
        buf: ForeignPtr<libc::shmid_ds>,
    ) -> Result<(), Errno> {
        let command = cmd & 0xff;

        match command {
            libc::IPC_STAT => {
                if buf.is_null() {
                    return Err(Errno::EFAULT);
                }
                let ds = ctx.objs.host.sysv_shm_borrow().shmctl_ipc_stat(shmid)?;
                ctx.objs.process.memory_borrow_mut().write(buf, &ds)
            }
            libc::IPC_RMID => ctx.objs.host.sysv_shm_borrow_mut().shmctl_ipc_rmid(shmid),
            _ => Err(Errno::ENOSYS),
        }
    }
}
