use core::sync::atomic::AtomicU32;

use linux_syscall::Result64 as LinuxSyscallResult64;
use linux_syscall::syscall;

use crate::errno::Errno;
use crate::time::kernel_timespec;
use crate::{bindings, const_conversions};

pub use bindings::linux_robust_list_head;
#[allow(non_camel_case_types)]
pub type robust_list_head = linux_robust_list_head;
unsafe impl shadow_pod::Pod for robust_list_head {}

pub const FUTEX_CMD_MASK: i32 = bindings::LINUX_FUTEX_CMD_MASK;

bitflags::bitflags! {
    /// Flags that can be used in the `op` argument for the [`futex`] syscall.
    #[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
    pub struct FutexOpFlags: i32 {
        const FUTEX_WAIT = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_WAIT);
        const FUTEX_WAKE = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_WAKE);
        const FUTEX_FD = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_FD);
        const FUTEX_REQUEUE = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_REQUEUE);
        const FUTEX_CMP_REQUEUE = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_CMP_REQUEUE);
        const FUTEX_WAKE_OP = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_WAKE_OP);
        const FUTEX_LOCK_PI = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_LOCK_PI);
        const FUTEX_UNLOCK_PI = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_UNLOCK_PI);
        const FUTEX_TRYLOCK_PI = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_TRYLOCK_PI);
        const FUTEX_WAIT_BITSET = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_WAIT_BITSET);
        const FUTEX_WAKE_BITSET = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_WAKE_BITSET);
        const FUTEX_WAIT_REQUEUE_PI = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_WAIT_REQUEUE_PI);
        const FUTEX_CMP_REQUEUE_PI = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_CMP_REQUEUE_PI);
        const FUTEX_LOCK_PI2 = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_LOCK_PI2);
        const FUTEX_PRIVATE_FLAG = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_PRIVATE_FLAG);
        const FUTEX_CLOCK_REALTIME = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_CLOCK_REALTIME);
    }
}

/// # Safety
/// See futex(2). Pointers must be valid or NULL.
pub unsafe fn futex_raw(
    uaddr: *mut u32,
    op: core::ffi::c_int,
    val: u32,
    utime: *const kernel_timespec,
    uaddr2: *mut u32,
    val3: u32,
) -> Result<core::ffi::c_int, Errno> {
    unsafe {
        syscall!(
            linux_syscall::SYS_futex,
            uaddr,
            op,
            val,
            utime,
            uaddr2,
            val3
        )
    }
    .try_i64()
    // the linux x86-64 syscall implementation returns an int so I don't think this should ever fail
    .map(|x| x.try_into().expect("futex() returned invalid int"))
    .map_err(Errno::from)
}

// I don't see any reason to mark this as "unsafe", but I didn't look through all of the possible
// futex operations
pub fn futex(
    uaddr: &AtomicU32,
    op: FutexOpFlags,
    val: u32,
    utime: Option<&kernel_timespec>,
    uaddr2: Option<&AtomicU32>,
    val3: u32,
) -> Result<core::ffi::c_int, Errno> {
    let utime = utime
        .map(core::ptr::from_ref)
        .unwrap_or(core::ptr::null_mut());
    let uaddr2 = uaddr2
        .map(AtomicU32::as_ptr)
        .unwrap_or(core::ptr::null_mut());

    unsafe { futex_raw(uaddr.as_ptr(), op.bits(), val, utime, uaddr2, val3) }
}

#[cfg(test)]
mod tests {
    use super::*;

    // miri doesn't support non-libc syscalls
    #[cfg(not(miri))]
    #[test]
    fn test_futex_error() {
        let rv = unsafe {
            futex_raw(
                core::ptr::null_mut(),
                0,
                0,
                core::ptr::null(),
                core::ptr::null_mut(),
                0,
            )
        };

        // check that errors are returned correctly even though it returns a signed integer
        assert_eq!(rv, Err(Errno::EFAULT));
    }
}
