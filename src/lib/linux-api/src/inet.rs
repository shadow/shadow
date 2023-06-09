use crate::bindings;

pub use bindings::linux_sockaddr_in;
#[allow(non_camel_case_types)]
pub type sockaddr_in = linux_sockaddr_in;
unsafe impl shadow_pod::Pod for sockaddr_in {}
