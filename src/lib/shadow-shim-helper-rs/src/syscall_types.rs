/// Represents a pointer to a virtual address in plugin memory.
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
#[repr(C)]
pub struct PluginPtr {
    // Temporarily public to ease the migration of replacing cshadow::PluginPtr.
    pub val: usize,
}

impl PluginPtr {
    pub fn null() -> Self {
        0usize.into()
    }

    pub fn is_null(&self) -> bool {
        self.val == 0
    }
}

impl From<PluginPtr> for usize {
    fn from(v: PluginPtr) -> usize {
        v.val
    }
}

impl From<usize> for PluginPtr {
    fn from(v: usize) -> PluginPtr {
        PluginPtr { val: v }
    }
}

impl From<u64> for PluginPtr {
    fn from(v: u64) -> PluginPtr {
        PluginPtr {
            val: v.try_into().unwrap(),
        }
    }
}

impl From<PluginPtr> for u64 {
    fn from(v: PluginPtr) -> u64 {
        v.val.try_into().unwrap()
    }
}

impl std::fmt::Pointer for PluginPtr {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let ptr = self.val as *const libc::c_void;
        std::fmt::Pointer::fmt(&ptr, f)
    }
}

/// Represents a pointer to a *physical* address in plugin memory.
#[derive(Copy, Clone, Debug)]
#[repr(C)]
pub struct PluginPhysicalPtr {
    // Temporarily public to ease the migration of replacing cshadow::PluginPhysicalPtr.
    pub val: usize,
}

impl From<PluginPhysicalPtr> for usize {
    fn from(v: PluginPhysicalPtr) -> usize {
        v.val
    }
}

impl From<usize> for PluginPhysicalPtr {
    fn from(v: usize) -> PluginPhysicalPtr {
        PluginPhysicalPtr { val: v }
    }
}

impl From<u64> for PluginPhysicalPtr {
    fn from(v: u64) -> PluginPhysicalPtr {
        PluginPhysicalPtr {
            val: v.try_into().unwrap(),
        }
    }
}

impl From<PluginPhysicalPtr> for u64 {
    fn from(v: PluginPhysicalPtr) -> u64 {
        v.val.try_into().unwrap()
    }
}

#[derive(Copy, Clone, Debug)]
#[repr(C)]
pub struct SysCallArgs {
    // SYS_* from sys/syscall.h.
    // (mostly included from
    // /usr/include/x86_64-linux-gnu/bits/syscall.h)
    pub number: libc::c_long,
    pub args: [SysCallReg; 6],
}

impl SysCallArgs {
    pub fn get(&self, i: usize) -> SysCallReg {
        self.args[i]
    }
    pub fn number(&self) -> i64 {
        self.number
    }
}

/// A register used for input/output in a syscall.
#[derive(Copy, Clone, Eq)]
#[repr(C)]
pub union SysCallReg {
    pub as_i64: i64,
    pub as_u64: u64,
    pub as_ptr: PluginPtr,
}
// SysCallReg and all of its fields must be transmutable with a 64 bit integer.
// TODO: Store as a single `u64` and explicitly transmute in the conversion
// operations.  This requires getting rid of the direct field access in our C
// code.
static_assertions::assert_eq_align!(SysCallReg, u64);
static_assertions::assert_eq_size!(SysCallReg, u64);

impl PartialEq for SysCallReg {
    fn eq(&self, other: &Self) -> bool {
        unsafe { self.as_u64 == other.as_u64 }
    }
}

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

impl From<u32> for SysCallReg {
    fn from(v: u32) -> Self {
        Self { as_u64: v as u64 }
    }
}

impl From<SysCallReg> for u32 {
    fn from(v: SysCallReg) -> u32 {
        (unsafe { v.as_u64 }) as u32
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

impl From<isize> for SysCallReg {
    fn from(v: isize) -> Self {
        Self { as_i64: v as i64 }
    }
}

impl From<SysCallReg> for isize {
    fn from(v: SysCallReg) -> isize {
        unsafe { v.as_i64 as isize }
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

impl TryFrom<SysCallReg> for u8 {
    type Error = <u8 as TryFrom<u64>>::Error;

    fn try_from(v: SysCallReg) -> Result<u8, Self::Error> {
        (unsafe { v.as_u64 }).try_into()
    }
}

impl TryFrom<SysCallReg> for u16 {
    type Error = <u16 as TryFrom<u64>>::Error;

    fn try_from(v: SysCallReg) -> Result<u16, Self::Error> {
        (unsafe { v.as_u64 }).try_into()
    }
}

impl TryFrom<SysCallReg> for i8 {
    type Error = <i8 as TryFrom<i64>>::Error;

    fn try_from(v: SysCallReg) -> Result<i8, Self::Error> {
        (unsafe { v.as_i64 }).try_into()
    }
}

impl TryFrom<SysCallReg> for i16 {
    type Error = <i16 as TryFrom<i64>>::Error;

    fn try_from(v: SysCallReg) -> Result<i16, Self::Error> {
        (unsafe { v.as_i64 }).try_into()
    }
}

impl From<PluginPtr> for SysCallReg {
    fn from(v: PluginPtr) -> Self {
        Self { as_ptr: v }
    }
}

impl From<SysCallReg> for PluginPtr {
    fn from(v: SysCallReg) -> PluginPtr {
        unsafe { v.as_ptr }
    }
}

// Useful for syscalls whose strongly-typed wrappers return some Result<(), ErrType>
impl From<()> for SysCallReg {
    fn from(_: ()) -> SysCallReg {
        SysCallReg { as_i64: 0 }
    }
}

impl std::fmt::Debug for SysCallReg {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("SysCallReg")
            .field("as_i64", unsafe { &self.as_i64 })
            .field("as_u64", unsafe { &self.as_u64 })
            .field("as_ptr", unsafe { &self.as_ptr })
            .finish()
    }
}
