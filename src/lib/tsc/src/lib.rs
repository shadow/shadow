// Force cargo to link against crates that aren't (yet) referenced from Rust
// code (but are referenced from this crate's C code).
#[allow(unused)]
use logger;
