use crate::host::context::ThreadContext;
use crate::host::descriptor::eventfd;
use crate::host::descriptor::{
    CompatDescriptor, Descriptor, DescriptorFlags, FileStatus, PosixFile,
};
use crate::host::syscall::handler::SyscallHandler;
use crate::host::syscall_types::SysCallArgs;
use crate::host::syscall_types::SyscallResult;

use std::sync::Arc;

use atomic_refcell::AtomicRefCell;
use nix::errno::Errno;
use nix::sys::eventfd::EfdFlags;

use syscall_logger::log_syscall;

impl SyscallHandler {
    #[log_syscall(/* rv */ libc::c_int, /* initval */ libc::c_uint)]
    pub fn eventfd(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let init_val: libc::c_uint = args.get(0).into();

        self.eventfd_helper(ctx, init_val, 0)
    }

    #[log_syscall(/* rv */ libc::c_int, /* initval */ libc::c_uint, /* flags */ nix::sys::eventfd::EfdFlags)]
    pub fn eventfd2(&self, ctx: &mut ThreadContext, args: &SysCallArgs) -> SyscallResult {
        let init_val: libc::c_uint = args.get(0).into();
        let flags: libc::c_int = args.get(1).into();

        self.eventfd_helper(ctx, init_val, flags)
    }

    fn eventfd_helper(
        &self,
        ctx: &mut ThreadContext,
        init_val: libc::c_uint,
        flags: libc::c_int,
    ) -> SyscallResult {
        log::trace!(
            "eventfd() called with initval {} and flags {}",
            init_val,
            flags
        );

        // get the flags
        let flags = match EfdFlags::from_bits(flags) {
            Some(x) => x,
            None => {
                log::warn!("Invalid eventfd flags: {}", flags);
                return Err(Errno::EINVAL.into());
            }
        };

        let mut file_flags = FileStatus::empty();
        let mut descriptor_flags = DescriptorFlags::empty();
        let mut semaphore_mode = false;

        if flags.contains(EfdFlags::EFD_NONBLOCK) {
            file_flags.insert(FileStatus::NONBLOCK);
        }

        if flags.contains(EfdFlags::EFD_CLOEXEC) {
            descriptor_flags.insert(DescriptorFlags::CLOEXEC);
        }

        if flags.contains(EfdFlags::EFD_SEMAPHORE) {
            semaphore_mode = true;
        }

        let file = eventfd::EventFdFile::new(init_val as u64, semaphore_mode, file_flags);
        let file = Arc::new(AtomicRefCell::new(file));

        let mut desc = Descriptor::new(PosixFile::EventFd(file));
        desc.set_flags(descriptor_flags);

        let fd = ctx.process.register_descriptor(CompatDescriptor::New(desc));

        log::trace!("eventfd() returning fd {}", fd);

        Ok(fd.into())
    }
}
