use crate::cshadow as c;
use crate::host::context::ThreadContext;
use crate::host::descriptor::{CompatFile, DescriptorFlags, FileStatus};
use crate::host::syscall::handler::SyscallHandler;
use crate::host::syscall_types::{PluginPtr, SysCallArgs, SyscallResult, TypedPluginPtr};

use syscall_logger::log_syscall;

impl SyscallHandler {
    #[log_syscall(/* rv */ libc::c_int, /* fd */ libc::c_int, /* request */ libc::c_ulong)]
    pub fn ioctl(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let fd: libc::c_int = args.get(0).into();
        let request: libc::c_ulong = args.get(1).into();
        let arg_ptr: PluginPtr = args.get(2).into(); // type depends on request

        log::trace!("Called ioctl() on fd {} with request {}", fd, request);

        // get the descriptor, or return early if it doesn't exist
        let desc = Self::get_descriptor_mut(ctx.process, fd)?;

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
                return Self::legacy_syscall(c::syscallhandler_ioctl, ctx, args);
            }
        };

        let file = file.inner_file().clone();
        let mut file = file.borrow_mut();

        // all file types that shadow implements should support non-blocking operation
        if request == libc::FIONBIO {
            let arg_ptr = TypedPluginPtr::new::<libc::c_int>(arg_ptr, 1);
            let arg = ctx.process.memory_mut().read_vals::<_, 1>(arg_ptr)?[0];

            let mut status = file.get_status();
            status.set(FileStatus::NONBLOCK, arg != 0);
            file.set_status(status);

            return Ok(0.into());
        }

        // handle file-specific ioctls
        file.ioctl(request, arg_ptr, ctx.process.memory_mut())
    }
}
