/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

// https://github.com/rust-lang/rfcs/blob/master/text/2585-unsafe-block-in-unsafe-fn.md
#![deny(unsafe_op_in_unsafe_fn)]

/// cbindgen:ignore
pub mod cshadow;

// modules with macros must be included before other modules
#[macro_use]
pub mod utility;

pub mod core;
pub mod host;
pub mod network;

// Force cargo to link against crates that aren't (yet) referenced from Rust
// code (but are referenced from this crate's C code).
// https://github.com/rust-lang/cargo/issues/9391
extern crate shadow_shmem;
extern crate shadow_tsc;
