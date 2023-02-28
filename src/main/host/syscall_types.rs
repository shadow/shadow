use crate::cshadow as c;
use crate::host::syscall_condition::SysCallCondition;
use crate::utility::NoTypeInference;

use log::Level::Debug;
use log::*;
use nix::errno::Errno;
use std::convert::From;
use std::marker::PhantomData;
use std::mem::size_of;

// XXX temp
pub use shadow_shim_helper_rs::syscall_types::{
    PluginPhysicalPtr, PluginPtr, SysCallArgs, SysCallReg,
};

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
            base: PluginPtr::from(usize::from(self.base) + included_start * size_of::<T>()),
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
                    i64::from(r.u.done.retval)
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
                u: SysCallReturnBody {
                    done: SysCallReturnDone {
                        retval: r,
                        // N/A for non-error result (and non-EINTR result in particular)
                        restartable: false,
                    },
                },
            },
            Err(SyscallError::Failed(failed)) => Self {
                state: c::SysCallReturnState_SYSCALL_DONE,
                u: SysCallReturnBody {
                    done: SysCallReturnDone {
                        retval: (-(failed.errno as i64)).into(),
                        restartable: failed.restartable,
                    },
                },
            },
            Err(SyscallError::Blocked(blocked)) => Self {
                state: c::SysCallReturnState_SYSCALL_BLOCK,
                u: SysCallReturnBody {
                    blocked: SysCallReturnBlocked {
                        cond: blocked.condition.into_inner(),
                        restartable: blocked.restartable,
                    },
                },
            },
            Err(SyscallError::Native) => Self {
                state: c::SysCallReturnState_SYSCALL_NATIVE,
                // No field for native. This is the recommended way to default-initialize a union.
                // https://rust-lang.github.io/rust-bindgen/using-unions.html#using-the-union-builtin
                u: unsafe { std::mem::zeroed::<SysCallReturnBody>() },
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
