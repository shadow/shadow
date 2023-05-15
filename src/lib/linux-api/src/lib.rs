//! This crate constants and types for interacting with the Linux kernel.
//! It is no_std and doesn't depend on libc.

#![cfg_attr(not(test), no_std)]

mod constants_bindings {
    #![allow(unused)]
    #![allow(non_upper_case_globals)]
    #![allow(non_camel_case_types)]
    #![allow(non_snake_case)]
    // https://github.com/rust-lang/rust/issues/66220
    #![allow(improper_ctypes)]
    #![allow(unsafe_op_in_unsafe_fn)]
    #![allow(clippy::all)]
    include!(concat!(env!("OUT_DIR"), "/constants.rs"));
}
pub use constants_bindings::*;

mod bindings {
    #![allow(unused)]
    #![allow(non_upper_case_globals)]
    #![allow(non_camel_case_types)]
    #![allow(non_snake_case)]
    // https://github.com/rust-lang/rust/issues/66220
    #![allow(improper_ctypes)]
    #![allow(unsafe_op_in_unsafe_fn)]
    #![allow(clippy::all)]
    include!(concat!(env!("OUT_DIR"), "/types.rs"));
}

pub mod signal;

use bindings::sigaction;
unsafe impl vasi::VirtualAddressSpaceIndependent for sigaction {}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn sigset_size() {
        // The kernel definition should (currently) be 8 bytes.
        // At some point this may get increased, but it shouldn't be the glibc
        // size of ~100 bytes.
        assert_eq!(std::mem::size_of::<bindings::sigset_t>(), 8);
    }
}
