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
