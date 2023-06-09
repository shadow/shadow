use crate::bindings;

pub use bindings::linux_rseq;
#[allow(non_camel_case_types)]
pub type rseq = linux_rseq;
unsafe impl shadow_pod::Pod for rseq {}
