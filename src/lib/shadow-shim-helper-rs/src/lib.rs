// https://github.com/rust-lang/rfcs/blob/master/text/2585-unsafe-block-in-unsafe-fn.md
#![deny(unsafe_op_in_unsafe_fn)]

use vasi::VirtualAddressSpaceIndependent;

pub mod emulated_time;
pub mod rootedcell;
pub mod shim_shmem;
pub mod signals;
pub mod simulation_time;

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
