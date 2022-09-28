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

// cargo spuriously drops the dependency on this crate if we don't reference it
// from our Rust code (it's otherwise currently only referenced from our C
// code).
#[allow(unused)]
use shadow_shmem;
