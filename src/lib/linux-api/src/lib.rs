//! Type definitions and utilities for interacting with the Linux API.
//! Does not depend on the `std` crate (i.e. is `no_std`) nor libc.
//!
//! We currently re-export C bindings. This allows normal C code, using
//! libc headers, to work with these kernel types. Normally this can't be done
//! because the libc and kernel headers contain incompatible definitions.
//!
//! To support this use-case, we prefix most names with `Linux` or `linux_`
//! as appropriate, so that the re-exported types don't conflict with the libc
//! types that would otherwise have the same names.
//!
//! We also can't directly wrap the bindgen'd type definitions with our own, or
//! else cbindgen will generate definitions that refer back to the original
//! kernel C types, which again causes conflicts. However we can use the bindings
//! to internally cast or transmute pointers back to the bound types, and/or
//! validate that our re-definitions match the bindings.

#![cfg_attr(not(test), no_std)]
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

pub mod errno;
pub mod sched;
pub mod signal;
pub mod time;
