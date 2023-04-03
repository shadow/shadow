use shadow_shim_helper_rs::syscall_types::ForeignPtr;
use syscall_logger::log_syscall;

use crate::cshadow as c;
use crate::host::descriptor::{CompatFile, DescriptorFlags, FileStatus};
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall_types::SyscallResult;

impl SyscallHandler {
    #[log_syscall(/* rv */ libc::c_int, /* fd */ libc::c_int, /* request */ libc::c_ulong)]
    pub fn ioctl(
        ctx: &mut SyscallContext,
        fd: libc::c_int,
        request: libc::c_ulong,
        arg_ptr: ForeignPtr<()>,
    ) -> SyscallResult {
        log::trace!("Called ioctl() on fd {} with request {}", fd, request);

        // get the descriptor, or return early if it doesn't exist
        let file = {
            let mut desc_table = ctx.objs.process.descriptor_table_borrow_mut();
            let desc = Self::get_descriptor_mut(&mut desc_table, fd)?;

            // add the CLOEXEC flag
            if request == libc::FIOCLEX {
                let mut flags = desc.flags();
                flags.insert(DescriptorFlags::CLOEXEC);
                desc.set_flags(flags);

                return Ok(0.into());
            }

            // remove the CLOEXEC flag
            if request == libc::FIONCLEX {
                let mut flags = desc.flags();
                flags.remove(DescriptorFlags::CLOEXEC);
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
        if request == libc::FIONBIO {
            let arg_ptr = arg_ptr.cast::<libc::c_int>();
            let arg = ctx.objs.process.memory_borrow_mut().read(arg_ptr)?;

            let mut status = file.get_status();
            status.set(FileStatus::NONBLOCK, arg != 0);
            file.set_status(status);

            return Ok(0.into());
        }

        // handle file-specific ioctls
        file.ioctl(request, arg_ptr, &mut ctx.objs.process.memory_borrow_mut())
    }
}
