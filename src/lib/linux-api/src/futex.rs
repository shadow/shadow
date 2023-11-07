use crate::bindings;

pub use bindings::linux_robust_list_head;
#[allow(non_camel_case_types)]
pub type robust_list_head = linux_robust_list_head;
unsafe impl shadow_pod::Pod for robust_list_head {}
