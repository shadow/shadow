use std::num::NonZeroU8;

use crate::host::memory_manager::MemoryManager;
use crate::host::syscall::handler::read_sockaddr;
use crate::host::syscall_types::{PluginPtr, SysCallReg, TypedPluginPtr};

use super::formatter::{FmtOptions, SyscallDisplay, SyscallVal};

/// Convert from a `SysCallReg`. This is a helper trait for the `simple_display_impl` and
/// `simple_debug_impl` macros. This is used instead of just `TryFrom` so that we can implement this
/// on any types without affecting `TryFrom` implementations in the rest of Shadow.
pub trait TryFromSyscallReg
where
    Self: Sized,
{
    fn try_from_reg(reg: SysCallReg) -> Option<Self>;
}

impl<T: TryFrom<SysCallReg>> TryFromSyscallReg for T {
    fn try_from_reg(reg: SysCallReg) -> Option<Self> {
        Self::try_from(reg).ok()
    }
}

/// Implement `SyscallDisplay` using its `Display` implementation. The type must implement
/// `TryFromSyscallReg`.
macro_rules! simple_display_impl {
    ($type:ty, $($types:ty),+) => {
        simple_display_impl!($type);
        simple_display_impl!($($types),+);
    };
    ($type:ty) => {
        impl SyscallDisplay for SyscallVal<'_, $type> {
            fn fmt(
                &self,
                f: &mut std::fmt::Formatter<'_>,
                _options: FmtOptions,
                _mem: &MemoryManager,
            ) -> std::fmt::Result {
                match <$type>::try_from_reg(self.reg) {
                    Some(x) => write!(f, "{x}"),
                    // if the conversion to type T was unsuccessful, just show an integer
                    None => write!(f, "{:#x} <invalid>", unsafe { self.reg.as_u64 }),
                }
            }
        }
    };
}

/// Implement `SyscallDisplay` using its `Debug` implementation. The type must implement
/// `TryFromSyscallReg`.
macro_rules! simple_debug_impl {
    ($type:ty, $($types:ty),+) => {
        simple_debug_impl!($type);
        simple_debug_impl!($($types),+);
    };
    ($type:ty) => {
        impl SyscallDisplay for SyscallVal<'_, $type> {
            fn fmt(
                &self,
                f: &mut std::fmt::Formatter<'_>,
                _options: FmtOptions,
                _mem: &MemoryManager,
            ) -> std::fmt::Result {
                match <$type>::try_from_reg(self.reg) {
                    Some(x) => write!(f, "{x:?}"),
                    // if the conversion to type T was unsuccessful, just show an integer
                    None => write!(f, "{:#x} <invalid>", unsafe { self.reg.as_u64 }),
                }
            }
        }
    };
}

/// Display the pointer and data. Accesses plugin memory. Can only be used for pod types (enforced
/// by the memory manager).
macro_rules! deref_pointer_impl {
    ($type:ty, $($types:ty),+) => {
        deref_pointer_impl!($type);
        deref_pointer_impl!($($types),+);
    };
    ($type:ty) => {
        impl SyscallDisplay for SyscallVal<'_, *const $type> {
            fn fmt(
                &self,
                f: &mut std::fmt::Formatter<'_>,
                options: FmtOptions,
                mem: &MemoryManager,
            ) -> std::fmt::Result {
                let ptr = PluginPtr::from(self.reg);
                match (options, mem.memory_ref(TypedPluginPtr::new::<$type>(ptr, 1))) {
                    (FmtOptions::Standard, Ok(vals)) => write!(f, "{} ({:p})", &(*vals)[0], ptr),
                    // if we couldn't read the memory, just show the pointer instead
                    (FmtOptions::Standard, Err(_)) => write!(f, "{ptr:p}"),
                    (FmtOptions::Deterministic, _) => write!(f, "<pointer>"),
                }
            }
        }
    };
}

/// Display the pointer without dereferencing any plugin memory. Useful for types like *void, or
/// when we don't care about the data.
macro_rules! safe_pointer_impl {
    ($type:ty, $($types:ty),+) => {
        safe_pointer_impl!($type);
        safe_pointer_impl!($($types),+);
    };
    ($type:ty) => {
        impl SyscallDisplay for SyscallVal<'_, *const $type> {
            fn fmt(
                &self,
                f: &mut std::fmt::Formatter<'_>,
                options: FmtOptions,
                _mem: &MemoryManager,
            ) -> std::fmt::Result {
                let ptr = PluginPtr::from(self.reg);
                match options {
                    FmtOptions::Standard => write!(f, "{ptr:p}"),
                    FmtOptions::Deterministic => write!(f, "<pointer>"),
                }
            }
        }
    };
}

/// Display the array pointer and data. Accesses plugin memory. Can only be used for pod types
/// (enforced by the memory manager).
macro_rules! deref_array_impl {
    ($type:ty, $($types:ty),+) => {
        deref_array_impl!($type);
        deref_array_impl!($($types),+);
    };
    ($type:ty) => {
        impl<const K: usize> SyscallDisplay for SyscallVal<'_, [$type; K]> {
            fn fmt(
                &self,
                f: &mut std::fmt::Formatter<'_>,
                options: FmtOptions,
                mem: &MemoryManager,
            ) -> std::fmt::Result {
                let ptr = PluginPtr::from(self.reg);
                match (options, mem.memory_ref(TypedPluginPtr::new::<$type>(ptr, K))) {
                    (FmtOptions::Standard, Ok(vals)) => write!(f, "{:?} ({:p})", &(*vals), ptr),
                    // if we couldn't read the memory, just show the pointer instead
                    (FmtOptions::Standard, Err(_)) => write!(f, "{ptr:p}"),
                    (FmtOptions::Deterministic, _) => write!(f, "<pointer>"),
                }
            }
        }
    };
}

// implement conversions from `SysCallReg`

impl TryFromSyscallReg for nix::fcntl::OFlag {
    fn try_from_reg(reg: SysCallReg) -> Option<Self> {
        Self::from_bits(reg.try_into().ok()?)
    }
}

impl TryFromSyscallReg for nix::sys::eventfd::EfdFlags {
    fn try_from_reg(reg: SysCallReg) -> Option<Self> {
        Self::from_bits(reg.try_into().ok()?)
    }
}

impl TryFromSyscallReg for nix::sys::socket::AddressFamily {
    fn try_from_reg(reg: SysCallReg) -> Option<Self> {
        Self::from_i32(reg.try_into().ok()?)
    }
}

impl TryFromSyscallReg for nix::sys::socket::MsgFlags {
    fn try_from_reg(reg: SysCallReg) -> Option<Self> {
        Self::from_bits(reg.try_into().ok()?)
    }
}

impl TryFromSyscallReg for nix::sys::stat::Mode {
    fn try_from_reg(reg: SysCallReg) -> Option<Self> {
        Self::from_bits(reg.try_into().ok()?)
    }
}

impl TryFromSyscallReg for nix::sys::mman::ProtFlags {
    fn try_from_reg(reg: SysCallReg) -> Option<Self> {
        Self::from_bits(reg.try_into().ok()?)
    }
}

impl TryFromSyscallReg for nix::sys::mman::MapFlags {
    fn try_from_reg(reg: SysCallReg) -> Option<Self> {
        Self::from_bits(reg.try_into().ok()?)
    }
}

impl TryFromSyscallReg for nix::sys::mman::MRemapFlags {
    fn try_from_reg(reg: SysCallReg) -> Option<Self> {
        Self::from_bits(reg.try_into().ok()?)
    }
}

// implement display formatting

simple_display_impl!(i8, i16, i32, i64, isize);
simple_display_impl!(u8, u16, u32, u64, usize);

deref_pointer_impl!(i8, i16, i32, i64, isize);
deref_pointer_impl!(u8, u16, u32, u64, usize);

deref_array_impl!(i8, i16, i32, i64, isize);
deref_array_impl!(u8, u16, u32, u64, usize);

safe_pointer_impl!(libc::c_void);
safe_pointer_impl!(libc::sockaddr);
safe_pointer_impl!(libc::sysinfo);

simple_debug_impl!(nix::fcntl::OFlag);
simple_debug_impl!(nix::sys::eventfd::EfdFlags);
simple_debug_impl!(nix::sys::socket::AddressFamily);
simple_debug_impl!(nix::sys::socket::MsgFlags);
simple_debug_impl!(nix::sys::stat::Mode);
simple_debug_impl!(nix::sys::mman::ProtFlags);
simple_debug_impl!(nix::sys::mman::MapFlags);
simple_debug_impl!(nix::sys::mman::MRemapFlags);

fn fmt_buffer(
    f: &mut std::fmt::Formatter<'_>,
    ptr: PluginPtr,
    len: usize,
    options: FmtOptions,
    mem: &MemoryManager,
) -> std::fmt::Result {
    const DISPLAY_LEN: usize = 40;

    if options == FmtOptions::Deterministic {
        return write!(f, "<pointer>");
    }

    let mem_ref = match mem.memory_ref_prefix(TypedPluginPtr::new::<u8>(ptr, len)) {
        Ok(x) => x,
        // the pointer didn't reference any valid memory
        Err(_) => return write!(f, "{ptr:p}"),
    };

    let mut s = String::with_capacity(DISPLAY_LEN);

    // the number of plugin mem bytes used; num_bytes <= s.len()
    let mut num_plugin_bytes = 0;

    for c in mem_ref.iter() {
        let escaped = std::ascii::escape_default(*c);

        if s.len() + escaped.len() > DISPLAY_LEN {
            break;
        }

        for b in escaped {
            s.push(b.into())
        }

        num_plugin_bytes += 1;
    }

    if len > num_plugin_bytes {
        write!(f, "\"{s}\"...")
    } else {
        write!(f, "\"{s}\"")
    }
}

fn fmt_string(
    f: &mut std::fmt::Formatter<'_>,
    ptr: PluginPtr,
    options: FmtOptions,
    mem: &MemoryManager,
) -> std::fmt::Result {
    const DISPLAY_LEN: usize = 40;

    if options == FmtOptions::Deterministic {
        return write!(f, "<pointer>");
    }

    // read up to one extra character to check if it's a null byte
    let mem_ref = match mem.memory_ref_prefix(TypedPluginPtr::new::<u8>(ptr, DISPLAY_LEN + 1)) {
        Ok(x) => x,
        // the pointer didn't reference any valid memory
        Err(_) => return write!(f, "{ptr:p}"),
    };

    // to avoid printing too many escaped bytes, limit the number of non-graphic and non-ascii
    // characters
    let mut non_graphic_remaining = DISPLAY_LEN / 3;

    // mem_ref will reference up to DISPLAY_LEN+1 bytes
    let mut s: Vec<NonZeroU8> = mem_ref
        .iter()
        // get bytes until a null byte
        .map_while(|x| NonZeroU8::new(*x))
        // stop after a certain number of non-graphic characters
        .map_while(|x| {
            if !x.get().is_ascii_graphic() {
                non_graphic_remaining = non_graphic_remaining.saturating_sub(1);
            }
            (non_graphic_remaining > 0).then_some(x)
        })
        .collect();

    let len = s.len();
    s.truncate(DISPLAY_LEN);
    let s: std::ffi::CString = s.into();

    #[allow(clippy::absurd_extreme_comparisons)]
    if len > DISPLAY_LEN || non_graphic_remaining <= 0 {
        write!(f, "{s:?}...")
    } else {
        write!(f, "{s:?}")
    }
}

/// Displays a byte buffer with a specified length.
pub struct SyscallBufferArg<const LEN_INDEX: usize> {}

impl<const LEN_INDEX: usize> SyscallDisplay for SyscallVal<'_, SyscallBufferArg<LEN_INDEX>> {
    fn fmt(
        &self,
        f: &mut std::fmt::Formatter<'_>,
        options: FmtOptions,
        mem: &MemoryManager,
    ) -> std::fmt::Result {
        let ptr = self.reg.into();
        let Ok(len) = self.args[LEN_INDEX].try_into() else {
            return write!(f, "{ptr:p}");
        };
        fmt_buffer(f, ptr, len, options, mem)
    }
}

/// Displays a nul-terminated string syscall argument.
pub struct SyscallStringArg {}

impl SyscallDisplay for SyscallVal<'_, SyscallStringArg> {
    fn fmt(
        &self,
        f: &mut std::fmt::Formatter<'_>,
        options: FmtOptions,
        mem: &MemoryManager,
    ) -> std::fmt::Result {
        let ptr = self.reg.into();
        fmt_string(f, ptr, options, mem)
    }
}

pub struct SyscallSockAddrArg<const LEN_INDEX: usize> {}

impl<const LEN_INDEX: usize> SyscallDisplay for SyscallVal<'_, SyscallSockAddrArg<LEN_INDEX>> {
    fn fmt(
        &self,
        f: &mut std::fmt::Formatter<'_>,
        options: FmtOptions,
        mem: &MemoryManager,
    ) -> std::fmt::Result {
        if options == FmtOptions::Deterministic {
            return write!(f, "<pointer>");
        }

        let ptr = self.reg.into();
        let Ok(len) = self.args[LEN_INDEX].try_into() else {
            return write!(f, "{ptr:p}");
        };

        let Ok(Some(addr)) = read_sockaddr(mem, ptr, len) else {
            return write!(f, "{ptr:p}");
        };

        write!(f, "{addr}")
    }
}
