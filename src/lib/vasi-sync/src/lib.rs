//! VirtualAddressSpaceIndependent synchronization primitives.
//!
//! This is a collection of synchronization primitives intended for use in
//! shared memory, which may be mapped into different processes, potentially at
//! different virtual addresses - see the [`vasi::VirtualAddressSpaceIndependent`]
//! trait for details.
//!
//! In contrast, the synchronization primitives in the `std` crate may or may
//! not work in this scenario; they are intended primarily to synchronize
//! threads within a single virtual address space. e.g. they may use `Box`
//! internally.
//!
//! This module contains tests that are designed to work with [loom].  See
//! [loom] documentation for full details, but a basic way to run these under
//! loom, from the shadow source directory is:
//!
//! ```shell
//! LOOM_MAX_PREEMPTIONS=3 \
//! RUSTFLAGS="--cfg loom" \
//! cargo test \
//! --manifest-path=src/Cargo.toml \
//! -p vasi-sync \
//! --target-dir=loomtarget \
//! -- --nocapture
//! ```
//!
//! Setting `--target-dir` avoids thrashing the build cache back and forth
//! between a loom build or not.
//!
//! In case of failure, see the loom documentation for guidance on debugging.
//! In particular LOOM_LOG=trace and/or LOOM_LOCATIONS=1 are a good place to start.
//!
//! [loom]: <https://docs.rs/loom/latest/loom/>

// https://github.com/rust-lang/rfcs/blob/master/text/2585-unsafe-block-in-unsafe-fn.md
#![deny(unsafe_op_in_unsafe_fn)]
// no_std except when testing.
// https://github.com/shadow/shadow/issues/2919
#![cfg_attr(all(not(test), not(loom)), no_std)]

pub mod atomic_tls_map;
pub mod lazy_lock;
pub mod scchannel;
pub mod scmutex;

/// This is public primarily for the integration tests in `tests/*`, which is the
/// recommended way of writing loom tests.
///
/// Not actually intended for usage by other crates.
pub mod sync;
