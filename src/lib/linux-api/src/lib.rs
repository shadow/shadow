//! Type definitions and utilities for interacting with the Linux API.
//! Does not depend on the `std` crate (i.e. is `no_std`) nor libc.

#![no_std]
// https://github.com/rust-lang/rfcs/blob/master/text/2585-unsafe-block-in-unsafe-fn.md
#![deny(unsafe_op_in_unsafe_fn)]
#![allow(clippy::not_unsafe_ptr_arg_deref)]
#![allow(clippy::enum_variant_names)]
#![allow(clippy::too_many_arguments)]

/// cbindgen:ignore
#[allow(non_upper_case_globals)]
#[allow(non_camel_case_types)]
#[allow(non_snake_case)]
// https://github.com/rust-lang/rust/issues/66220
#[allow(improper_ctypes)]
#[allow(unsafe_op_in_unsafe_fn)]
#[allow(clippy::all)]
#[allow(unused)]
mod bindings;

pub mod signal;
