/// shadow-specific syscall numbers. When running under shadow, the shim (or
/// managed code) can make syscalls with these numbers to invoke shadow-specific
/// syscalls.
//
// We comment-out old syscall numbers instead of removing them for a few reasons:
// - If shadow accidentally tries using an old version of the shim, it could lead to very confusing
//   behaviour if shadow and the shim were to interpret the syscall numbers differently.
// - If the plugin tries to interact with shadow by calling one of shadow's custom syscalls (for
//   example to disable interposition), we wouldn't want the syscall meaning to change, even though
//   we don't support this feature.
// - When looking at shadow logs (for example syscall counts) from old simulations using old shadow
//   versions, it might be less confusing if those old logs used the same syscall numbers.
#[derive(Debug, Eq, PartialEq, num_enum::TryFromPrimitive, num_enum::IntoPrimitive)]
#[repr(u32)]
#[allow(non_camel_case_types)]
pub enum ShadowSyscallNum {
    // Deprecated: SYS_shadow_set_ptrace_allow_native_syscalls = 1000,
    // Deprecated: SYS_shadow_get_ipc_blk = 1001,
    // Deprecated: SYS_shadow_get_shm_blk = 1002,
    hostname_to_addr_ipv4 = 1003,
    init_memory_manager = 1004,
    // Conceptually similar to SYS_sched_yield, but made by the shim to return
    // control to Shadow. For now, using a different syscall here is mostly for
    // debugging purposes, so that it doesn't appear that the managed code
    // issues a SYS_sched_yield.
    shadow_yield = 1005,
}

impl TryFrom<linux_api::syscall::SyscallNum> for ShadowSyscallNum {
    type Error = <ShadowSyscallNum as TryFrom<u32>>::Error;

    fn try_from(value: linux_api::syscall::SyscallNum) -> Result<Self, Self::Error> {
        ShadowSyscallNum::try_from(u32::from(value))
    }
}

impl From<ShadowSyscallNum> for linux_api::syscall::SyscallNum {
    fn from(value: ShadowSyscallNum) -> Self {
        linux_api::syscall::SyscallNum::from(u32::from(value))
    }
}

pub mod export {
    use super::*;

    /// Returns whether the given number is a shadow-specific syscall number.
    #[unsafe(no_mangle)]
    pub extern "C-unwind" fn syscall_num_is_shadow(n: core::ffi::c_long) -> bool {
        let Ok(n) = u32::try_from(n) else {
            return false;
        };
        ShadowSyscallNum::try_from(n).is_ok()
    }
}
