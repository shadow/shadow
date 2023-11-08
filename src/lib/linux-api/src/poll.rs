use crate::bindings;

pub use bindings::linux_pollfd;
#[allow(non_camel_case_types)]
pub type pollfd = linux_pollfd;
unsafe impl shadow_pod::Pod for pollfd {}
