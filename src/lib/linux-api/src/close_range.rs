use crate::bindings;

bitflags::bitflags! {
    /// Flags that can be used in the `flags` argument for the `close_range` syscall.
    #[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
    pub struct CloseRangeFlags: u32 {
        const CLOSE_RANGE_CLOEXEC = bindings::LINUX_CLOSE_RANGE_CLOEXEC;
        const CLOSE_RANGE_UNSHARE = bindings::LINUX_CLOSE_RANGE_UNSHARE;
    }
}
