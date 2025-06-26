//! Definitions from include/linux/types.h in linux source
//!
//! This header is *not* included in linux's installed user-space headers;
//! probably because the types defined there would conflict with those defined
//! in other user-space headers. Since we only run bindgen on the installed
//! user-space headers, we don't get these definitions bindgen'd.

// Re-exporting these to the C headers would result in conflicts with
// libc/system headers.
//! cbindgen:no-export

#![allow(non_camel_case_types)]

pub type gid_t = crate::posix_types::kernel_gid32_t;
pub type uid_t = crate::posix_types::kernel_uid32_t;

pub type off_t = crate::posix_types::kernel_off_t;
pub type loff_t = crate::posix_types::kernel_loff_t;

pub type size_t = crate::posix_types::kernel_size_t;

pub type umode_t = core::ffi::c_ushort;
