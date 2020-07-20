use crate::cbindings as c;
use std::convert::From;

impl From<PluginPtr> for c::PluginPtr {
    fn from(v: PluginPtr) -> c::PluginPtr {
        v.ptr
    }
}

#[derive(Copy, Clone)]
pub struct PluginPtr {
    ptr: c::PluginPtr,
}

impl From<usize> for PluginPtr {
    fn from(v: usize) -> PluginPtr {
        PluginPtr {
            ptr: c::PluginPtr { val: v as u64 },
        }
    }
}

impl From<u64> for PluginPtr {
    fn from(v: u64) -> PluginPtr {
        PluginPtr {
            ptr: c::PluginPtr { val: v },
        }
    }
}

impl From<c::PluginPtr> for PluginPtr {
    fn from(v: c::PluginPtr) -> PluginPtr {
        PluginPtr { ptr: v }
    }
}

impl From<SysCallReg> for PluginPtr {
    fn from(v: SysCallReg) -> PluginPtr {
        PluginPtr {
            ptr: unsafe { v.reg.as_ptr },
        }
    }
}

#[derive(Copy, Clone)]
pub struct SysCallReg {
    reg: c::SysCallReg,
}

impl From<SysCallReg> for c::SysCallReg {
    fn from(v: SysCallReg) -> c::SysCallReg {
        v.reg
    }
}

impl From<u64> for SysCallReg {
    fn from(v: u64) -> SysCallReg {
        SysCallReg {
            reg: c::SysCallReg { as_u64: v },
        }
    }
}

impl From<usize> for SysCallReg {
    fn from(v: usize) -> SysCallReg {
        SysCallReg {
            reg: c::SysCallReg { as_u64: v as u64 },
        }
    }
}

impl From<i64> for SysCallReg {
    fn from(v: i64) -> SysCallReg {
        SysCallReg {
            reg: c::SysCallReg { as_i64: v },
        }
    }
}

impl From<i32> for SysCallReg {
    fn from(v: i32) -> SysCallReg {
        SysCallReg {
            reg: c::SysCallReg { as_i64: v as i64 },
        }
    }
}

impl From<PluginPtr> for SysCallReg {
    fn from(v: PluginPtr) -> SysCallReg {
        SysCallReg {
            reg: c::SysCallReg { as_ptr: v.into() },
        }
    }
}
