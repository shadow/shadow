use crate::cbindings as c;
use std::convert::From;

#[derive(Copy, Clone)]
pub struct PluginPtr {
    ptr: c::PluginPtr,
}

impl From<PluginPtr> for c::PluginPtr {
    fn from(v: PluginPtr) -> c::PluginPtr {
        v.ptr
    }
}

impl From<c::PluginPtr> for PluginPtr {
    fn from(v: c::PluginPtr) -> PluginPtr {
        PluginPtr { ptr: v }
    }
}

impl From<PluginPtr> for usize {
    fn from(v: PluginPtr) -> usize {
        v.ptr.val as usize
    }
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

impl From<PluginPtr> for u64 {
    fn from(v: PluginPtr) -> u64 {
        v.ptr.val
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

impl From<SysCallReg> for u64 {
    fn from(v: SysCallReg) -> u64 {
        unsafe { v.reg.as_u64 }
    }
}

impl From<usize> for SysCallReg {
    fn from(v: usize) -> SysCallReg {
        SysCallReg {
            reg: c::SysCallReg { as_u64: v as u64 },
        }
    }
}

impl From<SysCallReg> for usize {
    fn from(v: SysCallReg) -> usize {
        unsafe { v.reg.as_u64 as usize }
    }
}

impl From<i64> for SysCallReg {
    fn from(v: i64) -> SysCallReg {
        SysCallReg {
            reg: c::SysCallReg { as_i64: v },
        }
    }
}

impl From<SysCallReg> for i64 {
    fn from(v: SysCallReg) -> i64 {
        unsafe { v.reg.as_i64 }
    }
}

impl From<i32> for SysCallReg {
    fn from(v: i32) -> SysCallReg {
        SysCallReg {
            reg: c::SysCallReg { as_i64: v as i64 },
        }
    }
}

impl From<SysCallReg> for i32 {
    fn from(v: SysCallReg) -> i32 {
        unsafe { v.reg.as_i64 as i32 }
    }
}

impl From<PluginPtr> for SysCallReg {
    fn from(v: PluginPtr) -> SysCallReg {
        SysCallReg {
            reg: c::SysCallReg { as_ptr: v.into() },
        }
    }
}

impl From<SysCallReg> for PluginPtr {
    fn from(v: SysCallReg) -> PluginPtr {
        PluginPtr {
            ptr: unsafe { v.reg.as_ptr },
        }
    }
}
