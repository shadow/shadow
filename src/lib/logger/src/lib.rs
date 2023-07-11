//! logger API suitable for C
//!
//! While we eventually want to move the implementation to Rust,
//! Rust code should generally use Rust's `log` crate instead.
//!
//! Since we use this in the shim, we eventually want this to not depend on
//! `std` nor `libc`. (The C part of the implementation currently uses libc).
//! <https://github.com/shadow/shadow/issues/2919>

// TODO:
// #![no_std]

// https://github.com/rust-lang/rfcs/blob/master/text/2585-unsafe-block-in-unsafe-fn.md
#![deny(unsafe_op_in_unsafe_fn)]

mod bindings {
    #![allow(non_upper_case_globals)]
    #![allow(non_camel_case_types)]
    #![allow(non_snake_case)]
    // https://github.com/rust-lang/rust/issues/66220
    #![allow(improper_ctypes)]
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

// For legacy compatibility.
// TODO: wrap or replace with an idiomatic rust api.
pub use bindings::*;

// Force linking; currently only used from C code.
extern crate linux_api;
