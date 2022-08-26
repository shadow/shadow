// https://github.com/rust-lang/rfcs/blob/master/text/2585-unsafe-block-in-unsafe-fn.md
#![deny(unsafe_op_in_unsafe_fn)]

pub mod emulated_time;
pub mod scmutex;
pub mod signals;
pub mod simulation_time;
