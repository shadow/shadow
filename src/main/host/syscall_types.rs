use crate::cshadow as c;
use std::convert::From;

#[derive(Copy, Clone, Debug)]
pub struct PluginPtr {
    ptr: c::PluginPtr,
}

impl PluginPtr {
    pub fn is_null(&self) -> bool {
        self.ptr.val == 0
    }
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

pub type SysCallArgs = c::SysCallArgs;
pub type SysCallReg = c::SysCallReg;

impl SysCallArgs {
    pub fn get(&self, i: usize) -> SysCallReg {
        self.args[i]
    }
    pub fn number(&self) -> i64 {
        self.number
    }
}

impl PartialEq for SysCallReg {
    fn eq(&self, other: &Self) -> bool {
        unsafe { self.as_u64 == other.as_u64 }
    }
}

impl Eq for SysCallReg {}

impl From<u64> for SysCallReg {
    fn from(v: u64) -> Self {
        Self { as_u64: v }
    }
}

impl From<SysCallReg> for u64 {
    fn from(v: SysCallReg) -> u64 {
        unsafe { v.as_u64 }
    }
}

impl From<usize> for SysCallReg {
    fn from(v: usize) -> Self {
        Self { as_u64: v as u64 }
    }
}

impl From<SysCallReg> for usize {
    fn from(v: SysCallReg) -> usize {
        unsafe { v.as_u64 as usize }
    }
}

impl From<i64> for SysCallReg {
    fn from(v: i64) -> Self {
        Self { as_i64: v }
    }
}

impl From<SysCallReg> for i64 {
    fn from(v: SysCallReg) -> i64 {
        unsafe { v.as_i64 }
    }
}

impl From<i32> for SysCallReg {
    fn from(v: i32) -> Self {
        Self { as_i64: v as i64 }
    }
}

impl From<SysCallReg> for i32 {
    fn from(v: SysCallReg) -> i32 {
        (unsafe { v.as_i64 }) as i32
    }
}

impl From<PluginPtr> for SysCallReg {
    fn from(v: PluginPtr) -> Self {
        Self { as_ptr: v.into() }
    }
}

impl From<SysCallReg> for PluginPtr {
    fn from(v: SysCallReg) -> PluginPtr {
        PluginPtr {
            ptr: unsafe { v.as_ptr },
        }
    }
}

// Useful for syscalls whose strongly-typed wrappers return some Result<(), ErrType>
impl Into<SysCallReg> for () {
    fn into(self) -> SysCallReg {
        SysCallReg { as_i64: 0 }
    }
}

impl std::fmt::Debug for c::SysCallReg {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("SysCallReg")
            .field("as_i64", unsafe { &self.as_i64 })
            .field("as_u64", unsafe { &self.as_u64 })
            .field("as_ptr", unsafe { &self.as_ptr })
            .finish()
    }
}
