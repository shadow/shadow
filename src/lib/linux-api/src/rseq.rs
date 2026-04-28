use crate::{bindings, const_conversions};

pub use bindings::linux_rseq;
#[allow(non_camel_case_types)]
pub type rseq = linux_rseq;

bitflags::bitflags! {
    #[allow(non_camel_case_types)]
    #[derive(Copy, Clone, Debug, Default, Eq, PartialEq)]
    pub struct rseq_flags: i32 {
        const RSEQ_FLAG_UNREGISTER = const_conversions::i32_from_u32(bindings::LINUX_rseq_flags_RSEQ_FLAG_UNREGISTER);
        const RSEQ_FLAG_SLICE_EXT_DEFAULT_ON = const_conversions::i32_from_u32(bindings::LINUX_rseq_flags_RSEQ_FLAG_SLICE_EXT_DEFAULT_ON);
    }
}
