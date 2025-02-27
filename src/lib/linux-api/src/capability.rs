use linux_syscall::Result as LinuxSyscallResult;
use linux_syscall::syscall;

use crate::bindings;
use crate::errno::Errno;

pub const LINUX_CAPABILITY_VERSION_3: u32 = bindings::LINUX__LINUX_CAPABILITY_VERSION_3;

#[allow(non_camel_case_types)]
pub type user_cap_header = __user_cap_header_struct;
#[allow(non_camel_case_types)]
pub type user_cap_data = __user_cap_data_struct;

// Somehow this is not automatically generated in bindings.rs
#[repr(C)]
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub struct __user_cap_header_struct {
    pub version: bindings::linux___u32,
    pub pid: ::core::ffi::c_int,
}

// Somehow this is not automatically generated in bindings.rs
#[repr(C)]
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub struct __user_cap_data_struct {
    pub effective: bindings::linux___u32,
    pub permitted: bindings::linux___u32,
    pub inheritable: bindings::linux___u32,
}

unsafe fn capget_raw(hdrp: *const user_cap_header, datap: *mut user_cap_data) -> Result<(), Errno> {
    unsafe { syscall!(linux_syscall::SYS_capget, hdrp, datap) }
        .check()
        .map_err(Errno::from)
}

// `linux/capability.h` typedefs `__user_cap_header_struct*` to `cap_user_header_t` and
// `__user_cap_data_struct*` to `cap_user_data_t`. The syscall definition uses `cap_user_header_t`
// and `cap_user_data_t`.
//
// ```
// SYSCALL_DEFINE2(capget, cap_user_header_t, header, cap_user_data_t, dataptr)
// ```
pub fn capget(hdrp: &user_cap_header, datap: Option<&mut [user_cap_data; 2]>) -> Result<(), Errno> {
    unsafe {
        capget_raw(
            hdrp,
            datap
                .map(|x| x.as_mut_ptr())
                .unwrap_or(core::ptr::null_mut()),
        )
    }
}

unsafe fn capset_raw(
    hdrp: *const user_cap_header,
    datap: *const user_cap_data,
) -> Result<(), Errno> {
    unsafe { syscall!(linux_syscall::SYS_capset, hdrp, datap) }
        .check()
        .map_err(Errno::from)
}

// `linux/capability.h` typedefs `__user_cap_header_struct*` to `cap_user_header_t` and
// `__user_cap_data_struct*` to `cap_user_data_t`. The syscall definition uses `cap_user_header_t`
// and `cap_user_data_t`.
//
// ```
// SYSCALL_DEFINE2(capset, cap_user_header_t, header, const cap_user_data_t, data)
// ```
pub fn capset(hdrp: &user_cap_header, datap: &[user_cap_data; 2]) -> Result<(), Errno> {
    unsafe { capset_raw(hdrp, datap.as_ptr()) }
}

unsafe impl shadow_pod::Pod for __user_cap_header_struct {}
unsafe impl shadow_pod::Pod for __user_cap_data_struct {}
