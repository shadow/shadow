use crate::bindings;

pub use bindings::linux___kernel_pid_t;
#[allow(non_camel_case_types)]
pub type kernel_pid_t = linux___kernel_pid_t;

pub use bindings::linux___kernel_mode_t;
#[allow(non_camel_case_types)]
pub type kernel_mode_t = bindings::linux___kernel_mode_t;

pub use bindings::linux___kernel_ulong_t;
#[allow(non_camel_case_types)]
pub type kernel_ulong_t = bindings::linux___kernel_ulong_t;

pub use bindings::linux___kernel_off_t;
#[allow(non_camel_case_types)]
pub type kernel_off_t = linux___kernel_off_t;
