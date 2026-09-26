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
pub const FUTEX_BITSET_MATCH_ANY: u32 = bindings::LINUX_FUTEX_BITSET_MATCH_ANY;

/// Represents a `FutexFlags` and `FutexOp`, as passed to the `futex` syscall.
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct FutexOpAndFlags {
    op: FutexOp,
    flags: FutexFlags,
}

impl FutexOpAndFlags {
    pub fn new(op: FutexOp, flags: FutexFlags) -> Self {
        Self { op, flags }
    }

    pub fn op(&self) -> FutexOp {
        self.op
    }

    pub fn flags(&self) -> FutexFlags {
        self.flags
    }
}

#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum FutexOpAndFlagsTryFromI32Error {
    UnknownOp(i32),
}

impl TryFrom<i32> for FutexOpAndFlags {
    type Error = FutexOpAndFlagsTryFromI32Error;
    fn try_from(value: i32) -> Result<Self, Self::Error> {
        // Currently all operations fit in the lowest 4 bits. Assume any other bits are flags.
        let op_mask = 0b1111;
        let op_value = value & op_mask;
        let op = FutexOp::try_from(op_value)
            .map_err(|_| FutexOpAndFlagsTryFromI32Error::UnknownOp(op_value))?;
        let flags = FutexFlags::from_bits_retain(value & !op_mask);
        Ok(FutexOpAndFlags::new(op, flags))
    }
}

impl From<FutexOpAndFlags> for i32 {
    fn from(value: FutexOpAndFlags) -> Self {
        i32::from(value.op) | value.flags.bits()
    }
}

bitflags::bitflags! {
    /// Flags that can contained in the `futex_op` parameter to the `futex` syscall.
    #[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
    pub struct FutexFlags: i32 {
        const FUTEX_PRIVATE_FLAG = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_PRIVATE_FLAG);
        const FUTEX_CLOCK_REALTIME = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_CLOCK_REALTIME);
    }
}

/// Operations that can be specified in the `futex_op` parameter to the `futex` syscall.
#[derive(
    Debug, Copy, Clone, Eq, PartialEq, num_enum::IntoPrimitive, num_enum::TryFromPrimitive,
)]
#[repr(i32)]
#[allow(non_camel_case_types)]
pub enum FutexOp {
    FUTEX_WAIT = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_WAIT),
    FUTEX_WAKE = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_WAKE),
    FUTEX_FD = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_FD),
    FUTEX_REQUEUE = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_REQUEUE),
    FUTEX_CMP_REQUEUE = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_CMP_REQUEUE),
    FUTEX_WAKE_OP = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_WAKE_OP),
    FUTEX_LOCK_PI = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_LOCK_PI),
    FUTEX_UNLOCK_PI = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_UNLOCK_PI),
    FUTEX_TRYLOCK_PI = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_TRYLOCK_PI),
    FUTEX_WAIT_BITSET = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_WAIT_BITSET),
    FUTEX_WAKE_BITSET = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_WAKE_BITSET),
    FUTEX_WAIT_REQUEUE_PI = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_WAIT_REQUEUE_PI),
    FUTEX_CMP_REQUEUE_PI = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_CMP_REQUEUE_PI),
    FUTEX_LOCK_PI2 = const_conversions::i32_from_u32(bindings::LINUX_FUTEX_LOCK_PI2),
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
    op: FutexOpAndFlags,
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

    unsafe { futex_raw(uaddr.as_ptr(), i32::from(op), val, utime, uaddr2, val3) }
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
