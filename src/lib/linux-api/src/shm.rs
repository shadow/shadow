use core::ffi::c_int;

use crate::bindings;
use crate::const_conversions;
use crate::stat::SFlag;

/// Minimum shared segment size.
pub const SHMMIN: usize = const_conversions::usize_from_u32(bindings::LINUX_SHMMIN);
/// Max number of segments system-wide.
pub const SHMMNI: usize = const_conversions::usize_from_u32(bindings::LINUX_SHMMNI);
/// Max shared segs per process.
pub const SHMSEG: usize = const_conversions::usize_from_u32(bindings::LINUX_SHMSEG);
// TODO: SHMMAX, SHMALL. bindgen doesn't understand these definitions.

/// bit-shift to encode "hugetlb" page sizes.
pub use bindings::LINUX_SHM_HUGE_SHIFT as SHM_HUGE_SHIFT;

/// parameter to `shmctl` syscall.
pub use bindings::linux_shmid64_ds as shmid64_ds;

use num_enum::IntoPrimitive;
use num_enum::TryFromPrimitive;

bitflags::bitflags! {
    /// Flags accepted by `shmget` syscall.
    ///
    /// Note that the `shmflg` value provided to the `shmget` syscall encodes
    /// additional information in other bitfields. Use `ShmgetFlagsParts` to
    /// parse or create the raw syscall parameter.
    #[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
    pub struct ShmgetFlags: i32 {
        /// Create a new segment.
        const IPC_CREAT = const_conversions::i32_from_u32(bindings::LINUX_IPC_CREAT);
        /// Ensure that a new segment is created.
        const IPC_EXCL = const_conversions::i32_from_u32(bindings::LINUX_IPC_EXCL);
        /// Allocate with huge pages.
        const SHM_HUGETLB = const_conversions::i32_from_u32(bindings::LINUX_SHM_HUGETLB);
        /// Do not reserve swap space.
        const SHM_NORESERVE = const_conversions::i32_from_u32(bindings::LINUX_SHM_NORESERVE);
    }
}

/// Represents a deconstructed `shmflg`, as passed to the `shmget` syscall.
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct ShmgetFlagsParts {
    /// Bit flags.
    pub flags: ShmgetFlags,
    /// Requested page size, log 2.
    pub tlb_size_log_2: u32,
    /// Requested permissions (as `mode` parameter to `open`)
    pub perms: SFlag,
}

impl ShmgetFlagsParts {
    /// Split a `shmflg` value, as provided to the `shmget` syscall, into consituent parts.
    pub fn from_shmflg(shmflg: c_int) -> Self {
        let tlb_size_shift = SHM_HUGE_SHIFT;
        let tlb_size_mask = 0b11_1111 << tlb_size_shift;

        let perms_shift = 0;
        let perms_mask = 0b1_1111_1111;
        Self {
            // Careful to case to unsized before shifting to avoid sign extension.
            tlb_size_log_2: (shmflg & tlb_size_mask) as u32 >> tlb_size_shift,
            perms: SFlag::from_bits_retain((shmflg & perms_mask) as u32 >> perms_shift),
            flags: ShmgetFlags::from_bits_retain(shmflg & !tlb_size_mask & !perms_mask),
        }
    }

    /// Convert to a `shmflg` value, suitable for `shmget` syscall param.
    ///
    /// Doesn't validate that the fields are valid. e.g. unexpected bits in any field
    /// may get packed into another field, shifted off, etc.
    ///
    // TODO: if we need something like this outside of testing, make a version
    // with error-checking. It's a bit fiddly, though.
    #[cfg(test)]
    fn unchecked_to_shmflg(self) -> c_int {
        self.flags.bits()
            | (self.tlb_size_log_2 << SHM_HUGE_SHIFT) as i32
            | self.perms.bits() as i32
    }
}

bitflags::bitflags! {
    /// Flags accepted by `shmat` syscall.
    #[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
    pub struct ShmatFlags: i32 {
        const SHM_RND = const_conversions::i32_from_u32(bindings::LINUX_SHM_RND);
        const SHM_EXEC= const_conversions::i32_from_u32(bindings::LINUX_SHM_EXEC);
        const SHM_RDONLY= const_conversions::i32_from_u32(bindings::LINUX_SHM_RDONLY);
        const SHM_REMAP= const_conversions::i32_from_u32(bindings::LINUX_SHM_REMAP);
    }
}

/// Command provided to the `shmctl` syscall.
#[derive(Debug, Copy, Clone, Eq, PartialEq, IntoPrimitive, TryFromPrimitive)]
#[repr(i32)]
#[allow(non_camel_case_types)]
pub enum ShmctlCmd {
    IPC_RMID = const_conversions::i32_from_u32(bindings::LINUX_IPC_RMID),
    IPC_SET = const_conversions::i32_from_u32(bindings::LINUX_IPC_SET),
    IPC_STAT = const_conversions::i32_from_u32(bindings::LINUX_IPC_STAT),
    IPC_INFO = const_conversions::i32_from_u32(bindings::LINUX_IPC_INFO),
    SHM_LOCK = const_conversions::i32_from_u32(bindings::LINUX_SHM_LOCK),
    SHM_UNLOCK = const_conversions::i32_from_u32(bindings::LINUX_SHM_UNLOCK),
    SHM_STAT = const_conversions::i32_from_u32(bindings::LINUX_SHM_STAT),
    SHM_INFO = const_conversions::i32_from_u32(bindings::LINUX_SHM_INFO),
    SHM_STAT_ANY = const_conversions::i32_from_u32(bindings::LINUX_SHM_STAT_ANY),
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::stat::SFlag;

    fn test_shmgetflags_value_roundtrip(shmflgs: core::ffi::c_int) {
        let parts = ShmgetFlagsParts::from_shmflg(shmflgs);
        let shmflgs2 = parts.unchecked_to_shmflg();
        assert_eq!(shmflgs2, shmflgs)
    }

    #[test]
    fn test_shmgetflags_values_roundtrips() {
        test_shmgetflags_value_roundtrip(0);
        test_shmgetflags_value_roundtrip(0xdeadbeefu32 as i32);
        test_shmgetflags_value_roundtrip(0xfedcba98u32 as i32);
        test_shmgetflags_value_roundtrip(0x01234567u32 as i32);
        test_shmgetflags_value_roundtrip(
            // flags
            (bindings::LINUX_IPC_CREAT|bindings::LINUX_IPC_EXCL|bindings::LINUX_SHM_HUGETLB|bindings::LINUX_SHM_NORESERVE) as i32
            // map size
            | bindings::LINUX_SHM_HUGE_2MB as i32
            // perms
            | (bindings::LINUX_S_IRUSR | bindings::LINUX_S_IWUSR) as i32,
        )
    }

    #[test]
    fn test_parse_shmgetflgs() {
        let shmflg =
            // flags
            (bindings::LINUX_IPC_CREAT|bindings::LINUX_IPC_EXCL|bindings::LINUX_SHM_HUGETLB|bindings::LINUX_SHM_NORESERVE) as i32
            // map size
            | bindings::LINUX_SHM_HUGE_2MB as i32
            // perms
            | (bindings::LINUX_S_IRUSR | bindings::LINUX_S_IWUSR) as i32;
        let parts = ShmgetFlagsParts::from_shmflg(shmflg);
        assert_eq!(
            parts.flags,
            ShmgetFlags::IPC_CREAT
                | ShmgetFlags::IPC_EXCL
                | ShmgetFlags::SHM_HUGETLB
                | ShmgetFlags::SHM_NORESERVE
        );
        assert_eq!(parts.tlb_size_log_2, 21);
        assert_eq!(parts.perms, SFlag::S_IRUSR | SFlag::S_IWUSR);
    }
}
