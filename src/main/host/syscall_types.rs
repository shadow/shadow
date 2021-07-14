use crate::cshadow as c;
use crate::host::syscall_condition::SysCallCondition;
use log::Level::Debug;
use log::*;
use nix::errno::Errno;
use std::convert::From;
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
    count: usize,
    _phantom: std::marker::PhantomData<T>,
}

impl<T> std::fmt::Debug for TypedPluginPtr<T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("TypedPluginPtr")
            .field("base", &self.base)
            .field("count", &self.count)
            .field("size_of::<T>", &size_of::<T>())
            .finish()
    }
}

impl<T> TypedPluginPtr<T> {
    /// Creates a typed pointer. Note though that the pointer *isn't* guaranteed
    /// to be aligned for `T`.
    pub fn new(ptr: PluginPtr, count: usize) -> Self {
        if log_enabled!(Debug) && usize::from(ptr) % std::mem::align_of::<T>() != 0 {
            // Linux allows unaligned pointers from user-space, being careful to
            // avoid unaligned accesses that aren's supported by the CPU.
            // https://www.kernel.org/doc/html/latest/core-api/unaligned-memory-access.html.
            //
            // We do the same (e.g. by avoiding direct dereference of such
            // pointers even if mmap'd into shadow), but some bugs may slip
            // through (e.g. by asking for a u8 pointer from the mapping code,
            // but then casting it to some other type). Here we leave a debug
            // message here as a sign-post that this could be the root cause of
            // weirdness that happens afterwards.
            debug!(
                "Creating unaligned pointer {:?}. This is legal, but could trigger latent bugs.",
                ptr
            );
        }
        TypedPluginPtr {
            base: ptr,
            count,
            _phantom: PhantomData,
        }
    }

    /// Raw plugin pointer.
    pub fn ptr(&self) -> PluginPtr {
        self.base
    }

    /// Number of items pointed to.
    pub fn len(&self) -> usize {
        self.count
    }

    /// Cast to type `U`. Fails if the total size isn't a multiple of `sizeof<U>`.
    pub fn cast<U>(&self) -> Option<TypedPluginPtr<U>> {
        let count_bytes = self.count * size_of::<T>();
        if count_bytes % size_of::<U>() != 0 {
            return None;
        }
        Some(TypedPluginPtr::new(self.base, count_bytes / size_of::<U>()))
    }

    /// Cast to u8. Infallible since size_of<u8> is 1.
    pub fn cast_u8(&self) -> TypedPluginPtr<u8> {
        self.cast::<u8>().unwrap()
    }

    /// Return a slice of this pointer.
    pub fn slice<R: std::ops::RangeBounds<usize>>(&self, range: R) -> TypedPluginPtr<T> {
        use std::ops::Bound;
        let excluded_end = match range.end_bound() {
            Bound::Included(e) => e + 1,
            Bound::Excluded(e) => *e,
            Bound::Unbounded => self.count,
        };
        let included_start = match range.start_bound() {
            Bound::Included(s) => *s,
            Bound::Excluded(s) => s + 1,
            Bound::Unbounded => 0,
        };
        assert!(included_start <= excluded_end);
        assert!(excluded_end <= self.count);
        // `<=` rather than `<`, to allow empty slice at end of ptr.
        // e.g. `assert_eq!(&[1,2,3][3..3], &[])` passes.
        assert!(included_start <= self.count);
        TypedPluginPtr {
            base: PluginPtr {
                ptr: c::PluginPtr {
                    val: (self.base.ptr.val as usize + included_start * size_of::<T>()) as u64,
                },
            },
            count: excluded_end - included_start,
            _phantom: PhantomData,
        }
    }
}

// Calling all of these errors is stretching the semantics of 'error' a bit,
// but it makes for fluent programming in syscall handlers using the `?` operator.
#[derive(Debug, PartialEq, Eq)]
pub enum SyscallError {
    Errno(nix::errno::Errno),
    Cond(SysCallCondition),
    Native,
}

pub type SyscallResult = Result<crate::host::syscall_types::SysCallReg, SyscallError>;

impl From<c::SysCallReturn> for SyscallResult {
    fn from(r: c::SysCallReturn) -> Self {
        match r.state {
            c::SysCallReturnState_SYSCALL_DONE => {
                match crate::utility::syscall::raw_return_value_to_result(unsafe {
                    r.retval.as_i64
                }) {
                    Ok(r) => Ok(r),
                    Err(e) => Err(e.into()),
                }
            }
            // SAFETY: XXX: We're assuming this points to a valid SysCallCondition.
            c::SysCallReturnState_SYSCALL_BLOCK => Err(SyscallError::Cond(unsafe {
                SysCallCondition::consume_from_c(r.cond)
            })),
            c::SysCallReturnState_SYSCALL_NATIVE => Err(SyscallError::Native),
            _ => panic!("Unexpected c::SysCallReturn state {}", r.state),
        }
    }
}

impl From<SyscallResult> for c::SysCallReturn {
    fn from(syscall_return: SyscallResult) -> Self {
        match syscall_return {
            Ok(r) => Self {
                state: c::SysCallReturnState_SYSCALL_DONE,
                retval: r.into(),
                cond: std::ptr::null_mut(),
            },
            Err(SyscallError::Errno(e)) => Self {
                state: c::SysCallReturnState_SYSCALL_DONE,
                retval: c::SysCallReg {
                    as_i64: -(e as i64),
                },
                cond: std::ptr::null_mut(),
            },
            Err(SyscallError::Cond(c)) => Self {
                state: c::SysCallReturnState_SYSCALL_BLOCK,
                retval: c::SysCallReg { as_i64: 0 },
                cond: c.into_inner(),
            },
            Err(SyscallError::Native) => Self {
                state: c::SysCallReturnState_SYSCALL_NATIVE,
                retval: c::SysCallReg { as_i64: 0 },
                cond: std::ptr::null_mut(),
            },
        }
    }
}

impl From<nix::errno::Errno> for SyscallError {
    fn from(e: nix::errno::Errno) -> Self {
        SyscallError::Errno(e)
    }
}

impl From<std::io::Error> for SyscallError {
    fn from(e: std::io::Error) -> Self {
        match std::io::Error::raw_os_error(&e) {
            Some(e) => SyscallError::Errno(nix::errno::from_i32(e)),
            None => {
                let default = Errno::ENOTSUP;
                warn!("Mapping error {} to {}", e, default);
                SyscallError::Errno(default)
            }
        }
    }
}
