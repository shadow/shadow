use shadow_pod::Pod;

#[allow(non_camel_case_types)]
pub type rusage = crate::bindings::linux_rusage;
unsafe impl Pod for rusage {}
