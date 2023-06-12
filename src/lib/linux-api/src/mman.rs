use crate::{bindings, const_conversions};

bitflags::bitflags! {
    /// Prot flags, as used with `mmap`. These are u64 to match the x86-64 `mmap`
    /// syscall parameter:
    /// <https://github.com/torvalds/linux/tree/v6.3/arch/x86/kernel/sys_x86_64.c#L86>
    #[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
    pub struct ProtFlags: u64 {
        const PROT_READ = const_conversions::u64_from_u32(bindings::LINUX_PROT_READ);
        const PROT_WRITE = const_conversions::u64_from_u32(bindings::LINUX_PROT_WRITE);
        const PROT_EXEC = const_conversions::u64_from_u32(bindings::LINUX_PROT_EXEC);
        const PROT_SEM = const_conversions::u64_from_u32(bindings::LINUX_PROT_SEM);
        const PROT_NONE = const_conversions::u64_from_u32(bindings::LINUX_PROT_NONE);
        const PROT_GROWSDOWN = const_conversions::u64_from_u32(bindings::LINUX_PROT_GROWSDOWN);
        const PROT_GROWSUP = const_conversions::u64_from_u32(bindings::LINUX_PROT_GROWSUP);
    }
}

bitflags::bitflags! {
    /// Map flags, as used with `mmap`. These are u64 to match the x86-64 `mmap`
    /// syscall parameter:
    /// <https://github.com/torvalds/linux/tree/v6.3/arch/x86/kernel/sys_x86_64.c#L86>
    #[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
    pub struct MapFlags: u64 {
        const MAP_TYPE = const_conversions::u64_from_u32(bindings::LINUX_MAP_TYPE);
        const MAP_FIXED = const_conversions::u64_from_u32(bindings::LINUX_MAP_FIXED);
        const MAP_ANONYMOUS = const_conversions::u64_from_u32(bindings::LINUX_MAP_ANONYMOUS);
        const MAP_POPULATE = const_conversions::u64_from_u32(bindings::LINUX_MAP_POPULATE);
        const MAP_NONBLOCK = const_conversions::u64_from_u32(bindings::LINUX_MAP_NONBLOCK);
        const MAP_STACK = const_conversions::u64_from_u32(bindings::LINUX_MAP_STACK);
        const MAP_HUGETLB = const_conversions::u64_from_u32(bindings::LINUX_MAP_HUGETLB);
        const MAP_SYNC = const_conversions::u64_from_u32(bindings::LINUX_MAP_SYNC);
        const MAP_FIXED_NOREPLACE = const_conversions::u64_from_u32(bindings::LINUX_MAP_FIXED_NOREPLACE);
        const MAP_UNINITIALIZED = const_conversions::u64_from_u32(bindings::LINUX_MAP_UNINITIALIZED);
        const MAP_SHARED = const_conversions::u64_from_u32(bindings::LINUX_MAP_SHARED);
        const MAP_PRIVATE = const_conversions::u64_from_u32(bindings::LINUX_MAP_PRIVATE);
        const MAP_SHARED_VALIDATE = const_conversions::u64_from_u32(bindings::LINUX_MAP_SHARED_VALIDATE);
        const MAP_HUGE_SHIFT = const_conversions::u64_from_u32(bindings::LINUX_MAP_HUGE_SHIFT);
        const MAP_HUGE_MASK = const_conversions::u64_from_u32(bindings::LINUX_MAP_HUGE_MASK);
        const MAP_HUGE_16KB = const_conversions::u64_from_u32(bindings::LINUX_MAP_HUGE_16KB);
        const MAP_HUGE_64KB = const_conversions::u64_from_u32(bindings::LINUX_MAP_HUGE_64KB);
        const MAP_HUGE_512KB = const_conversions::u64_from_u32(bindings::LINUX_MAP_HUGE_512KB);
        const MAP_HUGE_1MB = const_conversions::u64_from_u32(bindings::LINUX_MAP_HUGE_1MB);
        const MAP_HUGE_2MB = const_conversions::u64_from_u32(bindings::LINUX_MAP_HUGE_2MB);
        const MAP_HUGE_8MB = const_conversions::u64_from_u32(bindings::LINUX_MAP_HUGE_8MB);
        const MAP_HUGE_16MB = const_conversions::u64_from_u32(bindings::LINUX_MAP_HUGE_16MB);
        const MAP_HUGE_32MB = const_conversions::u64_from_u32(bindings::LINUX_MAP_HUGE_32MB);
        const MAP_HUGE_256MB = const_conversions::u64_from_u32(bindings::LINUX_MAP_HUGE_256MB);
        const MAP_HUGE_512MB = const_conversions::u64_from_u32(bindings::LINUX_MAP_HUGE_512MB);
        const MAP_HUGE_1GB = const_conversions::u64_from_u32(bindings::LINUX_MAP_HUGE_1GB);
        const MAP_HUGE_2GB = const_conversions::u64_from_u32(bindings::LINUX_MAP_HUGE_2GB);
        const MAP_HUGE_16GB = const_conversions::u64_from_u32(bindings::LINUX_MAP_HUGE_16GB);
        const MAP_GROWSDOWN = const_conversions::u64_from_u32(bindings::LINUX_MAP_GROWSDOWN);
        const MAP_DENYWRITE = const_conversions::u64_from_u32(bindings::LINUX_MAP_DENYWRITE);
        const MAP_EXECUTABLE = const_conversions::u64_from_u32(bindings::LINUX_MAP_EXECUTABLE);
        const MAP_LOCKED = const_conversions::u64_from_u32(bindings::LINUX_MAP_LOCKED);
        const MAP_NORESERVE = const_conversions::u64_from_u32(bindings::LINUX_MAP_NORESERVE);
    }
}

bitflags::bitflags! {
    /// Flags used with `mremap`. u64 to match the x86-64 `mremap` syscall parameter:
    /// <https://github.com/torvalds/linux/tree/v6.3/mm/mremap.c#L895>
    #[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
    pub struct MRemapFlags: u64 {
        const MREMAP_MAYMOVE = const_conversions::u64_from_u32(bindings::LINUX_MREMAP_MAYMOVE);
        const MREMAP_FIXED = const_conversions::u64_from_u32(bindings::LINUX_MREMAP_FIXED);
        const MREMAP_DONTUNMAP = const_conversions::u64_from_u32(bindings::LINUX_MREMAP_DONTUNMAP);
    }
}
