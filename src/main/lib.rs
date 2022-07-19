/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

// https://github.com/rust-lang/rfcs/blob/master/text/2585-unsafe-block-in-unsafe-fn.md
#![deny(unsafe_op_in_unsafe_fn)]

pub mod cshadow {
    // Inline the bindgen-generated Rust bindings, suppressing warnings.
    #![allow(unused)]
    #![allow(non_upper_case_globals)]
    #![allow(non_camel_case_types)]
    #![allow(non_snake_case)]
    // https://github.com/rust-lang/rust/issues/66220
    #![allow(improper_ctypes)]
    include!("bindings/rust/wrapper.rs");
}

// modules with macros must be included before other modules
#[macro_use]
pub mod utility;

pub mod core;
pub mod host;
pub mod network;
