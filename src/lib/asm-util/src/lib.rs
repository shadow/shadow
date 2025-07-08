// https://github.com/rust-lang/rfcs/blob/master/text/2585-unsafe-block-in-unsafe-fn.md
#![deny(unsafe_op_in_unsafe_fn)]

// Force cargo to link against crates that aren't (yet) referenced from Rust
// code (but are referenced from this crate's C code).
// https://github.com/rust-lang/cargo/issues/9391
extern crate logger;

/// cbindgen:ignore
pub mod c_internal {
    #![allow(non_upper_case_globals)]
    #![allow(non_camel_case_types)]
    #![allow(non_snake_case)]
    // https://github.com/rust-lang/rust/issues/66220
    #![allow(improper_ctypes)]
    include!(concat!(env!("OUT_DIR"), "/c_internal.rs"));
}

pub mod cpuid;
pub mod tsc;

/// Check whether the memory starting at `ip` starts with the instruction `insn`.
///
/// Particularly useful in situations where we can be confident that `ip` points
/// to a valid instruction, but can't otherwise guarantee how many bytes are
/// dereferenceable. e.g. for the (perhaps unlikely) situation where `ip` points
/// to a single-byte instruction, the next byte is not safely dereferenceable,
/// and `insn` is a multi-byte instruction; i.e. where naively converting `ip`
/// to a slice of the same size as `insn` would be unsound.
///
/// SAFETY: `ip` must be a dereferenceable pointer, pointing to the beginning
/// of a valid x86_64 instruction, and `insn` must be a valid x86_64 instruction.
unsafe fn ip_matches(ip: *const u8, insn: &[u8]) -> bool {
    // SAFETY:
    // * Caller has guaranteed that `ip` points to some valid instruction.
    // * Caller has guaranteed that `insn` is a valid instruction.
    // * No instruction can be a prefix of another, so `insn` can't be a prefix
    //   of some *other* instruction at `ip`.
    // * [`std::Iterator::all`] is short-circuiting.
    //
    // e.g. consider the case where `ip` points to a 1-byte `ret`
    // instruction, and the next byte of memory isn't accessible. That
    // single byte *cannot* match the first byte of `insn`, so we'll never
    // dereference `ip.offset(1)`, which would be unsound.
    insn.iter()
        .enumerate()
        .all(|(offset, byte)| unsafe { *ip.add(offset) == *byte })
}
