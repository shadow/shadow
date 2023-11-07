use core::num::NonZeroI32;

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

pub use bindings::linux___kernel_size_t;
#[allow(non_camel_case_types)]
pub type kernel_size_t = linux___kernel_size_t;

pub use bindings::linux___kernel_fd_set;
#[allow(non_camel_case_types)]
pub type kernel_fd_set = linux___kernel_fd_set;

/// Type-safe wrapper around [`kernel_pid_t`]. Value is strictly positive.
/// Interface inspired by `rustix::process::Pid`.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash)]
pub struct Pid(NonZeroI32);

impl Pid {
    pub fn from_raw(pid: kernel_pid_t) -> Option<Self> {
        if pid < 0 {
            None
        } else {
            Some(Self(NonZeroI32::new(pid).unwrap()))
        }
    }

    /// Returns a stricly positive integer for `Some`, or 0 for `None`.
    pub fn as_raw(this: Option<Self>) -> kernel_pid_t {
        this.map(|x| kernel_pid_t::from(x.0)).unwrap_or(0)
    }

    pub fn as_raw_nonzero(self) -> NonZeroI32 {
        self.0
    }
}

#[cfg(feature = "rustix")]
impl From<rustix::process::Pid> for Pid {
    fn from(value: rustix::process::Pid) -> Self {
        Pid(value.as_raw_nonzero())
    }
}

#[cfg(feature = "rustix")]
impl From<Pid> for rustix::process::Pid {
    fn from(value: Pid) -> Self {
        rustix::process::Pid::from_raw(value.as_raw_nonzero().into()).unwrap()
    }
}
