use crate::bindings;

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

unsafe impl shadow_pod::Pod for __user_cap_header_struct {}
unsafe impl shadow_pod::Pod for __user_cap_data_struct {}
