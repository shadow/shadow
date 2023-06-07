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

#![cfg_attr(not(test), no_std)]
// https://github.com/rust-lang/rfcs/blob/master/text/2585-unsafe-block-in-unsafe-fn.md
#![deny(unsafe_op_in_unsafe_fn)]
#![allow(clippy::not_unsafe_ptr_arg_deref)]
#![allow(clippy::enum_variant_names)]
#![allow(clippy::too_many_arguments)]

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

// Internally we often end up needing to convert from types that bindgen inferred, in const
// contexts.
//
// We could use `as`, but it'd be easy to accidentally truncate, especially if
// the constant we're converting isn't the type we thought it was.
//
// Because these are in const contexts, we can't use `try_from`.
mod const_conversions {
    pub const fn u64_from_u32(val: u32) -> u64 {
        // Guaranteed not to truncate
        val as u64
    }

    pub const fn i32_from_u32(val: u32) -> i32 {
        // Maybe not strictly necessary for safety, but probably
        // a mistake of some kind if this fails.
        assert!(val <= (i32::MAX as u32));

        val as i32
    }
}
