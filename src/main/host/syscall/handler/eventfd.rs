use std::sync::Arc;

use atomic_refcell::AtomicRefCell;
use linux_api::errno::Errno;
use linux_api::fcntl::DescriptorFlags;
use nix::sys::eventfd::EfdFlags;

use crate::host::descriptor::descriptor_table::DescriptorHandle;
use crate::host::descriptor::eventfd;
use crate::host::descriptor::{CompatFile, Descriptor, File, FileStatus, OpenFile};
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};

impl SyscallHandler {
    log_syscall!(
        eventfd,
        /* rv */ std::ffi::c_int,
        /* initval */ std::ffi::c_uint,
    );
    pub fn eventfd(
        ctx: &mut SyscallContext,
        init_val: std::ffi::c_uint,
    ) -> Result<DescriptorHandle, Errno> {
        Self::eventfd_helper(ctx, init_val, 0)
    }

    log_syscall!(
        eventfd2,
        /* rv */ std::ffi::c_int,
        /* initval */ std::ffi::c_uint,
        /* flags */ nix::sys::eventfd::EfdFlags,
    );
    pub fn eventfd2(
        ctx: &mut SyscallContext,
        init_val: std::ffi::c_uint,
        flags: std::ffi::c_int,
    ) -> Result<DescriptorHandle, Errno> {
        Self::eventfd_helper(ctx, init_val, flags)
    }

    fn eventfd_helper(
        ctx: &mut SyscallContext,
        init_val: std::ffi::c_uint,
        flags: std::ffi::c_int,
    ) -> Result<DescriptorHandle, Errno> {
        log::trace!("eventfd() called with initval {init_val} and flags {flags}");

        // get the flags
        let flags = match EfdFlags::from_bits(flags) {
            Some(x) => x,
            None => {
                log::warn!("Invalid eventfd flags: {}", flags);
                return Err(Errno::EINVAL);
            }
        };

        let mut file_flags = FileStatus::empty();
        let mut descriptor_flags = DescriptorFlags::empty();
        let mut semaphore_mode = false;

        if flags.contains(EfdFlags::EFD_NONBLOCK) {
            file_flags.insert(FileStatus::NONBLOCK);
        }

        if flags.contains(EfdFlags::EFD_CLOEXEC) {
            descriptor_flags.insert(DescriptorFlags::FD_CLOEXEC);
        }

        if flags.contains(EfdFlags::EFD_SEMAPHORE) {
            semaphore_mode = true;
        }

        let file = eventfd::EventFd::new(init_val as u64, semaphore_mode, file_flags);
        let file = Arc::new(AtomicRefCell::new(file));

        let mut desc = Descriptor::new(CompatFile::New(OpenFile::new(File::EventFd(file))));
        desc.set_flags(descriptor_flags);

        let fd = ctx
            .objs
            .thread
            .descriptor_table_borrow_mut(ctx.objs.host)
            .register_descriptor(desc)
            .or(Err(Errno::ENFILE))?;

        log::trace!("eventfd() returning fd {}", fd);

        Ok(fd)
    }
}
