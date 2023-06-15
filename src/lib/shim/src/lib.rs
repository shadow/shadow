// TODO:
// #![no_std]

/// cbindgen:ignore
mod bindings {
    #![allow(unused)]
    #![allow(non_upper_case_globals)]
    #![allow(non_camel_case_types)]
    #![allow(non_snake_case)]
    // https://github.com/rust-lang/rust/issues/66220
    #![allow(improper_ctypes)]
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

// Force cargo to link against crates that aren't (yet) referenced from Rust
// code (but are referenced from this crate's C code).
// https://github.com/rust-lang/cargo/issues/9391
extern crate logger;
extern crate shadow_shim_helper_rs;
extern crate shadow_shmem;
extern crate shadow_tsc;

// Rust's linking of a `cdylib` only considers Rust `pub extern "C"` entry
// points, and the symbols those recursively used, to be used. i.e. any function
// called from outside of the shim needs to be exported from the Rust code. We
// wrap some C implementations here.
pub mod export {
    use super::*;

    /// # Safety
    ///
    /// The syscall itself must be safe to make.
    #[no_mangle]
    pub unsafe extern "C" fn shim_api_syscall(
        n: core::ffi::c_long,
        arg1: u64,
        arg2: u64,
        arg3: u64,
        arg4: u64,
        arg5: u64,
        arg6: u64,
    ) -> i64 {
        unsafe { bindings::shimc_api_syscall(n, arg1, arg2, arg3, arg4, arg5, arg6) }
    }

    /// # Safety
    ///
    /// Pointers must be dereferenceable.
    #[no_mangle]
    pub unsafe extern "C" fn shim_api_getaddrinfo(
        node: *const core::ffi::c_char,
        service: *const core::ffi::c_char,
        hints: *const libc::addrinfo,
        res: *mut *mut libc::addrinfo,
    ) -> i32 {
        unsafe { bindings::shimc_api_getaddrinfo(node, service, hints, res) }
    }

    /// # Safety
    ///
    /// * Pointers must be dereferenceable.
    /// * `res` is invalidated afterwards.
    #[no_mangle]
    pub unsafe extern "C" fn shim_api_freeaddrinfo(res: *mut libc::addrinfo) {
        unsafe { bindings::shimc_api_freeaddrinfo(res) }
    }

    /// # Safety
    ///
    /// Pointers must be dereferenceable
    #[no_mangle]
    pub unsafe extern "C" fn shim_api_getifaddrs(ifap: *mut *mut libc::ifaddrs) -> i32 {
        unsafe { bindings::shimc_api_getifaddrs(ifap) }
    }

    /// # Safety
    ///
    /// * Pointers must be dereferenceable.
    /// * `ifa` is invalidated afterwards.
    #[no_mangle]
    pub unsafe extern "C" fn shim_api_freeifaddrs(ifa: *mut libc::ifaddrs) {
        unsafe { bindings::shimc_api_freeifaddrs(ifa) }
    }
}
