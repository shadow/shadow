#![cfg_attr(not(test), no_std)]

mod borrowed_fd_writer;
pub use borrowed_fd_writer::BorrowedFdWriter;

mod format_buffer;
pub use format_buffer::FormatBuffer;

pub mod utf8;
