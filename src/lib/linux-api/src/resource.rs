use linux_syscall::{Result as _, syscall};
use num_enum::{IntoPrimitive, TryFromPrimitive};
use shadow_pod::Pod;

use crate::{
    bindings,
    errno::Errno,
    posix_types::{Pid, kernel_pid_t},
};

#[allow(non_camel_case_types)]
pub type rusage = crate::bindings::linux_rusage;
unsafe impl Pod for rusage {}

#[allow(non_camel_case_types)]
pub type rlimit = crate::bindings::linux_rlimit;
unsafe impl Pod for rlimit {}

#[allow(non_camel_case_types)]
pub type rlimit64 = crate::bindings::linux_rlimit64;
unsafe impl Pod for rlimit64 {}

// `include/linux/resource.h` defines this as (~0ULL); for some reason
// bindgen is generating LINUX_RLIM64_INFINITY as an i32, though.
pub const RLIM64_INFINITY: u64 = bindings::LINUX_RLIM64_INFINITY as u64;
const _: () = assert!(RLIM64_INFINITY == !0);

// `include/asm-generic/resource.h` defines this as (~0UL); for some reason
// bindgen is generating LINUX_RLIM_INFINITY as an i32, though.
pub const RLIM_INFINITY: u64 = bindings::LINUX_RLIM_INFINITY as u64;
const _: () = assert!(RLIM_INFINITY == !0);

pub const RLIM_NLIMITS: u32 = crate::bindings::LINUX_RLIM_NLIMITS;

// Resource  identifier, as used in getrlimit, setrlimit, prlimit.
#[derive(Debug, Copy, Clone, Eq, PartialEq, IntoPrimitive, TryFromPrimitive)]
#[repr(u32)]
#[allow(non_camel_case_types)]
pub enum Resource {
    RLIMIT_CPU = bindings::LINUX_RLIMIT_CPU,
    RLIMIT_FSIZE = bindings::LINUX_RLIMIT_FSIZE,
    RLIMIT_DATA = bindings::LINUX_RLIMIT_DATA,
    RLIMIT_STACK = bindings::LINUX_RLIMIT_STACK,
    RLIMIT_CORE = bindings::LINUX_RLIMIT_CORE,
    RLIMIT_RSS = bindings::LINUX_RLIMIT_RSS,
    RLIMIT_NPROC = bindings::LINUX_RLIMIT_NPROC,
    RLIMIT_NOFILE = bindings::LINUX_RLIMIT_NOFILE,
    RLIMIT_MEMLOCK = bindings::LINUX_RLIMIT_MEMLOCK,
    RLIMIT_AS = bindings::LINUX_RLIMIT_AS,
    RLIMIT_LOCKS = bindings::LINUX_RLIMIT_LOCKS,
    RLIMIT_SIGPENDING = bindings::LINUX_RLIMIT_SIGPENDING,
    RLIMIT_MSGQUEUE = bindings::LINUX_RLIMIT_MSGQUEUE,
    RLIMIT_NICE = bindings::LINUX_RLIMIT_NICE,
    RLIMIT_RTPRIO = bindings::LINUX_RLIMIT_RTPRIO,
    RLIMIT_RTTIME = bindings::LINUX_RLIMIT_RTTIME,
}

/// Call the `prlimit64` syscall
///
/// # Safety
///
/// Technically, calling this function can't violate Safety in the rust-language
/// sense. It's been conservatively marked `unsafe` though, since in particular
/// *lowering* resource limits  may result in those limits being exceeded, and
/// resource limits being exceeded can result in surprising behaviors that may
/// terminate the process or exercise rarely-used error-handling paths -
/// primarily receiving fatal-by-default signals or related syscalls failing.
///
/// Similarly, technically passing non-dereferenceable pointers doesn't violate
/// safety in this process - the kernel will safely fail to access them and
/// return `EFAULT`.
pub unsafe fn prlimit64_raw(
    pid: kernel_pid_t,
    resource: core::ffi::c_uint,
    new_rlim: *const rlimit64,
    old_rlim: *mut rlimit64,
) -> Result<(), Errno> {
    unsafe {
        syscall!(
            linux_syscall::SYS_prlimit64,
            pid,
            resource,
            new_rlim,
            old_rlim
        )
    }
    .check()
    .map_err(Errno::from)
}

/// Call the `prlimit64` syscall
///
/// # Safety
///
/// Technically, calling this function can't violate Safety in the rust-language
/// sense. It's been conservatively marked `unsafe` though, since in particular
/// *lowering* resource limits  may result in those limits being exceeded, and
/// resource limits being exceeded can result in surprising behaviors that may
/// terminate the process or exercise rarely-used error-handling paths -
/// primarily receiving fatal-by-default signals or related syscalls failing.
pub unsafe fn prlimit64(
    pid: Pid,
    resource: Resource,
    new_rlim: Option<&rlimit64>,
    old_rlim: Option<&mut rlimit64>,
) -> Result<(), Errno> {
    let pid = kernel_pid_t::from(pid.as_raw_nonzero());
    let resource = core::ffi::c_uint::from(resource);
    let new_rlim = match new_rlim {
        Some(x) => core::ptr::from_ref(x),
        None => core::ptr::null(),
    };
    let old_rlim = match old_rlim {
        Some(x) => core::ptr::from_mut(x),
        None => core::ptr::null_mut(),
    };
    unsafe { prlimit64_raw(pid, resource, new_rlim, old_rlim) }
}
