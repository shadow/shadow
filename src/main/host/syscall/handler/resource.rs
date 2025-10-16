use linux_api::errno::Errno;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::host::process::ProcessId;
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::types::SyscallError;

impl SyscallHandler {
    fn prlimit64_impl(
        ctx: &mut SyscallContext,
        pid: linux_api::posix_types::kernel_pid_t,
        resource: std::ffi::c_uint,
        new_rlim: Option<&linux_api::resource::rlimit64>,
        mut old_rlim: Option<&mut linux_api::resource::rlimit64>,
    ) -> Result<(), SyscallError> {
        // TODO: For determinism, we may want to enforce static limits for certain resources, like
        // RLIMIT_NOFILE. Some applications like Tor will change behavior depending on these limits.

        let Ok(resource) = linux_api::resource::Resource::try_from(resource) else {
            return Err(Errno::EINVAL.into());
        };

        let native_pid = if pid == 0 {
            // process is calling prlimit on itself
            ctx.objs.process.native_pid()
        } else {
            // calling on another process
            let Ok(id) = ProcessId::try_from(pid) else {
                return Err(Errno::ESRCH.into());
            };
            let Some(process) = ctx.objs.host.process_borrow(id) else {
                return Err(Errno::ESRCH.into());
            };
            process.borrow(ctx.objs.host.root()).native_pid()
        };

        // SAFETY: native_pid can't be *our* process. Potentially unsafe for target (managed) process.
        unsafe {
            linux_api::resource::prlimit64(native_pid, resource, new_rlim, old_rlim.as_deref_mut())
        }?;
        log::trace!(
            "called prlimit64 native_pid:{native_pid:?} resource:{resource:?} new:{new_rlim:?} old:{old_rlim:?}"
        );
        Ok(())
    }

    log_syscall!(
        prlimit64,
        /* rv */ std::ffi::c_int,
        /* pid */ linux_api::posix_types::kernel_pid_t,
        /* resource */ linux_api::resource::Resource,
        /* new_rlim */ *const linux_api::resource::rlimit64,
        /* old_rlim */ *const linux_api::resource::rlimit64,
    );
    pub fn prlimit64(
        ctx: &mut SyscallContext,
        pid: linux_api::posix_types::kernel_pid_t,
        resource: std::ffi::c_uint,
        new_rlim_ptr: ForeignPtr<linux_api::resource::rlimit64>,
        old_rlim_ptr: ForeignPtr<linux_api::resource::rlimit64>,
    ) -> Result<(), SyscallError> {
        let new_rlim = if new_rlim_ptr.is_null() {
            None
        } else {
            Some(&ctx.objs.process.memory_borrow().read(new_rlim_ptr)?)
        };

        let mut old_rlim = if old_rlim_ptr.is_null() {
            None
        } else {
            Some(&mut linux_api::resource::rlimit64 {
                rlim_cur: 0,
                rlim_max: 0,
            })
        };

        SyscallHandler::prlimit64_impl(ctx, pid, resource, new_rlim, old_rlim.as_deref_mut())?;

        if !old_rlim_ptr.is_null() {
            ctx.objs
                .process
                .memory_borrow_mut()
                .write(old_rlim_ptr, old_rlim.unwrap())?;
        }

        Ok(())
    }

    log_syscall!(
        getrlimit,
        /* rv */ std::ffi::c_int,
        /* resource */ linux_api::resource::Resource,
        /* rlimit */ *const linux_api::resource::rlimit,
    );
    pub fn getrlimit(
        ctx: &mut SyscallContext,
        resource: std::ffi::c_uint,
        rlimit_ptr: ForeignPtr<linux_api::resource::rlimit>,
    ) -> Result<(), SyscallError> {
        let mut rlim_val64 = linux_api::resource::rlimit64 {
            rlim_cur: 0,
            rlim_max: 0,
        };
        SyscallHandler::prlimit64_impl(ctx, 0, resource, None, Some(&mut rlim_val64))?;
        let rlim_val = linux_api::resource::rlimit {
            rlim_cur: rlim_val64.rlim_cur,
            rlim_max: rlim_val64.rlim_max,
        };
        ctx.objs
            .process
            .memory_borrow_mut()
            .write(rlimit_ptr, &rlim_val)?;
        Ok(())
    }

    log_syscall!(
        setrlimit,
        /* rv */ std::ffi::c_int,
        /* resource */ linux_api::resource::Resource,
        /* rlimit */ *const linux_api::resource::rlimit,
    );
    pub fn setrlimit(
        ctx: &mut SyscallContext,
        resource: std::ffi::c_uint,
        rlimit: ForeignPtr<linux_api::resource::rlimit>,
    ) -> Result<(), SyscallError> {
        let rlim_val = ctx.objs.process.memory_borrow().read(rlimit)?;
        let rlim_val = linux_api::resource::rlimit64 {
            rlim_cur: rlim_val.rlim_cur,
            rlim_max: rlim_val.rlim_max,
        };
        SyscallHandler::prlimit64_impl(ctx, 0, resource, Some(&rlim_val), None)?;
        Ok(())
    }
}
