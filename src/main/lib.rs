/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

// https://github.com/rust-lang/rfcs/blob/master/text/2585-unsafe-block-in-unsafe-fn.md
#![deny(unsafe_op_in_unsafe_fn)]
#![allow(clippy::not_unsafe_ptr_arg_deref)]
#![allow(clippy::enum_variant_names)]
#![allow(clippy::too_many_arguments)]

/// cbindgen:ignore
pub mod cshadow {
    #![allow(non_upper_case_globals)]
    #![allow(non_camel_case_types)]
    #![allow(non_snake_case)]
    // https://github.com/rust-lang/rust/issues/66220
    #![allow(improper_ctypes)]
    #![allow(unsafe_op_in_unsafe_fn)]
    #![allow(clippy::all)]
    include!(concat!(env!("OUT_DIR"), "/cshadow.rs"));
}

// modules with macros must be included before other modules
#[macro_use]
pub mod utility;

pub mod core;
pub mod host;
pub mod network;
pub mod shadow;

// Force cargo to link against crates that aren't (yet) referenced from Rust
// code (but are referenced from this crate's C code).
// https://github.com/rust-lang/cargo/issues/9391
extern crate shadow_shmem;
extern crate shadow_tsc;

// shadow re-exports this definition from /usr/include/linux/tcp.h
// TODO: Provide this via the linux-api crate instead.
unsafe impl shadow_pod::Pod for crate::cshadow::tcp_info {}

// check that the size and alignment of `CompatUntypedForeignPtr` and `ForeignPtr<()>` are the same`
static_assertions::assert_eq_size!(
    cshadow::CompatUntypedForeignPtr,
    shadow_shim_helper_rs::syscall_types::UntypedForeignPtr,
);
static_assertions::assert_eq_align!(
    cshadow::CompatUntypedForeignPtr,
    shadow_shim_helper_rs::syscall_types::UntypedForeignPtr,
);
