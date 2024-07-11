use linux_api::close_range::CloseRangeFlags;
use linux_api::errno::Errno;
use linux_api::fcntl::DescriptorFlags;
use syscall_logger::log_syscall;

use crate::host::descriptor::descriptor_table;
use crate::host::syscall::handler::{SyscallContext, SyscallHandler};
use crate::host::syscall::types::SyscallError;
use crate::utility::callback_queue::CallbackQueue;

impl SyscallHandler {
    #[log_syscall(/* rv */ std::ffi::c_int, /* first */ std::ffi::c_uint,
                  /* last */ std::ffi::c_uint, /* flags */ CloseRangeFlags)]
    pub fn close_range(
        ctx: &mut SyscallContext,
        first: std::ffi::c_uint,
        last: std::ffi::c_uint,
        flags: std::ffi::c_uint,
    ) -> Result<std::ffi::c_int, SyscallError> {
        // close_range(2):
        // > EINVAL: [...], or first is greater than last.
        if first > last {
            return Err(Errno::EINVAL.into());
        }

        // if the start of the range is larger than the max possible fd, then do nothing
        if first > descriptor_table::FD_MAX {
            return Ok(0);
        }

        // restrict the end of the range to the max possible fd
        let last = std::cmp::min(last, descriptor_table::FD_MAX);

        // we ensured above that first and last are within the allowed fd range
        let first = first.try_into().unwrap();
        let last = last.try_into().unwrap();

        let range = first..=last;

        let Some(flags) = CloseRangeFlags::from_bits(flags) else {
            log::debug!("Invalid close_range flags: {flags}");
            return Err(Errno::EINVAL.into());
        };

        if flags.contains(CloseRangeFlags::CLOSE_RANGE_UNSHARE) {
            log::debug!("The CLOSE_RANGE_UNSHARE flag is not implemented");
            return Err(Errno::EINVAL.into());
        }

        let mut desc_table = ctx.objs.thread.descriptor_table_borrow_mut(ctx.objs.host);

        if flags.contains(CloseRangeFlags::CLOSE_RANGE_CLOEXEC) {
            // close_range(2):
            // > CLOSE_RANGE_CLOEXEC: Set the close-on-exec flag on the specified file descriptors,
            // > rather than immediately closing them.

            // set the CLOEXEC flag on all descriptors in the range
            for (fd, desc) in desc_table.iter_mut() {
                if range.contains(fd) {
                    desc.set_flags(desc.flags() | DescriptorFlags::FD_CLOEXEC);
                }
            }
        } else {
            // remove all descriptors in the range
            let descriptors = desc_table.remove_range(range);

            // close the removed descriptors
            crate::utility::legacy_callback_queue::with_global_cb_queue(|| {
                CallbackQueue::queue_and_run(|cb_queue| {
                    for desc in descriptors {
                        // close_range(2):
                        // > Errors closing a given file descriptor are currently ignored.
                        let _ = desc.close(ctx.objs.host, cb_queue);
                    }
                })
            });
        }

        Ok(0)
    }
}
