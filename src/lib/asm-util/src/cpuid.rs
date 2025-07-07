use core::arch::x86_64::{__cpuid, __cpuid_count};

use crate::ip_matches;

/* Constants below primarily from
 * <https://en.wikipedia.org/wiki/CPUID>. TODO: cross-check/cite primary
 * sources.
 */

/// Length in bytes of an x86-64 cpuid instruction.
pub const CPUID_INSN_LEN: usize = 2;
/// An x86-64 cpuid instruction.
pub const CPUID: [u8; CPUID_INSN_LEN] = [0x0f, 0xa2];

/// cpuid leaf for finding the rdrand bit.
pub const RDRAND_LEAF: u32 = 1;
/// cpuid leaf 1 doesn't have sub-leaves.
pub const RDRAND_SUB_LEAF: Option<u32> = None;
/// rdrand flag in a `cpuid(RDRAND_LEAF)` result.
pub const RDRAND_FLAG: u32 = 1 << 30;

/// cpuid leaf for finding the rdseed bit.
pub const RDSEED_LEAF: u32 = 7;
/// cpuid sub-leaf for finding the rdseed bit.
pub const RDSEED_SUB_LEAF: Option<u32> = Some(0);
/// rdseed flag in a `cpuid_count(RDSEED_LEAF, RDSEED_SUB_LEAF)` result.
pub const RDSEED_FLAG: u32 = 1 << 18;

pub use core::arch::x86_64::CpuidResult;

/// Whether the current CPU supports the `rdrand` instruction.
///
/// # Safety
///
/// `cpuid` instruction must be available. See [`cpuid`].
pub unsafe fn supports_rdrand() -> bool {
    unsafe { cpuid(RDRAND_LEAF, RDRAND_SUB_LEAF) }.ecx & RDRAND_FLAG != 0
}

/// Whether the current CPU supports the `rdseed` instruction.
///
/// # Safety
///
/// `cpuid` instruction must be available. See [`cpuid`].
pub unsafe fn supports_rdseed() -> bool {
    unsafe { cpuid(RDSEED_LEAF, RDSEED_SUB_LEAF) }.ebx & RDSEED_FLAG != 0
}

/// Execute the cpuid instruction for the given leaf and sub_leaf.
///
/// For leaves that don't have sub-leaves, providing a sub-leaf shouldn't change
/// the result. The reverse is of course not true; failing to provide a sub-leaf
/// will result in returning an arbitrary one.
///
/// # Safety
///
/// `cpuid` instruction must be available. This is generally true outside of
/// specialized execution environments such as SGX. See
/// <https://github.com/rust-lang/rust/issues/60123>.
pub unsafe fn cpuid(leaf: u32, sub_leaf: Option<u32>) -> CpuidResult {
    match sub_leaf {
        // SAFETY: Caller's responsibility.
        Some(sub_leaf) => unsafe { __cpuid_count(leaf, sub_leaf) },
        None => unsafe { __cpuid(leaf) },
    }
}

/// Whether `ip` points to a cpuid instruction.
///
/// # Safety
///
/// `ip` must be a dereferenceable pointer, pointing to the
/// beginning of a valid x86_64 instruction.
pub unsafe fn ip_is_cpuid(ip: *const u8) -> bool {
    unsafe { ip_matches(ip, &CPUID) }
}

mod export {
    /// Whether `buf` begins with a cpuid instruction.
    ///
    /// # Safety
    ///
    /// `ip` must be a dereferenceable pointer, pointing to the
    /// beginning of a valid x86_64 instruction.
    #[unsafe(no_mangle)]
    pub unsafe extern "C-unwind" fn isCpuid(ip: *const u8) -> bool {
        unsafe { super::ip_is_cpuid(ip) }
    }
}

#[cfg(test)]
mod test {
    use super::*;

    // miri doesn't support inline asm.
    #[cfg(not(miri))]
    #[test]
    fn test_supports_rdrand() {
        let inlined_res = unsafe { __cpuid(1) }.ecx & (1 << 30) != 0;
        assert_eq!(unsafe { supports_rdrand() }, inlined_res);
    }

    // miri doesn't support inline asm.
    #[cfg(not(miri))]
    #[test]
    fn test_supports_rdseed() {
        let inlined_res = unsafe { __cpuid_count(7, 0) }.ebx & (1 << 18) != 0;
        assert_eq!(unsafe { supports_rdseed() }, inlined_res);
    }
}
