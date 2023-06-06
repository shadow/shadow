//! Utilities related to the bytemuck crate
//!
//! This crate provides a temporary reimplementation of the `bytemuck::AnyBitPattern`
//! crate (formerly `shadow_rs::util::pod::Pod`), which will eventually become an alias
//! for `bytemuck::AnyBitPattern` or removed altogether.
//!
//! It also provides some extra helper functions related to `AnyBitPattern` and
//! potentially other bytemuck types.

mod anybitpattern;

pub use anybitpattern::{
    maybeuninit_bytes_of, maybeuninit_bytes_of_mut, maybeuninit_bytes_of_slice,
    maybeuninit_bytes_of_slice_mut, zeroed, AnyBitPattern,
};
