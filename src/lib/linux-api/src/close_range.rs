use linux_syscall::{syscall, Result64};

use crate::bindings;
use crate::errno::Errno;
use crate::posix_types::RawFd;

bitflags::bitflags! {
    /// Flags that can be used in the `flags` argument for the `close_range` syscall.
    #[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
    pub struct CloseRangeFlags: u32 {
        const CLOSE_RANGE_CLOEXEC = bindings::LINUX_CLOSE_RANGE_CLOEXEC;
        const CLOSE_RANGE_UNSHARE = bindings::LINUX_CLOSE_RANGE_UNSHARE;
    }
}

pub fn close_range_raw(
    first: core::ffi::c_uint,
    last: core::ffi::c_uint,
    flags: core::ffi::c_uint,
) -> Result<core::ffi::c_int, Errno> {
    unsafe { syscall!(linux_syscall::SYS_close_range, first, last, flags) }
        .try_i64()
        // the linux x86-64 syscall implementation returns an int so I don't think this should ever fail
        .map(|x| x.try_into().expect("close_range() returned invalid int"))
        .map_err(Errno::from)
}

pub fn close_range(
    first: RawFd,
    last: RawFd,
    flags: CloseRangeFlags,
) -> Result<core::ffi::c_int, Errno> {
    close_range_raw(
        first as core::ffi::c_uint,
        last as core::ffi::c_uint,
        flags.bits(),
    )
}
