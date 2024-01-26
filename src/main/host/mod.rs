//! Process management and emulation of Linux behaviour.
//!
//! This module contains the code responsible for starting the managed processes in a way that
//! allows Shadow to intercept their syscalls. It also contains the emulation of Linux hosts,
//! threads, processes, syscalls, files, network interfaces, etc.

pub mod context;
pub mod cpu;
pub mod descriptor;
pub mod futex_table;
#[allow(clippy::module_inception)]
pub mod host;
pub mod managed_thread;
pub mod memory_manager;
pub mod network;
pub mod process;
pub mod status_listener;
pub mod syscall;
pub mod thread;
pub mod timer;
