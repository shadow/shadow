use crate::bindings;
use num_enum::{IntoPrimitive, TryFromPrimitive};

/// Valid values for the `whence` parameter of the `lseek` syscall.
#[derive(Debug, Copy, Clone, Eq, PartialEq, IntoPrimitive, TryFromPrimitive)]
#[repr(u32)]
#[allow(non_camel_case_types)]
pub enum LSeekWhence {
    SEEK_SET = bindings::LINUX_SEEK_SET,
    SEEK_CUR = bindings::LINUX_SEEK_CUR,
    SEEK_END = bindings::LINUX_SEEK_END,
    SEEK_DATA = bindings::LINUX_SEEK_DATA,
    SEEK_HOLE = bindings::LINUX_SEEK_HOLE,
}
