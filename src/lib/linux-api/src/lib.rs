//! Type definitions and utilities for interacting with the Linux API.
//! Does not depend on the `std` crate (i.e. is `no_std`) nor libc.
//!
//! We currently re-export C bindings. This allows normal C code, using
//! libc headers, to work with these kernel types. Normally this can't be done
//! because the libc and kernel headers contain incompatible definitions.
//!
//! # Type names
//!
//! We provide 3 styles of types:
//!
//! * snake-cased-types prefixed with `linux_`. These are ABI-compatible with
//!   the corresponding kernel types. These types are re-exported in this crate's
//!   generated C header, and are the types used in exported C function APIs.
//!
//! * snake-cased-types *without* the `linux_` prefix. These are *also* ABI-compatible
//!   with the corresponding kernel types, and are intended for use in Rust
//!   code.  They never require or enforce invariants beyond that bytes are
//!   initialized, so are safe to transmute from initialized bytes, or from the
//!   corresponding `linux_` type if the required bytes are known to be
//!   initialized. See [`crate::signal::sigaction`] for an example of a type
//!   with such invariants.
//!
//!   We do *not* expose these types in C interfaces, since they have the same
//!   name as types in the original Linux headers, and often with the same name
//!   in C library headers. Exported C APIs should use the `linux_` types in their
//!   signatures, and convert internally to the non-prefixed types. In cases
//!   where the 2 types are not simply aliased, they usually implement
//!   `bytemuck::TransparentWrapper` or similar APIs that allow converting
//!   values *and references* in both directions.
//!
//! * camel-cased types are *not* necessarily bitwise-compatible with kernel types
//!   with similar names. These primarily include `enum`s and `bitflags` types.
//!   Kernel constants such as `O_WRONLY` are typically defined as instants of
//!   such types, and are convertible to and from the original integer types.

#![cfg_attr(not(any(test, feature = "std")), no_std)]
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

pub mod capability;
pub mod close_range;
pub mod epoll;
pub mod errno;
pub mod exit;
pub mod fcntl;
pub mod futex;
pub mod inet;
pub mod ioctls;
pub mod ldt;
pub mod limits;
pub mod mman;
pub mod netlink;
pub mod poll;
pub mod posix_types;
pub mod prctl;
pub mod resource;
pub mod rseq;
pub mod rtnetlink;
pub mod sched;
pub mod signal;
pub mod socket;
pub mod stat;
pub mod syscall;
pub mod sysinfo;
pub mod time;
pub mod ucontext;
pub mod utsname;
pub mod wait;

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

    pub const fn usize_from_u32(val: u32) -> usize {
        // Guaranteed not to truncate
        val as usize
    }

    pub const fn i32_from_u32(val: u32) -> i32 {
        // Maybe not strictly necessary for safety, but probably
        // a mistake of some kind if this fails.
        assert!(val <= (i32::MAX as u32));

        val as i32
    }

    pub const fn i32_from_u32_allowing_wraparound(val: u32) -> i32 {
        val as i32
    }

    pub const fn u16_from_u32(val: u32) -> u16 {
        assert!(val <= (u16::MAX as u32));

        val as u16
    }
}
