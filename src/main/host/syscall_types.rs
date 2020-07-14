use crate::cbindings;

pub type PluginPtr = cbindings::PluginPtr;

/// Represents a CPU register used in a sycall argument or return value.
#[derive(Debug, Copy, Clone)]
pub enum SysCallReg {
    I64(i64),
    U64(u64),
    Ptr(PluginPtr),
}
