use crate::bindings;
use crate::const_conversions;

#[derive(Copy, Clone, Debug, num_enum::TryFromPrimitive, num_enum::IntoPrimitive)]
#[repr(u64)]
#[allow(non_camel_case_types)]
pub enum AuxVecTag {
    AT_NULL = const_conversions::u64_from_u32(bindings::LINUX_AT_NULL),
    AT_IGNORE = const_conversions::u64_from_u32(bindings::LINUX_AT_IGNORE),
    AT_EXECFD = const_conversions::u64_from_u32(bindings::LINUX_AT_EXECFD),
    AT_PHDR = const_conversions::u64_from_u32(bindings::LINUX_AT_PHDR),
    AT_PHENT = const_conversions::u64_from_u32(bindings::LINUX_AT_PHENT),
    AT_PHNUM = const_conversions::u64_from_u32(bindings::LINUX_AT_PHNUM),
    AT_PAGESZ = const_conversions::u64_from_u32(bindings::LINUX_AT_PAGESZ),
    AT_BASE = const_conversions::u64_from_u32(bindings::LINUX_AT_BASE),
    AT_FLAGS = const_conversions::u64_from_u32(bindings::LINUX_AT_FLAGS),
    AT_ENTRY = const_conversions::u64_from_u32(bindings::LINUX_AT_ENTRY),
    AT_NOTELF = const_conversions::u64_from_u32(bindings::LINUX_AT_NOTELF),
    AT_UID = const_conversions::u64_from_u32(bindings::LINUX_AT_UID),
    AT_EUID = const_conversions::u64_from_u32(bindings::LINUX_AT_EUID),
    AT_GID = const_conversions::u64_from_u32(bindings::LINUX_AT_GID),
    AT_EGID = const_conversions::u64_from_u32(bindings::LINUX_AT_EGID),
    AT_PLATFORM = const_conversions::u64_from_u32(bindings::LINUX_AT_PLATFORM),
    AT_HWCAP = const_conversions::u64_from_u32(bindings::LINUX_AT_HWCAP),
    AT_CLKTCK = const_conversions::u64_from_u32(bindings::LINUX_AT_CLKTCK),
    AT_SECURE = const_conversions::u64_from_u32(bindings::LINUX_AT_SECURE),
    AT_BASE_PLATFORM = const_conversions::u64_from_u32(bindings::LINUX_AT_BASE_PLATFORM),
    AT_RANDOM = const_conversions::u64_from_u32(bindings::LINUX_AT_RANDOM),
    AT_HWCAP2 = const_conversions::u64_from_u32(bindings::LINUX_AT_HWCAP2),
    AT_RSEQ_FEATURE_SIZE = const_conversions::u64_from_u32(bindings::LINUX_AT_RSEQ_FEATURE_SIZE),
    AT_RSEQ_ALIGN = const_conversions::u64_from_u32(bindings::LINUX_AT_RSEQ_ALIGN),
    AT_HWCAP3 = const_conversions::u64_from_u32(bindings::LINUX_AT_HWCAP3),
    AT_HWCAP4 = const_conversions::u64_from_u32(bindings::LINUX_AT_HWCAP4),
    AT_EXECFN = const_conversions::u64_from_u32(bindings::LINUX_AT_EXECFN),
    AT_MINSIGSTKSZ = const_conversions::u64_from_u32(bindings::LINUX_AT_MINSIGSTKSZ),
}
