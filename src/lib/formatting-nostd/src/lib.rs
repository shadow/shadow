#![cfg_attr(not(test), no_std)]
// https://github.com/rust-lang/rfcs/blob/master/text/2585-unsafe-block-in-unsafe-fn.md
#![deny(unsafe_op_in_unsafe_fn)]

mod borrowed_fd_writer;
pub use borrowed_fd_writer::BorrowedFdWriter;

mod format_buffer;
pub use format_buffer::FormatBuffer;

pub mod utf8;
