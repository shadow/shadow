use shadow_shim_helper_rs::syscall_types::ForeignPtr;
use shadow_shim_helper_rs::util::DebugFormatter;

use super::formatter::{FmtOptions, SyscallDisplay, SyscallVal};
use crate::host::memory_manager::MemoryManager;
use crate::host::syscall::io::read_sockaddr;
use crate::host::syscall::types::ForeignArrayPtr;

fn fmt_int_with_suffix(
    f: &mut std::fmt::Formatter<'_>,
    ptr: u64,
    suffix: &str,
) -> std::fmt::Result {
    if ptr == 0 {
        write!(f, "{ptr:#x} <null>")
    } else {
        write!(f, "{ptr:#x} {}", suffix)
    }
}

fn fmt_ptr_with_suffix<T>(
    f: &mut std::fmt::Formatter<'_>,
    ptr: ForeignPtr<T>,
    suffix: &str,
) -> std::fmt::Result {
    if ptr.is_null() {
        write!(f, "{ptr:p} <null>")
    } else {
        write!(f, "{ptr:p} {}", suffix)
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
                match <$type>::try_from(self.reg).ok() {
                    Some(x) => write!(f, "{x}"),
                    // if the conversion to type T was unsuccessful, just show an integer
                    None => fmt_int_with_suffix(f, u64::from(self.reg), "<invalid-value>"),
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
                match <$type>::try_from(self.reg).ok() {
                    Some(x) => write!(f, "{x:?}"),
                    // if the conversion to type T was unsuccessful, just show an integer
                    None => fmt_int_with_suffix(f, u64::from(self.reg), "<invalid-value>"),
                }
            }
        }
    };
}

/// Implement `SyscallDisplay` for a bitflags type. The type must implement `TryFromSyscallReg`.
macro_rules! bitflags_impl {
    ($type:ty, $($types:ty),+) => {
        bitflags_impl!($type);
        bitflags_impl!($($types),+);
    };
    ($type:ty) => {
        impl SyscallDisplay for SyscallVal<'_, $type> {
            fn fmt(
                &self,
                f: &mut std::fmt::Formatter<'_>,
                _options: FmtOptions,
                _mem: &MemoryManager,
            ) -> std::fmt::Result {
                match <$type>::try_from(self.reg).ok() {
                    Some(x) => {
                        if x.is_empty() {
                            write!(f, "(empty)")
                        } else {
                            bitflags::parser::to_writer(&x, f)
                        }
                    }
                    // if the conversion to type T was unsuccessful, just show an integer
                    None => fmt_int_with_suffix(f, u64::from(self.reg), "<invalid-value>"),
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
                let ptr = ForeignPtr::<$type>::from(self.reg);
                match (options, mem.memory_ref(ForeignArrayPtr::new(ptr, 1))) {
                    (FmtOptions::Standard, Ok(vals)) => write!(f, "{:?} ({:p})", &(*vals)[0], ptr),
                    // if we couldn't read the memory, just show the pointer instead
                    (FmtOptions::Standard, Err(_)) => fmt_ptr_with_suffix(f, ptr, "<invalid-read>"),
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
                let ptr = ForeignPtr::<()>::from(self.reg);
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
                let ptr = ForeignPtr::<$type>::from(self.reg);
                match (options, mem.memory_ref(ForeignArrayPtr::new(ptr, K))) {
                    (FmtOptions::Standard, Ok(vals)) => write!(f, "{:?} ({:p})", &(*vals), ptr),
                    // if we couldn't read the memory, just show the pointer instead
                    (FmtOptions::Standard, Err(_)) => fmt_ptr_with_suffix(f, ptr, "<invalid-read>"),
                    (FmtOptions::Deterministic, _) => write!(f, "<pointer>"),
                }
            }
        }
    };
}

// implement display formatting

simple_display_impl!(i8, i16, i32, i64, isize);
simple_display_impl!(u8, u16, u32, u64, usize);

deref_pointer_impl!(i8, i16, i32, i64, isize);
deref_pointer_impl!(u8, u16, u32, u64, usize);
deref_pointer_impl!(linux_api::stat::stat);
deref_pointer_impl!(linux_api::sched::clone_args);
deref_pointer_impl!(linux_api::time::timespec);
deref_pointer_impl!(linux_api::time::kernel_timespec);
deref_pointer_impl!(linux_api::time::kernel_old_timeval);

deref_array_impl!(i8, i16, i32, i64, isize);
deref_array_impl!(u8, u16, u32, u64, usize);

safe_pointer_impl!(std::ffi::c_void);
safe_pointer_impl!(libc::sockaddr);
safe_pointer_impl!(linux_api::sysinfo::sysinfo);
safe_pointer_impl!(libc::iovec);

// nix still uses an old bitflags version which isn't supported by `bitflags_impl`
simple_debug_impl!(linux_api::sched::CloneFlags);
simple_debug_impl!(linux_api::time::ITimerId);
simple_debug_impl!(linux_api::time::ClockId);
simple_debug_impl!(nix::sys::stat::Mode);
simple_debug_impl!(nix::sys::eventfd::EfdFlags);
simple_debug_impl!(nix::sys::socket::MsgFlags);

simple_display_impl!(linux_api::prctl::PrctlOp);
simple_display_impl!(linux_api::socket::AddressFamily);

bitflags_impl!(linux_api::close_range::CloseRangeFlags);
bitflags_impl!(linux_api::fcntl::OFlag);
bitflags_impl!(linux_api::mman::ProtFlags);
bitflags_impl!(linux_api::mman::MapFlags);
bitflags_impl!(linux_api::mman::MRemapFlags);
bitflags_impl!(linux_api::time::ClockNanosleepFlags);

fn fmt_buffer(
    f: &mut std::fmt::Formatter<'_>,
    ptr: ForeignPtr<u8>,
    len: usize,
    options: FmtOptions,
    mem: &MemoryManager,
) -> std::fmt::Result {
    const DISPLAY_LEN: usize = 40;

    if options == FmtOptions::Deterministic {
        return write!(f, "<pointer>");
    }

    let mem_ref = match mem.memory_ref_prefix(ForeignArrayPtr::new(ptr, len)) {
        Ok(x) => x,
        // the pointer didn't reference any valid memory
        Err(_) => return fmt_ptr_with_suffix(f, ptr, "<invalid-addr>"),
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
    ptr: ForeignPtr<u8>,
    len: Option<usize>,
    options: FmtOptions,
    mem: &MemoryManager,
) -> std::fmt::Result {
    const DISPLAY_LEN: usize = 40;

    if options == FmtOptions::Deterministic {
        return write!(f, "<pointer>");
    }

    // the pointer may point to a buffer of unknown length, so we may have to choose our own size
    let len = len.unwrap_or(
        // read up to one extra character to check if it's a NUL byte
        //
        // each byte may take 1 byte to display (ex: 0x41 -> "A") or up to 4 bytes to display (ex:
        // 0x00 -> "\x00"), so a buffer of size `DISPLAY_LEN + 1` should always be enough space to
        // print a string of length `DISPLAY_LEN`
        DISPLAY_LEN + 1,
    );

    let mem_ref = match mem.memory_ref_prefix(ForeignArrayPtr::new(ptr, len)) {
        Ok(x) => x,
        // the pointer didn't reference any valid memory
        Err(_) => return fmt_ptr_with_suffix(f, ptr, "<invalid-addr>"),
    };

    let mut s = String::with_capacity(DISPLAY_LEN);

    // the number of plugin mem bytes used; num_bytes <= s.len()
    let mut found_nul = false;

    for c in mem_ref.iter() {
        // if it's a NUL byte, it's the end of the string
        if *c == 0 {
            found_nul = true;
            break;
        }

        let escaped = std::ascii::escape_default(*c);

        if s.len() + escaped.len() > DISPLAY_LEN {
            break;
        }

        for b in escaped {
            s.push(b.into())
        }
    }

    if found_nul {
        write!(f, "\"{s}\"")
    } else {
        write!(f, "\"{s}\"...")
    }
}

/// Format a plugin's `libc::msghdr`. Any pointers contained in the `libc::msghdr` must be pointers
/// within the plugin's memory space.
fn fmt_msghdr(
    f: &mut std::fmt::Formatter<'_>,
    msg: &libc::msghdr,
    _options: FmtOptions,
    mem: &MemoryManager,
) -> std::fmt::Result {
    let ptr = ForeignPtr::from_raw_ptr(msg.msg_name as *mut u8);

    // read the socket address from `msg.msg_name`
    let addr = read_sockaddr(mem, ptr, msg.msg_namelen);

    // prepare the socket address for formatting
    let msg_name = DebugFormatter(move |fmt| {
        match addr {
            Ok(Some(addr)) => write!(fmt, "{addr} ({:p})", ptr),
            Ok(None) => write!(fmt, "{:p}", ptr),
            // if we weren't able to read the sockaddr (NULL, EFAULT, etc), just show the pointer
            Err(_) => fmt_ptr_with_suffix(fmt, ptr, "<invalid-read>"),
        }
    });

    // prepare the message flags for formatting
    let msg_flags =
        DebugFormatter(
            move |fmt| match nix::sys::socket::MsgFlags::from_bits(msg.msg_flags) {
                Some(x) => write!(fmt, "{x:?}"),
                None => write!(fmt, "{:#x} <invalid>", msg.msg_flags),
            },
        );

    // format msg
    f.debug_struct("msghdr")
        .field("msg_name", &msg_name)
        .field("msg_namelen", &msg.msg_namelen)
        .field("msg_iov", &msg.msg_iov)
        .field("msg_iovlen", &msg.msg_iovlen)
        .field("msg_control", &msg.msg_control)
        .field("msg_controllen", &msg.msg_controllen)
        .field("msg_flags", &msg_flags)
        .finish()?;

    Ok(())
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
        let len: libc::size_t = self.args[LEN_INDEX].into();
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
        fmt_string(f, ptr, None, options, mem)
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
        let len = self.args[LEN_INDEX].into();

        let Ok(addr) = read_sockaddr(mem, ptr, len) else {
            return fmt_ptr_with_suffix(f, ptr, "<invalid-read>");
        };

        let Some(addr) = addr else {
            return write!(f, "{ptr:p}");
        };

        write!(f, "{addr}")
    }
}

impl SyscallDisplay for SyscallVal<'_, *const libc::msghdr> {
    fn fmt(
        &self,
        f: &mut std::fmt::Formatter<'_>,
        options: FmtOptions,
        mem: &MemoryManager,
    ) -> std::fmt::Result {
        let ptr: ForeignPtr<libc::msghdr> = self.reg.into();

        if options == FmtOptions::Deterministic {
            return write!(f, "<pointer>");
        }

        // read the msghdr
        let ptr = ForeignArrayPtr::new(ptr, 1);
        let Ok(msg) = mem.memory_ref(ptr) else {
            // if we couldn't read the memory, just show the pointer instead
            return fmt_ptr_with_suffix(f, ptr.ptr(), "<invalid-read>");
        };
        let msg = &(*msg)[0];

        // format the msghdr
        fmt_msghdr(f, msg, options, mem)?;

        // show the original pointer
        write!(f, " ({:p})", ptr.ptr())
    }
}
