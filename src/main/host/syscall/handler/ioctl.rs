use linux_api::errno::Errno;
use linux_api::fcntl::DescriptorFlags;
use linux_api::ioctls::IoctlRequest;
use log::debug;
use shadow_shim_helper_rs::syscall_types::ForeignPtr;

use crate::cshadow as c;
use crate::host::descriptor::{CompatFile, FileStatus};
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::types::SyscallResult;

impl SyscallHandler {
    log_syscall!(
        ioctl,
        /* rv */ std::ffi::c_int,
        /* fd */ std::ffi::c_uint,
        /* cmd */ std::ffi::c_uint,
        /* arg */ std::ffi::c_ulong,
    );
    pub fn ioctl(
        ctx: &mut SyscallContext,
        fd: std::ffi::c_uint,
        cmd: std::ffi::c_uint,
        arg_ptr: ForeignPtr<()>,
    ) -> SyscallResult {
        log::trace!("Called ioctl() on fd {fd} with cmd {cmd}");

        let Ok(cmd) = IoctlRequest::try_from(cmd) else {
            debug!("Unrecognized ioctl cmd {cmd}");
            return Err(Errno::EINVAL.into());
        };

        // get the descriptor, or return early if it doesn't exist
        let file = {
            let mut desc_table = ctx.objs.thread.descriptor_table_borrow_mut(ctx.objs.host);
            let desc = Self::get_descriptor_mut(&mut desc_table, fd)?;

            // add the CLOEXEC flag
            if cmd == IoctlRequest::FIOCLEX {
                let mut flags = desc.flags();
                flags.insert(DescriptorFlags::FD_CLOEXEC);
                desc.set_flags(flags);

                return Ok(0.into());
            }

            // remove the CLOEXEC flag
            if cmd == IoctlRequest::FIONCLEX {
                let mut flags = desc.flags();
                flags.remove(DescriptorFlags::FD_CLOEXEC);
                desc.set_flags(flags);

                return Ok(0.into());
            }

            // NOTE: anything past this point should only modify the file, not the descriptor

            let file = match desc.file() {
                CompatFile::New(file) => file,
                // if it's a legacy file, use the C syscall handler instead
                CompatFile::Legacy(_) => {
                    drop(desc_table);
                    return Self::legacy_syscall(c::syscallhandler_ioctl, ctx);
                }
            };

            file.inner_file().clone()
        };

        let mut file = file.borrow_mut();

        // all file types that shadow implements should support non-blocking operation
        if cmd == IoctlRequest::FIONBIO {
            let arg_ptr = arg_ptr.cast::<std::ffi::c_int>();
            let arg = ctx.objs.process.memory_borrow_mut().read(arg_ptr)?;

            let mut status = file.status();
            status.set(FileStatus::NONBLOCK, arg != 0);
            file.set_status(status);

            return Ok(0.into());
        }

        // handle file-specific ioctls
        file.ioctl(cmd, arg_ptr, &mut ctx.objs.process.memory_borrow_mut())
    }
}
