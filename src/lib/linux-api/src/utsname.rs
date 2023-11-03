use crate::bindings;

pub use bindings::linux_new_utsname;
#[allow(non_camel_case_types)]
pub type new_utsname = linux_new_utsname;
unsafe impl shadow_pod::Pod for linux_new_utsname {}
