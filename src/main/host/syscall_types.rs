use crate::cshadow as c;
use nix::errno::Errno;
use std::convert::From;
use std::io::{Seek, SeekFrom};
use std::marker::PhantomData;
use std::mem::size_of;

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

/// Wrapper around a PluginPtr that encapsulates its type, size, and current
/// position.
#[derive(Copy, Clone)]
pub struct TypedPluginPtr<T> {
    base: PluginPtr,
    offset: usize,
    count: usize,
    _phantom: std::marker::PhantomData<T>,
}

impl<T> std::fmt::Debug for TypedPluginPtr<T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("TypedPluginPtr")
            .field("base", &self.base)
            .field("offset", &self.offset)
            .field("count", &self.count)
            .field("size_of::<T>", &size_of::<T>())
            .finish()
    }
}

impl<T> TypedPluginPtr<T> {
    pub fn new(ptr: PluginPtr, count: usize) -> Self {
        TypedPluginPtr {
            base: ptr,
            offset: 0,
            count,
            _phantom: PhantomData,
        }
    }

    /// Raw plugin pointer for current position.
    pub fn ptr(&self) -> Result<PluginPtr, nix::errno::Errno> {
        if self.offset >= self.count {
            return Err(Errno::EFAULT);
        }
        Ok(PluginPtr {
            ptr: c::PluginPtr {
                val: self.base.ptr.val + ((self.offset * std::mem::size_of::<T>()) as u64),
            },
        })
    }

    /// Number of items remaining at current position.
    pub fn items_remaining(&self) -> Result<usize, Errno> {
        if self.offset > self.count {
            Err(Errno::EFAULT)
        } else {
            Ok(self.count - self.offset)
        }
    }

    /// Number of bytes remaining at current position.
    pub fn bytes_remaining(&self) -> Result<usize, Errno> {
        Ok(self.items_remaining()? * size_of::<T>())
    }

    /// Analagous to std::io::Seek, but seeks item-wise instead of byte-wise.
    pub fn seek_item(&mut self, pos: SeekFrom) -> std::io::Result<u64> {
        let new_offset = match pos {
            SeekFrom::Current(x) => self.offset as i64 + x,
            SeekFrom::End(x) => self.count as i64 + x,
            SeekFrom::Start(x) => x as i64,
        };
        // Seeking before the beginning is an error (but seeking to or past the
        // end isn't).
        if new_offset < 0 {
            return Err(std::io::Error::from_raw_os_error(Errno::EFAULT as i32));
        }
        self.offset = new_offset as usize;
        Ok(self.offset as u64)
    }
}

impl Seek for TypedPluginPtr<u8> {
    fn seek(&mut self, pos: SeekFrom) -> std::io::Result<u64> {
        self.seek_item(pos)
    }
}
