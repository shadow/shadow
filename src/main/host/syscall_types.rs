use crate::cshadow as c;
use crate::host::syscall_condition::SysCallCondition;
use crate::utility::NoTypeInference;

use log::Level::Debug;
use log::*;
use nix::errno::Errno;
use std::convert::From;
use std::marker::PhantomData;
use std::mem::size_of;

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

impl PartialEq for SysCallReg {
    fn eq(&self, other: &Self) -> bool {
        unsafe { self.as_u64 == other.as_u64 }
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
        Self { as_ptr: v.into() }
    }
}

impl From<SysCallReg> for PluginPtr {
    fn from(v: SysCallReg) -> PluginPtr {
        PluginPtr::from(unsafe { v.as_ptr })
    }
}

// Useful for syscalls whose strongly-typed wrappers return some Result<(), ErrType>
impl From<()> for SysCallReg {
    fn from(_: ()) -> SysCallReg {
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
    pub fn new<U>(ptr: PluginPtr, count: usize) -> Self
    where
        U: NoTypeInference<This = T>,
    {
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

    pub fn is_empty(&self) -> bool {
        self.count == 0
    }

    pub fn is_null(&self) -> bool {
        self.base.is_null()
    }

    /// Cast to type `U`. Fails if the total size isn't a multiple of `sizeof<U>`.
    pub fn cast<U>(&self) -> Option<TypedPluginPtr<U>> {
        let count_bytes = self.count * size_of::<T>();
        if count_bytes % size_of::<U>() != 0 {
            return None;
        }
        Some(TypedPluginPtr::new::<U>(
            self.base,
            count_bytes / size_of::<U>(),
        ))
    }

    /// Cast to u8. Infallible since `size_of<u8>` is 1.
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
                val: (self.base.val + included_start * size_of::<T>()),
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
    Failed(Failed),
    Blocked(Blocked),
    Native,
}

#[derive(Debug, PartialEq, Eq)]
pub struct Blocked {
    pub condition: SysCallCondition,
    pub restartable: bool,
}

#[derive(Debug, PartialEq, Eq)]
pub struct Failed {
    pub errno: nix::errno::Errno,
    pub restartable: bool,
}

pub type SyscallResult = Result<crate::host::syscall_types::SysCallReg, SyscallError>;

impl From<c::SysCallReturn> for SyscallResult {
    fn from(r: c::SysCallReturn) -> Self {
        match r.state {
            c::SysCallReturnState_SYSCALL_DONE => {
                match crate::utility::syscall::raw_return_value_to_result(unsafe {
                    r.u.done.retval.as_i64
                }) {
                    Ok(r) => Ok(r),
                    Err(e) => Err(SyscallError::Failed(Failed {
                        errno: e,
                        restartable: unsafe { r.u.done.restartable },
                    })),
                }
            }
            // SAFETY: XXX: We're assuming this points to a valid SysCallCondition.
            c::SysCallReturnState_SYSCALL_BLOCK => Err(SyscallError::Blocked(Blocked {
                condition: unsafe { SysCallCondition::consume_from_c(r.u.blocked.cond) },
                restartable: unsafe { r.u.blocked.restartable },
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
                u: c::SysCallReturnBody {
                    done: c::SysCallReturnDone {
                        retval: r,
                        // N/A for non-error result (and non-EINTR result in particular)
                        restartable: false,
                    },
                },
            },
            Err(SyscallError::Failed(failed)) => Self {
                state: c::SysCallReturnState_SYSCALL_DONE,
                u: c::SysCallReturnBody {
                    done: c::SysCallReturnDone {
                        retval: (-(failed.errno as i64)).into(),
                        restartable: failed.restartable,
                    },
                },
            },
            Err(SyscallError::Blocked(blocked)) => Self {
                state: c::SysCallReturnState_SYSCALL_BLOCK,
                u: c::SysCallReturnBody {
                    blocked: c::SysCallReturnBlocked {
                        cond: blocked.condition.into_inner(),
                        restartable: blocked.restartable,
                    },
                },
            },
            Err(SyscallError::Native) => Self {
                state: c::SysCallReturnState_SYSCALL_NATIVE,
                // No field for native. This is the recommended way to default-initialize a union.
                // https://rust-lang.github.io/rust-bindgen/using-unions.html#using-the-union-builtin
                u: unsafe { std::mem::zeroed::<c::SysCallReturnBody>() },
            },
        }
    }
}

impl From<nix::errno::Errno> for SyscallError {
    fn from(e: nix::errno::Errno) -> Self {
        SyscallError::Failed(Failed {
            errno: e,
            restartable: false,
        })
    }
}

impl From<std::io::Error> for SyscallError {
    fn from(e: std::io::Error) -> Self {
        match std::io::Error::raw_os_error(&e) {
            Some(e) => SyscallError::Failed(Failed {
                errno: nix::errno::from_i32(e),
                restartable: false,
            }),
            None => {
                let default = Errno::ENOTSUP;
                warn!("Mapping error {} to {}", e, default);
                SyscallError::from(default)
            }
        }
    }
}

#[derive(Copy, Clone, Debug)]
#[repr(C)]
pub struct SysCallReturnDone {
    pub retval: SysCallReg,
    // Only meaningful when `retval` is -EINTR.
    //
    // Whether the interrupted syscall is restartable.
    pub restartable: bool,
}

#[derive(Copy, Clone, Debug)]
#[repr(C)]
pub struct SysCallReturnBlocked {
    pub cond: *mut c::SysCallCondition,
    // True if the syscall is restartable in the case that it was interrupted by
    // a signal. e.g. if the syscall was a `read` operation on a socket without
    // a configured timeout. See socket(7).
    pub restartable: bool,
}

#[derive(Copy, Clone)]
#[repr(C)]
pub union SysCallReturnBody {
    pub done: SysCallReturnDone,
    pub blocked: SysCallReturnBlocked,
}