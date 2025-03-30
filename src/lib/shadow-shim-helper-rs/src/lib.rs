// https://github.com/rust-lang/rfcs/blob/master/text/2585-unsafe-block-in-unsafe-fn.md
#![deny(unsafe_op_in_unsafe_fn)]
// we do some static assertions to make sure C bindings are okay.
#![allow(clippy::assertions_on_constants)]

use vasi::VirtualAddressSpaceIndependent;

pub mod emulated_time;
pub mod explicit_drop;
pub mod ipc;
pub mod notnull;
pub mod option;
pub mod rootedcell;
pub mod shadow_syscalls;
pub mod shim_event;
pub mod shim_shmem;
pub mod simulation_time;
pub mod syscall_types;
pub mod util;

#[repr(transparent)]
#[derive(
    Debug, PartialEq, Eq, PartialOrd, Ord, Hash, Copy, Clone, VirtualAddressSpaceIndependent,
)]
pub struct HostId(u32);

impl From<u32> for HostId {
    fn from(i: u32) -> Self {
        HostId(i)
    }
}

impl From<HostId> for u32 {
    fn from(i: HostId) -> Self {
        i.0
    }
}

// Force cargo to link against crates that aren't (yet) referenced from Rust
// code (but are referenced from this crate's C code).
// https://github.com/rust-lang/cargo/issues/9391
extern crate logger;
extern crate shadow_shmem;
